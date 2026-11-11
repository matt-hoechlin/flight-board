// Flight Board: Better Routes (FlightAware), the route checks, the route log.
// One tab of the flight_board sketch. Arduino joins the tabs in name order, after flight_board.ino.
// =====================================================================
//  v0.9.35: Better Routes. The owner's own FlightAware AeroAPI key (Personal plan), asked only when the free
//  route doesn't match the plane. GET /flights/{ident}: one result set per lookup ($0.005 at list price).
// =====================================================================

uint32_t thisMonth() { struct tm t; if (!getLocalTime(&t, 0)) return 0; return (uint32_t)(t.tm_year + 1900) * 100 + t.tm_mon + 1; }
void faSaveCounts() { Preferences p; p.begin("fboard", false); p.putUInt("fam", faMonth); p.putUInt("fan", faCount); p.putUInt("faf", faFixed); p.end(); }
void faLoadCounts() {
  if (faLoaded) return;
  Preferences p; p.begin("fboard", true); faMonth = p.getUInt("fam", 0); faCount = p.getUInt("fan", 0); faFixed = p.getUInt("faf", 0); p.end();
  faLoaded = true;
}
void faRollMonth() { uint32_t m = thisMonth(); if (m && m != faMonth) { faMonth = m; faCount = 0; faFixed = 0; faSaveCounts(); } }
bool faKeyRefused() { return faLastErr == 401 || faLastErr == 403; }
bool faLimitReached() { int lim = FA_LIMITS[constrain(faLimitI, 0, 3)]; return lim && faCount >= (uint32_t)lim; }
// v0.9.37: spread the monthly limit over the month: 10% up front, the rest as the days go by
bool faPaceOk() {
  int lim = FA_LIMITS[constrain(faLimitI, 0, 3)];
  if (!lim || faLimitI == 3) return true;                       // v0.9.43: Full is never paced
  struct tm t; if (!getLocalTime(&t, 0)) return true;              // no clock yet: the plain limit still applies
  static const int DIM[12] = { 31,28,31,30,31,30,31,31,30,31,30,31 };
  int days = DIM[t.tm_mon] + ((t.tm_mon == 1 && (t.tm_year + 1900) % 4 == 0) ? 1 : 0);
  float done = (t.tm_mday - 1 + (t.tm_hour + t.tm_min / 60.0f) / 24.0f) / days;
  return faCount < (uint32_t)(lim * 0.10f + lim * 0.90f * done);
}
// v0.9.38: why a lookup can't be made now (nullptr = it can)
const char* faWhyNot(bool user) {     // v0.9.40: user = details opened (skips the pacing, not the limit)
  if (!faKey[0]) return "no key";
  if (faUseI == 0) return "off (setting)";
  if (!faPlaceOk) return "not used for this place";
  if (faKeyRefused()) return "key not accepted";
  faLoadCounts(); faRollMonth();
  if (faLimitReached()) return "monthly limit reached";
  if (!user && !faPaceOk()) return "budget paced";
  unsigned long now = millis();
  for (int i = 0; i < FA_PER_MIN; i++) if (!faTimes[i] || now - faTimes[i] >= 60000UL) return nullptr;   // a free slot this minute
  return "busy (8 a minute)";
}
bool faReady() { return faWhyNot(false) == nullptr; }
bool isHomePlace(const Place& p);
// v0.9.39: Better Routes is switched on for this place (for the footer)
bool faOnFor(const Place& pl) {
  if (!faKey[0] || !faUseI || faKeyRefused()) return false;
  return faUseI == 3 || isHomePlace(pl) || (faUseI == 2 && pl.isAirport);
}
// v0.9.40: "Which flights". The board can't know where a plane is going until it asks; these are its best guess.
bool faLow(const Plane& p) { return p.altFt < FA_LOW_FT || (p.rateOk && labs((long)p.climbRate) >= FA_VS_FPM); }
long groundFtNear(double lat, double lon);
bool faOverhead(const Plane& p) {                 // will pass within the Overhead Alert distance, below its height
  if (!faPlace) return false;
  static double gLat = 999, gLon = 999; static long gFt = 0;
  if (fabs(gLat - faPlace->lat) > 0.001 || fabs(gLon - faPlace->lon) > 0.001) { gLat = faPlace->lat; gLon = faPlace->lon; gFt = groundFtNear(gLat, gLon); }
  float limit = ALERT_DIST_NM[unitsMode][alertDistI];
  long below  = ALERT_BELOW_FT[unitsMode][alertBelowI];
  if (below && p.altFt - gFt > below) return false;
  if (p.distNm <= limit) return true;
  if (p.gs < 40) return false;
  float b = p.bearing * DEG_TO_RAD, x = p.distNm * sinf(b), y = p.distNm * cosf(b);
  float v = p.gs / 3600.0f, t = p.track * DEG_TO_RAD, vx = v * sinf(t), vy = v * cosf(t);
  float tc = -(x * vx + y * vy) / (v * v);       // seconds to the closest point (straight line)
  if (tc < 0 || tc > 900) return false;          // moving away, or more than 15 minutes off
  float dx = x + vx * tc, dy = y + vy * tc;
  return sqrtf(dx * dx + dy * dy) <= limit;
}
const char* faFlightWhyNot(const Plane& p) {     // nullptr = this flight is one to ask about
  if (faWhichI == 0 || faLow(p)) return nullptr;
  if (faWhichI == 1) return faOverhead(p) ? nullptr : "high, passing by";
  return "high (Low only)";
}   // a lookup may be made now

bool faMemoInit() {
  if (faMemo) return true;
  faMemo = (FaMemo*)ps_malloc(sizeof(FaMemo) * FAMEMO_N);
  if (!faMemo) faMemo = (FaMemo*)malloc(sizeof(FaMemo) * FAMEMO_N);
  if (faMemo) memset(faMemo, 0, sizeof(FaMemo) * FAMEMO_N);
  return faMemo != nullptr;
}
void faMemoClear() { if (faMemo) memset(faMemo, 0, sizeof(FaMemo) * FAMEMO_N); faRepeats = 0; }
FaMemo* faMemoFind(const char* cs) {
  if (!faMemoInit()) return nullptr;
  uint32_t now = millis();
  for (int i = 0; i < FAMEMO_N; i++) {
    FaMemo& m = faMemo[i];
    if (!m.cs[0] || strcmp(m.cs, cs)) continue;
    if (now - m.ms > FAMEMO_KEEP_MS) { m.cs[0] = 0; return nullptr; }
    return &m;
  }
  return nullptr;
}
FaMemo* faMemoPut(const char* cs) {
  FaMemo* m = faMemoFind(cs);
  if (m || !faMemo) return m;
  m = &faMemo[faMemoNext]; faMemoNext = (faMemoNext + 1) % FAMEMO_N;
  memset(m, 0, sizeof *m); copyStr(m->cs, sizeof m->cs, cs);
  return m;
}
// a stored FlightAware answer back into a route (positions from the airport list)
bool faFromMemo(const FaMemo& m, RouteCache& r) {
  const Airport* a = findAirport(m.from); const Airport* b = findAirport(m.to);
  if (!a || !b) return false;
  r.n = 2;
  copyStr(r.code[0], sizeof r.code[0], m.from); copyStr(r.city[0], sizeof r.city[0], a->city); r.lat[0] = a->lat; r.lon[0] = a->lon;
  copyStr(r.code[1], sizeof r.code[1], m.to);   copyStr(r.city[1], sizeof r.city[1], b->city); r.lat[1] = b->lat; r.lon[1] = b->lon;
  r.fromFa = true;
  return true;
}

// Ask FlightAware for this flight; fills rc with the leg that is in the air now.
// v0.9.38: faRouteR() also says why not: FA_R_NOTYET = it has the flight but not airborne yet (just took off).
enum { FA_R_OK = 1, FA_R_NOTYET, FA_R_NONE, FA_R_ERR };
int faLastR = 0;
int faRouteR(const char* callsign, RouteCache& rc);
bool faRoute(const char* callsign, RouteCache& rc) { return faRouteR(callsign, rc) == FA_R_OK; }
int faRouteR(const char* callsign, RouteCache& rc) {
  String body;
  faLastAt = millis(); if (!faLastAt) faLastAt = 1;
  faTimes[faTimesNext] = faLastAt; faTimesNext = (faTimesNext + 1) % FA_PER_MIN;
  faCount++; faSaveCounts();
  netStats.faAsked++;
  bool ok = httpGetKey("https://aeroapi.flightaware.com/aeroapi/flights/" + String(callsign) + "?max_pages=1", body, 8000, faKey);
  faLastErr = ok ? 200 : lastHttpCode;
  if (!ok) return faLastR = FA_R_ERR;
  JsonDocument filter;
  for (const char* end : {"origin", "destination"}) {
    filter["flights"][0][end]["code_icao"] = true;
    filter["flights"][0][end]["code_iata"] = true;
    filter["flights"][0][end]["city"]      = true;
  }
  filter["flights"][0]["actual_off"] = true;
  filter["flights"][0]["actual_on"]  = true;
  filter["flights"][0]["cancelled"]  = true;
  JsonDocument doc;
  if (deserializeJson(doc, body, DeserializationOption::Filter(filter))) return faLastR = FA_R_ERR;
  body = String();
  JsonObject best; const char* bestOff = "";
  bool waiting = false;                                          // a leg that hasn't taken off (in FlightAware's eyes) yet
  for (JsonObject f : doc["flights"].as<JsonArray>()) {          // the leg in the air now: off the ground, not landed
    const char* off = f["actual_off"] | (const char*)"";
    if (f["cancelled"] | false) continue;
    if (!off[0] && f["actual_on"].isNull()) waiting = true;
    if (!off[0] || !f["actual_on"].isNull()) continue;
    if (strcmp(off, bestOff) > 0) { best = f; bestOff = off; }   // ISO times: the latest take-off wins
  }
  if (best.isNull()) return faLastR = waiting ? FA_R_NOTYET : FA_R_NONE;
  RouteCache r = rc;
  r.n = 0;
  for (const char* end : {"origin", "destination"}) {
    JsonObject ap = best[end];
    const char* icao = ap["code_icao"] | (const char*)"";
    const char* iata = ap["code_iata"] | (const char*)"";
    const Airport* a = icao[0] ? findAirportByIdent(icao) : nullptr;
    if (!a && iata[0]) a = findAirport(iata);
    if (!a) return faLastR = FA_R_NONE;                          // need its position for the route line
    copyStr(r.code[r.n], sizeof r.code[0], iata[0] ? iata : a->code);
    noteAlias(r.code[r.n], icao);
    const char* city = ap["city"] | (const char*)"";
    copyStr(r.city[r.n], sizeof r.city[0], city[0] ? city : a->city);
    r.lat[r.n] = a->lat; r.lon[r.n] = a->lon;
    r.n++;
  }
  r.fromFa = true;
  rc = r;
  return faLastR = FA_R_OK;
}

const float FIT_END_MI = 60, FIT_HEADING_DEG = 60;   // v0.9.37: 60 (was 90)
const long  FIT_LOW_FT = 18000;   // v0.9.37: below this a plane is near one end: within 40 mi + 5 mi per 1,000 ft   // v0.9.34
// A route fits if the plane isn't too far off the line between the airports:
// excess distance <= direct x 0.25 + 60 miles (from v29)
// v0.9.34: ...and, away from both ends, it is flying towards the destination (within 90 degrees). A flight number
// reused on another route, or the return trip, no longer passes because it happens to be between the two airports.
float angleOff(float a, float b);
bool fitLeg(const RouteCache& rc, const Plane& p, int& a, int& b) {
  float bestExcess = 1e9;
  bool found = false;
  int whyHdg = -1; bool whyLow = false;           // v0.9.38: why the route didn't fit, for the route log
  for (int i = 0; i < rc.n; i++)
    for (int j = i + 1; j < rc.n; j++) {
      float fromA  = distanceMiles(rc.lat[i], rc.lon[i], p.lat, p.lon);
      float toB    = distanceMiles(p.lat, p.lon, rc.lat[j], rc.lon[j]);
      float direct = distanceMiles(rc.lat[i], rc.lon[i], rc.lat[j], rc.lon[j]);
      float excess = fromA + toB - direct;
      if (excess > direct * 0.25 + 60 || excess >= bestExcess) continue;
      if (p.gs > 100 && fromA > FIT_END_MI && toB > FIT_END_MI &&
          angleOff(p.track, bearingDeg(p.lat, p.lon, rc.lat[j], rc.lon[j])) > FIT_HEADING_DEG) {   // flying the other way
        int o = (int)angleOff(p.track, bearingDeg(p.lat, p.lon, rc.lat[j], rc.lon[j])); if (o > whyHdg) whyHdg = o;
        continue;
      }
      if (p.altFt < FIT_LOW_FT && fminf(fromA, toB) > 40.0f + p.altFt / 1000.0f * 5.0f) { whyLow = true; continue; }   // v0.9.37: too low for mid-route
      bestExcess = excess; a = i; b = j; found = true;
    }
  if (!found) {
    if (whyHdg >= 0) snprintf(fitWhy, sizeof fitWhy, "heading %d deg off", whyHdg);
    else if (whyLow) snprintf(fitWhy, sizeof fitWhy, "too low for mid-route");
    else snprintf(fitWhy, sizeof fitWhy, "not on the way");
  }
  return found;
}

// Airline name for a 3-letter code from adsbdb, for flights whose route it doesn't know
const char* lookupAirline(const char* callsign) {
  char icao[4] = { callsign[0], callsign[1], callsign[2], 0 };
  for (int i = 0; i < AIRLINE_CACHE_SIZE; i++)
    if (strcmp(airlineCache[i].icao, icao) == 0) return airlineCache[i].name;
  AirlineCache& ac = airlineCache[airlineCacheNext];
  airlineCacheNext = (airlineCacheNext + 1) % AIRLINE_CACHE_SIZE;
  copyStr(ac.icao, sizeof ac.icao, icao);
  ac.name[0] = '\0';
  String body;
  if (httpGet("https://api.adsbdb.com/v0/airline/" + String(icao), body, 5000)) {
    JsonDocument doc;
    if (!deserializeJson(doc, body)) copyStr(ac.name, sizeof ac.name, doc["response"][0]["name"] | "");
  }
  return ac.name;
}

int8_t airportStatus(Plane& p, const Place& apt);
uint32_t placeKey(const Place& apt);
void seenPut(const Plane& p, uint32_t key, uint32_t now);
void inferLeg(Plane& p, const Place& apt, const RouteCache* rc);
void setLeg(Plane& p, const RouteCache* rc);
void nearAirportLeg(Plane& p, const RouteCache* rc);

// Route lookup, then (airport mode) arriving / departing / overflight
void rlogAdd(const Plane& p);
void addRoute(Plane& p, const Place& place) {
  const RouteCache* rc = nullptr;
  rlFree[0] = rlFa[0] = 0;
  faPlaceOk = faUseI == 3 || (faUseI >= 1 && isHomePlace(place)) || (faUseI == 2 && place.isAirport);   // v0.9.38
  faPlace = &place;                                                                                       // v0.9.40
  if (isAirlineCallsign(p.callsign)) {                 // private planes: no lookup
    rc = getRoute(p.callsign);
    setLeg(p, rc);
  }
  p.status = place.isAirport ? airportStatus(p, place) : ST_NONE;
  if (place.isAirport) { seenPut(p, placeKey(place), millis()); inferLeg(p, place, rc); }
  else if (rc) nearAirportLeg(p, rc);                  // v0.9.32: Home / other places
  if (rc) rlogAdd(p);
}

// v0.9.38: route log. One line when what's shown for a flight changes, or FlightAware was involved.
bool rlogInit() {
  if (rlog) return true;
  rlog = (RLog*)ps_malloc(sizeof(RLog) * RLOG_N);
  if (rlog) memset(rlog, 0, sizeof(RLog) * RLOG_N);
  return rlog != nullptr;
}
void rlogAdd(const Plane& p) {
  if (!rlogOn || !rlogInit()) return;
  char shown[24];
  if (p.fits >= 1 && (p.fromCode[0] || p.toCode[0])) snprintf(shown, sizeof shown, "%s%s -> %s", p.fits == 2 ? "~ " : "", p.fromCode[0] ? p.fromCode : "?", p.toCode[0] ? p.toCode : "?");
  else copyStr(shown, sizeof shown, "(blank)");
  for (int k = 1; k <= rlogCount && k <= RLOG_N; k++) {          // the last line for this flight
    const RLog& e = rlog[(rlogNext - k + RLOG_N) % RLOG_N];
    if (strcmp(e.cs, p.callsign)) continue;
    if (!strcmp(e.shown, shown) && (!rlFa[0] || !strcmp(e.fa, rlFa) || !strncmp(rlFa, "not asked", 9) || !strncmp(rlFa, "retry in", 8) || !strncmp(rlFa, "route from", 10))) return;
    break;
  }
  RLog& e = rlog[rlogNext]; rlogNext = (rlogNext + 1) % RLOG_N; if (rlogCount < RLOG_N) rlogCount++;
  e.t = time(nullptr); copyStr(e.cs, sizeof e.cs, p.callsign); copyStr(e.type, sizeof e.type, p.type);
  e.alt = p.altFt; e.nm = p.distNm;
  copyStr(e.freeR, sizeof e.freeR, rlFree); copyStr(e.fa, sizeof e.fa, rlFa[0] ? rlFa : "not needed"); copyStr(e.shown, sizeof e.shown, shown);
}
void rlogText(String& s) {
  char line[200], t[12];
  snprintf(line, sizeof line, "\nRoute log (newest first, last %d decisions; cleared on restart)\n", RLOG_N); s += line;
  if (!rlogOn) { s += "  off (Settings > Better Routes)\n"; return; }
  if (!rlog || !rlogCount) { s += "  nothing yet\n"; return; }
  for (int k = 1; k <= rlogCount; k++) {
    const RLog& e = rlog[(rlogNext - k + RLOG_N) % RLOG_N];
    struct tm tm; localtime_r(&e.t, &tm); strftime(t, sizeof t, "%H:%M:%S", &tm);
    char alt[16]; formatAlt(e.alt, alt, sizeof alt);
    snprintf(line, sizeof line, "%s  %-8s %-5s %7s ft %5.1f NM  free: %s  |  FA: %s  |  shown: %s\n", t, e.cs, e.type, alt, e.nm, e.freeR, e.fa, e.shown);
    s += line;
  }
}

void setLeg(Plane& p, const RouteCache* rc) {
  if (rc->airline[0]) copyStr(p.airline, sizeof p.airline, rc->airline);
  else if (!findAirline(p.callsign) && !p.mil) copyStr(p.airline, sizeof p.airline, lookupAirline(p.callsign));
  int a = 0, b = rc->n - 1;
  bool known = rc->n >= 2;                                       // v0.9.37: no route at all also asks FlightAware
  // v0.9.38: the free route, for the route log
  if (!known) copyStr(rlFree, sizeof rlFree, "none");
  else { rlFree[0] = 0; for (int i = 0; i < rc->n && i < 3; i++) { if (i) strncat(rlFree, " -> ", sizeof rlFree - strlen(rlFree) - 1); strncat(rlFree, rc->code[i], sizeof rlFree - strlen(rlFree) - 1); } }
  bool fitsFree = known && fitLeg(*rc, p, a, b);
  RouteCache* w = const_cast<RouteCache*>(rc);                   // v0.9.35: Better Routes works on the cached route
  if (known && !rc->fromFa) {                                    // v0.9.40: a free route turned down stays turned down
    uint32_t now = millis();
    if (!fitsFree) w->freeBadMs = now ? now : 1;
    else if (rc->freeBadMs && now - rc->freeBadMs < FREE_BAD_MS) { fitsFree = false; copyStr(fitWhy, sizeof fitWhy, "turned down earlier"); }
  }
  if (known && !rc->fromFa) { strncat(rlFree, fitsFree ? " ok" : ", no: ", sizeof rlFree - strlen(rlFree) - 1); if (!fitsFree) strncat(rlFree, fitWhy, sizeof rlFree - strlen(rlFree) - 1); }
  if (rc->fromFa && fitsFree) copyStr(rlFa, sizeof rlFa, "route from FlightAware (kept)");
  // v0.9.40: details just opened for this plane: ask FlightAware even if the free route fits
  bool user = faAskCs[0] && !strcmp(faAskCs, p.callsign) && millis() - faAskMs < 120000UL && !rc->fromFa;
  bool urgent = faUrgentCs[0] && !strcmp(faUrgentCs, p.callsign) && millis() - faUrgentMs < 300000UL;   // v0.9.43: overhead alert
  if (user) faAskCs[0] = 0;
  if (!fitsFree || (user && !w->faTried)) {               // v0.9.34: a route that doesn't match the plane is never shown
    bool fixed = false;
    if (!w->faTried) {
      FaMemo* m = faMemoFind(p.callsign);                        // v0.9.38: asked before? use that answer
      uint32_t now = millis();
      bool retry = m && m->state == FAM_NOTYET && m->tries < 2;
      bool again = false, ask = false;
      if (m && !retry) {
        if (m->state == FAM_ROUTE) {
          RouteCache fa = *w;
          if (faFromMemo(*m, fa) && fitLeg(fa, p, a, b)) { *w = fa; w->faTried = true; fixed = true; faRepeats++; snprintf(rlFa, sizeof rlFa, "from memory: %s -> %s (no charge)", m->from, m->to); }
          else if (m->tries < 2 && now - m->ms >= FA_RETRY_MS) again = ask = true;   // v0.9.40: same flight number, another trip: ask once more
          else { w->faTried = true; faRepeats++; snprintf(rlFa, sizeof rlFa, "from memory: %s -> %s, doesn't fit", m->from, m->to); }
        } else { w->faTried = true; faRepeats++; copyStr(rlFa, sizeof rlFa, m->state == FAM_NOTYET ? "asked twice, never airborne" : "asked before: no route"); }
      } else if (retry && now - m->ms < FA_RETRY_MS) {
        copyStr(rlFa, sizeof rlFa, "retry in a few minutes");
      } else ask = true;
      if (ask) {
        const char* why = faWhyNot(user || urgent);
        if (!why && !user && !urgent && !again) why = faFlightWhyNot(p);     // v0.9.40: Which flights
        if (why) snprintf(rlFa, sizeof rlFa, "not asked: %s", why);
        else {
          w->faTried = true;
          RouteCache fa = *w;
          int r = faRouteR(p.callsign, fa);
          FaMemo* mm = faMemoPut(p.callsign);
          if (mm) { mm->tries++; mm->ms = now; }
          const char* how = again ? "asked again" : retry ? "retry" : user ? "details opened" : urgent ? "overhead alert" : "asked";
          if (r == FA_R_OK) {
            if (mm) { mm->state = FAM_ROUTE; copyStr(mm->from, sizeof mm->from, fa.code[0]); copyStr(mm->to, sizeof mm->to, fa.code[fa.n - 1]); }
            if (fitLeg(fa, p, a, b)) {
              *w = fa; w->faTried = true; fixed = true;
              faFixed++; faSaveCounts();
              snprintf(rlFa, sizeof rlFa, "%s: %s -> %s (fixed)", how, fa.code[0], fa.code[fa.n - 1]);
            } else snprintf(rlFa, sizeof rlFa, "%s: %s -> %s, doesn't fit", how, fa.code[0], fa.code[fa.n - 1]);
          } else if (r == FA_R_NOTYET || r == FA_R_ERR) {
            if (mm) mm->state = FAM_NOTYET;
            if (mm && mm->tries < 2) w->faTried = false;         // one more go, 3 minutes on
            if (r == FA_R_ERR) snprintf(rlFa, sizeof rlFa, "%s: error %d", how, faLastErr);
            else snprintf(rlFa, sizeof rlFa, "%s: not airborne yet", how);
          } else {
            if (mm) mm->state = FAM_NOROUTE;
            snprintf(rlFa, sizeof rlFa, "%s: no route", how);
          }
        }
      }
    }
    if (!fixed) {
      if (!fitsFree) { if (known) { p.fits = -1; p.legNote = LEG_MISMATCH; } return; }
      fitLeg(*rc, p, a, b);                                      // details opened, FlightAware didn't help: keep the free route
    }
  }
  p.fits = 1;
  p.routeFa = rc->fromFa;                                        // v0.9.39
  copyStr(p.fromCode, sizeof p.fromCode, rc->code[a]);
  copyStr(p.toCode,   sizeof p.toCode,   rc->code[b]);
  copyStr(p.fromCity, sizeof p.fromCity, rc->city[a]);
  copyStr(p.toCity,   sizeof p.toCity,   rc->city[b]);
}

