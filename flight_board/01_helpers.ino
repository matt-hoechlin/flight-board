// Flight Board: Small helpers: distances and bearings, airports and airlines, aircraft names, clock text, web requests.
// One tab of the flight_board sketch. Arduino joins the tabs in name order, after flight_board.ino.
// =====================================================================
//  Small helpers
// =====================================================================
const double DEG = M_PI / 180.0;

// Written declarations for functions used above where they're defined.
// (The Arduino IDE usually adds these itself, but it missed some in v0.7.8.)
float  readRadius(Preferences& pr, const char* exactKey, const char* oldKey, int def);
void   restartNow(uint8_t why);
String diagText();
void   factoryResetPrompt();
void   buildNetworkOptions(int n, const String& selected);
void   skyNote(const Plane* list, int n, const Place& place);
void   noteFetchFail(int s, int16_t code);
void   checkOverhead(int idx, int count, unsigned long at, int src);
int    fetchUrl(int s, const String& url, const Place& place, Plane* list);   // v0.9.44
void   emergFeed(const Plane* list, int n, int idx);
void   emergPoll(int src);
const char* sqMeaning(int c, int* level);
const char* sqTag(int c);
bool   sqEmergency(int c);
int    homeIndex();
long   groundFtNear(double lat, double lon);
bool   isHomePlace(const Place& p);

float distanceMiles(double lat1, double lon1, double lat2, double lon2) {
  double dLat = (lat2 - lat1) * DEG, dLon = (lon2 - lon1) * DEG;
  double a = sin(dLat / 2) * sin(dLat / 2) +
             cos(lat1 * DEG) * cos(lat2 * DEG) * sin(dLon / 2) * sin(dLon / 2);
  return 3958.8 * 2 * atan2(sqrt(a), sqrt(1 - a));
}

float bearingDeg(double lat1, double lon1, double lat2, double lon2) {
  double y = sin((lon2 - lon1) * DEG) * cos(lat2 * DEG);
  double x = cos(lat1 * DEG) * sin(lat2 * DEG) -
             sin(lat1 * DEG) * cos(lat2 * DEG) * cos((lon2 - lon1) * DEG);
  return fmod(atan2(y, x) / DEG + 360.0, 360.0);
}

const char* compass(float b) {
  static const char* dirs[] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
  if (!isfinite(b)) return "?";
  b = fmodf(b, 360.0f); if (b < 0) b += 360.0f;          // -10 is 350
  return dirs[((int)((b + 22.5f) / 45.0f)) & 7];
}

// Callsigns, registrations and types: letters, digits and dashes only (they go into web pages and web addresses)
void copyIdent(char* dst, size_t len, const char* src) {
  size_t n = 0;
  if (src) for (; *src && *src != ' ' && n < len - 1; src++)
    if (isalnum((unsigned char)*src) || *src == '-') dst[n++] = *src;
  dst[n] = '\0';
}

// A real place on Earth?
bool coordsOk(double lat, double lon) {
  return isfinite(lat) && isfinite(lon) && lat >= -90 && lat <= 90 && lon >= -180 && lon <= 180;
}

void copyStr(char* dst, size_t len, const char* src) {
  strncpy(dst, src ? src : "", len - 1);
  dst[len - 1] = '\0';
}

void copyTrim(char* dst, size_t len, const char* src) {
  size_t n = 0;
  if (src) while (src[n] && src[n] != ' ' && n < len - 1) { dst[n] = src[n]; n++; }
  dst[n] = '\0';
}

// v0.9.32: route codes the airport list files under another code. The list uses FAA codes for US fields
// (Mesa Gateway is IWA); airlines and adsbdb use the IATA code (AZA). adsbdb also sends the ICAO code (KIWA),
// so each route remembers the pair here and findAirport() falls back to it.
struct CodeAlias { char code[5]; const Airport* a; };
const int ALIAS_MAX = 24;
CodeAlias codeAlias[ALIAS_MAX];
int aliasNext = 0;
const Airport* findAlias(const char* code) {
  for (int i = 0; i < ALIAS_MAX; i++)
    if (codeAlias[i].a && strcasecmp(codeAlias[i].code, code) == 0) return codeAlias[i].a;
  return nullptr;
}
const Airport* findAirportByIdent(const char* ident);
void noteAlias(const char* code, const char* icao) {
  if (!code[0] || !icao || !icao[0] || findAlias(code)) return;
  const Airport* a = findAirportByIdent(icao);
  if (!a || strcasecmp(a->code, code) == 0) return;      // unknown, or already filed under this code
  for (int i = 0; i < NUM_AIRPORTS; i++)
    if (strcmp(AIRPORTS[i].code, code) == 0) return;      // the code belongs to another airport in the list: leave it
  CodeAlias& e = codeAlias[aliasNext];
  e.a = nullptr; copyStr(e.code, sizeof e.code, code); e.a = a;   // pointer last: readers on the other core
  aliasNext = (aliasNext + 1) % ALIAS_MAX;
}

const Airport* findAirportByIdent(const char* ident) {
  for (int i = 0; i < NUM_AIRPORTS; i++)
    if (strcasecmp(AIRPORTS[i].ident, ident) == 0) return &AIRPORTS[i];
  return nullptr;
}

// Accepts what the setup page sends: "SEA  Seattle-Tacoma Intl - Seattle, WA  (KSEA)"
// (the id in brackets wins), or just a code typed by hand: "sea", "O02", "KRNO", "EGLL".
// When a code belongs to more than one airport, the US one comes first in the list.
const Airport* findAirport(const char* text) {
  const char* br = strrchr(text, '(');
  if (br) {
    char id[12] = {0};
    int n = 0;
    for (const char* c = br + 1; *c && *c != ')' && n < 11; c++) id[n++] = *c;
    const Airport* a = findAirportByIdent(id);
    if (a) return a;
  }
  char code[6] = {0};
  int n = 0;
  while (*text == ' ') text++;
  while (n < 5 && isalnum((unsigned char)text[n])) { code[n] = toupper(text[n]); n++; }
  if (n < 3 || n > 4 || isalnum((unsigned char)text[n])) return nullptr;
  for (int i = 0; i < NUM_AIRPORTS; i++)
    if (strcmp(AIRPORTS[i].code, code) == 0) return &AIRPORTS[i];
  if (n == 4) { const Airport* a = findAirportByIdent(code); if (a) return a; }
  return findAlias(code);                                  // v0.9.33: AZA -> Mesa Gateway (KIWA), ...
}

// "SEA  Seattle-Tacoma Intl - Seattle, WA  (KSEA)"; abroad: "LHR  London Heathrow - London (GB)  (EGLL)"
String airportLabel(const Airport* a) {
  String s = String(a->code) + "  " + a->name + " - " + a->city;
  s += a->us ? String(", ") + a->region : String(" (") + a->region + ")";
  return s + "  (" + a->ident + ")";
}

// Clock rule for the chosen time zone
const char* tzRule() { return TIME_ZONES[(tzIndex >= 0 && tzIndex < NUM_TIME_ZONES) ? tzIndex : TZ_DEFAULT].posix; }

// Military / DoW aircraft: the feed's military flag, a military type, or a military callsign
bool isHeliType(const char* t);   // v0.9.33, below with the helicopter rules
bool isMilitary(int dbFlags, const char* callsign, const char* type) {
  if (dbFlags & 1) return true;
  static const char* MIL_TYPES[] = {"C30J","C130","C5M","C17","K35R","KC46","KC10","E3TF","E6","P8","F35","F16","F18","F15","F22",
                                    "A10","B52","B1","B2","H60","V22","UH1","AH64","CH47","T38","T6","C12","C40","C32","C37"};
  for (const char* m : MIL_TYPES) if (strcmp(type, m) == 0) return true;    // whole code: "C17" is not "C172"
  static const char* MIL_CALLS[] = {"RCH","CNV","PAT","SAM","SPAR","VENUS","EXEC","NAVY","ARMY","TOPCAT","BOXER","AIO"};
  for (const char* m : MIL_CALLS) {
    size_t n = strlen(m);
    if (strncmp(callsign, m, n) == 0 && (isdigit((unsigned char)callsign[n]) || callsign[n] == 0)) return true;
  }
  return false;
}

bool isAirlineCallsign(const char* cs) {
  return isupper(cs[0]) && isupper(cs[1]) && isupper(cs[2]) && isdigit(cs[3]);
}

const Airline* findAirline(const char* cs) {
  if (!isAirlineCallsign(cs)) return nullptr;
  for (int i = 0; i < NUM_AIRLINES; i++)
    if (strncmp(cs, AIRLINES[i].icao, 3) == 0) return &AIRLINES[i];
  return nullptr;
}

// Airline name for the board column: built-in short name, else the name adsbdb gave,
// trimmed of words like "Airlines" ("Frontier Airlines" -> "Frontier")
void airlineShortFor(const Plane& p, char* buf, size_t len) {
  const char* cs = p.callsign;
  if (p.mil) { copyStr(buf, len, "Secret Squirrel"); return; }
  if (cs[0] == 'N' && isdigit((unsigned char)cs[1])) { copyStr(buf, len, "Private"); return; }
  const Airline* a = findAirline(cs);
  if (a) { copyStr(buf, len, a->shortName); return; }
  if (p.airline[0]) {
    static const char* DROP[] = {" Airlines", " Airline", " Air Lines", " Airways", " Aviation", " International", " Intl", " Inc", " LLC", " Ltd", " Co."};
    copyStr(buf, len, p.airline);
    for (const char* d : DROP) { char* f = strstr(buf, d); if (f && f != buf) *f = '\0'; }
    return;
  }
  if (isAirlineCallsign(cs)) { snprintf(buf, len, "%.3s", cs); return; }
  buf[0] = '\0';
}

// Full airline name for the detail page
const char* airlineFullFor(const Plane& p) {
  if (p.mil) return "Secret Squirrel";
  const char* cs = p.callsign;
  if (cs[0] == 'N' && isdigit((unsigned char)cs[1])) return "Private aircraft";
  const Airline* a = findAirline(cs);
  if (a) return a->fullName;
  return p.airline;
}

bool startsWith(const char* s, const char* p) { return strncmp(s, p, strlen(p)) == 0; }
bool startsWithDigitAfter(const char* s, const char* p) {
  size_t n = strlen(p);
  return strncmp(s, p, n) == 0 && isdigit((unsigned char)s[n]);
}

// Plane maker from the aircraft type code (from v29's list; military checked first)
const char* makerName(const char* t) {
  if (!t[0]) return "";
  const char* mil[] = {"C30J","C130","C5M","F35","F16","F18","C17","K35R"};
  for (const char* m : mil) if (strcmp(t, m) == 0) return "Military";       // whole code: "C17" is not "C172"
  if (startsWith(t, "BCS1") || startsWith(t, "BCS3") || (t[0] == 'A' && isdigit((unsigned char)t[1]))) return "Airbus";
  if (startsWithDigitAfter(t, "B7") || startsWith(t, "B37M") || startsWith(t, "B38M") ||
      startsWith(t, "B39M") || startsWith(t, "B3XM")) return "Boeing";
  if (startsWithDigitAfter(t, "MD") || startsWithDigitAfter(t, "DC")) return "McDonnell Douglas";
  if (startsWith(t, "CRJ") || startsWith(t, "CL3") || startsWith(t, "CL6") || startsWith(t, "GLE") ||
      startsWith(t, "GL5") || startsWith(t, "GL7") || startsWith(t, "BD")) return "Bombardier";
  if ((t[0] == 'E' && isdigit((unsigned char)t[1])) || startsWith(t, "E55P") ||
      startsWith(t, "E50P") || startsWith(t, "E45X")) return "Embraer";
  if (startsWith(t, "DH8")) return "De Havilland";
  if (t[0] == 'A' && t[1] == 'T' && t[2] >= '4' && t[2] <= '7') return "ATR";
  if (startsWith(t, "PC12") || startsWith(t, "PC24")) return "Pilatus";
  if (startsWithDigitAfter(t, "BE") || startsWith(t, "B350")) return "Beechcraft";
  if (startsWithDigitAfter(t, "PA")) return "Piper";
  if (startsWith(t, "SR2") || startsWith(t, "SF50")) return "Cirrus";
  if (startsWith(t, "GLF") || startsWith(t, "G280") || startsWith(t, "GA5C") || startsWith(t, "GA6C") ||
      startsWith(t, "GA7C") || startsWith(t, "GA8C")) return "Gulfstream";
  if (startsWith(t, "LJ")) return "Learjet";
  if (startsWithDigitAfter(t, "FA") || startsWith(t, "F2TH") || startsWith(t, "F900")) return "Dassault";
  if (startsWith(t, "H25")) return "Hawker";
  if (startsWith(t, "TBM")) return "Daher";
  if (startsWith(t, "KODI")) return "Kodiak";
  if (startsWith(t, "R22") || startsWith(t, "R44") || startsWith(t, "R66")) return "Robinson";
  if (startsWith(t, "B06") || startsWith(t, "B407") || startsWith(t, "B429") || startsWith(t, "B412")) return "Bell";
  if (startsWithDigitAfter(t, "EC") || startsWith(t, "AS3") || startsWith(t, "AS5") ||
      startsWithDigitAfter(t, "H1") || startsWith(t, "H60")) return "Airbus Helicopters";
  if (startsWithDigitAfter(t, "DA")) return "Diamond";
  if (startsWith(t, "HDJT")) return "Honda";
  if (startsWith(t, "SW4")) return "Fairchild";
  if (t[0] == 'C' && isdigit((unsigned char)t[1])) return "Cessna";
  return "";
}

const char* modelName(const char* t) {
  for (int i = 0; i < NUM_TYPE_NAMES; i++)
    if (strcmp(t, TYPE_NAMES[i].code) == 0) return TYPE_NAMES[i].name;
  return t;
}

void clockText(char* buf, size_t len, bool withSeconds) {
  struct tm t;
  if (!getLocalTime(&t, 0)) { snprintf(buf, len, "--:--"); return; }
  if (clock24) {                           // v0.9.28
    if (withSeconds) snprintf(buf, len, "%02d:%02d:%02d", t.tm_hour, t.tm_min, t.tm_sec);
    else snprintf(buf, len, "%02d:%02d", t.tm_hour, t.tm_min);
    return;
  }
  int h = t.tm_hour % 12; if (h == 0) h = 12;
  if (withSeconds) snprintf(buf, len, "%d:%02d:%02d %s", h, t.tm_min, t.tm_sec, t.tm_hour < 12 ? "AM" : "PM");
  else snprintf(buf, len, "%d:%02d %s", h, t.tm_min, t.tm_hour < 12 ? "AM" : "PM");
}

// "THU NOV 5"
void dateText(char* buf, size_t len) {
  static const char* DAYS[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
  static const char* MONTHS[] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};
  struct tm t;
  if (!getLocalTime(&t, 0)) { buf[0] = '\0'; return; }
  snprintf(buf, len, "%s %s %d", DAYS[t.tm_wday % 7], MONTHS[t.tm_mon % 12], t.tm_mday);
}

String urlEncode(const char* s) {
  String out;
  for (; *s; s++) {
    unsigned char c = *s;
    if (isalnum(c) || c == '-' || c == '_' || c == '.') out += (char)c;
    else { char b[4]; snprintf(b, sizeof b, "%%%02X", c); out += b; }
  }
  return out;
}

String htmlEscape(const String& s) {
  String out;
  for (unsigned i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '&') out += "&amp;";
    else if (c == '<') out += "&lt;";
    else if (c == '>') out += "&gt;";
    else if (c == '"') out += "&quot;";
    else if (c == '\'') out += "&#39;";
    else out += c;
  }
  return out;
}

// v0.9.48: undoes a "chunked" answer (HTTP/1.1 sends the body in pieces, each after its size in hex, ending with 0)
struct Dechunk {
  int st = 0; long left = 0; bool done = false, bad = false;    // st: 0 size line, 1 data, 2 line end after data, 3 rest of size line
  void feed(const char* p, int n, String& out) {
    int i = 0;
    while (i < n && !done && !bad) {
      char c = p[i];
      if (st == 0 || st == 3) {
        if (c == '\n') { if (left == 0) done = true; else st = 1; i++; }
        else if (st == 0 && isxdigit((unsigned char)c)) { left = left * 16 + (isdigit((unsigned char)c) ? c - '0' : tolower(c) - 'a' + 10); if (left > 2000000) bad = true; i++; }
        else { if (c != '\r' && c != ' ') st = 3; i++; }          // ";extension" or spaces: skip to the line's end
      } else if (st == 1) {
        int k = n - i < left ? n - i : (int)left;
        out.concat(p + i, k); left -= k; i += k;
        if (left == 0) st = 2;
      } else { if (c == '\n') { st = 0; left = 0; } i++; }       // the CR LF after each piece
    }
  }
};

// One HTTPS GET; returns true and fills body on HTTP 200
bool httpGetKey(const String& url, String& body, int timeoutMs, const char* apiKey);
bool httpGet(const String& url, String& body, int timeoutMs) { return httpGetKey(url, body, timeoutMs, nullptr); }
// v0.9.35: apiKey = FlightAware's x-apikey header (nullptr for the free sources)
bool httpGetKey(const String& url, String& body, int timeoutMs, const char* apiKey) {
  wdtPet();
  struct PetAfter { ~PetAfter() { wdtPet(); } } petAfter;   // and again when it returns, however it returns
  WiFiClientSecure client;
  client.setInsecure();               // public data, no certificate check
  client.setHandshakeTimeout((timeoutMs + 999) / 1000);   // v0.9.45: the secure handshake used to be allowed 120 s
  HTTPClient http;
  http.setTimeout(timeoutMs);
  http.setConnectTimeout(timeoutMs);
  // v0.9.48: HTTP/1.1 again (adsb.lol now refuses 1.0 with "426"); a chunked body is undone below.
  // "Connection: close", so a body without a length still ends when the server hangs up.
  http.setReuse(false);
  const char* hk[] = { "Transfer-Encoding" };
  http.collectHeaders(hk, 1);
  http.setUserAgent(USER_AGENT);
  if (!http.begin(client, url)) {
    lastHttpCode = -100; lastHttpLargest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
    Serial.printf("  connect failed: %s\n", url.c_str()); return false;
  }
  if (apiKey && apiKey[0]) http.addHeader("x-apikey", apiKey);
  int code = http.GET();
  lastHttpCode = code;
  if (code != 200) lastHttpLargest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
  if (code != 200) {
    Serial.printf("  HTTP %d: %s\n", code, url.c_str());
    http.end();
    return false;
  }
  // v0.9.45: read the answer in pieces, checking in as it arrives, with a hard limit on the whole thing.
  // A server that sends a trickle used to hold the data part past the 40 s safety restart.
  bool chunked = http.header("Transfer-Encoding").indexOf("chunked") >= 0;
  Dechunk dc;
  int len = chunked ? -1 : http.getSize();           // -1 = not given: read until the server closes
  if (len > 0) body.reserve(len);
  auto* st = http.getStreamPtr();
  uint32_t start = millis(), lastByte = start, limit = (uint32_t)timeoutMs * 2 < 15000 ? 15000 : (uint32_t)timeoutMs * 2;
  char buf[513];
  bool ok = st != nullptr;
  while (ok) {
    int a = st->available();
    if (a > 0) {
      int r = st->readBytes(buf, a < 512 ? a : 512);
      if (r > 0) {
        lastByte = millis();
        if (chunked) { dc.feed(buf, r, body); if (dc.bad) ok = false; if (dc.done) break; }
        else { buf[r] = 0; body += buf; }
      }
      if (len > 0 && (int)body.length() >= len) break;          // all of it
    } else {
      if (!st->connected()) break;                               // server finished
      if (millis() - lastByte > (uint32_t)timeoutMs) ok = false; // went quiet
      else vTaskDelay(pdMS_TO_TICKS(5));
    }
    if (millis() - start > limit) ok = false;                    // too slow overall
    wdtPet();
  }
  if (ok && len > 0 && (int)body.length() < len) ok = false;     // cut off
  if (ok && chunked && !dc.done) ok = false;                     // cut off before the last (empty) piece
  http.end();
  if (!ok) {
    Serial.printf("  slow/cut-off answer (%u of %d bytes, %lu ms): %s\n", body.length(), len, millis() - start, url.c_str());
    body = String(); lastHttpCode = ERR_SLOW; lastHttpLargest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
    return false;
  }
  return true;
}

