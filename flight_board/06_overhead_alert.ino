// Flight Board: Overhead alert: the background task's half (spotting the plane).
// One tab of the flight_board sketch. Arduino joins the tabs in name order, after flight_board.ino.
// ---------------------------------------------------------------------
//  Overhead alert: the background task's half
// ---------------------------------------------------------------------
int homeIndex() { return (!noPlaces && numPlaces > 0 && isHomePlace(places[0])) ? 0 : -1; }

// Ground height near a spot: the closest airport's elevation (within 30 NM), else 0
long groundFtNear(double lat, double lon) {
  float best = 30 * 1.15078f; long ft = 0;
  for (int i = 0; i < NUM_AIRPORTS; i++) {
    if (fabs(AIRPORTS[i].lat - lat) > 0.6 || fabs(AIRPORTS[i].lon - lon) > 0.9) continue;
    float d = distanceMiles(lat, lon, AIRPORTS[i].lat, AIRPORTS[i].lon);
    if (d < best) { best = d; ft = AIRPORTS[i].elevFt; }
  }
  return ft;
}

// Find the aircraft that will pass closest-soonest over Home, within the alert settings.
// idx/count: the update just made (if it was for Home, it's reused; otherwise Home gets a quick look of its own).
void checkOverhead(int idx, int count, unsigned long at, int src) {
  int h = homeIndex();
  bool was = alertShared.valid;
  if (!alertOn || h < 0) {
    if (was) { xSemaphoreTake(dataLock, portMAX_DELAY); alertShared.valid = false; alertGen++; xSemaphoreGive(dataLock); }
    return;
  }
  xSemaphoreTake(dataLock, portMAX_DELAY);
  Place home = places[h];
  xSemaphoreGive(dataLock);
  if (!home.resolved) return;
  int n = count;
  if (idx != h) {                            // the board shows somewhere else: a small look around Home
    home.radiusNm = ALERT_FETCH_NM;
    n = -1;
    for (int k = 0; k < 2 && n < 0; k++) n = fetchFrom((src + k) % 3, home, workList);
    at = millis();
  }
  if (n < 0) return;                         // no news: keep the last prediction

  static double gLat = 999, gLon = 999; static long gFt = 0;
  if (fabs(gLat - home.lat) > 0.001 || fabs(gLon - home.lon) > 0.001) { gLat = home.lat; gLon = home.lon; gFt = groundFtNear(gLat, gLon); }

  int u = unitsMode;
  float limit = ALERT_DIST_NM[u][alertDistI];
  long below  = ALERT_BELOW_FT[u][alertBelowI];
  int  warn   = ALERT_WARN_S[alertWarnI];
  AlertInfo a; memset(&a, 0, sizeof a);
  float bestT = 1e9f;
  for (int i = 0; i < n; i++) {
    const Plane& p = workList[i];
    if (p.gs < 40) continue;                                  // parked, hovering or no speed
    long hgt = p.altFt - gFt;
    if (hgt < 200) continue;                                  // on (or about to touch) the ground
    if (below && hgt > below) continue;
    float b = p.bearing * DEG_TO_RAD, x = p.distNm * sinf(b), y = p.distNm * cosf(b);
    float v = p.gs / 3600.0f, t = p.track * DEG_TO_RAD, vx = v * sinf(t), vy = v * cosf(t);
    float tc = -(x * vx + y * vy) / (v * v);                  // seconds to the closest point (straight line)
    float dx = x + vx * tc, dy = y + vy * tc, dmin = sqrtf(dx * dx + dy * dy);
    if (dmin > limit) continue;
    if (tc < -(ALERT_NOW_S + ALERT_PASS_S) || tc > warn + fetchSeconds + 15) continue;   // passed, or too far off
    if (tc >= bestT) continue;
    bestT = tc;
    a.valid = true; a.p = p; a.x = x; a.y = y; a.vx = vx; a.vy = vy; a.tca = tc; a.dmin = dmin; a.homeFt = gFt; a.at = at;
  }
  if (a.valid) {
    if (isAirlineCallsign(a.p.callsign) && !a.p.routeFa) {     // v0.9.43: the alert plane may skip the pacing
      if (strcmp(faUrgentCs, a.p.callsign)) { copyStr(faUrgentCs, sizeof faUrgentCs, a.p.callsign); faUrgentMs = millis(); }
    }
    if (a.p.fits != 1 && isAirlineCallsign(a.p.callsign)) addRoute(a.p, home);   // airline and route, if known
    Serial.printf("Overhead: %s in %.0f s, %.2f NM\n", a.p.callsign, a.tca, a.dmin);
  }
  if (!a.valid && !was) return;
  xSemaphoreTake(dataLock, portMAX_DELAY);
  alertShared = a;
  alertGen++;
  xSemaphoreGive(dataLock);
}



// ---------------------------------------------------------------------
//  v0.9.44: Squawk codes. The background task keeps a short list of aircraft squawking an emergency code.
//  They come from the board's own updates and, for the distance settings, from asking the data source
//  which aircraft are squawking one code (7700 every minute, the others every 3 minutes, one call per 30 s).
//  An aircraft counts once it's been seen in two separate updates.
// ---------------------------------------------------------------------
bool sqEmergency(int c) { return c == 7700 || c == 7600 || c == 7500 || c == 7400; }

// Short meaning (details page) and tag (board list). level: 0 everyday, 1 special, 2 emergency
const char* sqMeaning(int c, int* level) {
  int l = 0; const char* m = "";                // v0.9.46: an ordinary ATC code gets no tag (was "TALKING TO ATC")
  if (c == 7700) { l = 2; m = "EMERGENCY"; } else if (c == 7600) { l = 2; m = "RADIO FAILURE"; }
  else if (c == 7500) { l = 2; m = "HIJACK"; } else if (c == 7400) { l = 2; m = "LOST LINK"; }
  else if (c == 1255) { l = 1; m = "FIREFIGHTING"; } else if (c == 1277) { l = 1; m = "SEARCH & RESCUE"; }
  else if (c == 4000) { l = 1; m = "MILITARY"; } else if (c == 7777) { l = 1; m = "INTERCEPT"; }
  else if (c == 1200) m = "VFR"; else if (c == 1202) m = "GLIDER";
  if (level) *level = l;
  return m;
}
const char* sqTag(int c) {
  return c == 1255 ? "FIRE" : c == 1277 ? "SAR" : (c == 4000 || c == 7777) ? "MIL" : nullptr;
}

String sqkUrl(int s, int code) {
  if (s == 1) return "https://opendata.adsb.fi/api/v2/sqk/" + String(code);
  if (s == 2) return "https://api.airplanes.live/v2/squawk/" + String(code);
  return "https://api.adsb.lol/v2/sqk/" + String(code);
}

// Note one sighting (background task). onBoard: from the board's own update (Scan area counts those only)
void emergNote(const Plane& p, const Place& home, bool homeOk, uint32_t now) {
  float nm = homeOk ? distanceMiles(home.lat, home.lon, p.lat, p.lon) / 1.15078f : 0;
  float brg = homeOk ? bearingDeg(home.lat, home.lon, p.lat, p.lon) : 0;
  if (emergI >= 2 && (!homeOk || nm > EMERG_NM[emergI])) return;
  xSemaphoreTake(dataLock, portMAX_DELAY);
  int k = -1;
  for (int i = 0; i < emergSharedN; i++) if (emergShared[i].icao == p.icao) k = i;
  if (k < 0 || emergShared[k].code != p.squawk) {
    if (k < 0) { if (emergSharedN >= MAX_EMERG) { xSemaphoreGive(dataLock); return; } k = emergSharedN++; }
    Emerg& e = emergShared[k]; memset(&e, 0, sizeof e);
    e.icao = p.icao; e.code = p.squawk; e.hits = 1; e.firstMs = now;
  } else if (now - emergShared[k].lastMs > 5000) emergShared[k].hits = emergShared[k].hits < 9 ? emergShared[k].hits + 1 : 9;
  Emerg& e = emergShared[k];
  bool routed = e.routed; Plane keep = e.p;
  e.lastMs = now; e.homeNm = nm; e.homeBrg = brg; e.p = p;
  if (routed && keep.fits == 1) {                              // keep the route found earlier
    e.p.fits = 1; e.p.routeFa = keep.routeFa;
    copyStr(e.p.fromCode, sizeof e.p.fromCode, keep.fromCode); copyStr(e.p.toCode, sizeof e.p.toCode, keep.toCode);
    copyStr(e.p.fromCity, sizeof e.p.fromCity, keep.fromCity); copyStr(e.p.toCity, sizeof e.p.toCity, keep.toCity);
  }
  emergGen++;
  xSemaphoreGive(dataLock);
}

// The board's own update (before checkOverhead reuses workList)
void emergFeed(const Plane* list, int n, int idx) {
  if (!emergI) return;
  int h = homeIndex();
  Place home; bool homeOk = false;
  if (h >= 0) { xSemaphoreTake(dataLock, portMAX_DELAY); home = places[h]; xSemaphoreGive(dataLock); homeOk = home.resolved; }
  uint32_t now = millis();
  for (int i = 0; i < n; i++) {
    if (sqEmergency(list[i].squawk)) emergNote(list[i], home, homeOk, now);
    else {                                                     // changed its code: no longer an emergency
      xSemaphoreTake(dataLock, portMAX_DELAY);
      for (int k = 0; k < emergSharedN; k++)
        if (emergShared[k].icao == list[i].icao) { emergShared[k] = emergShared[--emergSharedN]; emergGen++; break; }
      xSemaphoreGive(dataLock);
    }
  }
  (void)idx;
}

// Ask the data source about one code every 30 s (distance settings), then tidy the list
void emergPoll(int src) {
  uint32_t now = millis();
  int h = homeIndex();
  static uint32_t lastPoll = 0; static int cyc = 0;
  const int CYCLE[6] = { 7700, 7600, 7700, 7500, 7700, 7400 };
  if (emergI >= 2 && h >= 0 && WiFi.status() == WL_CONNECTED && (!lastPoll || now - lastPoll >= EMERG_POLL_MS)) {
    lastPoll = now;
    Place home; xSemaphoreTake(dataLock, portMAX_DELAY); home = places[h]; xSemaphoreGive(dataLock);
    int code = CYCLE[cyc]; cyc = (cyc + 1) % 6;
    xSemaphoreTake(dataLock, portMAX_DELAY);                   // a first sighting? check that code again first
    for (int k = 0; k < emergSharedN; k++) if (emergShared[k].hits == 1) { code = emergShared[k].code; break; }
    xSemaphoreGive(dataLock);
    if (home.resolved) {
      int n = -1;
      (void)src;                                               // v0.9.45: adsb.fi, then airplanes.live; adsb.lol is kept for the board's own updates
      for (int k = 1; k <= 2 && n < 0; k++) { crumb.netSrc = k; n = fetchUrl(k, sqkUrl(k, code), home, workList); }
      for (int i = 0; i < n; i++) if (workList[i].squawk == code) emergNote(workList[i], home, true, now);
    }
  }
  // tidy: forget ones not seen for a while; look up the route once confirmed
  Plane need; bool doRoute = false; Place home;
  xSemaphoreTake(dataLock, portMAX_DELAY);
  for (int k = 0; k < emergSharedN; ) {
    const Emerg& e = emergShared[k];
    unsigned long keep = emergI == 1 ? 120000UL : e.code == 7700 ? 180000UL : 420000UL;
    if (now - e.lastMs > keep || !emergI) { emergShared[k] = emergShared[--emergSharedN]; emergGen++; }
    else k++;
  }
  for (int k = 0; k < emergSharedN && !doRoute; k++)
    if (emergShared[k].hits >= 2 && !emergShared[k].routed && isAirlineCallsign(emergShared[k].p.callsign) && h >= 0) {
      emergShared[k].routed = true; need = emergShared[k].p; doRoute = true; home = places[h];
    }
  xSemaphoreGive(dataLock);
  if (doRoute) {
    addRoute(need, home);
    xSemaphoreTake(dataLock, portMAX_DELAY);
    for (int k = 0; k < emergSharedN; k++)
      if (emergShared[k].icao == need.icao && need.fits == 1) {
        Plane& q = emergShared[k].p; q.fits = 1; q.routeFa = need.routeFa;
        copyStr(q.fromCode, sizeof q.fromCode, need.fromCode); copyStr(q.toCode, sizeof q.toCode, need.toCode);
        copyStr(q.fromCity, sizeof q.fromCity, need.fromCity); copyStr(q.toCity, sizeof q.toCity, need.toCity);
        if (!q.airline[0]) copyStr(q.airline, sizeof q.airline, need.airline);
        emergGen++;
      }
    xSemaphoreGive(dataLock);
  }
}
