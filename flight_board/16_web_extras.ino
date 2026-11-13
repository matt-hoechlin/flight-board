// Flight Board: Web console: remote control, fonts, help, legal, setup mode.
// One tab of the flight_board sketch. Arduino joins the tabs in name order, after flight_board.ino.
// "Remote Control": /board?show=N, ?refresh=1, ?layout=0|1, ?key=prev|next|select|back, ?ac=N. Answers the board's state.
// The main loop does the work (the same as the knob), so the e-paper is only ever drawn from one place.
// v0.9.47: what the radar shows, for a Flight Board Round (round touch screen) on the same network. Small (~2 KB):
// place, range, nearby airports, and each aircraft's position (NM east/north of the place) when the board last
// heard; "age" says how long ago, so the round screen can move them on. "sel": the aircraft open on the board.
void handleRadarJson() {
  if (!hostOk()) { sendBlocked(); return; }
  if (!radarLock) { server.send(503, "text/plain", "not ready"); return; }
  static RadarSnap snap;                                         // (static: too big for the web task's stack)
  xSemaphoreTake(radarLock, portMAX_DELAY);
  snap = radarShared;
  xSemaphoreGive(radarLock);
  String o; o.reserve(160 + snap.n * 72);
  char b[256];
  String nm = snap.name; nm.replace("\"", "'"); nm.replace("\\", "/");
  snprintf(b, sizeof b, "{\"v\":1,\"fw\":\"%s\",\"name\":\"%s\",\"range\":%.1f,\"age\":%lu,\"view\":%d,\"u\":%d,\"home\":%d,\"sel\":\"",
           FW_VERSION, nm.c_str(), snap.range, (unsigned long)(millis() - snap.t0), screenView == V_DETAIL ? 1 : 0, unitsMode, snap.atHome ? 1 : 0);
  o += b;
  if (snap.sel >= 0 && snap.sel < snap.n) { snprintf(b, sizeof b, "%06lx", (unsigned long)snap.b[snap.sel].icao); o += b; }
  o += "\",\"ap\":[";
  for (int i = 0; i < snap.nAp; i++) {
    snprintf(b, sizeof b, "%s[\"%s\",%.2f,%.2f]", i ? "," : "", snap.ap[i].code, snap.ap[i].x, snap.ap[i].y); o += b;
  }
  o += "],\"p\":[";
  for (int i = 0; i < snap.n; i++) {
    const RadarBlip& p = snap.b[i];
    String cs = p.cs; cs.replace("\"", "");
    // [hex, callsign, type, x, y, track, speed, altitude, climb, squawk, airliner, can be opened]
    snprintf(b, sizeof b, "%s[\"%06lx\",\"%s\",\"%s\",%.2f,%.2f,%.0f,%.0f,%ld,%d,%d,%d,%d]", i ? "," : "",
             (unsigned long)p.icao, cs.c_str(), p.type, p.x, p.y, p.trk, p.gs, p.alt, p.vs, p.squawk, p.airliner ? 1 : 0, i < ROUTE_LOOKUPS ? 1 : 0);
    o += b;
  }
  o += "]";
  if (snap.alert) {                                              // v0.9.51: the overhead alert on show, as of now
    float dt = (long)(millis() - snap.aAt) / 1000.0f;
    String cs = snap.acs; cs.replace("\"", "");
    snprintf(b, sizeof b, ",\"al\":{\"cs\":\"%s\",\"type\":\"%s\",\"from\":\"%s\",\"to\":\"%s\",", cs.c_str(), snap.atype, snap.afrom, snap.ato);
    o += b;
    String air = snap.aair, tn = snap.atn; air.replace("\"", "'"); tn.replace("\"", "'");       // v0.9.52
    snprintf(b, sizeof b, "\"air\":\"%s\",\"tn\":\"%s\",\"id\":\"%06lx\",\"fx\":%.3f,\"fy\":%.3f,\"fage\":%lu,", air.c_str(), tn.c_str(),
             (unsigned long)snap.aid, snap.ax, snap.ay, (unsigned long)(millis() - snap.aAt));
    o += b;
    snprintf(b, sizeof b, "\"x\":%.3f,\"y\":%.3f,\"vx\":%.5f,\"vy\":%.5f,\"t\":%.1f,\"dmin\":%.2f,", snap.ax + snap.avx * dt, snap.ay + snap.avy * dt,
             snap.avx, snap.avy, snap.atca - dt, snap.admin);
    o += b;
    snprintf(b, sizeof b, "\"above\":%ld,\"climb\":%d,\"gs\":%.0f,\"trk\":%.0f,\"today\":%d}", snap.aAbove + (long)(snap.aClimb * dt / 60), snap.aClimb,
             snap.ags, snap.atrk, snap.aToday);
    o += b;
  }
  scopeRadarPart(o);                                             // v0.9.48: the Scope that's asking: its settings
  o += "}";
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", o);
}

void handleBoard() {
  bool acts = server.hasArg("show") || server.hasArg("layout") || server.hasArg("refresh") || server.hasArg("key") || server.hasArg("ac") || server.hasArg("acx");
  if (acts ? !fromOwnPage(true) : !hostOk()) { sendBlocked(); return; }
  int cur = curPlace, lay = boardLayout;
  if (server.hasArg("show")) {
    int i = server.arg("show").toInt();
    if (!noPlaces && i >= 0 && i < numPlaces) { webShow = i; cur = i; }
  }
  if (server.hasArg("layout")) { lay = constrain((int)server.arg("layout").toInt(), 0, 1); webLayout = lay; }
  if (server.hasArg("refresh")) webRefresh = true;
  if (server.hasArg("key")) {
    String k = server.arg("key");
    webKey = k == "prev" ? WK_PREV : k == "next" ? WK_NEXT : k == "select" ? WK_SELECT : k == "back" ? WK_BACK : WK_NONE;
  }
  if (server.hasArg("ac")) {
    int i = server.arg("ac").toInt();
    if (i >= 0 && i < ROUTE_LOOKUPS) webAircraft = i;
  }
  if (server.hasArg("acx")) {                                     // v0.9.47: by its hex (the round screen), so it can't open the wrong one
    uint32_t hx = strtoul(server.arg("acx").c_str(), nullptr, 16);
    int n = viewCount < ROUTE_LOOKUPS ? viewCount : ROUTE_LOOKUPS;
    for (int i = 0; i < n; i++) if (view[i].icao == hx) { webAircraft = i; break; }
  }
  if (server.hasArg("key") || server.hasArg("ac") || server.hasArg("acx")) delay(350);      // let the board act, so the answer shows the new state
  cur = curPlace;
  if (webShow >= 0) cur = webShow;
  // Answer: place|layout|view (0 board, 1 details)|aircraft on show|callsigns of the aircraft that can be opened
  int n = viewCount < ROUTE_LOOKUPS ? viewCount : ROUTE_LOOKUPS;
  String out = String(cur) + "|" + String(lay) + "|" + String(screenView == V_DETAIL ? 1 : 0) + "|" + String(detailIdx) + "|";
  for (int i = 0; i < n; i++) { if (i) out += ","; out += view[i].callsign; }
  out += "|";
  for (int i = 0; i < numPlaces && !noPlaces; i++) {
    int8_t ic = placeIcon(places[i]);
    out += ic == ICON_HOUSE ? "h:" : ic == ICON_PLANE ? "a:" : "p:";
    String nm = places[i].isAirport ? places[i].code : places[i].name;
    nm.replace(";", ","); nm.replace("|", "/");
    out += nm; out += ";";
  }
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "text/plain", out);
}

// The x buttons: /clear?home=1 | ?apt=N | ?oth=N removes that entry right away (the list closes up),
// /clear?home=1&undo=<text> puts Your Home back. Answers "ok" or "?".
void handleClear() {
  if (!fromOwnPage(true)) { sendBlocked(); return; }
  Preferences pr;
  pr.begin("fboard", false);
  bool done = false;
  if (server.hasArg("home")) {
    if (server.hasArg("undo")) { String v = server.arg("undo"); v.trim(); pr.putString("h", v); }
    else pr.putString("h", "");               // its saved lookup ("hg") stays, so Undo needs no new lookup
    done = true;
  } else if (server.hasArg("apt")) {
    int k = server.arg("apt").toInt();
    if (k >= 0 && k < MAX_AIRPORTS_SET) {
      for (int i = k; i < MAX_AIRPORTS_SET - 1; i++)
        pr.putString(("a" + String(i)).c_str(), pr.getString(("a" + String(i + 1)).c_str(), ""));
      pr.putString(("a" + String(MAX_AIRPORTS_SET - 1)).c_str(), "");
      done = true;
    }
  } else if (server.hasArg("oth")) {
    int k = server.arg("oth").toInt();
    if (k >= 0 && k < MAX_OTHER) {
      for (int i = k; i < MAX_OTHER - 1; i++) {
        const char* keys[] = { "o", "on", "og" };
        for (const char* key : keys)
          pr.putString((String(key) + String(i)).c_str(), pr.getString((String(key) + String(i + 1)).c_str(), ""));
      }
      pr.putString(("o" + String(MAX_OTHER - 1)).c_str(), "");
      pr.putString(("on" + String(MAX_OTHER - 1)).c_str(), "");
      pr.remove(("og" + String(MAX_OTHER - 1)).c_str());
      done = true;
    }
  }
  pr.end();
  if (done) placesChanged = true;              // the main loop reloads the places and redraws
  server.sendHeader("Cache-Control", "no-store");
  server.send(done ? 200 : 400, "text/plain", done ? "ok" : "?");
}

// Web fonts (a year in the browser's cache: they only change with new firmware)
void sendFont(const uint8_t* data, uint32_t len) {
  server.sendHeader("Cache-Control", "max-age=31536000");
  server.send_P(200, "font/woff", (const char*)data, len);
}
void handleFontIS()  { sendFont(WF_IS, WF_IS_LEN); }
void handleFontISB() { sendFont(WF_ISB, WF_ISB_LEN); }
void handleFontBS()  { sendFont(WF_BS, WF_BS_LEN); }
void handleFontGM()  { sendFont(WF_GM, WF_GM_LEN); }

void handleBg() {
  server.sendHeader("Cache-Control", "max-age=604800");   // phones keep it for a week
  server.send_P(200, "image/jpeg", (const char*)BG_JPG, BG_JPG_LEN);
}

void handleHelp() {
  String b = pageBanner("<svg class=i viewBox='0 0 24 24'><path d='M12 6.5C10 5 7 4.5 3 5v13.5c4-.5 7 0 9 1.5 2-1.5 5-2 9-1.5V5c-4-.5-7 0-9 1.5z'/><path d='M12 6.5V20'/></svg>Instructions &amp; Help", "", headerChips(false));
  b += F("<a class=btn href=/>&larr;&nbsp; Back to Board Setup</a>");
  b += FPSTR(HELP_BODY);
  b += F("<div id=c-nav class=card><a class=lb href=/>&larr; Back to Board Setup</a><a class=lb href=/settings><svg class=i viewBox='0 0 24 24'><circle cx='12' cy='12' r='3'/><path d='M12 2.8l1.6 2.4 2.8-.9.5 2.9 2.9.5-.9 2.8 2.4 1.6-2.4 1.6.9 2.8-2.9.5-.5 2.9-2.8-.9-1.6 2.4-1.6-2.4-2.8.9-.5-2.9-2.9-.5.9-2.8L2.8 12l2.4-1.6-.9-2.8 2.9-.5.5-2.9 2.8.9z'/></svg>Settings</a><a class=lb href=/legal><svg class=i viewBox='0 0 24 24'><path d='M12 3.5V20M7 20h10M5 7h14M8 4.8l4-1.3 4 1.3'/><path d='M5 7l-2.8 6.5a2.8 2.8 0 0 0 5.6 0z'/><path d='M19 7l-2.8 6.5a2.8 2.8 0 0 0 5.6 0z'/></svg>Credits &amp; Legal</a></div><div class=foot>&copy; 2026 Volary Dynamics</div>");
  sendPage(b);
}

void handleLegal() {
  String b = pageBanner("<svg class=i viewBox='0 0 24 24'><path d='M12 3.5V20M7 20h10M5 7h14M8 4.8l4-1.3 4 1.3'/><path d='M5 7l-2.8 6.5a2.8 2.8 0 0 0 5.6 0z'/><path d='M19 7l-2.8 6.5a2.8 2.8 0 0 0 5.6 0z'/></svg>Credits &amp; Legal", "", headerChips(false));
  b += F("<a class=btn href=/>&larr;&nbsp; Back to Board Setup</a>");
  b += F("<div class='card by'><svg class=vdm viewBox='-6 -6 76 76' aria-hidden=true><rect x='-6' y='-6' width='76' height='76' rx='14' fill='#0b0e12'/><path d='M50 13.5 A26 26 0 1 0 57.5 30' fill='none' stroke='#e8edf2' stroke-width='4.2' stroke-linecap='round'/><path d='M16 36 C23 28 30 28 35 37 C40 27 49 19 62 16 C52 23 45 30 37 44 C32 35 25 33 16 36 Z' fill='#c8ff3d'/></svg><div class=byt><b>Made by Volary Dynamics</b><span>Designed and built in Reno, Nevada, USA</span></div><p class=mk><a href=https://volarydynamics.com target=_blank rel=noopener><svg class=i viewBox='0 0 24 24'><circle cx='12' cy='12' r='9'/><ellipse cx='12' cy='12' rx='4' ry='9'/><path d='M3 12h18'/></svg>volarydynamics.com</a><a href=mailto:info@volarydynamics.com><svg class=i viewBox='0 0 24 24'><rect x='3' y='5.5' width='18' height='13' rx='1.5'/><path d='M3.5 6.5l8.5 6.5 8.5-6.5'/></svg>info@volarydynamics.com</a></p></div>");   // v0.9.31: slim maker bar
  b += FPSTR(LEGAL_BODY);
  b += F("<div id=c-nav class=card><a class=lb href=/>&larr; Back to Board Setup</a><a class=lb href=/settings><svg class=i viewBox='0 0 24 24'><circle cx='12' cy='12' r='3'/><path d='M12 2.8l1.6 2.4 2.8-.9.5 2.9 2.9.5-.9 2.8 2.4 1.6-2.4 1.6.9 2.8-2.9.5-.5 2.9-2.8-.9-1.6 2.4-1.6-2.4-2.8.9-.5-2.9-2.9-.5.9-2.8L2.8 12l2.4-1.6-.9-2.8 2.9-.5.5-2.9 2.8.9z'/></svg>Settings</a><a class=lb href=/help><svg class=i viewBox='0 0 24 24'><path d='M12 6.5C10 5 7 4.5 3 5v13.5c4-.5 7 0 9 1.5 2-1.5 5-2 9-1.5V5c-4-.5-7 0-9 1.5z'/><path d='M12 6.5V20'/></svg>Instructions &amp; Help</a></div><div class=foot>&copy; 2026 Volary Dynamics</div>");
  sendPage(b);
}

void handleNotFound() {   // captive portal: send everything to the setup page
  server.sendHeader("Location", "http://" SETUP_IP "/", true);
  server.send(302, "text/plain", "");
}

// "skies" + 6 random letters/digits, leaving out look-alikes (0/o, 1/l/i)
String makeSetupPassword() {
  const char* ok = "abcdefghjkmnpqrstuvwxyz23456789";
  String p = "skies";
  for (int i = 0; i < 6; i++) p += ok[esp_random() % strlen(ok)];
  return p;
}

void runSetupMode() {
  setupMode = true;
  // Network name from the board's ID; password random, made once and kept
  // (a factory reset makes a new one)
  WiFi.mode(WIFI_AP_STA);
  char buf[32];
  snprintf(buf, sizeof buf, "FlightBoard-%s", boardId().c_str());
  apName = buf;
  prefs.begin("fboard", false);
  webTheme = constrain(prefs.getInt("theme", 0), 0, 1);
  apPass = prefs.getString("appw", "");
  if (apPass.length() < 8) {
    apPass = makeSetupPassword();
    prefs.putString("appw", apPass);
  }
  String ssid = prefs.getString("ssid", "");
  prefs.end();
  scanNetworks(ssid);                 // scan before the access point starts

  WiFi.softAPConfig(IPAddress(10, 47, 0, 1), IPAddress(10, 47, 0, 1), IPAddress(255, 255, 255, 0));
  WiFi.softAP(apName.c_str(), apPass.c_str());
  delay(200);
  dns.start(53, "*", WiFi.softAPIP());
  server.collectHeaders(FB_HEADERS, 3);
  server.on("/", HTTP_GET, handleRoot);
  server.on("/save", HTTP_POST, handleSave);
  server.on("/find", HTTP_GET, handleFind);
  server.on("/help", HTTP_GET, handleHelp);
  server.on("/bg.jpg", HTTP_GET, handleBg);
  server.on("/f/is.woff", HTTP_GET, handleFontIS);
  server.on("/f/isb.woff", HTTP_GET, handleFontISB);
  server.on("/f/bs.woff", HTTP_GET, handleFontBS);
  server.on("/f/gm.woff", HTTP_GET, handleFontGM);
  server.on("/legal", HTTP_GET, handleLegal);
  server.on("/scan", HTTP_GET, handleScan);
  server.onNotFound(handleNotFound);
  server.begin();
  Serial.printf("Setup mode: join '%s' (password %s), open http://" SETUP_IP "\n", apName.c_str(), apPass.c_str());

  showSetupScreen();

  unsigned long bothSince = 0;             // MENU + EXIT held together
  for (;;) {
    dns.processNextRequest();
    server.handleClient();
    if (digitalRead(BTN_MENU) == LOW && digitalRead(BTN_EXIT) == LOW) {   // hold 5 s: factory reset (asks first)
      if (!bothSince) bothSince = millis();
      if (millis() - bothSince > (unsigned long)RESET_HOLD_S * 1000) {
        bothSince = 0;
        factoryResetPrompt();              // erases and restarts, or shows "Cancelled" and returns
        showSetupScreen();
      }
    } else bothSince = 0;
    if (restartAt && millis() > restartAt) {
      showMessage("Saved!", "Restarting...", nullptr);
      restartNow(PLANNED_SETUP_SAVED);
    }
    delay(2);
  }
}

