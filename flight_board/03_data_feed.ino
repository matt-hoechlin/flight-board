// Flight Board: Fetching aircraft from adsb.lol / adsb.fi / airplanes.live, and free route lookups.
// One tab of the flight_board sketch. Arduino joins the tabs in name order, after flight_board.ino.
// =====================================================================
//  Fetching planes and routes (runs in the background task)
// =====================================================================
String sourceUrl(int s, const Place& p) {
  String lat = String(p.lat, 4), lon = String(p.lon, 4), dist = String((int)ceilf(p.radiusNm));
  if (s == 1) return "https://opendata.adsb.fi/api/v2/lat/" + lat + "/lon/" + lon + "/dist/" + dist;
  if (s == 2) return "https://api.airplanes.live/v2/point/" + lat + "/" + lon + "/" + dist;
  return "https://api.adsb.lol/v2/point/" + lat + "/" + lon + "/" + dist;
}

// Fills list (closest first, airborne only). Returns -1 on failure, else the plane count.
int fetchFrom(int s, const Place& place, Plane* list) { return fetchUrl(s, sourceUrl(s, place), place, list); }

// v0.9.44: the same for any of the source's lists (e.g. by squawk code); distances are from place
int fetchUrl(int s, const String& url, const Place& place, Plane* list) {
  String body;
  unsigned long t0 = millis();
  netStats.requests++;
  if (!httpGet(url, body, 8000)) { noteFetchFail(s, lastHttpCode); return -1; }
  netStats.lastMs = millis() - t0; netStats.lastBytes = body.length(); netStats.lastAt = millis(); netStats.lastSource = s;
  Serial.printf("  %s: %d bytes\n", SOURCE_NAMES[s], body.length());

  JsonDocument filter;
  for (const char* name : {"ac", "aircraft"}) {
    filter[name][0]["flight"]    = true;
    filter[name][0]["r"]         = true;
    filter[name][0]["hex"]       = true;
    filter[name][0]["t"]         = true;
    filter[name][0]["alt_baro"]  = true;
    filter[name][0]["baro_rate"] = true;
    filter[name][0]["geom_rate"] = true;     // v0.9.27: GPS climb rate, when that's all there is
    filter[name][0]["gs"]        = true;
    filter[name][0]["track"]     = true;
    filter[name][0]["lat"]       = true;
    filter[name][0]["lon"]       = true;
    filter[name][0]["dbFlags"]   = true;
    filter[name][0]["category"]  = true;     // v0.9.33: A7 = helicopter
    filter[name][0]["squawk"]    = true;     // v0.9.44
  }
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, body, DeserializationOption::Filter(filter));
  body = String();
  if (err) { Serial.printf("  JSON error: %s\n", err.c_str()); noteFetchFail(s, ERR_GARBLED); return -1; }

  JsonArray arr = doc["ac"].is<JsonArray>() ? doc["ac"].as<JsonArray>() : doc["aircraft"].as<JsonArray>();
  int n = 0;
  for (JsonObject ac : arr) {
    if (n >= MAX_PLANES) break;
    if (!ac["lat"].is<double>() || !ac["lon"].is<double>()) continue;   // no position
    if (!ac["alt_baro"].is<long>()) continue;                           // on the ground
    Plane& p = list[n];
    memset(&p, 0, sizeof p);

    const char* cs = ac["flight"].as<const char*>();
    if (!cs || !cs[0] || cs[0] == ' ') cs = ac["r"].as<const char*>();
    bool fromHex = false;
    if (!cs || !cs[0]) { cs = ac["hex"] | "?"; fromHex = true; }     // last resort: the transponder code
    copyIdent(p.callsign, sizeof p.callsign, cs);
    if (fromHex) for (char* c = p.callsign; *c; c++) *c = toupper((unsigned char)*c);   // 2B3EA0, not 2b3ea0
    if (!p.callsign[0]) strcpy(p.callsign, "?");
    copyIdent(p.reg, sizeof p.reg, ac["r"] | "");
    copyIdent(p.type, sizeof p.type, ac["t"] | "");
    p.altFt     = ac["alt_baro"].as<long>();
    p.rateOk    = true;
    if (ac["baro_rate"].is<long>())      p.climbRate = ac["baro_rate"].as<long>();
    else if (ac["geom_rate"].is<long>()) p.climbRate = ac["geom_rate"].as<long>();   // v0.9.27: GPS rate
    else { p.climbRate = 0; p.rateOk = false; }
    p.gs        = ac["gs"] | 0.0f;
    p.track     = ac["track"] | 0.0f;
    p.lat       = ac["lat"];
    p.lon       = ac["lon"];
    if (!coordsOk(p.lat, p.lon) || p.altFt < -2000 || p.altFt > 120000) continue;   // impossible: skip it
    if (!isfinite(p.gs) || p.gs < 0 || p.gs > 2500) p.gs = 0;
    if (!isfinite(p.track)) p.track = 0;
    p.track = fmodf(p.track, 360.0f); if (p.track < 0) p.track += 360.0f;
    if (p.climbRate < -30000 || p.climbRate > 30000) { p.climbRate = 0; p.rateOk = false; }
    p.distNm    = distanceMiles(place.lat, place.lon, p.lat, p.lon) / 1.15078;
    p.bearing   = bearingDeg(place.lat, place.lon, p.lat, p.lon);
    p.fits      = -1;
    p.legNote   = LEG_NONE;
    p.near      = 0;
    p.routeFa   = false;
    { const char* hx = ac["hex"] | ""; if (*hx == '~') hx++; p.icao = strtoul(hx, nullptr, 16); }
    p.mil       = isMilitary(ac["dbFlags"] | 0, p.callsign, p.type);
    p.heli      = strcmp(ac["category"] | "", "A7") == 0 || isHeliType(p.type);
    { const char* sq = ac["squawk"] | "";                         // v0.9.44: "7700" -> 7700
      p.squawk = (strlen(sq) == 4 && isdigit((unsigned char)sq[0]) && isdigit((unsigned char)sq[3])) ? (int16_t)atoi(sq) : -1; }
    n++;
  }
  for (int i = 1; i < n; i++) {           // closest first
    Plane key = list[i];
    int j = i - 1;
    while (j >= 0 && list[j].distNm > key.distNm) { list[j + 1] = list[j]; j--; }
    list[j + 1] = key;
  }
  netStats.ok[s]++;
  return n;
}

void addAirport(RouteCache& rc, JsonObject ap) {
  if (ap.isNull() || rc.n >= 3) return;
  const char* code = ap["iata_code"] | (const char*)"";
  if (!code[0]) code = ap["icao_code"] | "???";
  copyStr(rc.code[rc.n], sizeof rc.code[0], code);
  noteAlias(rc.code[rc.n], ap["icao_code"] | "");         // v0.9.33
  const char* city = ap["municipality"] | (const char*)"";
  if (!city[0]) city = ap["name"] | "";
  copyStr(rc.city[rc.n], sizeof rc.city[0], city);
  rc.lat[rc.n] = ap["latitude"] | 0.0;
  rc.lon[rc.n] = ap["longitude"] | 0.0;
  rc.n++;
}

const RouteCache* getRoute(const char* callsign) {
  netStats.routesAsked++;
  for (int i = 0; i < CACHE_SIZE; i++)
    if (routeCache[i].callsign[0] && strcmp(routeCache[i].callsign, callsign) == 0) {
      netStats.routesFromMemory++;
      return &routeCache[i];
    }
  netStats.routesOnline++;

  RouteCache& rc = routeCache[cacheNext];
  cacheNext = (cacheNext + 1) % CACHE_SIZE;
  memset(&rc, 0, sizeof rc);
  copyStr(rc.callsign, sizeof rc.callsign, callsign);

  String body;
  bool got = httpGet("https://api.adsbdb.com/v0/callsign/" + String(callsign), body, 5000);
  if (!got && lastHttpCode == 404) netStats.routesUnknown++;              // adsbdb doesn't know this flight
  else if (!got) {
    netStats.routesFailed++; netStats.routeLastErr = lastHttpCode; netStats.routeLastErrAt = millis();
    crumb.lastErr = lastHttpCode; crumb.lastErrSrc = 9; crumb.lastErrUpS = millis() / 1000;
  }
  if (got) {
    JsonDocument filter;
    for (const char* end : {"origin", "midpoint", "destination"}) {
      filter["response"]["flightroute"][end]["iata_code"]    = true;
      filter["response"]["flightroute"][end]["icao_code"]    = true;
      filter["response"]["flightroute"][end]["municipality"] = true;
      filter["response"]["flightroute"][end]["name"]         = true;
      filter["response"]["flightroute"][end]["latitude"]     = true;
      filter["response"]["flightroute"][end]["longitude"]    = true;
    }
    filter["response"]["flightroute"]["airline"]["name"] = true;
    JsonDocument doc;
    if (!deserializeJson(doc, body, DeserializationOption::Filter(filter))) {
      JsonObject fr = doc["response"]["flightroute"];
      copyStr(rc.airline, sizeof rc.airline, fr["airline"]["name"] | "");
      if (!fr.isNull() && !fr["origin"].isNull() && !fr["destination"].isNull()) {
        addAirport(rc, fr["origin"]);
        addAirport(rc, fr["midpoint"]);
        addAirport(rc, fr["destination"]);
      }
    }
  }
  return &rc;
}

