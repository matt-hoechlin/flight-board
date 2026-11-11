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
    if (!a.p.airline[0] && a.p.fits < 0) addRoute(a.p, home);   // airline and route, if known
    Serial.printf("Overhead: %s in %.0f s, %.2f NM\n", a.p.callsign, a.tca, a.dmin);
  }
  if (!a.valid && !was) return;
  xSemaphoreTake(dataLock, portMAX_DELAY);
  alertShared = a;
  alertGen++;
  xSemaphoreGive(dataLock);
}

