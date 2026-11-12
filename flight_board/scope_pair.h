#pragma once
// =====================================================================
//  Flight Board <-> Flight Board Scope pairing (v1). Shared by both sketches: keep the two copies the same.
//
//  Over ESP-NOW (Wi-Fi radio, no network needed), only while you are pairing:
//    board  -> everyone : BEACON  "a board is pairing", every 100 ms for 60 s
//    scope  -> board    : HELLO   its one-off public key (P-256)
//    board  -> scope    : KEYS    the board's one-off public key + the Wi-Fi details, locked (AES-256-GCM) with a key
//                                 made from both public keys, the secret they share (ECDH) and the 4-digit code
//    scope  -> board    : DONE    proof it unlocked them (so the right code was typed), or NOPE (it didn't)
//  Someone just listening learns nothing (the shared secret never goes over the air). Without the code nobody can
//  read or fake the details; a wrong code simply fails, and the board gives up after 3 tries.
// =====================================================================
#include <stdint.h>
#include <string.h>
#include <mbedtls/ecdh.h>
#include <mbedtls/gcm.h>
#include <mbedtls/sha256.h>
#include <esp_random.h>

enum SpType : uint8_t { SP_BEACON = 1, SP_HELLO = 2, SP_KEYS = 3, SP_DONE = 4, SP_NOPE = 5 };
static const int SP_HDR = 6;                       // "FBSC", version, type
static const int SP_PUB = 65;                      // P-256 public key, uncompressed
static const int SP_MAX = 250;                     // ESP-NOW's largest message

struct SpCreds { char ssid[33]; char pass[65]; char id[17]; char host[33]; };

static int spRng(void*, unsigned char* out, size_t n) { esp_fill_random(out, n); return 0; }

static void spHdr(uint8_t* m, uint8_t type) { m[0] = 'F'; m[1] = 'B'; m[2] = 'S'; m[3] = 'C'; m[4] = 1; m[5] = type; }
// The message's type, or 0 if it isn't one of ours
static int spType(const uint8_t* m, int n) {
  if (n < SP_HDR || m[0] != 'F' || m[1] != 'B' || m[2] != 'S' || m[3] != 'C' || m[4] != 1) return 0;
  return m[5];
}

// ---- one-off key pair
struct SpKey { mbedtls_ecp_group g; mbedtls_mpi d; mbedtls_ecp_point Q; uint8_t pub[SP_PUB]; bool ok; };
static bool spKeyNew(SpKey& k) {
  mbedtls_ecp_group_init(&k.g); mbedtls_mpi_init(&k.d); mbedtls_ecp_point_init(&k.Q);
  size_t ol = 0;
  k.ok = mbedtls_ecp_group_load(&k.g, MBEDTLS_ECP_DP_SECP256R1) == 0
      && mbedtls_ecdh_gen_public(&k.g, &k.d, &k.Q, spRng, nullptr) == 0
      && mbedtls_ecp_point_write_binary(&k.g, &k.Q, MBEDTLS_ECP_PF_UNCOMPRESSED, &ol, k.pub, SP_PUB) == 0 && ol == SP_PUB;
  return k.ok;
}
static void spKeyFree(SpKey& k) { mbedtls_ecp_point_free(&k.Q); mbedtls_mpi_free(&k.d); mbedtls_ecp_group_free(&k.g); memset(k.pub, 0, SP_PUB); k.ok = false; }
// The secret both sides get from their own key and the other's public key
static bool spShared(SpKey& k, const uint8_t* peer, uint8_t z[32]) {
  mbedtls_ecp_point P; mbedtls_mpi s;
  mbedtls_ecp_point_init(&P); mbedtls_mpi_init(&s);
  bool ok = mbedtls_ecp_point_read_binary(&k.g, &P, peer, SP_PUB) == 0 && mbedtls_ecp_check_pubkey(&k.g, &P) == 0
         && mbedtls_ecdh_compute_shared(&k.g, &s, &P, &k.d, spRng, nullptr) == 0 && mbedtls_mpi_write_binary(&s, z, 32) == 0;
  mbedtls_mpi_free(&s); mbedtls_ecp_point_free(&P);
  return ok;
}
// The lock's key: the shared secret, the code, and both public keys (so neither can be swapped)
static void spDerive(const uint8_t z[32], const char* code4, const uint8_t* scopePub, const uint8_t* boardPub, uint8_t key[32]) {
  mbedtls_sha256_context c; mbedtls_sha256_init(&c); mbedtls_sha256_starts(&c, 0);
  mbedtls_sha256_update(&c, (const unsigned char*)"FBSCOPE1", 8);
  mbedtls_sha256_update(&c, z, 32);
  mbedtls_sha256_update(&c, (const unsigned char*)code4, 4);
  mbedtls_sha256_update(&c, scopePub, SP_PUB);
  mbedtls_sha256_update(&c, boardPub, SP_PUB);
  mbedtls_sha256_finish(&c, key); mbedtls_sha256_free(&c);
}
static bool spSeal(const uint8_t key[32], const uint8_t* iv, const uint8_t* aad, int na, const uint8_t* in, int n, uint8_t* out, uint8_t tag[16]) {
  mbedtls_gcm_context g; mbedtls_gcm_init(&g);
  bool ok = mbedtls_gcm_setkey(&g, MBEDTLS_CIPHER_ID_AES, key, 256) == 0
         && mbedtls_gcm_crypt_and_tag(&g, MBEDTLS_GCM_ENCRYPT, n, iv, 12, aad, na, in, out, 16, tag) == 0;
  mbedtls_gcm_free(&g); return ok;
}
static bool spOpen(const uint8_t key[32], const uint8_t* iv, const uint8_t* aad, int na, const uint8_t* in, int n, const uint8_t* tag, uint8_t* out) {
  mbedtls_gcm_context g; mbedtls_gcm_init(&g);
  bool ok = mbedtls_gcm_setkey(&g, MBEDTLS_CIPHER_ID_AES, key, 256) == 0
         && mbedtls_gcm_auth_decrypt(&g, n, iv, 12, aad, na, tag, 16, in, out) == 0;
  mbedtls_gcm_free(&g); return ok;
}

// ---- the Wi-Fi details: each field as its length then its text
static int spPack(const SpCreds& c, uint8_t* o) {
  int n = 0;
  const char* f[4] = { c.ssid, c.pass, c.id, c.host };
  for (int i = 0; i < 4; i++) { int l = strlen(f[i]); o[n++] = (uint8_t)l; memcpy(o + n, f[i], l); n += l; }
  return n;
}
static bool spUnpack(const uint8_t* in, int n, SpCreds& c) {
  memset(&c, 0, sizeof c);
  char* f[4] = { c.ssid, c.pass, c.id, c.host }; const int cap[4] = { 32, 64, 16, 32 };
  int p = 0;
  for (int i = 0; i < 4; i++) {
    if (p >= n) return false;
    int l = in[p++];
    if (l > cap[i] || p + l > n) return false;
    memcpy(f[i], in + p, l); f[i][l] = 0; p += l;
  }
  return p == n && c.ssid[0];
}

// ---- messages (each returns its length, 0 if it couldn't be made)
static int spMsgBeacon(uint8_t* m) { spHdr(m, SP_BEACON); return SP_HDR; }
static int spMsgNope(uint8_t* m) { spHdr(m, SP_NOPE); return SP_HDR; }
static int spMsgHello(uint8_t* m, const uint8_t* scopePub) { spHdr(m, SP_HELLO); memcpy(m + SP_HDR, scopePub, SP_PUB); return SP_HDR + SP_PUB; }
// KEYS: header, board public key, iv, length, locked details, tag (header + public key are covered by the tag too)
static int spMsgKeys(uint8_t* m, const uint8_t key[32], const uint8_t* boardPub, const SpCreds& c) {
  uint8_t plain[160];
  int n = spPack(c, plain);
  if (SP_HDR + SP_PUB + 12 + 1 + n + 16 > SP_MAX) return 0;
  spHdr(m, SP_KEYS); memcpy(m + SP_HDR, boardPub, SP_PUB);
  uint8_t* iv = m + SP_HDR + SP_PUB; esp_fill_random(iv, 12);
  m[SP_HDR + SP_PUB + 12] = (uint8_t)n;
  uint8_t* ct = m + SP_HDR + SP_PUB + 13;
  bool ok = spSeal(key, iv, m, SP_HDR + SP_PUB, plain, n, ct, ct + n);
  memset(plain, 0, sizeof plain);
  return ok ? SP_HDR + SP_PUB + 13 + n + 16 : 0;
}
static const uint8_t* spKeysPub(const uint8_t* m) { return m + SP_HDR; }
static bool spReadKeys(const uint8_t* m, int len, const uint8_t key[32], SpCreds& c) {
  if (len < SP_HDR + SP_PUB + 13 + 16) return false;
  int n = m[SP_HDR + SP_PUB + 12];
  if (len != SP_HDR + SP_PUB + 13 + n + 16) return false;
  uint8_t plain[160];
  if (n > (int)sizeof plain) return false;
  const uint8_t* ct = m + SP_HDR + SP_PUB + 13;
  bool ok = spOpen(key, m + SP_HDR + SP_PUB, m, SP_HDR + SP_PUB, ct, n, ct + n, plain) && spUnpack(plain, n, c);
  memset(plain, 0, sizeof plain);
  return ok;
}
static const uint8_t SP_DONE_IV[12] = { 'D', 'O', 'N', 'E', 'D', 'O', 'N', 'E', 'D', 'O', 'N', 'E' };
// DONE carries the word "DONE", locked with the shared key (v1.1: the chip's AES-GCM refuses an empty message,
// so the first try at a DONE with nothing in it never went out, and the board never heard it had worked)
static int spMsgDone(uint8_t* m, const uint8_t key[32]) {
  spHdr(m, SP_DONE);
  return spSeal(key, SP_DONE_IV, m, SP_HDR, (const uint8_t*)"DONE", 4, m + SP_HDR, m + SP_HDR + 4) ? SP_HDR + 4 + 16 : 0;
}
static bool spCheckDone(const uint8_t* m, int len, const uint8_t key[32]) {
  uint8_t w[4];
  return len == SP_HDR + 4 + 16 && spOpen(key, SP_DONE_IV, m, SP_HDR, m + SP_HDR, 4, m + SP_HDR + 4, w) && !memcmp(w, "DONE", 4);
}
