// Flight Board: Saved settings -> the list of places; looking up a ZIP code or town.
// One tab of the flight_board sketch. Arduino joins the tabs in name order, after flight_board.ino.
// =====================================================================
//  Saved settings -> list of places
// =====================================================================
// Prefs keys (v0.5.6 layout, "v6" = true once moved over from older versions):
//   h        = home location as typed: ZIP, town, or "lat, lon" ("" = no home)
//   hg       = saved lookup for a home ZIP/town: "query<TAB>lat<TAB>lon<TAB>name"
//   a0..a9   = airports (OurAirports id, e.g. "KSEA")
//   o0..o2   = other places as typed, on0..on2 = their names, og0..og2 = saved lookups
//   hrad, arad = scan radius for home/other places and for airports (NM, whole numbers, up to v0.7.5)
//   hradf, aradf = the same, exact (NM, from v0.7.6); units = 0 aviation, 1 US, 2 metric
//   ssid pass fresh setup boot = WiFi and housekeeping
const int HOME_RADIUS_DEFAULT    = 15;
const int AIRPORT_RADIUS_DEFAULT = 10;

// "SEA Seattle": airport code and city
void airportHeaderName(char* buf, size_t len, const Airport* a) {
  snprintf(buf, len, "%s %s", a->code, a->city);
}

void fillAirportPlace(Place& p, const Airport* a, float radius) {
  memset(&p, 0, sizeof p);
  airportHeaderName(p.name, sizeof p.name, a);
  snprintf(p.longName, sizeof p.longName, "%s (%s)", a->name, a->code);
  copyStr(p.code, sizeof p.code, a->code);
  p.lat = a->lat; p.lon = a->lon;
  p.radiusNm = radius;
  p.isAirport = true;
  p.elevFt = a->elevFt;
  p.resolved = true;
  p.customName = true;
}

// Reads "38.9606, -119.9409", "38.9606 N 119.9409 W", "38.96N,119.94W". False for ZIPs and towns.
bool parseLatLon(const char* text, double& lat, double& lon) {
  double v[2]; int n = 0;
  const char* c = text;
  while (*c) {
    unsigned char ch = *c;
    if (isdigit(ch) || ((ch == '-' || ch == '.') && (isdigit((unsigned char)c[1]) || c[1] == '.'))) {
      if (n == 2) return false;
      char* end;
      v[n] = strtod(c, &end);
      c = end;
      while (*c == ' ' || (unsigned char)*c == 0xC2 || (unsigned char)*c == 0xB0) c++;   // spaces and the degree sign
      char h = toupper((unsigned char)*c);
      if (h == 'S' || h == 'W') { if (v[n] > 0) v[n] = -v[n]; c++; }
      else if (h == 'N' || h == 'E') c++;
      n++;
    } else if (ch == ',' || ch == ' ' || ch == ';' || ch == '/') {
      c++;
    } else {
      return false;                                       // letters: it's a town name
    }
  }
  if (n != 2) return false;
  lat = v[0]; lon = v[1];
  return lat >= -90 && lat <= 90 && lon >= -180 && lon <= 180 && !(lat == 0 && lon == 0);
}

// HOME or an "other place" from what was typed. Coordinates work straight away;
// a ZIP or town uses the saved lookup if there is one, otherwise it's looked up once online.
void makeLocationPlace(Place& p, const String& query, const String& name, float radius, const char* gkey) {
  memset(&p, 0, sizeof p);
  String q = query; q.trim();
  copyStr(p.query, sizeof p.query, q.c_str());
  copyStr(p.gkey, sizeof p.gkey, gkey);
  p.radiusNm = radius;
  p.customName = name.length() > 0;
  copyStr(p.name, sizeof p.name, p.customName ? name.c_str() : q.c_str());
  double lat, lon;
  if (parseLatLon(q.c_str(), lat, lon)) {
    p.lat = lat; p.lon = lon; p.resolved = true;
    if (!p.customName) snprintf(p.name, sizeof p.name, "%.2f,%.2f", lat, lon);
  } else {
    String g = prefs.getString(gkey, "");
    int t1 = g.indexOf('\t'), t2 = g.indexOf('\t', t1 + 1), t3 = g.indexOf('\t', t2 + 1);
    if (t1 > 0 && t2 > t1 && t3 > t2 && g.substring(0, t1) == q) {
      p.lat = g.substring(t1 + 1, t2).toDouble();
      p.lon = g.substring(t2 + 1, t3).toDouble();
      if (!p.customName) copyStr(p.name, sizeof p.name, g.substring(t3 + 1).c_str());
      p.resolved = true;
    }
  }
  copyStr(p.longName, sizeof p.longName, p.name);
}

// One-time move from the v0.3-v0.5.5 settings to the new layout
void migrateSettings() {
  prefs.begin("fboard", false);
  int lt = prefs.getInt("lt", -1);
  String q = prefs.getString("q", "");
  double lat = prefs.getDouble("lat", 0), lon = prefs.getDouble("lon", 0);
  int rad = prefs.getInt("rad", HOME_RADIUS_DEFAULT);
  String home;
  String airports[MAX_AIRPORTS_SET]; int na = 0;
  String oq[MAX_OTHER], on[MAX_OTHER], og[MAX_OTHER]; int no = 0;

  if (lt == 2 && (lat != 0 || lon != 0)) home = String(lat, 4) + ", " + String(lon, 4);
  else if (lt == 1 && q.length()) {
    home = q;
    if (prefs.getBool("res", false))
      prefs.putString("hg", q + "\t" + String(prefs.getDouble("rlat", 0), 5) + "\t" +
                            String(prefs.getDouble("rlon", 0), 5) + "\t" + prefs.getString("rname", q));
  } else if (lt == 0) {
    const Airport* a = findAirport(q.c_str());
    if (a) airports[na++] = a->ident;
  }

  if (prefs.isKey("x0")) {
    for (int i = 0; i < 10; i++) {
      String xq = prefs.getString(("x" + String(i)).c_str(), ""); xq.trim();
      if (!xq.length()) continue;
      const Airport* a = findAirport(xq.c_str());
      if (a) { if (na < MAX_AIRPORTS_SET) airports[na++] = a->ident; }
      else if (no < MAX_OTHER) {
        oq[no] = xq;
        on[no] = prefs.getString(("xn" + String(i)).c_str(), "");
        og[no] = prefs.getString(("xg" + String(i)).c_str(), "");
        no++;
      }
    }
  } else {
    String favs = prefs.getString("favs", "");
    int start = 0;
    while (start < (int)favs.length() && na < MAX_AIRPORTS_SET) {
      int comma = favs.indexOf(',', start); if (comma < 0) comma = favs.length();
      const Airport* a = findAirport(favs.substring(start, comma).c_str());
      if (a) airports[na++] = a->ident;
      start = comma + 1;
    }
  }

  prefs.putString("h", home);
  for (int i = 0; i < MAX_AIRPORTS_SET; i++) prefs.putString(("a" + String(i)).c_str(), i < na ? airports[i] : String(""));
  for (int i = 0; i < MAX_OTHER; i++) {
    prefs.putString(("o" + String(i)).c_str(), i < no ? oq[i] : String(""));
    prefs.putString(("on" + String(i)).c_str(), i < no ? on[i] : String(""));
    prefs.putString(("og" + String(i)).c_str(), i < no ? og[i] : String(""));
  }
  prefs.putInt("hrad", lt == 0 ? HOME_RADIUS_DEFAULT : rad);
  prefs.putInt("arad", AIRPORT_RADIUS_DEFAULT);
  prefs.putBool("v6", true);
  prefs.end();
  Serial.printf("Settings moved to the new layout: home '%s', %d airports, %d other places\n", home.c_str(), na, no);
}

// Knob order: HOME (if set), airports, other places
// Settings that don't change which aircraft are fetched: read them without touching the places
void loadDisplaySettings(Preferences& p) {
  tzIndex = p.getInt("tz", TZ_DEFAULT);
  radarStyle = constrain(p.getInt("radar", RADAR_DEFAULT), 0, NUM_RADAR_STYLES - 1);
  radarBright = constrain(p.getInt("rbri", 100), 25, 100);
  webTheme = constrain(p.getInt("theme", 0), 0, 1);
  boardLayout = constrain(p.getInt("lay", 1), 0, 1);   // new boards: list + radar
  unitsMode = constrain(p.getInt("units", U_AVIATION), 0, 2);
  screenFlip  = p.getBool("flip", false);
  clock24     = p.getBool("c24", false);
  alertOn     = p.getBool("oa", true);
  alertDistI  = constrain(p.getInt("od", 1), 0, 2);
  alertBelowI = constrain(p.getInt("oh", 1), 0, 4);
  alertWarnI  = constrain(p.getInt("ow", 1), 0, 2);
  fetchSeconds = UPD_SECONDS[constrain(p.getInt("upd", 2), 0, 2)];
  { String k = p.getString("fak", "");                         // v0.9.35: Better Routes
    if (strcmp(k.c_str(), faKey) != 0) {                         // a new key: forget a refusal, give mismatched flights another go
      copyStr(faKey, sizeof faKey, k.c_str()); faLastErr = 0;
      for (int i = 0; i < CACHE_SIZE; i++) routeCache[i].faTried = false;
      faMemoClear();                                             // v0.9.38: a new key starts fresh
    } }
  { int u = constrain(p.getInt("fau", 1), 0, 3);                 // v0.9.38: where FlightAware is used
    if (u != faUseI) for (int i = 0; i < CACHE_SIZE; i++) routeCache[i].faTried = false;
    faUseI = u; }
  faWhichC  = constrain(p.getInt("faw", 1), 0, 2);               // v0.9.40
  faLimitC  = constrain(p.getInt("fal", 1), 0, 2);
  faPlanI   = p.isKey("fap") ? constrain(p.getInt("fap", 0), 0, 2)            // v0.9.43: one choice
                             : (faWhichC == 1 && faLimitC == 1 ? 0 : 2);       // old defaults = Light, anything else = Custom
  faWhichI  = faPlanI == 0 ? 1 : faPlanI == 1 ? 0 : faWhichC;
  faLimitI  = faPlanI == 0 ? 1 : faPlanI == 1 ? 3 : faLimitC;
  faDetailI = constrain(p.getInt("fad", 1), 0, 1);
  rlogOn = p.getInt("rlo", 0) == 0;
}

void loadPlaces() {
  prefs.begin("fboard", true);
  bool moved = prefs.getBool("v6", false);
  prefs.end();
  if (!moved) migrateSettings();

  prefs.begin("fboard", true);
  float hrad = readRadius(prefs, "hradf", "hrad", HOME_RADIUS_DEFAULT);
  float arad = readRadius(prefs, "aradf", "arad", AIRPORT_RADIUS_DEFAULT);
  float orad = prefs.isKey("oradf") ? prefs.getFloat("oradf", hrad) : hrad;   // before v0.9.4: same as Home
  numPlaces = 0;
  String home = prefs.getString("h", "");
  home.trim();
  if (home.length()) {
    makeLocationPlace(places[numPlaces], home, "HOME", hrad, "hg");
    numPlaces++;
  }
  for (int i = 0; i < MAX_AIRPORTS_SET; i++) {
    const Airport* a = findAirportByIdent(prefs.getString(("a" + String(i)).c_str(), "").c_str());
    if (a) fillAirportPlace(places[numPlaces++], a, arad);
  }
  for (int i = 0; i < MAX_OTHER; i++) {
    String oq = prefs.getString(("o" + String(i)).c_str(), ""); oq.trim();
    if (!oq.length()) continue;
    String on = prefs.getString(("on" + String(i)).c_str(), ""); on.trim();
    char key[6]; snprintf(key, sizeof key, "og%d", i);
    makeLocationPlace(places[numPlaces++], oq, on, orad, key);
  }
  loadDisplaySettings(prefs);
  prefs.end();
  noPlaces = numPlaces == 0;
  if (noPlaces) {                           // keep one harmless place so nothing indexes an empty list;
    fillAirportPlace(places[0], &AIRPORTS[0], arad);   // the board shows "No places set up" instead
    numPlaces = 1;
  }
  for (int i = 0; i < numPlaces; i++)
    Serial.printf("Place %d: %s  %.4f, %.4f  %d NM%s%s\n", i, places[i].name, places[i].lat, places[i].lon,
                  (int)lroundf(places[i].radiusNm), places[i].isAirport ? "  (airport)" : "", places[i].resolved ? "" : "  (to look up)");
}

// =====================================================================
//  Looking up a ZIP code or town (done once, then saved)
// =====================================================================
bool geocode(const String& query, double& lat, double& lon, char* name, size_t nameLen, char* fullName, size_t fullLen) {
  String q = query; q.trim();
  String body;
  JsonDocument doc;

  bool isZip = q.length() == 5;
  for (unsigned i = 0; i < q.length() && isZip; i++) if (!isdigit((unsigned char)q[i])) isZip = false;

  if (isZip) {
    if (!httpGet("https://api.zippopotam.us/us/" + q, body, 8000)) return false;
    if (deserializeJson(doc, body)) return false;
    JsonObject pl = doc["places"][0];
    if (pl.isNull()) return false;
    lat = atof(pl["latitude"] | "0");
    lon = atof(pl["longitude"] | "0");
    if (!coordsOk(lat, lon)) return false;
    copyStr(name, nameLen, pl["place name"] | q.c_str());
    snprintf(fullName, fullLen, "%s, %s", (const char*)(pl["place name"] | ""), (const char*)(pl["state abbreviation"] | ""));
    return lat != 0 || lon != 0;
  }

  // Town name, e.g. "Reno" or "Reno, NV". If the ", NV" part finds nothing, try without it.
  for (int attempt = 0; attempt < 2; attempt++) {
    String search = q;
    if (attempt == 1) {
      int comma = q.indexOf(',');
      if (comma < 0) break;
      search = q.substring(0, comma);
    }
    if (!httpGet("https://geocoding-api.open-meteo.com/v1/search?count=1&language=en&format=json&name=" +
                 urlEncode(search.c_str()), body, 8000)) continue;
    doc.clear();
    if (deserializeJson(doc, body)) continue;
    JsonObject r = doc["results"][0];
    if (r.isNull()) continue;
    lat = r["latitude"] | 0.0;
    lon = r["longitude"] | 0.0;
    if (!coordsOk(lat, lon)) continue;
    copyStr(name, nameLen, r["name"] | search.c_str());
    snprintf(fullName, fullLen, "%s, %s", (const char*)(r["name"] | ""), (const char*)(r["admin1"] | (const char*)(r["country"] | "")));
    return true;
  }
  return false;
}

// Looks up a HOME or other-place ZIP/town (background task) and saves the answer
bool resolvePlace(int idx, char* fullName, size_t fullLen) {
  Preferences prefs;                       // its own handle (runs in the background task)
  xSemaphoreTake(dataLock, portMAX_DELAY);
  Place p = places[idx];
  xSemaphoreGive(dataLock);

  double lat = 0, lon = 0;
  char shortName[24] = "";
  if (!geocode(String(p.query), lat, lon, shortName, sizeof shortName, fullName, fullLen)) {
    Serial.printf("Couldn't find '%s'\n", p.query);
    return false;
  }
  Serial.printf("'%s' is %s at %.4f, %.4f\n", p.query, fullName, lat, lon);
  if (p.gkey[0]) {
    prefs.begin("fboard", false);
    prefs.putString(p.gkey, String(p.query) + "\t" + String(lat, 5) + "\t" + String(lon, 5) + "\t" + shortName);
    prefs.end();
  }
  xSemaphoreTake(dataLock, portMAX_DELAY);
  places[idx].lat = lat;
  places[idx].lon = lon;
  if (!places[idx].customName) {
    copyStr(places[idx].name, sizeof places[idx].name, shortName);
    copyStr(places[idx].longName, sizeof places[idx].longName, shortName);
  }
  places[idx].resolved = true;
  xSemaphoreGive(dataLock);
  return true;
}

