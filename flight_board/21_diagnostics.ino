// Flight Board: Diagnostic Log, restart log, crash notes.
// One tab of the flight_board sketch. Arduino joins the tabs in name order, after flight_board.ino.
// =====================================================================
//  Setup / loop
// =====================================================================
// =====================================================================
//  Diagnostics
// =====================================================================
const char* resetText(uint8_t r, uint8_t planned) {
  switch (r) {
    case ESP_RST_POWERON:  return "Power on (plugged in)";
    case ESP_RST_BROWNOUT: return "Power dip (brownout)";
    case ESP_RST_PANIC:    return "Software crash";
    case ESP_RST_TASK_WDT: return "Safety restart (part of the board stopped)";
    case ESP_RST_INT_WDT: case ESP_RST_WDT: return "Stuck (watchdog)";
    case ESP_RST_EXT:      return "Reset button";
    case ESP_RST_USB:      return "USB reset (new firmware?)";
    case ESP_RST_DEEPSLEEP: return "Woke from sleep";
    case ESP_RST_SW:
      switch (planned) {
        case PLANNED_WIFI:        return "Settings saved (new WiFi)";
        case PLANNED_BUTTON:      return "Restart button (settings page)";
        case PLANNED_SETUP:       return "MENU held: setup";
        case PLANNED_SETUP_SAVED: return "Setup saved";
        case PLANNED_FACTORY:     return "Factory reset";
        case PLANNED_UPDATE:      return "Firmware update installed";
        default:                  return "Restarted by software";
      }
    default: return "Unknown";
  }
}

const char* netStepText(uint8_t s) {
  switch (s) { case NS_WIFI: return "reconnecting WiFi"; case NS_LOOKUP: return "looking up a place";
               case NS_FETCH: return "fetching aircraft"; case NS_ROUTES: return "looking up routes"; default: return "idle"; }
}

// v0.9.32: which part stopped (safety restart), and what it was doing
const char* failSrcName(uint8_t s);
String stuckText(const DiagEntry& d) {
  String t;
  switch (d.stuck) {
    case 1 + PET_NET:   t = "data (" + String(netStepText(d.netStep));
                        if (d.netStep == NS_FETCH) { t += " from "; t += failSrcName(d.netSrc); }
                        t += ")"; break;
    case 1 + PET_WEB:   t = d.webStep ? "web page (answering a page)" : "web page (idle)"; break;
    case 1 + PET_RADAR: t = d.radarStep ? "radar LCD (drawing)" : "radar LCD (idle)"; break;
    case 1 + PET_LOOP:  t = d.loopStep == LS_CLEAN ? "e-ink screen (nightly deep clean)" : d.loopStep ? "e-ink screen (refreshing the panel)" : "buttons and screen (idle)"; break;
    default: break;
  }
  return t;
}

// The router's reason codes for a WiFi disconnect, in plain words
const char* wifiReasonText(uint8_t r) {
  switch (r) {
    case 0:   return "none";
    case 2:   return "login timed out";
    case 3:   return "router ended it";
    case 4:   return "idle too long";
    case 8:   return "board disconnected";
    case 15: case 204: return "password handshake timed out (weak signal or interference)";
    case 200: return "lost the router's signal";
    case 201: return "network not found";
    case 202: return "password rejected";
    case 203: return "router refused";
    case 205: return "connection failed";
    default:  return "other";
  }
}

void onWifiEvent(arduino_event_id_t e, arduino_event_info_t info) {
  if (e == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
    wifiDropsNow++;
    wifiReasonNow = info.wifi_sta_disconnected.reason;
    wifiDropAt = millis();
  } else if (e == ARDUINO_EVENT_WIFI_STA_GOT_IP) {
    if (!wifiUpAt) wifiUpAt = millis();
  }
}

// "2 h 14 m", "5 m 10 s"
void durText(char* buf, size_t len, uint32_t s) {
  if (s >= 86400)     snprintf(buf, len, "%lu d %lu h", (unsigned long)(s / 86400), (unsigned long)(s % 86400 / 3600));
  else if (s >= 3600) snprintf(buf, len, "%lu h %lu m", (unsigned long)(s / 3600), (unsigned long)(s % 3600 / 60));
  else                snprintf(buf, len, "%lu m %lu s", (unsigned long)(s / 60), (unsigned long)(s % 60));
}

void saveDiagLog() {
  Preferences p;
  p.begin("fboard", false);
  p.putBytes("diag3", diagLog, sizeof diagLog);
  if (p.isKey("diag")) p.remove("diag");         // the older layouts (up to v0.9.0, up to v0.9.31)
  if (p.isKey("diag2")) p.remove("diag2");
  p.end();
}

// At start: log why the board restarted, with the notes the last run left behind
void diagBoot() {
  esp_reset_reason_t r = esp_reset_reason();
  bool ok = crumb.magic == CRUMB_MAGIC;
  memset(diagLog, 0, sizeof diagLog);
  {
    Preferences p;
    p.begin("fboard", true);
    if (p.getBytes("diag3", diagLog, sizeof diagLog) != sizeof diagLog) memset(diagLog, 0, sizeof diagLog);   // v0.9.32 layout
    p.end();
  }
  memmove(&diagLog[1], &diagLog[0], sizeof(DiagEntry) * (DIAG_N - 1));
  DiagEntry& d = diagLog[0];
  memset(&d, 0, sizeof d);
  d.reason = r;
  d.crumbOk = ok;
  if (ok) {
    d.planned = crumb.planned; d.upS = crumb.upS; d.minHeap = crumb.minHeap;
    d.stackNet = crumb.stackNet; d.stackWeb = crumb.stackWeb; d.stackRadar = crumb.stackRadar; d.stackLoop = crumb.stackLoop;
    d.wifiDrops = crumb.wifiDrops; d.wifiReason = crumb.wifiReason;
    d.netStep = crumb.netStep; d.webStep = crumb.webStep; d.loopStep = crumb.loopStep;
    d.feedOk = crumb.feedOk; d.feedFail = crumb.feedFail; d.heapNow = crumb.heapNow; d.largestNow = crumb.largestNow;
    d.lastErr = crumb.lastErr; d.lastErrSrc = crumb.lastErrSrc;
    d.lastErrAgoS = (crumb.lastErr && crumb.upS >= crumb.lastErrUpS) ? crumb.upS - crumb.lastErrUpS : 0;
    d.radarStep = crumb.radarStep; d.netSrc = crumb.netSrc;
    if (r == ESP_RST_TASK_WDT) {                       // v0.9.32: the part that stopped checked in longest ago
      uint32_t newest = 0; for (int i = 0; i < 4; i++) if (crumb.pet[i] > newest) newest = crumb.pet[i];
      int worst = -1; uint32_t worstAge = 0;
      for (int i = 0; i < 4; i++) {
        if (!crumb.pet[i]) continue;                   // never checked in (radar not connected, say)
        uint32_t age = newest - crumb.pet[i];
        if (age >= worstAge) { worstAge = age; worst = i; }
      }
      if (worst >= 0 && worstAge >= WDT_S / 2) d.stuck = 1 + worst;
    }
  }
  saveDiagLog();
  memset(&crumb, 0, sizeof crumb);
  crumb.magic = CRUMB_MAGIC;
  Serial.printf("Restart: %s", resetText(d.reason, d.planned));
  if (ok && d.upS) {
    char t[24]; durText(t, sizeof t, d.upS);
    Serial.printf(" after %s (net: %s, web: %s, screen: %s; lowest memory %lu KB; WiFi disconnects %u, last: %s)",
                  t, netStepText(d.netStep), d.webStep ? "answering a page" : "idle", d.loopStep ? "drawing" : "idle",
                  (unsigned long)(d.minHeap / 1024), d.wifiDrops, wifiReasonText(d.wifiReason));
  }
  Serial.println();
}

// Every couple of seconds from the loop: keep the notes up to date
void diagTick() {
  crumb.upS = millis() / 1000;
  crumb.minHeap = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);
  if (netTaskH)   crumb.stackNet   = uxTaskGetStackHighWaterMark(netTaskH);
  if (webTaskH)   crumb.stackWeb   = uxTaskGetStackHighWaterMark(webTaskH);
  if (radarTaskH) crumb.stackRadar = uxTaskGetStackHighWaterMark(radarTaskH);
  crumb.stackLoop = uxTaskGetStackHighWaterMark(nullptr);
  crumb.wifiDrops = wifiDropsNow;
  crumb.wifiReason = wifiReasonNow;
  crumb.feedOk = netStats.ok[0] + netStats.ok[1] + netStats.ok[2];
  crumb.feedFail = netStats.fail[0] + netStats.fail[1] + netStats.fail[2];
  crumb.heapNow = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
  crumb.largestNow = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
  if (!wifiUpRssi && WiFi.status() == WL_CONNECTED) wifiUpRssi = WiFi.RSSI();
  if (!diagStamped && time(nullptr) > 1700000000) {        // clock is set: date the restart
    diagStamped = true;
    clockSetAt = time(nullptr);
    diagLog[0].when = time(nullptr) - millis() / 1000;
    saveDiagLog();
  }
}

// Restart on purpose, noting why (so the log doesn't call it a crash)
void restartNow(uint8_t why) {
  crumb.planned = why;
  delay(50);
  ESP.restart();
}

// A failed aircraft fetch: count it and remember why (also in the notes that survive a crash)
void noteFetchFail(int s, int16_t code) {
  netStats.fail[s]++;
  netStats.lastErr[s] = code; netStats.lastErrAt[s] = millis(); netStats.lastErrLargest[s] = lastHttpLargest;
  if (code == 429) netStats.limited[s]++;
  crumb.lastErr = code; crumb.lastErrSrc = s; crumb.lastErrUpS = millis() / 1000;
}

// Why a request failed, in plain words
String failText(int16_t code, uint32_t largest) {
  char b[80];
  if (code == 429) return "too many requests (429)";
  if (code == 403) return "refused (403)";
  if (code == 404) return "not found (404)";
  if (code == ERR_GARBLED) return "garbled reply";
  if (code >= 500 && code < 600) { snprintf(b, sizeof b, "server error (%d)", code); return b; }
  if (code > 0) { snprintf(b, sizeof b, "HTTP %d", code); return b; }
  const char* t;
  switch (code) {
    case -1:   t = "couldn't connect"; break;
    case -2: case -3: t = "couldn't send the request"; break;
    case -4:   t = "not connected"; break;
    case -5:   t = "connection lost"; break;
    case -8:   t = "out of memory"; break;
    case -11:  t = "timed out"; break;
    case -100: t = "couldn't start the request"; break;
    default:   snprintf(b, sizeof b, "error %d", code); return b;
  }
  if (largest && largest < 40000 && (code == -1 || code == -8 || code == -100)) {
    snprintf(b, sizeof b, "%s (low memory: largest block %lu KB)", t, (unsigned long)(largest / 1024));
    return b;
  }
  return t;
}

const char* failSrcName(uint8_t s) { return s < 3 ? SOURCE_WEB[s] : s == 9 ? "route lookup (adsbdb)" : "?"; }

// Sky stats: unique aircraft, and the closest / highest / fastest since start
void skyNote(const Plane* list, int n, const Place& place) {
  for (int i = 0; i < n; i++) {
    const Plane& p = list[i];
    if (p.icao && sky.unique < SEEN_MAX) {
      bool known = false;
      for (int k = 0; k < sky.unique && !known; k++) known = sky.seen[k] == p.icao;
      if (!known) sky.seen[sky.unique++] = p.icao;
    }
    if (p.distNm < sky.closestNm) {
      sky.closestNm = p.distNm;
      copyStr(sky.closestCs, sizeof sky.closestCs, p.callsign);
      copyStr(sky.closestAt, sizeof sky.closestAt, place.isAirport ? place.code : place.name);
    }
    if (p.altFt > sky.highestFt) { sky.highestFt = p.altFt; copyStr(sky.highestCs, sizeof sky.highestCs, p.callsign); }
    if (p.gs > sky.fastestKt && p.gs < 800) { sky.fastestKt = p.gs; copyStr(sky.fastestCs, sizeof sky.fastestCs, p.callsign); }
  }
}

// "Nov 06 08:31 PM", or "not yet"
void whenText(char* buf, size_t len, time_t t) {
  if (!t) { copyStr(buf, len, "not yet"); return; }
  struct tm tmv; localtime_r(&t, &tmv);
  strftime(buf, len, "%b %d %I:%M %p", &tmv);
}

// The diagnostics as plain text, for the settings page (and the Copy button)
String diagText() {
  char sn[24], t[24], t2[24], line[300];
  serialNumber(sn, sizeof sn);
  String s = "Flight Board v" + String(FW_VERSION) + "  " + String(sn) + "\n";
  durText(t, sizeof t, millis() / 1000);
  snprintf(line, sizeof line, "Running for %s\n", t); s += line;

  s += "\nWiFi\n";
  if (WiFi.status() == WL_CONNECTED) {
    snprintf(line, sizeof line, "  %s, %d dBm now (%d dBm when it connected, %.1f s after start)\n",
             wifiName.c_str(), WiFi.RSSI(), wifiUpRssi, wifiUpAt / 1000.0f); s += line;
    snprintf(line, sizeof line, "  Access point %s, channel %d\n", WiFi.BSSIDstr().c_str(), WiFi.channel()); s += line;
  } else s += "  Not connected\n";
  if (wifiDropsNow) {
    durText(t, sizeof t, (millis() - wifiDropAt) / 1000);
    snprintf(line, sizeof line, "  Disconnects or failed tries: %u, last %s ago: %s (%u)\n", wifiDropsNow, t, wifiReasonText(wifiReasonNow), wifiReasonNow);
  } else snprintf(line, sizeof line, "  Disconnects or failed tries: 0\n");
  s += line;

  s += "\nData feed\n";
  snprintf(line, sizeof line, "  Requests: %lu\n", (unsigned long)netStats.requests); s += line;
  for (int k = 0; k < 3; k++) {
    snprintf(line, sizeof line, "  %-15s %lu ok, %lu failed", SOURCE_WEB[k], (unsigned long)netStats.ok[k], (unsigned long)netStats.fail[k]);
    s += line;
    if (netStats.limited[k]) { snprintf(line, sizeof line, " (%lu too many requests)", (unsigned long)netStats.limited[k]); s += line; }
    if (netStats.lastErr[k]) {
      durText(t, sizeof t, (millis() - netStats.lastErrAt[k]) / 1000);
      s += " | last failure "; s += t; s += " ago: "; s += failText(netStats.lastErr[k], netStats.lastErrLargest[k]);
    }
    s += "\n";
  }
  if (netStats.lastSource >= 0) {
    durText(t, sizeof t, (millis() - netStats.lastAt) / 1000);
    snprintf(line, sizeof line, "  Last: %s, %s ago, took %lu ms, %.1f KB\n", SOURCE_WEB[netStats.lastSource], t,
             (unsigned long)netStats.lastMs, netStats.lastBytes / 1024.0f);
  } else snprintf(line, sizeof line, "  Last: none yet\n");
  s += line;
  snprintf(line, sizeof line, "  Routes: %lu asked, %lu from memory, %lu looked up online, %lu not known, %lu failed",
           (unsigned long)netStats.routesAsked, (unsigned long)netStats.routesFromMemory, (unsigned long)netStats.routesOnline,
           (unsigned long)netStats.routesUnknown, (unsigned long)netStats.routesFailed);
  s += line;
  if (netStats.routesFailed) {
    durText(t, sizeof t, (millis() - netStats.routeLastErrAt) / 1000);
    s += " (last "; s += t; s += " ago: "; s += failText(netStats.routeLastErr, 0); s += ")";
  }
  s += "\n";
  if (!faKey[0]) s += "  FlightAware (Better Routes): off, no key\n";       // v0.9.35
  else if (!faUseI) s += "  FlightAware (Better Routes): paused (Use FlightAware for: Off)\n";
  else {
    faLoadCounts();
    int lim = FA_LIMITS[constrain(faLimitI, 0, 3)];
    snprintf(line, sizeof line, "  FlightAware (Better Routes): %lu lookups this month%s%s, about $%.2f, %lu routes fixed; %lu since power-on; last answer %s\n",
             (unsigned long)faCount, lim ? " of " : "", lim ? String(lim).c_str() : "", faCount * FA_COST, (unsigned long)faFixed,
             (unsigned long)netStats.faAsked, faLastErr == 0 ? "none yet" : faLastErr == 200 ? "OK" : faKeyRefused() ? "key not accepted" : String(faLastErr).c_str());
    s += line;
    { static const char* const U[] = { "off", "Home only", "Home + airports", "all places" };          // v0.9.38
      static const char* WF[] = { "all flights", "low + overhead flights", "low flights only" };   // v0.9.40
      static const char* PL[] = { "Light", "Full", "Custom" };                                     // v0.9.43
      snprintf(line, sizeof line, "    used for: %s, %s (%s); details opened: %s; %lu repeat lookups avoided since power-on\n", U[constrain(faUseI, 0, 3)],
               PL[constrain(faPlanI, 0, 2)], WF[constrain(faWhichI, 0, 2)], faDetailI ? "look it up" : "don't", (unsigned long)faRepeats); s += line; }
  }

  s += "\nBoard\n";
  uint16_t rev = ESP.getChipRevision();
  snprintf(line, sizeof line, "  %s v%u.%u, %lu MHz, %.1f C\n", ESP.getChipModel(), rev / 100, rev % 100,
           (unsigned long)ESP.getCpuFreqMHz(), temperatureRead()); s += line;
  snprintf(line, sizeof line, "  Free memory: %lu KB now, lowest %lu KB, largest block %lu KB\n",
           (unsigned long)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
           (unsigned long)(heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL) / 1024),
           (unsigned long)(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) / 1024));
  s += line;
  snprintf(line, sizeof line, "  PSRAM: %lu KB in use of %.1f MB\n", (unsigned long)((ESP.getPsramSize() - ESP.getFreePsram()) / 1024), ESP.getPsramSize() / 1048576.0f); s += line;
  snprintf(line, sizeof line, "  Spare stack: net %.1f KB, web %.1f KB, radar %.1f KB, screen %.1f KB\n",
           crumb.stackNet / 1024.0f, crumb.stackWeb / 1024.0f, crumb.stackRadar / 1024.0f, crumb.stackLoop / 1024.0f);
  s += line;
  whenText(t, sizeof t, lastCleanAt);
  snprintf(line, sizeof line, "  E-ink screen: %lu quick, %lu full refreshes; last deep clean %s\n", (unsigned long)epdQuick, (unsigned long)epdFull, t);
  s += line;
  whenText(t, sizeof t, clockSetAt);
  snprintf(line, sizeof line, "  Clock set: %s\n", t); s += line;

  s += "\nFirmware\n";
  snprintf(line, sizeof line, "  Built %s %s, Arduino core %d.%d.%d\n", __DATE__, __TIME__,
           ESP_ARDUINO_VERSION_MAJOR, ESP_ARDUINO_VERSION_MINOR, ESP_ARDUINO_VERSION_PATCH); s += line;
  snprintf(line, sizeof line, "  Program %.2f MB, %.2f MB free; flash chip %lu MB\n", ESP.getSketchSize() / 1048576.0f,
           (ESP.getFreeSketchSpace() > ESP.getSketchSize() ? ESP.getFreeSketchSpace() - ESP.getSketchSize() : 0) / 1048576.0f,
           (unsigned long)(ESP.getFlashChipSize() / 1048576)); s += line;

  s += "\nSince start\n";
  snprintf(line, sizeof line, "  Aircraft seen: %d%s unique\n", sky.unique, sky.unique >= SEEN_MAX ? "+" : ""); s += line;
  if (radarStyle > 0 && radarFrames) {
    unsigned long per = radarPeriodMs ? radarPeriodMs : RADAR_FRAME_MS;
    snprintf(line, sizeof line, "  Radar LCD: %lu fps measured (draw %lu ms, send %lu ms, limit %d fps)\n", (1000UL + per / 2) / per,
             (unsigned long)radarDrawMs, (unsigned long)radarSendMs, 1000 / RADAR_FRAME_MS); s += line;
  }
  snprintf(line, sizeof line, "  Overhead alerts: %lu%s\n", (unsigned long)alertsShown,
           !alertOn ? " (alert off)" : homeIndex() < 0 ? " (no Home set)" : ""); s += line;
  if (sky.closestCs[0]) {
    snprintf(t, sizeof t, "%.1f", showDist(sky.closestNm));
    snprintf(line, sizeof line, "  Closest: %s, %s %s from %s\n", sky.closestCs, t, UN.dist, sky.closestAt); s += line;
    formatAlt(showAlt(sky.highestFt), t2, sizeof t2);
    snprintf(line, sizeof line, "  Highest: %s at %s %s\n", sky.highestCs, t2, UN.alt); s += line;
    snprintf(line, sizeof line, "  Fastest: %s at %d %s\n", sky.fastestCs, showSpd(sky.fastestKt), UN.spd); s += line;
  }
  rlogText(s);                                         // v0.9.38
  s += "\nRestart log (newest first)\n";
  for (int i = 0; i < DIAG_N; i++) {
    const DiagEntry& d = diagLog[i];
    if (!d.reason && !d.when) continue;
    char when[32] = "time not known";
    if (d.when) { time_t w = d.when; struct tm tmv; localtime_r(&w, &tmv); strftime(when, sizeof when, "%b %d %I:%M %p", &tmv); }
    snprintf(line, sizeof line, "%d. %s  %s", i + 1, when, resetText(d.reason, d.planned)); s += line;
    if (d.crumbOk && d.upS) {
      durText(t, sizeof t, d.upS);
      snprintf(line, sizeof line, " after %s\n   net: %s, web: %s, screen: %s\n   lowest memory %lu KB, WiFi disconnects %u",
               t, netStepText(d.netStep), d.webStep ? "answering a page" : "idle", d.loopStep ? "drawing" : "idle",
               (unsigned long)(d.minHeap / 1024), d.wifiDrops);
      if (d.stuck) { String st = stuckText(d); snprintf(line + strlen(line), sizeof line - strlen(line), "\n   stopped: %s", st.c_str()); }
      s += line;
      if (d.wifiDrops) { s += " (last: "; s += wifiReasonText(d.wifiReason); s += ")"; }
      snprintf(line, sizeof line, "\n   data feed %u ok, %u failed; free memory %lu KB (largest block %lu KB)",
               d.feedOk, d.feedFail, (unsigned long)(d.heapNow / 1024), (unsigned long)(d.largestNow / 1024));
      s += line;
      if (d.lastErr) {
        durText(t, sizeof t, d.lastErrAgoS);
        s += "\n   last failure: "; s += failSrcName(d.lastErrSrc); s += ", "; s += failText(d.lastErr, 0); s += ", "; s += t; s += " before";
      }
    }
    s += "\n";
  }
  return s;
}

