// Flight Board: Main loop helpers: switching places, opening details, factory reset.
// One tab of the flight_board sketch. Arduino joins the tabs in name order, after flight_board.ino.
// =====================================================================
//  Main loop helpers
// =====================================================================
void copySharedToView() {
  xSemaphoreTake(dataLock, portMAX_DELAY);
  seenGen = shared.gen;
  if (shared.ok) {
    viewCount = shared.count;
    memcpy(view, shared.planes, sizeof(Plane) * viewCount);
    viewOk = true;
    viewSource = shared.source;
    copyStr(viewUpdated, sizeof viewUpdated, shared.updated);
  } else {
    viewOk = false;                    // keep showing the last planes, with the error in the footer
    copyStr(viewStatus, sizeof viewStatus, shared.status);
  }
  xSemaphoreGive(dataLock);

  if (screenView == V_DETAIL) {        // stay on the same plane if it's still around
    int found = -1;
    for (int i = 0; i < viewCount && i < ROUTE_LOOKUPS; i++)
      if (strcmp(view[i].callsign, detailCallsign) == 0) { found = i; break; }
    if (found >= 0) detailIdx = found;
    else if (detailIdx >= viewCount || detailIdx >= ROUTE_LOOKUPS) screenView = V_BOARD;
    else copyStr(detailCallsign, sizeof detailCallsign, view[detailIdx].callsign);
  }
}


// ---------------------------------------------------------------------
//  Overhead alert: the main loop's half
// ---------------------------------------------------------------------
void endAlert() {
  screenView = V_BOARD;
  lastFullMs = 0;                          // one full refresh clears the countdown's ghosting
  refreshScreen(true);
}

void overheadTick(unsigned long now) {
  if (alertGen != alertSeenGen) {
    xSemaphoreTake(dataLock, portMAX_DELAY);
    AlertInfo a = alertShared;
    alertSeenGen = alertGen;
    xSemaphoreGive(dataLock);
    if (screenView != V_ALERT) alertCur = a;
    else if (a.valid && a.p.icao == alertCur.p.icao) alertCur = a;   // fresher position for the one on show
  }
  if (screenView == V_ALERT) {
    if (!alertOn || homeIndex() < 0 || alertSecs(now) < -(ALERT_NOW_S + ALERT_PASS_S)) { endAlert(); return; }
    if (now - alertDrawnMs >= ALERT_REDRAW_MS) refreshScreen(false);
    return;
  }
  if (!alertOn || !alertCur.valid || (screenView != V_BOARD && screenView != V_DETAIL && screenView != V_EMERG)) return;   // v0.9.44: overhead first
  float secs = alertSecs(now);
  if (secs > ALERT_WARN_S[alertWarnI] || secs < -ALERT_NOW_S) return;
  if (alertCur.p.icao == lastAlertIcao && now - lastAlertMs < ALERT_REPEAT_MS) return;
  lastAlertIcao = alertCur.p.icao;
  lastAlertMs = now;
  alertsShown++;
  { struct tm t; int d = getLocalTime(&t, 0) ? t.tm_yday : -1; if (d != alertsDay) { alertsDay = d; alertsToday = 0; } alertsToday++; }   // v0.9.51
  knobDelta = 0;
  Serial.printf("Overhead alert: %s\n", alertCur.p.callsign);
  screenView = V_ALERT;
  refreshScreen(false);
}

// v0.9.44: emergency squawks: the main loop's half
void emergTick(unsigned long now) {
  if (emergGen != 0) {
    static uint32_t seen = 0;
    if (emergGen != seen) {
      xSemaphoreTake(dataLock, portMAX_DELAY);
      seen = emergGen; emergCurN = emergSharedN;
      memcpy(emergCur, emergShared, sizeof(Emerg) * emergSharedN);
      xSemaphoreGive(dataLock);
      for (int i = 0; i < emergCurN; i++) if (emergCur[i].icao == emergShow.icao) emergShow = emergCur[i];   // fresher
      if (screenView == V_BOARD) refreshScreen(false);         // footer note on or off
    }
  }
  if (screenView == V_EMERG) {
    bool still = false;
    for (int i = 0; i < emergCurN; i++) if (emergCur[i].icao == emergShow.icao) still = true;
    if (!still || !emergI || now - emergShownMs > EMERG_SHOW_MS) endAlert();
    return;
  }
  if (!emergI || (screenView != V_BOARD && screenView != V_DETAIL)) return;
  for (int i = 0; i < emergCurN; i++) {
    const Emerg& e = emergCur[i];
    if (e.hits < 2) continue;
    bool done = false;
    for (int k = 0; k < emergAlertedN; k++) if (emergAlerted[k] == e.icao) done = true;
    if (done) continue;
    if (emergAlertedN < 8) emergAlerted[emergAlertedN++] = e.icao;
    else { memmove(emergAlerted, emergAlerted + 1, sizeof(uint32_t) * 7); emergAlerted[7] = e.icao; }
    emergShow = e; emergShownMs = now; emergsShown++;
    knobDelta = 0;
    Serial.printf("Emergency squawk %04d: %s\n", e.code, e.p.callsign);
    screenView = V_EMERG;
    refreshScreen(false);
    return;
  }
}

// The screen you were on is saved as its place name, so it still works if places are added or removed
int savedScreen() {
  Preferences p;
  p.begin("fboard", true);
  String name = p.getString("cur", "");
  p.end();
  for (int i = 0; i < numPlaces; i++) if (name == places[i].name) return i;
  return 0;
}

void saveScreen() {
  Preferences p;
  p.begin("fboard", false);
  if (p.getString("cur", "") != places[curPlace].name) p.putString("cur", places[curPlace].name);
  p.end();
}

void switchPlace(int idx) {
  if (idx == curPlace) return;
  xSemaphoreTake(dataLock, portMAX_DELAY);
  curPlace = idx;
  placeGen++;
  xSemaphoreGive(dataLock);
  fetchNow = true;
  viewCount = 0;
  viewOk = false;
  { char ln[64]; placeLongName(places[idx], ln, sizeof ln); snprintf(viewStatus, sizeof viewStatus, "Scanning for aircraft near %s...", ln); }
  screenView = V_BOARD;
  refreshScreen(false);
  saveScreenAt = millis() + 5000;          // save once the knob has been still for a bit
}

void openDetail(int idx) {
  int shown = viewCount < ROUTE_LOOKUPS ? viewCount : ROUTE_LOOKUPS;
  if (shown == 0) return;
  detailIdx = (idx % shown + shown) % shown;
  copyStr(detailCallsign, sizeof detailCallsign, view[detailIdx].callsign);
  const Plane& dp = view[detailIdx];                     // v0.9.40: ask FlightAware about the plane being looked at
  if (faDetailI && !dp.routeFa && isAirlineCallsign(dp.callsign) && faOnFor(places[curPlace])) {
    copyStr(faAskCs, sizeof faAskCs, dp.callsign); faAskMs = millis(); fetchNow = true;
  }
  screenView = V_DETAIL;
  refreshScreen(false);
}

// Erase everything: our settings and the WiFi details the ESP32 keeps, then start as new
void factoryReset() {
  Serial.println("Factory reset");
  Preferences p;
  p.begin("fboard", false);
  p.clear();
  p.end();
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(true, true);              // also forget the saved network
  showMessage("Erased", "All settings are gone.", "Restarting as a new board...");
  delay(1500);
  restartNow(PLANNED_FACTORY);
}

bool bothHeldAtBoot() {                      // MENU + EXIT held while plugging in
  unsigned long t0 = millis();
  while (millis() - t0 < 1000) {
    if (digitalRead(BTN_MENU) != LOW || digitalRead(BTN_EXIT) != LOW) return false;
    delay(20);
  }
  return true;
}

// "ERASE ALL SETTINGS?" Knob press erases; EXIT, or 15 s with no answer, cancels.
void factoryResetPrompt() {
  showMessage("ERASE ALL SETTINGS?", "WiFi, places and time zone will be erased.",
              "Press the knob to erase.   EXIT cancels.");
  unsigned long t0 = millis();
  while ((digitalRead(BTN_MENU) == LOW || digitalRead(BTN_EXIT) == LOW) && millis() - t0 < 10000) { wdtPet(); delay(20); }
  delay(100);                                 // let the buttons settle after letting go
  t0 = millis();
  while (millis() - t0 < 15000) {
    if (digitalRead(ROT_PRESS) == LOW) { delay(40); if (digitalRead(ROT_PRESS) == LOW) factoryReset(); }
    if (digitalRead(BTN_EXIT) == LOW)  { delay(40); if (digitalRead(BTN_EXIT) == LOW) break; }
    wdtPet(); delay(20);
  }
  showMessage("Cancelled", "Nothing was erased.", nullptr);
  while (digitalRead(BTN_EXIT) == LOW) { wdtPet(); delay(20); }
  delay(1000);
}

bool menuHeldAtBoot() {
  if (digitalRead(BTN_MENU) != LOW) return false;
  unsigned long t0 = millis();
  while (millis() - t0 < 1000) { if (digitalRead(BTN_MENU) != LOW) return false; delay(20); }
  return true;
}

