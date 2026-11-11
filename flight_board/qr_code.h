// qr_code.h — small QR code generator for the Flight Board setup screen.
// Byte mode, error correction level M, versions 1-10 (up to 213 bytes of text).
// Follows the algorithm of Project Nayuki's "QR Code generator" library (MIT licence).
//
// Usage:
//   QrCode qr;
//   if (qrEncode("http://192.168.4.1", qr))
//     for (y...) for (x...) if (qr.dark(x, y)) ...draw a square...
#pragma once
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

const int QR_MAX_VERSION = 10;
const int QR_MAX_SIZE    = 17 + 4 * QR_MAX_VERSION;   // 57 modules

struct QrCode {
  int     version = 0;
  int     size    = 0;
  uint8_t mod[QR_MAX_SIZE][QR_MAX_SIZE];     // 1 = dark
  uint8_t isFn[QR_MAX_SIZE][QR_MAX_SIZE];    // 1 = part of a fixed pattern
  bool dark(int x, int y) const { return mod[y][x] != 0; }
};

namespace qrdetail {

// Error correction level M, indexed by version (index 0 unused)
static const int8_t ECC_PER_BLOCK[11] = { -1, 10, 16, 26, 18, 24, 16, 18, 22, 22, 26 };
static const int8_t NUM_BLOCKS[11]    = { -1,  1,  1,  1,  2,  2,  4,  4,  4,  5,  5 };
static const int    FORMAT_ECL_BITS   = 0;   // M

inline int numRawDataModules(int ver) {
  int result = (16 * ver + 128) * ver + 64;
  if (ver >= 2) {
    int numAlign = ver / 7 + 2;
    result -= (25 * numAlign - 10) * numAlign - 55;
    if (ver >= 7) result -= 36;
  }
  return result;
}

inline int numDataCodewords(int ver) {
  return numRawDataModules(ver) / 8 - ECC_PER_BLOCK[ver] * NUM_BLOCKS[ver];
}

inline uint8_t gfMul(uint8_t x, uint8_t y) {
  int z = 0;
  for (int i = 7; i >= 0; i--) {
    z = (z << 1) ^ ((z >> 7) * 0x11D);
    z ^= ((y >> i) & 1) * x;
  }
  return (uint8_t)z;
}

inline void rsDivisor(int degree, uint8_t* result) {
  memset(result, 0, degree);
  result[degree - 1] = 1;
  uint8_t root = 1;
  for (int i = 0; i < degree; i++) {
    for (int j = 0; j < degree; j++) {
      result[j] = gfMul(result[j], root);
      if (j + 1 < degree) result[j] ^= result[j + 1];
    }
    root = gfMul(root, 0x02);
  }
}

inline void rsRemainder(const uint8_t* data, int len, const uint8_t* gen, int degree, uint8_t* result) {
  memset(result, 0, degree);
  for (int i = 0; i < len; i++) {
    uint8_t factor = data[i] ^ result[0];
    memmove(result, result + 1, degree - 1);
    result[degree - 1] = 0;
    for (int j = 0; j < degree; j++) result[j] ^= gfMul(gen[j], factor);
  }
}

inline void setFn(QrCode& q, int x, int y, bool d) { q.mod[y][x] = d; q.isFn[y][x] = 1; }

inline void drawFinder(QrCode& q, int x, int y) {
  for (int dy = -4; dy <= 4; dy++)
    for (int dx = -4; dx <= 4; dx++) {
      int dist = abs(dx) > abs(dy) ? abs(dx) : abs(dy);
      int xx = x + dx, yy = y + dy;
      if (xx >= 0 && xx < q.size && yy >= 0 && yy < q.size) setFn(q, xx, yy, dist != 2 && dist != 4);
    }
}

inline void drawAlignment(QrCode& q, int x, int y) {
  for (int dy = -2; dy <= 2; dy++)
    for (int dx = -2; dx <= 2; dx++) {
      int dist = abs(dx) > abs(dy) ? abs(dx) : abs(dy);
      setFn(q, x + dx, y + dy, dist != 1);
    }
}

inline void drawFormatBits(QrCode& q, int mask) {
  int data = FORMAT_ECL_BITS << 3 | mask;
  int rem = data;
  for (int i = 0; i < 10; i++) rem = (rem << 1) ^ ((rem >> 9) * 0x537);
  int bits = (data << 10 | rem) ^ 0x5412;
  // (named fmtBit, not "bit": Arduino already has a bit() macro that would replace it)
  auto fmtBit = [&](int i) { return ((bits >> i) & 1) != 0; };
  int n = q.size;
  for (int i = 0; i <= 5; i++) setFn(q, 8, i, fmtBit(i));
  setFn(q, 8, 7, fmtBit(6));
  setFn(q, 8, 8, fmtBit(7));
  setFn(q, 7, 8, fmtBit(8));
  for (int i = 9; i < 15; i++) setFn(q, 14 - i, 8, fmtBit(i));
  for (int i = 0; i < 8; i++)  setFn(q, n - 1 - i, 8, fmtBit(i));
  for (int i = 8; i < 15; i++) setFn(q, 8, n - 15 + i, fmtBit(i));
  setFn(q, 8, n - 8, true);   // always-dark module
}

inline void drawVersion(QrCode& q) {
  if (q.version < 7) return;
  int rem = q.version;
  for (int i = 0; i < 12; i++) rem = (rem << 1) ^ ((rem >> 11) * 0x1F25);
  long bits = (long)q.version << 12 | rem;
  for (int i = 0; i < 18; i++) {
    bool b = ((bits >> i) & 1) != 0;
    int a = q.size - 11 + i % 3, c = i / 3;
    setFn(q, a, c, b);
    setFn(q, c, a, b);
  }
}

inline void drawFunctionPatterns(QrCode& q) {
  for (int i = 0; i < q.size; i++) {
    setFn(q, 6, i, i % 2 == 0);
    setFn(q, i, 6, i % 2 == 0);
  }
  drawFinder(q, 3, 3);
  drawFinder(q, q.size - 4, 3);
  drawFinder(q, 3, q.size - 4);

  if (q.version > 1) {
    int numAlign = q.version / 7 + 2;
    int step = (q.version * 4 + numAlign * 2 + 1) / (numAlign * 2 - 2) * 2;
    int pos[7];
    pos[0] = 6;
    for (int i = numAlign - 1, p = q.size - 7; i >= 1; i--, p -= step) pos[i] = p;
    for (int i = 0; i < numAlign; i++)
      for (int j = 0; j < numAlign; j++) {
        if ((i == 0 && j == 0) || (i == 0 && j == numAlign - 1) || (i == numAlign - 1 && j == 0)) continue;
        drawAlignment(q, pos[i], pos[j]);
      }
  }
  drawFormatBits(q, 0);   // placeholder, redrawn once the mask is chosen
  drawVersion(q);
}

inline void drawCodewords(QrCode& q, const uint8_t* data, int len) {
  int i = 0;
  for (int right = q.size - 1; right >= 1; right -= 2) {
    if (right == 6) right = 5;
    for (int vert = 0; vert < q.size; vert++)
      for (int j = 0; j < 2; j++) {
        int x = right - j;
        bool upward = ((right + 1) & 2) == 0;
        int y = upward ? q.size - 1 - vert : vert;
        if (!q.isFn[y][x] && i < len * 8) {
          q.mod[y][x] = (data[i >> 3] >> (7 - (i & 7))) & 1;
          i++;
        }
      }
  }
}

inline void applyMask(QrCode& q, int m) {
  for (int y = 0; y < q.size; y++)
    for (int x = 0; x < q.size; x++) {
      if (q.isFn[y][x]) continue;
      bool inv;
      switch (m) {
        case 0:  inv = (x + y) % 2 == 0; break;
        case 1:  inv = y % 2 == 0; break;
        case 2:  inv = x % 3 == 0; break;
        case 3:  inv = (x + y) % 3 == 0; break;
        case 4:  inv = (x / 3 + y / 2) % 2 == 0; break;
        case 5:  inv = x * y % 2 + x * y % 3 == 0; break;
        case 6:  inv = (x * y % 2 + x * y % 3) % 2 == 0; break;
        default: inv = ((x + y) % 2 + x * y % 3) % 2 == 0; break;
      }
      if (inv) q.mod[y][x] ^= 1;
    }
}

// Penalty score used to pick the best-looking mask (any mask decodes correctly).
inline long penalty(const QrCode& q) {
  long score = 0;
  int n = q.size;
  // Rule 1: runs of 5+ same-colour modules in a row/column
  for (int pass = 0; pass < 2; pass++)
    for (int a = 0; a < n; a++) {
      int run = 1;
      for (int b = 1; b < n; b++) {
        uint8_t cur  = pass ? q.mod[b][a] : q.mod[a][b];
        uint8_t prev = pass ? q.mod[b - 1][a] : q.mod[a][b - 1];
        if (cur == prev) { run++; if (run == 5) score += 3; else if (run > 5) score++; }
        else run = 1;
      }
    }
  // Rule 2: 2x2 blocks of the same colour
  for (int y = 0; y < n - 1; y++)
    for (int x = 0; x < n - 1; x++) {
      uint8_t c = q.mod[y][x];
      if (c == q.mod[y][x + 1] && c == q.mod[y + 1][x] && c == q.mod[y + 1][x + 1]) score += 3;
    }
  // Rule 3: finder-like 1:1:3:1:1 patterns with 4 light modules on one side
  static const uint8_t P[11] = {1,0,1,1,1,0,1,0,0,0,0};
  for (int pass = 0; pass < 2; pass++)
    for (int a = 0; a < n; a++)
      for (int b = 0; b + 11 <= n; b++) {
        bool fwd = true, rev = true;
        for (int k = 0; k < 11; k++) {
          uint8_t v = pass ? q.mod[b + k][a] : q.mod[a][b + k];
          if (v != P[k]) fwd = false;
          if (v != P[10 - k]) rev = false;
        }
        if (fwd) score += 40;
        if (rev) score += 40;
      }
  // Rule 4: balance of dark and light
  long darkCount = 0;
  for (int y = 0; y < n; y++) for (int x = 0; x < n; x++) darkCount += q.mod[y][x];
  long total = (long)n * n;
  long k = (labs(darkCount * 20 - total * 10) + total - 1) / total - 1;
  if (k > 0) score += k * 10;
  return score;
}

}  // namespace qrdetail

// Encode text into a QR code. Returns false if the text is too long (> 213 bytes).
inline bool qrEncode(const char* text, QrCode& q) {
  using namespace qrdetail;
  int len = strlen(text);

  int ver;
  for (ver = 1; ver <= QR_MAX_VERSION; ver++) {
    int ccBits = ver <= 9 ? 8 : 16;
    if (4 + ccBits + len * 8 <= numDataCodewords(ver) * 8) break;
  }
  if (ver > QR_MAX_VERSION) return false;

  q.version = ver;
  q.size = 17 + 4 * ver;
  memset(q.mod, 0, sizeof q.mod);
  memset(q.isFn, 0, sizeof q.isFn);

  // Data bits: mode (byte), length, the text, terminator, padding
  const int capBytes = numDataCodewords(ver);
  static uint8_t data[400];
  memset(data, 0, sizeof data);
  int bitLen = 0;
  auto put = [&](uint32_t val, int n) {
    for (int i = n - 1; i >= 0; i--, bitLen++)
      if ((val >> i) & 1) data[bitLen >> 3] |= 0x80 >> (bitLen & 7);
  };
  put(0x4, 4);
  put(len, ver <= 9 ? 8 : 16);
  for (int i = 0; i < len; i++) put((uint8_t)text[i], 8);
  int term = capBytes * 8 - bitLen; if (term > 4) term = 4;
  put(0, term);
  put(0, (8 - bitLen % 8) % 8);
  for (uint8_t pad = 0xEC; bitLen < capBytes * 8; pad ^= 0xEC ^ 0x11) put(pad, 8);

  // Split into blocks, add error correction, interleave
  const int numBlocks = NUM_BLOCKS[ver];
  const int eccLen = ECC_PER_BLOCK[ver];
  const int rawCodewords = numRawDataModules(ver) / 8;
  const int numShort = numBlocks - rawCodewords % numBlocks;
  const int shortLen = rawCodewords / numBlocks;            // total length of a short block
  static uint8_t blocks[8][160];
  uint8_t gen[32], ecc[32];
  rsDivisor(eccLen, gen);
  for (int i = 0, k = 0; i < numBlocks; i++) {
    int datLen = shortLen - eccLen + (i < numShort ? 0 : 1);
    rsRemainder(data + k, datLen, gen, eccLen, ecc);
    memcpy(blocks[i], data + k, datLen);
    int p = datLen;
    if (i < numShort) blocks[i][p++] = 0;                    // dummy, skipped below
    memcpy(blocks[i] + p, ecc, eccLen);
    k += datLen;
  }
  static uint8_t all[400];
  int outLen = 0;
  for (int i = 0; i < shortLen + 1; i++)
    for (int j = 0; j < numBlocks; j++)
      if (i != shortLen - eccLen || j >= numShort) all[outLen++] = blocks[j][i];

  // Draw, then try all 8 masks and keep the best
  drawFunctionPatterns(q);
  drawCodewords(q, all, outLen);
  int best = 0;
  long bestScore = -1;
  for (int m = 0; m < 8; m++) {
    applyMask(q, m);
    drawFormatBits(q, m);
    long s = penalty(q);
    if (bestScore < 0 || s < bestScore) { bestScore = s; best = m; }
    applyMask(q, m);   // undo (masking twice cancels out)
  }
  applyMask(q, best);
  drawFormatBits(q, best);
  return true;
}
