// Flight Board: Web console: firmware update from the browser.
// One tab of the flight_board sketch. Arduino joins the tabs in name order, after flight_board.ino.
// ---- Firmware Update: a .bin file uploaded from the Settings page ----
// Checked on the way in: it must be an ESP32-S3 program (not a bootloader or a whole-flash "merged" file) and must
// carry FW_MARK. Nothing changes until the whole file is in and the ESP32 has verified it; otherwise the old
// version stays.
struct FwUpload { size_t got; int k; int vl; bool found, began; char ver[16]; String err; int parts; bool extra; } fwUp;
int8_t fwFailTab[FW_MARK_PRE];

void fwScanStart() {
  fwUp.k = 0; fwUp.vl = 0; fwUp.found = false; fwUp.ver[0] = 0;
  fwFailTab[0] = 0;                                     // where to carry on after a near-match ("FLIGHTBOARD-F...")
  for (int i = 1, k = 0; i < FW_MARK_PRE; i++) {
    while (k > 0 && FW_MARK[i] != FW_MARK[k]) k = fwFailTab[k - 1];
    if (FW_MARK[i] == FW_MARK[k]) k++;
    fwFailTab[i] = k;
  }
}

// Look for "FLIGHTBOARD-FW:<version>;" anywhere in the file, even split between two pieces
void fwScan(const uint8_t* b, size_t n) {
  for (size_t i = 0; i < n && !fwUp.found; i++) {
    char c = (char)b[i];
    if (fwUp.k == FW_MARK_PRE) {                        // reading the version
      if (c == ';' && fwUp.vl > 0) { fwUp.ver[fwUp.vl] = 0; fwUp.found = true; break; }
      if ((isdigit((unsigned char)c) || c == '.') && fwUp.vl < (int)sizeof fwUp.ver - 1) { fwUp.ver[fwUp.vl++] = c; continue; }
      fwUp.k = 0; fwUp.vl = 0;                          // not a version after all: keep looking
    }
    while (fwUp.k > 0 && c != FW_MARK[fwUp.k]) fwUp.k = fwFailTab[fwUp.k - 1];
    if (c == FW_MARK[fwUp.k]) fwUp.k++;
  }
}

// An ESP32-S3 program starts with 0xE9, chip 9 (ESP32-S3), and its description block (magic ABCD5432) at 0x20
bool fwLooksLikeApp(const uint8_t* b, size_t n) {
  return n >= 0x24 && b[0] == 0xE9 && (b[12] | (b[13] << 8)) == 9 &&
         b[0x20] == 0x32 && b[0x21] == 0x54 && b[0x22] == 0xCD && b[0x23] == 0xAB;
}

void handleUpdateUpload() {
  HTTPUpload& up = server.upload();
  if (up.status == UPLOAD_FILE_START && fwUp.parts++ > 0) { fwUp.extra = true; return; }   // a 2nd file: ignored
  if (fwUp.extra && up.status != UPLOAD_FILE_ABORTED) return;
  if (up.status == UPLOAD_FILE_START) {
    fwUp.got = 0; fwUp.began = false; fwUp.err = "";
    fwScanStart();
    if (!fromOwnPage(false)) { fwUp.err = "Refused: not from the Flight Board's own page."; return; }
    if (fwAsk != FWA_YES || (uint32_t)server.client().remoteIP() != fwAskIp || millis() - fwYesAt > FW_YES_MS) {
      fwUp.err = "Not allowed: press MENU on the board when it asks."; return;
    }
    fwUpdating = true;                                  // (before clearing the yes: the screen stays on the update)
    fwAsk = FWA_NONE;                                   // used up: the next update asks again
    Serial.printf("Firmware Update: receiving %s\n", up.filename.c_str());
  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (fwUp.err.length()) return;                      // already refused: let the rest go by
    if (fwUp.got == 0) {
      if (!fwLooksLikeApp(up.buf, up.currentSize)) { fwUp.err = "Not Flight Board firmware."; return; }
      if (!Update.begin(UPDATE_SIZE_UNKNOWN)) { fwUp.err = "Couldn't start (" + String(Update.errorString()) + ")."; return; }
      fwUp.began = true;
    }
    fwScan(up.buf, up.currentSize);
    wdtPet();                                           // a big upload is one long page request
    if (Update.write(up.buf, up.currentSize) != up.currentSize) fwUp.err = "Couldn't write it (" + String(Update.errorString()) + ").";
    fwUp.got += up.currentSize;
  } else if (up.status == UPLOAD_FILE_END) {
    if (!fwUp.err.length() && !fwUp.found) fwUp.err = "Not Flight Board firmware.";
    if (fwUp.err.length()) { if (fwUp.began) Update.abort(); }
    else if (!Update.end(true)) fwUp.err = "The file didn't check out (" + String(Update.errorString()) + ").";
    Serial.printf("Firmware Update: %u bytes, %s\n", (unsigned)fwUp.got, fwUp.err.length() ? fwUp.err.c_str() : fwUp.ver);
  } else if (up.status == UPLOAD_FILE_ABORTED) {
    if (fwUp.began) Update.abort();
    fwUp.began = false;
    fwUp.err = "The upload stopped part way.";
    fwUp.parts = 0; fwUp.extra = false;
    fwUpdating = false;
  }
}

// The page asks the board to ask its owner (POST /fwask?v=1.2.3), then checks the answer (GET /fwask)
void handleFwAsk() {
  if (!fromOwnPage(true)) { sendBlocked(); return; }
  server.sendHeader("Cache-Control", "no-store");
  unsigned long now = millis();
  if (server.method() == HTTP_POST) {
    if (fwUpdating || fwAsk == FWA_ASK) { server.send(200, "text/plain", "busy"); return; }
    String v = server.arg("v"); char cv[16]; int n = 0;
    for (unsigned i = 0; i < v.length() && n < (int)sizeof cv - 1; i++) if (isdigit((unsigned char)v[i]) || v[i] == '.') cv[n++] = v[i];
    cv[n] = 0;
    strlcpy(fwAskVer, cv, sizeof fwAskVer);
    fwAskReset = server.arg("fr") == "1";                // shown on the board, so MENU agrees to both
    fwAskIp = (uint32_t)server.client().remoteIP();
    fwAskAt = now;
    fwAsk = FWA_ASK;
    server.send(200, "text/plain", "ask");
    return;
  }
  if ((uint32_t)server.client().remoteIP() != fwAskIp) { server.send(200, "text/plain", "none"); return; }
  if (fwAsk == FWA_ASK) {
    unsigned long gone = now - fwAskAt;
    server.send(200, "text/plain", "ask " + String(gone < FW_ASK_MS ? (FW_ASK_MS - gone + 999) / 1000 : 0));
  } else server.send(200, "text/plain", fwAsk == FWA_YES ? "yes" : fwAsk == FWA_NO ? "no" : "none");
}

void handleUpdateDone() {
  fwUp.parts = 0; fwUp.extra = false;                   // ready for the next upload
  if (!fwUp.err.length() && fwUp.found) {
    server.sendHeader("Connection", "close");
    server.send(200, "text/plain", "OK " + String(fwUp.ver));
    strlcpy(fwNewVer, fwUp.ver, sizeof fwNewVer);
    fwResetAfter = fwAskReset;                  // v0.9.28
    restartKind = RESTART_UPDATE;               // restart first, then let the screen go (see the main loop)
    restartAt = millis() + 1500;
  } else {
    server.send(200, "text/plain", "ERR " + (fwUp.err.length() ? fwUp.err : String("Nothing was received.")));
  }
  fwUpdating = false;
}

// ---- Instructions & Help, About & Legal (stored on the board, work without internet) ----
const char HELP_BODY[] PROGMEM = R"HTML(
<div class=cols><div class=card><h2><span class=ic><svg class=i viewBox='0 0 24 24'><circle cx='12' cy='12' r='7'/><path d='M12 12l3-3.5'/><path d='M12 2v1.5M21.5 12H20M12 21.5V20M2.5 12H4M18.7 5.3l-1 1M5.3 5.3l1 1'/></svg></span>Button and Knob Functionality</h2><table>
<tr><td>Turn knob</td><td>Next / previous screen: <svg class=i viewBox='0 0 24 24'><path d='M3.5 11L12 4l8.5 7'/><path d='M6 9.5V20h12V9.5'/><path d='M10 20v-5.5h4V20'/></svg> Home, <svg class=i viewBox='0 0 24 24'><path d='M21 15.5l-8-5V4.2a1.5 1.5 0 0 0-3 0v6.3l-8 5v2l8-2.5V20l-2 1.5v1.2l3.5-1 3.5 1v-1.2L13 20v-5l8 2.5z' fill='currentColor' stroke='none'/></svg> airports, <svg class=i viewBox='0 0 24 24'><path d='M12 21.5s-6.5-6.2-6.5-11.5a6.5 6.5 0 0 1 13 0c0 5.3-6.5 11.5-6.5 11.5z'/><circle cx='12' cy='10' r='2.4'/></svg> other places (the icon in the title bar shows which). After power-on the board always starts on Home, or the first airport if Home is not set.</td></tr>
<tr><td>Press knob</td><td>Details for the nearest aircraft. Rotate up/down to step through the screens; depress to go back.</td></tr>
<tr><td>Hold knob &ndash; 1 second</td><td>Switch the Flight Board between List + Radar and List</td></tr>
<tr><td>EXIT</td><td>Returns the Flight Board to the Home screen</td></tr>
<tr><td>Hold EXIT &ndash; 3 seconds</td><td>Shows this page's address and QR code</td></tr>
<tr><td>MENU</td><td>Refreshes on-screen data from the API source</td></tr>
<tr><td>Hold MENU &ndash; 5 seconds</td><td>Restarts the Flight Board into setup mode (to change WiFi)</td></tr>
<tr><td>Hold MENU + EXIT &ndash; 5 seconds</td><td>Factory reset - clears all settings and saved data</td></tr></table></div>
<div class=card><h2><span class=ic><svg class=i viewBox='0 0 24 24'><rect x='4' y='3.5' width='16' height='17' rx='1.5'/><path d='M8 8.5h8M8 12h8M8 15.5h5'/></svg></span>Reading the Board</h2><table>
<tr><td>FLIGHT</td><td>Callsign or tail number (N-numbers are typically private aircraft)</td></tr>
<tr><td>Secret Squirrel</td><td>Shown as the airline for military or government aircraft</td></tr>
<tr><td>ROUTE</td><td><svg class=i viewBox='0 0 24 24'><path d='M21 15.5l-8-5V4.2a1.5 1.5 0 0 0-3 0v6.3l-8 5v2l8-2.5V20l-2 1.5v1.2l3.5-1 3.5 1v-1.2L13 20v-5l8 2.5z' fill='currentColor' stroke='none'/></svg> where the aircraft is flying | ~ might be a different leg of current flight plan | -- route unknown</td></tr>
<tr><td>ALT</td><td>Altitude - &#9650; climbing | &#9660; descending</td></tr>
<tr><td>GS / HDG / distance</td><td>Ground speed, heading (degrees and compass direction), distance</td></tr>
<tr><td>Screen</td><td>Normal or Upside down, for mounting the board the other way up (Settings page). The knob follows.</td></tr>
<tr><td>Units</td><td>Aviation (feet, knots, nautical miles), US (feet, mph, miles) or Metric (metres, km/h, km), on the Settings page.</td></tr>
<tr><td>List</td><td>Flight, airline, type, route, altitude, ground speed, heading and distance. Hold the knob for 1 second to switch between List + Radar and List.</td></tr>
<tr><td>List + Radar (Default)</td><td>Numbered dots on the radar match the numbered rows. The arrow points the way the aircraft is flying; the dots behind it show where it was 15, 30 and 45 seconds ago (a longer tail means a faster aircraft). Rings are at the distances in the bottom left corner (for example 2/5/10 NM, or 5/10/20 km in Metric); closer distances are spread out, and the outer circle is the scan radius.</td></tr>
<tr><td>Footer</td><td>Last data refresh &middot; "X of X Aircraft" = only the nearest X fit on screen; press the knob and turn it to step through more &middot; API data source &middot; WiFi signal &middot; date &middot; screen number (home, airports, other locations)</td></tr></table></div>
<div class=card><h2><span class=ic><svg class=i viewBox='0 0 24 24'><circle cx='10.5' cy='10.5' r='6'/><path d='M15 15l5.5 5.5'/></svg></span>Aircraft Details</h2><table>
<tr><td>Top left</td><td>Route, cities, aircraft type, registration</td></tr>
<tr><td>Tiles</td><td>Altitude (&#9650; climbing | &#9660; descending | &#9644; level), ground speed, heading, and distance with the direction relevant to your position.</td></tr>
<tr><td>Progress Bar</td><td>Where the aircraft is between its origin and destination, distance flown and to go, and a time to go. The time assumes a straight line at speed &ndash; actual arrivals may be slightly later.</td></tr>
<tr><td>Dashed Bar, ~</td><td>The route might be a different leg of the same flight number</td></tr>
<tr><td>No Bar</td><td>Private, unscheduled or no data available</td></tr></table></div>
<div class=card><h2><span class=ic><svg class=i viewBox='0 0 24 24'><path d='M6 16V11a6 6 0 0 1 12 0v5l1.5 2h-15z'/><path d='M10 20.5a2 2 0 0 0 4 0'/><path d='M12 3v2'/></svg></span>Overhead Alert</h2><table>
<tr><td>What it does</td><td>When an aircraft is about to pass close over Your Home, the board switches to an alert screen with a countdown to its closest point, so you can step outside and see it. Home only.</td></tr>
<tr><td>LOOK</td><td>Which way to look, and how high: 0&deg; is the horizon, 90&deg; is straight up.</td></tr>
<tr><td>Sky view</td><td>The centre is straight up and the edge is the horizon. The dot is the aircraft; the arrow shows where it&rsquo;s heading.</td></tr>
<tr><td>Countdown</td><td>Time to the closest point. Positions update once a minute, so it&rsquo;s an estimate.</td></tr>
<tr><td>Settings</td><td>On or off, how close, only aircraft below a height, and how early the countdown starts (Settings page).</td></tr>
<tr><td>Back to the board</td><td>Any button, or on its own about 20 seconds after the aircraft passes.</td></tr></table></div>
<div class=card><h2><span class=ic><svg class=i viewBox='0 0 24 24'><path d='M5.5 3.5h13M6.5 3.5l1.5 6h8l1.5-6M9 9.5v1M15 9.5v1M9.5 10.5l-1.5 10M14.5 10.5l1.5 10M6.5 20.5h11'/></svg></span>Airport Screens</h2><table>
<tr><td>ARRIVING</td><td>Coming in to land, or just landed and still on the runway. It's descending towards the selected airport, or its flight plan ends at the displayed location.</td></tr>
<tr><td>DEPARTING</td><td>Taking off or just took off. It's climbing away from the selected airport, or its flight plan starts at the displayed location.</td></tr>
<tr><td>OVERHEAD</td><td>Flying over the airport right now (within ~1 mile / 2 km), but not landing or taking off. Typically a plane passing high above; sometimes a small plane or helicopter circling low near the field. May switch to ARRIVING if a low descent is initiated towards the airport.</td></tr>
<tr><td>OVERFLIGHT</td><td>Passing through the area: somewhere in the scan circle but not over the airport and likely not landing or taking off at the displayed location. Small planes using a nearby airfield show here too.</td></tr></table></div>
<div class=card><h2><span class=ic><svg class=i viewBox='0 0 24 24'><circle cx='12' cy='12' r='9'/><circle cx='12' cy='12' r='5'/><path d='M12 12l6.3-6.3'/><circle cx='15.5' cy='15' r='1.1' fill='currentColor' stroke='none'/></svg></span>GC9A01 Radar LCD Add-On</h2><ul>
<li><b>What you need:</b> a 1.28" color LCD, 240&times;240, GC9A01 driver, 7-pin SPI, 3.3&nbsp;V, plus 7 male-to-female jumper wires.
<table style="margin:6px 0 2px"><tr><td>VCC</td><td>3V3, plastic-side row</td></tr><tr><td>GND</td><td>GND, screen-side row</td></tr>
<tr><td>SCL</td><td>IO15</td></tr><tr><td>SDA</td><td>IO17</td></tr><tr><td>DC</td><td>IO21</td></tr><tr><td>CS</td><td>IO9</td></tr><tr><td>RST</td><td>Not connected</td></tr></table></li>
<li><b>Never connect VCC to 5V. Unplug USB while wiring.</b></li>
<li>Choose Classic Radar, ATC Scope, Amber Sweep, or TCAS Traffic. Choose Off if no LCD is connected.</li><li>TCAS Traffic: hollow diamond = traffic, filled = close, amber circle = close and low, red square = overhead alert. &ldquo;+50&rdquo; = 5,000 ft.</li>
<li>Dims the radar display: 100%, 75%, 50%, or 25%. The LCD backlight always stays on.</li>
<li>Shows the same place and range as the Flight Board. The aircraft open/selected on the detail page turns yellow.</li><li>During an overhead alert it shows the sky view and the countdown.</li></ul></div>
<div class=card><h2><span class=ic><svg class=i viewBox='0 0 24 24'><rect x='2.5' y='6' width='19' height='12' rx='1.5'/><path d='M5.5 9.5h8M5.5 12h10M5.5 14.5h6'/></svg></span>The E-Ink Screen</h2><ul><li>A brief black-and-white flash every 15 minutes clears ghosting from earlier screens. This is normal.</li>
<li>Every night at 1AM, the display runs a deep cleaning cycle.</li><li><b>Screen updates</b> (Settings &gt; Display): the flight list and radar refresh every 60 seconds by default, without a flash. 20 or 40 seconds gives fresher positions, but e-ink panels are rated for a limited number of updates, so faster settings wear the panel out sooner (20 s: about three times as fast).</li></ul></div>
<div class=card><h2><span class=ic><svg class=i viewBox='0 0 24 24'><path d='M14.5 5.5a4.5 4.5 0 0 0 5.6 5.6L12 19.2a2.3 2.3 0 0 1-3.2-3.2l8.1-8.1'/><path d='M14.5 5.5l2.8 2.8'/></svg></span>Troubleshooting</h2><table>
<tr><td>NO WIFI</td><td>Check your router. To join a different network, hold MENU for 5 seconds to display the setup screen.</td></tr>
<tr><td>.local address won't open</td><td>Common on Android devices. Use the Flight Board's IP address or the QR code instead (hold EXIT for 3 seconds to display).</td></tr>
<tr><td>No aircraft</td><td>No aircraft in range, or the data sources are busy. Try a wider radius in settings or press MENU to refresh the data.</td></tr>
<tr><td>LCD add-on radar dark</td><td>Check that Radar LCD Add-On (Settings page) isn't set to Off / Not Connected. Double check wiring.</td></tr>
<tr><td>Slow or missed updates</td><td>Check the Wi-Fi signal shown in the title bar. If it&rsquo;s below &minus;80 dBm, move the board closer to the router.</td></tr>
<tr><td>Random restarts or WiFi trouble</td><td>The Diagnostic Log, on the Settings page, shows why the board last restarted and how the Wi-Fi is performing.</td></tr>
<tr><td>Factory reset (erase everything)</td><td>If all else fails, perform a factory reset by holding MENU and EXIT for 5 seconds.</td></tr></table></div>
</div>)HTML";

const char LEGAL_BODY[] PROGMEM = R"HTML(
<div class=warn><h2><span class=ic><svg class=i viewBox='0 0 24 24'><path d='M12 3.5L2.5 20h19z'/><path d='M12 10v4.5'/><circle cx='12' cy='17.2' r='1' fill='currentColor' stroke='none'/></svg></span>Notice</h2><p><b>Not for navigation or flight safety.</b> Aircraft data comes from volunteer networks and can be late, incomplete, or wrong.</p></div>
<div class=cols>
<div class=card><h2><span class=ic><svg class=i viewBox='0 0 24 24'><circle cx='12' cy='12' r='9'/><ellipse cx='12' cy='12' rx='4' ry='9'/><path d='M3 12h18M4.5 7.5h15M4.5 16.5h15'/></svg></span>Where the Data Comes From</h2><ul>
<li><b>adsb.lol</b>: Aircraft Positions, Open Database License (ODbL)<span class=lic>, adsb.lol</span></li>
<li><b>adsb.fi</b>: Aircraft Positions (backup source)<span class=lic>, adsb.fi</span></li>
<li><b>airplanes.live</b>: Aircraft Positions (backup source)<span class=lic>, airplanes.live</span></li>
<li><b>adsbdb</b>: Flight Routes and Airline Names<span class=lic>, adsbdb.com</span></li>
<li><b>FlightAware AeroAPI</b>: Flight Routes, only with your own key (optional)<span class=lic>, flightaware.com</span></li>
<li><b>OurAirports</b>: Airport Names (IATA), Codes and Locations (Public Domain)<span class=lic>, ourairports.com</span></li>
<li><b>Open-Meteo</b>: Town Search, CC BY 4.0<span class=lic>, open-meteo.com</span></li>
<li><b>Zippopotam.us</b>: Zip Code Lookups<span class=lic>, zippopotam.us</span></li>
<li><b>NTP Pool / NIST</b>: Time and Date</li>
</ul></div>
<div class=card><h2><span class=ic><svg class=i viewBox='0 0 24 24'><path d='M8 7l-5 5 5 5M16 7l5 5-5 5M14 4.5l-4 15'/></svg></span>Open-Source Software</h2><ul>
<li>Arduino core for ESP32 (Espressif) <span class=lic>LGPL-2.1 / Apache-2.0</span></li>
<li>GxEPD2 e-paper driver (Jean-Marc Zingg) <span class=lic>GPL-3.0</span></li>
<li>Adafruit GFX and Adafruit GC9A01A <span class=lic>BSD / MIT</span></li>
<li>Waveshare e-Paper demo: 4-grey mode for the 5.79&Prime; panel (Waveshare team) <span class=lic>MIT</span></li>
<li>ArduinoJson (Benoit Blanchon) <span class=lic>MIT</span></li>
<li>QR code generator, based on Project Nayuki <span class=lic>MIT</span></li></ul></div>
<div class=card><h2><span class=ic><svg class=i viewBox='0 0 24 24'><path d='M3.5 19l5-14 5 14M5.3 14h6.4'/><path d='M15.5 12.5c.8-.9 4.5-1.4 4.5 1.5V19M20 15.5c-4-.4-5 .8-5 2s1 1.8 2.3 1.5c1.6-.4 2.7-1.6 2.7-3.5'/></svg></span>Fonts</h2><ul>
<li>DejaVu Sans <span class=lic>Bitstream Vera license</span></li>
<li>B612 (Airbus / Intactile) <span class=lic>SIL Open Font License 1.1</span></li>
<li>Big Shoulders, Instrument Sans, Geist Mono (web pages) <span class=lic>SIL Open Font License 1.1</span></li></ul></div>
<div class=card><h2><span class=ic><svg class=i viewBox='0 0 24 24'><rect x='4.5' y='10.5' width='15' height='10' rx='1.5'/><path d='M8 10.5V7.5a4 4 0 0 1 8 0v3'/></svg></span>Privacy</h2><ul>
<li><b>Stored only on this board:</b> This Flight Board stores your WiFi name and password, your home and other locations, and your settings. A factory reset erases all of them.</li>
<li><b>What the board sends, and to whom:</b>
<ul><li><b>Aircraft data:</b> Your configured location(s) and range go to adsb.lol, adsb.fi, and airplanes.live to find nearby aircraft.</li>
<li><b>Route lookups:</b> Flight callsigns go to adsbdb.com to look up routes and airlines.</li>
<li><b>FlightAware (only if you add your own key):</b> The callsign of a flight whose free route doesn&rsquo;t match goes to FlightAware&rsquo;s AeroAPI, sent with your key.</li>
<li><b>Map lookups:</b> A zip code or town you type in is sent once to Zippopotam.us or Open-Meteo for location services.</li>
<li><b>Time:</b> A request for the time and date goes to the NTP Pool and NIST.</li></ul></li>
<li><b>Never sent:</b> Your Wi-Fi password, the board&rsquo;s serial number, or any details of how you use the Flight Board. Volary Dynamics receives no data from your Flight Board and has no remote access to it.</li>
</ul></div>
</div><div class=card><h2><span class=ic><svg class=i viewBox='0 0 24 24'><path d='M12 3.5V20M7 20h10M5 7h14M8 4.8l4-1.3 4 1.3'/><path d='M5 7l-2.8 6.5a2.8 2.8 0 0 0 5.6 0z'/><path d='M19 7l-2.8 6.5a2.8 2.8 0 0 0 5.6 0z'/></svg></span>Warranty, Liability &amp; Trademarks</h2><p class=legalese>THE FLIGHT BOARD (&ldquo;THE DEVICE&rdquo;), ITS FIRMWARE, SOFTWARE, CODE, AND ALL INFORMATION IT DISPLAYS ARE PROVIDED &ldquo;AS IS&rdquo; AND &ldquo;AS AVAILABLE&rdquo;, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE, TITLE, NON-INFRINGEMENT AND ACCURACY. VOLARY DYNAMICS DOES NOT WARRANT THAT OPERATION WILL BE UNINTERRUPTED, TIMELY OR ERROR-FREE, THAT DEFECTS WILL BE CORRECTED, OR THAT THE DEVICE, ITS FIRMWARE, SOFTWARE, CODE, AND HARDWARE ARE SECURE OR FREE OF VULNERABILITIES, BUGS, OR OTHER HARMFUL COMPONENTS. TO THE MAXIMUM EXTENT PERMITTED BY LAW, IN NO EVENT SHALL VOLARY DYNAMICS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, CONSEQUENTIAL OR EXEMPLARY DAMAGES, INCLUDING LOSS OF DATA, PROPERTY DAMAGE OR NETWORK SECURITY INCIDENTS, ARISING OUT OF OR IN CONNECTION WITH THE USE OF OR INABILITY TO USE THE DEVICE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGES. TO THE MAXIMUM EXTENT PERMITTED BY LAW, VOLARY DYNAMICS&rsquo; TOTAL LIABILITY FOR ALL CLAIMS ARISING OUT OF OR IN CONNECTION WITH THE DEVICE SHALL NOT EXCEED ONE U.S. DOLLAR ($1.00). YOU USE THE DEVICE AT YOUR OWN RISK. BY USING THE DEVICE, YOU ACCEPT THESE TERMS.</p><p class=legalese style="margin-top:10px">THIRD-PARTY SERVICES (FLIGHTAWARE). THE &ldquo;BETTER ROUTES&rdquo; FEATURE IS OPTIONAL AND USES FLIGHTAWARE&rsquo;S AEROAPI ONLY WITH AN API KEY THAT YOU OBTAIN DIRECTLY FROM FLIGHTAWARE AND ENTER YOURSELF. VOLARY DYNAMICS IS NOT AFFILIATED WITH, ENDORSED BY, SPONSORED BY OR A PARTNER OR RESELLER OF FLIGHTAWARE, AND RECEIVES NO PAYMENT, COMMISSION, REFERRAL FEE OR OTHER COMPENSATION OF ANY KIND IN CONNECTION WITH ANY FLIGHTAWARE ACCOUNT, PLAN OR SUBSCRIPTION. YOUR USE OF AEROAPI IS GOVERNED SOLELY BY YOUR OWN AGREEMENT WITH FLIGHTAWARE, INCLUDING ITS LICENSE, TERMS AND PRICING, WHICH FLIGHTAWARE MAY CHANGE AT ANY TIME. YOU ARE SOLELY RESPONSIBLE FOR YOUR API KEY, FOR COMPLYING WITH THAT AGREEMENT AND FOR ALL FEES CHARGED TO YOUR ACCOUNT, INCLUDING FEES INCURRED THROUGH THE DEVICE. ANY USAGE COUNT, COST ESTIMATE OR MONTHLY LIMIT SHOWN BY THE DEVICE IS AN APPROXIMATION FOR CONVENIENCE ONLY AND IS NOT A BILLING RECORD OR A GUARANTEE THAT CHARGES WILL NOT EXCEED IT. VOLARY DYNAMICS MAKES NO REPRESENTATION OR WARRANTY AS TO THE AVAILABILITY, ACCURACY OR COST OF ANY THIRD-PARTY SERVICE AND SHALL HAVE NO LIABILITY FOR ANY FEES, CHARGES OR LOSSES ARISING FROM ITS USE. FLIGHTAWARE AND AEROAPI ARE TRADEMARKS OF THEIR RESPECTIVE OWNER.</p><p class=legalese style="margin-top:10px">AIRLINE NAMES AND AIRCRAFT TYPE NAMES BELONG TO THEIR OWNERS AND ARE SHOWN ONLY TO IDENTIFY FLIGHTS. FLIGHT BOARD AND VOLARY DYNAMICS ARE NOT AFFILIATED WITH OR ENDORSED BY ANY AIRLINE, MANUFACTURER, DATA SOURCE OR THE FAA.</p></div>
)HTML";

