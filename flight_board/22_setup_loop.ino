// Flight Board: setup() and loop().
// One tab of the flight_board sketch. Arduino joins the tabs in name order, after flight_board.ino.
void setup() {
  Serial.begin(115200);
  diagBoot();
  delay(300);
  Serial.printf("\nFlight Board v%s\n", FW_VERSION);

  pinMode(EPD_PWR, OUTPUT);
  digitalWrite(EPD_PWR, HIGH);
  delay(50);
  for (int i = 0; i < NUM_BTN; i++) pinMode(BTN_PINS[i], INPUT_PULLUP);

  SPI.begin(EPD_SCK, -1, EPD_MOSI, -1);
  display.init(115200, true, 2, false);
  { Preferences fp; fp.begin("fboard", true); screenFlip = fp.getBool("flip", false); fp.end(); }
  display.setRotation(screenFlip ? 2 : ROTATION);

  prefs.begin("fboard", true);
  String ssid = prefs.getString("ssid", "");
  String pass = prefs.getString("pass", "");
  wifiName = ssid;
  hostName = "flightboard-" + boardId();
  hostName.toLowerCase();
  bool forceSetup = prefs.getBool("setup", false);
  bool fresh = prefs.getBool("fresh", false);
  prefs.end();
  if (digitalRead(BTN_MENU) == LOW && digitalRead(BTN_EXIT) == LOW && bothHeldAtBoot()) {
    factoryResetPrompt();                  // erases and restarts, or returns if cancelled
  }
  bool menuHeld = menuHeldAtBoot();       // check before the boot screen, while MENU is still held
  bool setupNeeded = !ssid.length() || forceSetup || menuHeld;

  // Start connecting now, so WiFi comes up while the boot screen draws
  if (!setupNeeded) {
    WiFi.onEvent(onWifiEvent);
    WiFi.mode(WIFI_STA);
    WiFi.setHostname(hostName.c_str());
    WiFi.setAutoReconnect(true);
    WiFi.begin(ssid.c_str(), pass.c_str());
  }
  unsigned long bootShown = millis();
  showBootScreen();
  while (millis() - bootShown < (unsigned long)BOOT_SCREEN_MS) {   // any button skips the wait
    bool pressed = false;
    for (int i = 0; i < NUM_BTN; i++) if (digitalRead(BTN_PINS[i]) == LOW) pressed = true;
    if (pressed) break;
    delay(20);
  }

  if (setupNeeded) runSetupMode();          // never returns

  dataLock = xSemaphoreCreateMutex();
  loadPlaces();
  curPlace = 0;                            // always start on HOME (or the first airport if there is no home)
  char line2[64];

  unsigned long t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 20000) delay(250);   // usually already connected by now

  if (WiFi.status() != WL_CONNECTED) {
    snprintf(line2, sizeof line2, "Can't connect to %s yet", ssid.c_str());
    showMessage("NO WIFI", line2, "Still trying. To change WiFi, hold MENU while plugging in.");
    delay(4000);
  } else {
    Serial.printf("WiFi connected, IP %s\n", WiFi.localIP().toString().c_str());
    configTzTime(tzRule(), "pool.ntp.org", "time.nist.gov");
    struct tm t;
    getLocalTime(&t, 5000);

    char fullName[48];
    copyStr(fullName, sizeof fullName, places[curPlace].name);
    if (!places[curPlace].resolved) resolvePlace(curPlace, fullName, sizeof fullName);
    if (fresh) {
      prefs.begin("fboard", false);
      prefs.putBool("fresh", false);
      prefs.end();
      char near[72];
      snprintf(near, sizeof near, "Watching skies near %s", fullName);
      showMessage("Connected!", near, "Knob: screens   Press: aircraft details   Hold EXIT 3 s: settings");
      delay(5000);
    }
  }

  // Buttons: count presses by interrupt from now on
  for (int i = 0; i < NUM_BTN; i++) {
    bool timed = (i == B_MENU || i == B_EXIT || i == B_PRESS);           // v0.9.25: press + release times
    attachInterruptArg(BTN_PINS[i], timed ? onBtnEdge : onButton, (void*)(intptr_t)i, timed ? CHANGE : FALLING);
  }

  { char ln[64]; placeLongName(places[curPlace], ln, sizeof ln); snprintf(viewStatus, sizeof viewStatus, "Scanning for aircraft near %s...", ln); }
  screenView = V_BOARD;
  refreshScreen(false);
  fetchNow = true;
  // v0.9.32: safety restart (see wdtPet). The loop is watched from here on; the tasks watch themselves.
  { esp_task_wdt_config_t wc = { .timeout_ms = WDT_S * 1000, .idle_core_mask = 0, .trigger_panic = true };
    if (esp_task_wdt_reconfigure(&wc) != ESP_OK) esp_task_wdt_init(&wc); }
  loopTaskH = xTaskGetCurrentTaskHandle();
  wdtWatchMe();
  xTaskCreatePinnedToCore(netTask, "net", 16384, nullptr, 1, &netTaskH, 0);
  xTaskCreatePinnedToCore(webTask, "web", 12288, nullptr, 1, &webTaskH, 0);   // settings page on the home network
  radarLock = xSemaphoreCreateMutex();
  radarPublish(true);
  xTaskCreatePinnedToCore(radarTask, "radar", 8192, nullptr, 1, &radarTaskH, 1);  // same core as the loop: it yields often
  lastInputMs = millis();
}

// Firmware Update on the e-paper. Returns true while it owns the screen (the rest of the loop waits).
bool firmwareScreen(unsigned long now) {
  if (fwAsk == FWA_ASK) {
    if (!fwShown) {
      fwShown = true;
      takePresses(B_MENU); takePresses(B_PRESS); takeTaps(B_EXIT, 0); takeTaps(B_PRESS, 0);   // only a press made while asking counts
      char ln[64]; snprintf(ln, sizeof ln, fwAskReset ? "Press MENU to install v%s and erase all settings" : "Press MENU to install v%s",
                            fwAskVer[0] ? fwAskVer : "?");
      showMessage(fwAskReset ? "Update + factory reset?" : "Allow firmware update?", ln, "EXIT cancels. No answer in 60 s cancels.");
      takePresses(B_MENU);                                  // presses during the refresh don't count either
    }
    if (takePresses(B_MENU) > 0) {
      fwYesAt = millis(); fwAsk = FWA_YES;
      showMessage("Updating firmware", "Receiving the file - keep the board plugged in", nullptr);
    } else if (btnHeldMs(B_EXIT, now) || takeTaps(B_EXIT, 0) || now - fwAskAt > FW_ASK_MS) {
      fwAsk = FWA_NO;
      btnHoldUsed(B_EXIT);                                  // letting go of EXIT does nothing else
    }
    return true;
  }
  if (fwAsk == FWA_YES && !fwUpdating && now - fwYesAt > FW_YES_MS) fwAsk = FWA_NO;   // the file never came
  if (fwUpdating && !fwShown) {
    fwShown = true;
    showMessage("Updating firmware", "Receiving the file - keep the board plugged in", nullptr);
  }
  if (fwShown && !fwUpdating && fwAsk != FWA_YES && !restartAt) {   // over without installing: back to the board
    fwShown = false;
    takePresses(B_MENU); takePresses(B_PRESS); takeTaps(B_EXIT, 0); takeTaps(B_PRESS, 0); knobDelta = 0;
    lastInputMs = now;
    screenView = V_BOARD;
    lastFullMs = 0;
    refreshScreen(true);
    return false;
  }
  return fwShown && !restartAt;
}

void loop() {
  unsigned long now = millis();
  crumb.loopStep = LS_IDLE;
  wdtPet();
  static unsigned long lastDiag = 0;
  if (now - lastDiag > 2000) { lastDiag = now; diagTick(); }
  btnService(now);

  // Firmware Update: the board asks first, then shows the update; nothing else draws meanwhile
  if (firmwareScreen(now)) { delay(20); return; }

  // Hold MENU + EXIT 5 s: factory reset (asks first, the knob confirms)
  if (digitalRead(BTN_MENU) == LOW && digitalRead(BTN_EXIT) == LOW) {
    if (!bothDownSince) bothDownSince = now;
    menuDownSince = 0;                     // not also MENU-alone (setup)
    btnHoldUsed(B_EXIT);                   // and not EXIT's own actions when it's let go
    if (now - bothDownSince > (unsigned long)RESET_HOLD_S * 1000) {
      bothDownSince = 0;
      factoryResetPrompt();                // erases and restarts, or shows "Cancelled" and returns
      takePresses(B_MENU); takePresses(B_EXIT); takeTaps(B_EXIT, 0); takeTaps(B_PRESS, 0); knobDelta = 0;
      lastInputMs = millis();
      screenView = V_BOARD;
      refreshScreen(false);
      return;
    }
  } else bothDownSince = 0;

  // Hold MENU 5 s: run setup again
  if (digitalRead(BTN_MENU) == LOW && digitalRead(BTN_EXIT) != LOW) {
    if (!menuDownSince) menuDownSince = now;
    if (now - menuDownSince > (unsigned long)SETUP_HOLD_S * 1000) {
      prefs.begin("fboard", false);
      prefs.putBool("setup", true);
      prefs.end();
      showMessage("Setup", "Restarting into setup...", nullptr);
      restartNow(PLANNED_SETUP);
    }
  } else menuDownSince = 0;

  // EXIT: a short press acts when released; holding it 3 s shows the settings address
  int exitP = 0;
  takePresses(B_EXIT);                    // EXIT acts when let go (a tap), or after a 3 s hold
  if (btnHeldMs(B_EXIT, now) >= (unsigned long)SETTINGS_HOLD_MS) {
    btnHoldUsed(B_EXIT);
    knobDelta = 0;
    screenView = V_SETTINGS;
    lastInputMs = now;
    refreshScreen(false);
  }
  exitP = takeTaps(B_EXIT, SETTINGS_HOLD_MS) ? 1 : 0;

  // Settings page saved new places: reload and start over on the first one
  if (placesChanged) {
    placesChanged = false;
    xSemaphoreTake(dataLock, portMAX_DELAY);
    loadPlaces();
    curPlace = savedScreen();               // stay on the same screen if it's still there
    placeGen++;
    xSemaphoreGive(dataLock);
    configTzTime(tzRule(), "pool.ntp.org", "time.nist.gov");   // time zone may have changed
    fetchNow = true;
    viewCount = 0;
    viewOk = false;
    { char ln[64]; placeLongName(places[curPlace], ln, sizeof ln); snprintf(viewStatus, sizeof viewStatus, "Scanning for aircraft near %s...", ln); }
    screenView = V_BOARD;
    settingsChanged = false;                // the full reload covers those too
    if (display.getRotation() != (screenFlip ? 2 : ROTATION)) {   // turned over: a full refresh in the new direction
      display.setRotation(screenFlip ? 2 : ROTATION);
      lastFullMs = 0;
      refreshScreen(true);
    } else refreshScreen(false);
  }
  // Settings page saved display/radar/alert settings only: keep the flights, redraw only if the board looks different
  if (settingsChanged && !placesChanged) {
    settingsChanged = false;
    applyDisplaySettings();
  }
  if (restartAt && now > restartAt) {     // asked for by the settings page
    if (restartKind == RESTART_UPDATE) {
      char ln[64];
      if (fwResetAfter) {                   // v0.9.28: erase now; the new firmware starts as a new board
        Preferences p; p.begin("fboard", false); p.clear(); p.end();
        WiFi.disconnect(true, true);
        snprintf(ln, sizeof ln, "Restarting on v%s as a new board...", fwNewVer);
        showMessage("Update installed", ln, "All settings erased.");
      } else {
        snprintf(ln, sizeof ln, "Restarting on v%s...", fwNewVer);
        showMessage("Update installed", ln, nullptr);
      }
      restartNow(PLANNED_UPDATE);
    }
    if (restartKind == RESTART_FACTORY) factoryReset();
    showMessage("Restarting", restartKind == RESTART_WIFI ? "Joining the new WiFi..." : "Back in a moment...", nullptr);
    restartNow(restartKind == RESTART_WIFI ? PLANNED_WIFI : PLANNED_BUTTON);
  }
  if (saveScreenAt && now > saveScreenAt) { saveScreenAt = 0; saveScreen(); }

  // Remote Control requests from the settings page
  if (webShow >= 0) {
    int i = webShow; webShow = -1;
    knobDelta = 0; lastInputMs = now;
    if (i < numPlaces && !noPlaces) {
      if (i != curPlace) switchPlace(i);
      else if (screenView != V_BOARD) { screenView = V_BOARD; refreshScreen(false); }
    }
  }
  if (webAircraft >= 0) {                 // open that aircraft's details
    int i = webAircraft; webAircraft = -1;
    knobDelta = 0; lastInputMs = now;
    int shown = viewCount < ROUTE_LOOKUPS ? viewCount : ROUTE_LOOKUPS;
    if (i < shown) openDetail(i);
  }
  if (webLayout >= 0) {
    int l = webLayout; webLayout = -1;
    if (l != boardLayout) {
      boardLayout = l;
      Preferences lp; lp.begin("fboard", false); lp.putInt("lay", boardLayout); lp.end();
      if (screenView == V_BOARD) refreshScreen(false);
    }
  }
  if (webRefresh) { webRefresh = false; Serial.println("Web: refresh"); fetchNow = true; }

  int menu  = takePresses(B_MENU);
  int press = takePresses(B_PRESS);
  // On the board, the knob press acts when released, so holding it can switch the layout instead
  int pressShort = 0;
  if (screenView == V_BOARD && btnHeldMs(B_PRESS, now) >= (unsigned long)LAYOUT_HOLD_MS) {
    btnHoldUsed(B_PRESS);
    boardLayout = 1 - boardLayout;
    Preferences lp; lp.begin("fboard", false); lp.putInt("lay", boardLayout); lp.end();
    lastInputMs = now;
    refreshScreen(false);
  }
  pressShort = takeTaps(B_PRESS, screenView == V_BOARD ? LAYOUT_HOLD_MS : 0) ? 1 : 0;
  if (pressShort) lastInputMs = now;
  int up    = takePresses(B_UP);
  int down  = takePresses(B_DOWN);
  if (screenFlip) { int t = up; up = down; down = t; }   // upside down: the knob turns the other way
  if (webKey) {                           // Remote Control: the same as the knob and EXIT
    int k = webKey; webKey = WK_NONE;
    if (k == WK_PREV) up++; else if (k == WK_NEXT) down++;
    else if (k == WK_SELECT) pressShort = 1; else if (k == WK_BACK) exitP = 1;
  }
  (void)press;                            // the knob press is read on release (pressShort), on every screen
  if (menu || exitP || pressShort || up || down) lastInputMs = now;
  if (up || down) { knobDelta += down - up; lastKnobMs = now; }

  if (menu) {
    Serial.println("MENU: refresh"); fetchNow = true;
    if (screenView == V_BOARD && !noPlaces) { menuRefreshAt = now ? now : 1; refreshScreen(false); }   // shows it now
  }
  if (menuRefreshAt && now - menuRefreshAt > MENU_REFRESH_MAX_MS) {        // no answer: put the clock back
    menuRefreshAt = 0;
    if (screenView == V_BOARD) refreshScreen(false);
  }

  if (screenView == V_ALERT) {             // any button: back to the board
    if (exitP || pressShort || menu || up || down) { knobDelta = 0; endAlert(); }
  } else if (screenView == V_SETTINGS) {
    if (exitP || pressShort || menu || up || down) { knobDelta = 0; screenView = V_BOARD; refreshScreen(false); }
    else if (now - lastInputMs > (unsigned long)DETAIL_TIMEOUT_S * 1000) { screenView = V_BOARD; refreshScreen(false); }
  } else if (screenView == V_BOARD) {
    if (exitP && curPlace != 0) { knobDelta = 0; menuRefreshAt = now ? now : 1; switchPlace(0); }   // header: Refreshing...
    else if (pressShort) { knobDelta = 0; openDetail(0); }
    else if (knobDelta && now - lastKnobMs >= (unsigned long)KNOB_SETTLE_MS) {
      int idx = ((curPlace + knobDelta) % numPlaces + numPlaces) % numPlaces;
      knobDelta = 0;
      switchPlace(idx);
    }
  } else {   // detail page
    if (exitP || pressShort) { knobDelta = 0; screenView = V_BOARD; refreshScreen(false); }
    else if (knobDelta && now - lastKnobMs >= (unsigned long)DETAIL_SETTLE_MS) {
      int d = knobDelta;
      knobDelta = 0;
      openDetail(detailIdx + d);
    } else if (now - lastInputMs > (unsigned long)DETAIL_TIMEOUT_S * 1000) {
      screenView = V_BOARD;
      refreshScreen(false);
    }
  }

  bool newData = false;
  if (shared.gen != seenGen) {           // new data from the background task
    menuRefreshAt = 0;                   // the clock comes back with the new data
    copySharedToView();
    newData = true;
    refreshScreen(true);
  }
  overheadTick(millis());
  {                                      // tell the radar if anything it shows has changed
    static uint32_t lastSig = 0;
    uint32_t sig = seenGen * 31u + curPlace * 7u + (uint32_t)screenView * 3u + detailIdx * 131u + viewCount * 17u + noPlaces;
    if (newData || sig != lastSig) { lastSig = sig; radarPublish(newData); }
  }
  maybeDeepClean(now);
  delay(20);
}
