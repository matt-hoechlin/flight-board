// Flight Board: Arriving / departing / overflight, and legs for planes near airports.
// One tab of the flight_board sketch. Arduino joins the tabs in name order, after flight_board.ino.
// ---------------------------------------------------------------------
//  Airport mode: arriving, departing or flying over (v0.9.27, tuned on recorded SFO + RNO traffic)
//  What the plane is doing beats the schedule (flight numbers are reused for the trip back):
//   1. At the field (within 2 NM, below 400 ft): rolling on the runway or just off it. It keeps the
//      label it had at the last update; new to the board, it's taking off.
//   2. Climbing / descending, below a ceiling that grows with distance (jets are 8,000-10,000 ft up
//      4-6 NM out): departing / arriving, unless a nearer airfield explains it better.
//   3. Once arriving (or departing), it stays so while it keeps descending or level (climbing or level).
//   4. Otherwise the schedule, if the plane isn't doing the opposite; otherwise overhead / overflight.
// ---------------------------------------------------------------------
const float FIELD_NM        = 2.0;    // "at the field": this close...
const long  FIELD_AGL_FT    = 400;    // ...and this low
const long  ARR_CEIL_FT     = 6000, ARR_CEIL_PER_NM = 500,  ARR_CEIL_MAX = 12000;   // highest "arriving" at a distance
const long  DEP_CEIL_FT     = 6000, DEP_CEIL_PER_NM = 1200, DEP_CEIL_MAX = 16000;   // highest "departing"
const long  FLOOR_PER_NM    = 200,  FLOOR_LESS_FT   = 500;  // lowest a plane landing here / leaving here would be
const unsigned long SEEN_KEEP_MS = 5UL * 60000UL;           // forget an aircraft after this long
const float AWAY_DEG = 75, LINED_UP_DEG = 25;                 // "moving away from" / "pointed at" a field

long arrCeil(float d) { long c = ARR_CEIL_FT + (long)(ARR_CEIL_PER_NM * d); return c > ARR_CEIL_MAX ? ARR_CEIL_MAX : c; }
long depCeil(float d) { long c = DEP_CEIL_FT + (long)(DEP_CEIL_PER_NM * d); return c > DEP_CEIL_MAX ? DEP_CEIL_MAX : c; }
long pathFloor(float d) { return (long)(FLOOR_PER_NM * d) - FLOOR_LESS_FT; }

// The last update's view of each aircraft, per place
struct SeenPlane { uint32_t icao, place, ms; int32_t altFt; int8_t status; float distNm; };
const int SEENPL_MAX = 96;
SeenPlane seenPl[SEENPL_MAX];
uint32_t placeKey(const Place& apt) { uint32_t h = 2166136261u; for (const char* c = apt.code; *c; c++) h = (h ^ (uint8_t)*c) * 16777619u; return h | 1; }
int seenFind(uint32_t icao, uint32_t key, uint32_t now) {          // index, or -1 (plain types: Arduino IDE prototypes)
  for (int i = 0; i < SEENPL_MAX; i++) {
    const SeenPlane& e = seenPl[i];
    if (e.icao == icao && e.place == key && now - e.ms >= 1000 && now - e.ms <= SEEN_KEEP_MS) return i;   // an earlier update
  }
  return -1;
}
void seenPut(const Plane& p, uint32_t key, uint32_t now) {
  int slot = 0; uint32_t oldest = 0;
  for (int i = 0; i < SEENPL_MAX; i++) {
    if (seenPl[i].icao == p.icao && seenPl[i].place == key) { slot = i; break; }
    uint32_t age = seenPl[i].icao ? now - seenPl[i].ms : 0xFFFFFFFFu;
    if (age >= oldest) { oldest = age; slot = i; }
  }
  seenPl[slot] = { p.icao, key, now, (int32_t)p.altFt, p.status, p.distNm };
}

// Airfields near the place (worked out once per place)
const int NEAR_MAX = 12;
const float NEAR_NM = 25;
const Airport* nearFields[NEAR_MAX]; int numNear = 0; uint32_t nearKey = 0;
void loadNearFields(const Place& apt, uint32_t key) {
  if (nearKey == key) return;
  nearKey = key; numNear = 0;
  for (int i = 0; i < NUM_AIRPORTS && numNear < NEAR_MAX; i++) {
    const Airport& a = AIRPORTS[i];
    if (!strcmp(a.code, apt.code) || !strcmp(a.ident, apt.code)) continue;
    float d = distanceMiles(apt.lat, apt.lon, a.lat, a.lon) / 1.15078f;
    if (d > 0.5f && d <= NEAR_NM) nearFields[numNear++] = &a;
  }
}
float angleOff(float a, float b) { float d = fabsf(fmodf(a - b + 540.0f, 360.0f) - 180.0f); return d; }
// Moving away from a field (a departure keeps going roughly the way it left)
bool awayFrom(const Plane& p, double lat, double lon) { return p.gs > 30 && angleOff(p.track, bearingDeg(lat, lon, p.lat, p.lon)) <= AWAY_DEG; }
// Pointed at the place (lined up for it)
bool linedUp(const Plane& p) { return p.gs > 30 && angleOff(p.track, fmodf(p.bearing + 180.0f, 360.0f)) <= LINED_UP_DEG; }

// A nearer airfield explains this climb / descent better: the plane is clearly closer to it, low enough to be
// using it, and: (departing) moving away from it, or (arriving) too low for the place or right next to that field.
bool otherFieldExplains(const Plane& p, float d, long agl, bool arriving) {
  for (int i = 0; i < numNear; i++) {
    const Airport& a = *nearFields[i];
    float dx = distanceMiles(p.lat, p.lon, a.lat, a.lon) / 1.15078f;
    if (dx >= d - 1) continue;                                   // not clearly nearer
    long aglx = p.altFt - a.elevFt;
    if (aglx > (arriving ? arrCeil(dx) : depCeil(dx))) continue;  // too high for it
    if (agl < pathFloor(d)) return true;                          // too low to be the place's traffic
    if (arriving ? dx < d / 3 : awayFrom(p, a.lat, a.lon)) return true;
  }
  return false;
}

// v0.9.33: helicopters. They hover, fly level and land on pads next to the airport (Las Vegas tour
// helicopters land at a terminal about 1 NM from LAS), so climb rate says little. Instead: only close in
// (HELI_NM) and low (HELI_AGL_FT); closing in on the place = arriving; moving out after lifting off here =
// departing. Farther out or higher, a helicopter is never called arriving / departing.
const float HELI_NM = 4.0, HELI_MOVE_NM = 0.1;   // how close; how much the distance must change per update
const long  HELI_AGL_FT = 1500;
const long  HELI_STEP_FT = 200;      // a real climb / descent since the last update
bool isHeliType(const char* t) {
  static const char* const H[] = { "EC20","EC25","EC30","EC35","EC45","EC55","EC75","AS32","AS3B","AS50","AS55","AS65",
    "B06","B06T","B407","B412","B427","B429","B430","B47G","B505","B212","B222","B230","R22","R44","R66","H500","H269",
    "MD52","MD60","EXPL","A109","A119","A139","A149","A169","A189","S76","S92","S61","H60","UH1","H47","H64","BK17",
    "EN28","EN48","NH90","MI8","KMAX","H160","G2CA" };
  if (!t || !t[0]) return false;
  for (const char* h : H) if (strcmp(t, h) == 0) return true;
  return false;
}
int8_t heliStatus(const Plane& p, int mi, int8_t prev, long agl, float d, long elev) {   // mi: seenPl index or -1
  const SeenPlane* m = mi >= 0 ? &seenPl[mi] : nullptr;
  int8_t far = d <= OVERHEAD_NM ? ST_OVERHEAD : ST_OVERFLIGHT;
  if (d > HELI_NM || agl > HELI_AGL_FT) return far;
  long dAlt = m ? p.altFt - m->altFt : 0;                         // since the last update
  bool atPad  = d <= FIELD_NM && agl < FIELD_AGL_FT;              // the pad can be a mile from the airport's point
  bool wasPad = m && m->distNm <= FIELD_NM && m->altFt - elev < FIELD_AGL_FT;
  bool closing = m && p.distNm < m->distNm - HELI_MOVE_NM;
  bool opening = m && p.distNm > m->distNm + HELI_MOVE_NM;
  bool down = dAlt <= -HELI_STEP_FT, up = dAlt >= HELI_STEP_FT;
  // Coming down to the pad: lower than last time, near the field, getting closer (or already there)
  if (down && d <= FIELD_NM + 0.5f && (closing || atPad)) return ST_ARRIVING;
  // Going up from the pad: it was down there last time, or it's climbing out right next to it
  if ((wasPad && (up || opening) && !atPad) || (up && atPad && !closing)) return ST_DEPARTING;
  if (atPad && !m) return ST_DEPARTING;                            // new to the board at the pad: it just came up
  // Keep the label while it carries on: departing while it keeps moving out, arriving while it stays near
  if (prev == ST_DEPARTING && (opening || up) && !down) return ST_DEPARTING;
  if (prev == ST_ARRIVING && !opening && d <= FIELD_NM + 0.5f) return ST_ARRIVING;
  if (atPad && (prev == ST_ARRIVING || prev == ST_DEPARTING)) return prev;   // hovering at the pad
  return far;
}

int8_t airportStatus(Plane& p, const Place& apt) {
  uint32_t now = millis(), key = placeKey(apt);
  loadNearFields(apt, key);
  int mi = seenFind(p.icao, key, now);
  const SeenPlane* m = mi >= 0 ? &seenPl[mi] : nullptr;
  if (!p.rateOk && m && now - m->ms >= 20000) {                  // no climb rate sent: work it out
    p.climbRate = (int)((p.altFt - m->altFt) * 60000.0 / (double)(now - m->ms));
    p.rateOk = true;
  }
  int8_t prev = m ? m->status : ST_NONE;
  long agl = p.altFt - apt.elevFt;
  float d = p.distNm;
  bool climbing = p.climbRate >= CLIMB_FPM, descending = p.climbRate <= -CLIMB_FPM;
  if (p.heli) return heliStatus(p, mi, prev, agl, d, apt.elevFt);              // v0.9.33

  // 1. at the field
  if (d <= FIELD_NM && agl < FIELD_AGL_FT) {
    if (climbing) return ST_DEPARTING;                            // a clear climb or descent wins (touch-and-go,
    if (descending) return ST_ARRIVING;                           // go-around); rolling: keep the last label
    if (prev == ST_ARRIVING || prev == ST_DEPARTING) return prev;
    if (m && m->altFt > p.altFt + 200) return ST_ARRIVING;
    return ST_DEPARTING;                                          // new to the board on the runway: taking off
  }
  // 2. climbing / descending near the place, not a nearer field's traffic
  bool arr = descending && agl <= arrCeil(d) && agl >= pathFloor(d);
  bool dep = climbing   && agl <= depCeil(d) && agl >= pathFloor(d);
  // 3. keeps going the same way
  if (prev == ST_ARRIVING  && p.climbRate <  CLIMB_FPM && agl <= ARR_CEIL_MAX) arr = true;
  if (prev == ST_DEPARTING && p.climbRate > -CLIMB_FPM && agl <= DEP_CEIL_MAX) dep = true;
  if (dep && !awayFrom(p, apt.lat, apt.lon)) dep = (prev == ST_DEPARTING);  // a departure heads away from here
  if (arr && !linedUp(p) && otherFieldExplains(p, d, agl, true)) arr = false;   // lined up: it's ours
  if (dep && otherFieldExplains(p, d, agl, false)) dep = false;
  if (arr && !dep) return ST_ARRIVING;
  if (dep && !arr) return ST_DEPARTING;
  // 4. the schedule, unless the plane is clearly doing the opposite
  if (p.fits == 1 && strcmp(p.toCode, apt.code) == 0   && p.climbRate <  500) return ST_ARRIVING;
  if (p.fits == 1 && strcmp(p.fromCode, apt.code) == 0 && p.climbRate > -500 && agl <= depCeil(d)) return ST_DEPARTING;   // could have left here
  return d <= OVERHEAD_NM ? ST_OVERHEAD : ST_OVERFLIGHT;
}

bool isHere(const char* code, const Place& apt, const Airport* a) {
  if (!code[0]) return false;
  return strcasecmp(code, apt.code) == 0 || (a && strcasecmp(code, a->ident) == 0) || (a && findAlias(code) == a);
}

// The plane is clearly landing (or taking off) here, but the leg on show doesn't
// end (or start) here. Trust what we see: this airport is that end of the leg.
//  - Here is on the schedule: take the stop next to it. If the schedule has it
//    the other way round (RNO -> BOS while landing at RNO), it's the return trip.
//  - Here isn't on the schedule (or no schedule): the other end is unknown.
void inferLeg(Plane& p, const Place& apt, const RouteCache* rc) {
  bool arr = p.status == ST_ARRIVING, dep = p.status == ST_DEPARTING;
  if (!arr && !dep) return;
  const Airport* a = findAirport(apt.code);
  if (arr && p.fits >= 0 && isHere(p.toCode, apt, a))   return;   // already shows "-> here"
  if (dep && p.fits >= 0 && isHere(p.fromCode, apt, a)) return;   // already shows "here ->"

  int k = -1, n = rc ? rc->n : 0;
  for (int i = 0; i < n; i++) if (isHere(rc->code[i], apt, a)) k = i;

  const char* otherCode = "";
  const char* otherCity = "";
  if (k >= 0 && n >= 2) {
    bool forward = arr ? k > 0 : k < n - 1;            // schedule runs the same way
    int o = arr ? (forward ? k - 1 : k + 1) : (forward ? k + 1 : k - 1);
    otherCode = rc->code[o];
    otherCity = rc->city[o];
    p.fits = forward ? 1 : 2;                          // on schedule = sure
    p.legNote = forward ? LEG_NONE : LEG_RETURN;
  } else {
    p.fits = 2;
    p.legNote = LEG_OTHER_END_UNKNOWN;
  }
  const char* hereCode = (k >= 0) ? rc->code[k] : apt.code;
  const char* hereCity = (k >= 0 && rc->city[k][0]) ? rc->city[k] : (a ? a->city : "");
  if (arr) {
    copyStr(p.fromCode, sizeof p.fromCode, otherCode); copyStr(p.fromCity, sizeof p.fromCity, otherCity);
    copyStr(p.toCode,   sizeof p.toCode,   hereCode);  copyStr(p.toCity,   sizeof p.toCity,   hereCity);
  } else {
    copyStr(p.fromCode, sizeof p.fromCode, hereCode);  copyStr(p.fromCity, sizeof p.fromCity, hereCity);
    copyStr(p.toCode,   sizeof p.toCode,   otherCode); copyStr(p.toCity,   sizeof p.toCity,   otherCity);
  }
}

// ---------------------------------------------------------------------
//  v0.9.32: Home and other places. An airliner clearly taking off from (or landing at) an airport near you
//  gets that leg, so a SWA flight number that starts the day BWI -> MDW doesn't show BWI -> MDW while it
//  climbs out of Reno. Careful on purpose: a wrong route is worse than an honest "~". All of these must hold:
//   - an airline flight with a route on file that doesn't already fit (fits != 1);
//   - the airport is one that airline routes looked up around you use (so a 737 isn't credited to a small
//     airstrip next to the airport it really used);
//   - within 10 NM of it, below a normal climb-out / approach height for that distance, climbing (or
//     descending) at least 500 fpm, and heading away from it (or pointed at it, within 30 degrees);
//   - the same airport and direction on the update before (one odd reading can't change a route).
//  Level overflights, however low, are never touched.
// ---------------------------------------------------------------------
const float NEAR_APT_NM = 10.0;
const uint32_t NEAR_KEEP_MS = 20UL * 60000UL;   // v0.9.38: how long a guess is kept
const int   NEAR_FPM    = 500;
const float NEAR_TOWARD_DEG = 30;
struct NearSeen { uint32_t icao, ms; char code[5]; int8_t dir;
                  uint32_t keptMs; char kFrom[5], kTo[5]; int8_t kFits, kNote, kNear; };   // v0.9.38: the guess, kept
const int NEARSEEN_MAX = 32;
NearSeen nearSeen[NEARSEEN_MAX];

// What the plane is clearly doing at airport a: 1 = landing there, 2 = taking off from there, 0 = neither
int8_t nearAirportDir(const Plane& p, const Airport& a) {
  if (!p.rateOk || p.gs < 60) return 0;
  float d = distanceMiles(p.lat, p.lon, a.lat, a.lon) / 1.15078f;
  if (d > NEAR_APT_NM) return 0;
  long agl = p.altFt - a.elevFt;
  if (agl < pathFloor(d)) return 0;
  if (p.climbRate >= NEAR_FPM && agl <= depCeil(d) && awayFrom(p, a.lat, a.lon)) return 2;
  if (p.climbRate <= -NEAR_FPM && agl <= arrCeil(d) &&
      angleOff(p.track, bearingDeg(p.lat, p.lon, a.lat, a.lon)) <= NEAR_TOWARD_DEG) return 1;
  return 0;
}

void nearAirportLeg(Plane& p, const RouteCache* rc) {
  if (!rc || !isAirlineCallsign(p.callsign)) return;
  // candidate airports: ones that looked-up airline routes around here use (and the flight's own schedule)
  const Airport* best = nullptr; int8_t bestDir = 0; float bestD = 1e9;
  for (int i = 0; i < CACHE_SIZE + 1; i++) {
    const RouteCache& r = i < CACHE_SIZE ? routeCache[i] : *rc;
    if (!r.callsign[0]) continue;
    for (int k = 0; k < r.n && k < 3; k++) {
      const Airport* a = findAirport(r.code[k]);
      if (!a || a == best) continue;
      int8_t dir = nearAirportDir(p, *a);
      if (!dir) continue;
      float d = distanceMiles(p.lat, p.lon, a->lat, a->lon);
      if (d < bestD) { bestD = d; best = a; bestDir = dir; }
    }
  }
  // Another airfield closer to the plane that explains it just as well (a business jet out of Henderson next to
  // LAS, say): can't tell which, so leave the route alone.
  if (best) {
    float bestNm = bestD / 1.15078f;
    for (int i = 0; i < NUM_AIRPORTS; i++) {
      const Airport& a = AIRPORTS[i];
      if (&a == best || fabs(a.lat - p.lat) > 0.2 || fabs(a.lon - p.lon) > 0.3) continue;   // quick box first
      if (distanceMiles(p.lat, p.lon, a.lat, a.lon) / 1.15078f >= bestNm) continue;          // not closer
      long aglx = p.altFt - a.elevFt;                   // low enough to be using it, going the same way (any heading)
      float dx = distanceMiles(p.lat, p.lon, a.lat, a.lon) / 1.15078f;
      if (aglx >= pathFloor(dx) && (bestDir == 2 ? aglx <= depCeil(dx) : aglx <= arrCeil(dx))) { best = nullptr; break; }
    }
  }
  // two updates in a row, same airport, same direction
  uint32_t now = millis();
  int slot = -1, oldest = 0; uint32_t oldAge = 0;
  for (int i = 0; i < NEARSEEN_MAX; i++) {
    if (nearSeen[i].icao == p.icao) { slot = i; break; }
    uint32_t age = nearSeen[i].icao ? now - nearSeen[i].ms : 0xFFFFFFFFu;
    if (age >= oldAge) { oldAge = age; oldest = i; }
  }
  bool confirmed = false;
  if (slot >= 0) {
    const NearSeen& e = nearSeen[slot];
    confirmed = best && e.dir == bestDir && !strcmp(e.code, best->code) && now - e.ms >= 1000 && now - e.ms <= 150000UL;
  } else { slot = oldest; nearSeen[slot].keptMs = 0; }
  nearSeen[slot].icao = p.icao; nearSeen[slot].ms = now; nearSeen[slot].dir = best ? bestDir : 0;
  copyStr(nearSeen[slot].code, sizeof nearSeen[slot].code, best ? best->code : "");
  if (!confirmed) {                                    // v0.9.38: keep the guess made earlier for this flight
    NearSeen& e = nearSeen[slot];
    bool against = p.rateOk && (e.kNear == 2 ? p.climbRate <= -NEAR_FPM : p.climbRate >= NEAR_FPM);   // went around / turned back
    if (against) e.keptMs = 0;
    if (p.fits != 1 && e.keptMs && now - e.keptMs <= NEAR_KEEP_MS) {
      const Airport* fa = findAirport(e.kFrom); const Airport* ta = findAirport(e.kTo);
      copyStr(p.fromCode, sizeof p.fromCode, e.kFrom); copyStr(p.fromCity, sizeof p.fromCity, fa ? fa->city : "");
      copyStr(p.toCode,   sizeof p.toCode,   e.kTo);   copyStr(p.toCity,   sizeof p.toCity,   ta ? ta->city : "");
      p.fits = e.kFits; p.legNote = e.kNote; p.near = e.kNear;
    }
    return;
  }
  // A leg that already fits is only changed when it contradicts what the plane is doing here: shown as landing
  // at an airport it's clearly climbing out of (BWI -> RNO while departing RNO), or the other way round.
  if (p.fits == 1) {
    bool contra = bestDir == 2 ? !strcasecmp(p.toCode, best->code) : !strcasecmp(p.fromCode, best->code);
    if (!contra) return;
  }

  // on the flight's schedule: the real leg (or the return trip); otherwise "~ Departing RNO Reno"
  bool arr = bestDir == 1;
  int k = -1;
  for (int i = 0; i < rc->n && i < 3; i++) if (!strcasecmp(rc->code[i], best->code) || !strcasecmp(rc->code[i], best->ident)) k = i;
  const char* otherCode = ""; const char* otherCity = "";
  if (k >= 0 && rc->n >= 2) {
    bool forward = arr ? k > 0 : k < rc->n - 1;
    int o = arr ? (forward ? k - 1 : k + 1) : (forward ? k + 1 : k - 1);
    otherCode = rc->code[o]; otherCity = rc->city[o];
    p.fits = forward ? 1 : 2;
    p.legNote = forward ? LEG_NONE : LEG_RETURN;
  } else {
    p.fits = 2;
    p.legNote = LEG_OTHER_END_UNKNOWN;
  }
  const char* hereCode = k >= 0 ? rc->code[k] : best->code;
  const char* hereCity = (k >= 0 && rc->city[k][0]) ? rc->city[k] : best->city;
  if (arr) {
    copyStr(p.fromCode, sizeof p.fromCode, otherCode); copyStr(p.fromCity, sizeof p.fromCity, otherCity);
    copyStr(p.toCode,   sizeof p.toCode,   hereCode);  copyStr(p.toCity,   sizeof p.toCity,   hereCity);
  } else {
    copyStr(p.fromCode, sizeof p.fromCode, hereCode);  copyStr(p.fromCity, sizeof p.fromCity, hereCity);
    copyStr(p.toCode,   sizeof p.toCode,   otherCode); copyStr(p.toCity,   sizeof p.toCity,   otherCity);
  }
  p.near = bestDir;
  NearSeen& e = nearSeen[slot];                        // v0.9.38: remember it for this flight
  e.keptMs = now ? now : 1; copyStr(e.kFrom, sizeof e.kFrom, p.fromCode); copyStr(e.kTo, sizeof e.kTo, p.toCode);
  e.kFits = p.fits; e.kNote = p.legNote; e.kNear = p.near;
}

Plane workList[MAX_PLANES];   // background task's own list

void netTask(void*) {
  wdtWatchMe();
  int goodSource = 0;
  unsigned long lastFetch = 0;
  bool clockSet = false;

  for (;;) {
    // Wait until a fetch is due or asked for
    crumb.netStep = NS_IDLE;
    wdtPet();
    while (fwUpdating || fwAsk == FWA_ASK || fwAsk == FWA_YES ||
           (!fetchNow && millis() - lastFetch < (unsigned long)fetchSeconds * 1000)) { wdtPet(); vTaskDelay(pdMS_TO_TICKS(100)); }
    fetchNow = false;
    lastFetch = millis();

    if (noPlaces) continue;                  // nothing to watch yet
    uint32_t gen = placeGen;
    int fetchedIdx = -1;                     // the place this update was for
    unsigned long fetchedAt = 0;
    char status[64] = "";
    int  count = -1;

    if (WiFi.status() != WL_CONNECTED) {
      crumb.netStep = NS_WIFI;
      WiFi.reconnect();
      for (int i = 0; i < 60 && WiFi.status() != WL_CONNECTED; i++) { wdtPet(); vTaskDelay(pdMS_TO_TICKS(250)); }
    }
    if (WiFi.status() != WL_CONNECTED) {
      snprintf(status, sizeof status, "No WiFi (%s) - retrying", wifiName.c_str());
    } else {
      if (!clockSet) { configTzTime(tzRule(), "pool.ntp.org", "time.nist.gov"); clockSet = true; }

      xSemaphoreTake(dataLock, portMAX_DELAY);
      int idx = curPlace;
      Place place = places[idx];
      xSemaphoreGive(dataLock);

      char fullName[48];
      crumb.netStep = NS_LOOKUP;
      if (!place.resolved && !resolvePlace(idx, fullName, sizeof fullName)) {
        snprintf(status, sizeof status, "Couldn't find %.20s - hold MENU 5s to fix", place.query);
      } else {
        xSemaphoreTake(dataLock, portMAX_DELAY);
        place = places[idx];
        xSemaphoreGive(dataLock);

        crumb.netStep = NS_FETCH;
        for (int k = 0; k < 3 && count < 0; k++) {
          int s = (goodSource + k) % 3;
          crumb.netSrc = s;
          count = fetchFrom(s, place, workList);
          if (count >= 0) goodSource = s;
        }
        fetchedIdx = idx;
        fetchedAt = millis();
        if (count < 0) {
          snprintf(status, sizeof status, "All 3 flight data sources failed");
        } else {
          Serial.printf("%d planes near %s\n", count, place.name);
          skyNote(workList, count, place);
          crumb.netStep = NS_ROUTES;
          for (int i = 0; i < count && i < ROUTE_LOOKUPS; i++) {
            if (placeGen != gen) break;                        // place changed: stop early
            addRoute(workList[i], place);
          }
        }
      }
    }

    if (placeGen != gen) { fetchNow = true; continue; }    // place changed meanwhile: start over

    xSemaphoreTake(dataLock, portMAX_DELAY);
    if (count >= 0) {
      memcpy(shared.planes, workList, sizeof(Plane) * count);
      shared.count = count;
      shared.ok = true;
      shared.source = goodSource;
      clockText(shared.updated, sizeof shared.updated, false);
      shared.status[0] = '\0';
    } else {
      shared.ok = false;
      copyStr(shared.status, sizeof shared.status, status);
    }
    shared.gen++;
    xSemaphoreGive(dataLock);

    // Overhead alert: anything about to pass close over Home? (workList is free again now)
    if (count > 0 && fetchedIdx >= 0) emergFeed(workList, count, fetchedIdx);           // v0.9.44: squawk codes
    crumb.netStep = NS_OVERHEAD;
    if (WiFi.status() == WL_CONNECTED && fetchedIdx >= 0) checkOverhead(fetchedIdx, count, fetchedAt, goodSource);
    crumb.netStep = NS_SQUAWK;
    emergPoll(goodSource);
  }
}


