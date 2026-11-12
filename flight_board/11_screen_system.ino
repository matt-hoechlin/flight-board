// Flight Board: Settings QR screen, screen refresh, deep clean, boot screens, messages.
// One tab of the flight_board sketch. Arduino joins the tabs in name order, after flight_board.ino.
void drawQr(const char* text, int16_t x, int16_t y, int16_t px) {
  static QrCode qr;
  if (!qrEncode(text, qr)) return;
  for (int my = 0; my < qr.size; my++)
    for (int mx = 0; mx < qr.size; mx++)
      if (qr.dark(mx, my)) display.fillRect(x + mx * px, y + my * px, px, px, GxEPD_BLACK);
}

// Hold EXIT 3 s: how to open the settings page from the home network
void drawSettingsScreen() {
  const int16_t W = display.width(), H = display.height();
  char url[64], ipUrl[40];
  snprintf(url, sizeof url, "%s.local", hostName.c_str());
  snprintf(ipUrl, sizeof ipUrl, "http://%s", WiFi.localIP().toString().c_str());
  display.fillScreen(GxEPD_WHITE);
  display.drawRect(0, 0, W, H, GxEPD_BLACK);
  drawHeader("SETTINGS", nullptr, nullptr, ICON_NONE);
  display.setFont(&DV_MID);
  display.setCursor(20, 82);  display.print("On a phone or computer on your WiFi");
  display.setFont(&DV_ROW);
  display.setCursor(20, 106); display.print("("); display.print(wifiName); display.print("), open:");
  display.setFont(&DV_BIG);
  display.setCursor(20, 150); display.print(url);
  display.setFont(&DV_ROW);
  display.setCursor(20, 182); display.print("or scan the code, or type "); display.print(ipUrl);
  display.setCursor(20, 206); display.print("Changes apply immediately upon saving.");
  display.setFont(&DV_SMALL);
  display.setCursor(20, 234); display.print("On Android, use the number address or the QR code.");
  display.setCursor(20, 258); display.print("Press any button to go back.");
  drawQr(ipUrl, 600, 62, 6);               // the IP address works on every phone
}

// Quick refresh normally. A full (flashing) refresh clears ghosting, but only with an automatic
// data update on the main board, at most every FULL_REFRESH_MIN minutes -- never on a button press.
void refreshScreen(bool fromData);
void greyFadeToWhite();                  // (further down, with the grey boot screen)
void greyPanelSetup();
bool greyRefresh();

// Radar look/brightness, alert options and web theme take effect by themselves (the radar and alert code read
// the settings every time). Units, layout and time zone need the same flights redrawn; upside down a full refresh.
void applyDisplaySettings() {
  int oldUnits = unitsMode, oldLay = boardLayout, oldTz = tzIndex; bool oldC24 = clock24, oldAlert = alertOn;
  xSemaphoreTake(dataLock, portMAX_DELAY);
  Preferences p; p.begin("fboard", true);
  loadDisplaySettings(p);
  p.end();
  xSemaphoreGive(dataLock);
  if (tzIndex != oldTz) configTzTime(tzRule(), "pool.ntp.org", "time.nist.gov");
  if (clock24 != oldC24 && shared.updated[0] && shared.updated[0] != '-') clockText(shared.updated, sizeof shared.updated, false);
  if (display.getRotation() != (screenFlip ? 2 : ROTATION)) {   // turned over: a full refresh in the new direction
    display.setRotation(screenFlip ? 2 : ROTATION);
    lastFullMs = 0;
    refreshScreen(true);
  } else if (unitsMode != oldUnits || boardLayout != oldLay || tzIndex != oldTz || clock24 != oldC24 ||
             alertOn != oldAlert) {      // v0.9.33: the alert icon next to the clock
    refreshScreen(false);                   // same flights, new look
  }                                         // otherwise the e-paper is left alone
}

void refreshScreen(bool fromData) {
  crumb.loopStep = LS_DRAW;                // back to idle at the top of the next loop
  if (greyFadePending) { greyFadePending = false; greyFadeToWhite(); }   // first list after a grey boot screen
  unsigned long t0 = millis();
  bool full = fromData && screenView == V_BOARD &&
              millis() - lastFullMs >= (unsigned long)FULL_REFRESH_MIN * 60000UL;
  if (full) display.setFullWindow();
  else      display.setPartialWindow(0, 0, display.width(), display.height());
  display.firstPage();
  do {
    if (screenView == V_SETTINGS) drawSettingsScreen();
    else if (screenView == V_ALERT) drawOverhead();
    else if (screenView == V_EMERG) drawEmergency();     // v0.9.44
    else if (screenView == V_DETAIL && detailIdx < viewCount) drawDetail();
    else drawBoard();
  } while (display.nextPage());
  display.powerOff();                       // rest the panel between updates
  if (full) lastFullMs = millis();
  if (full) epdFull++; else epdQuick++;
  if (screenView == V_ALERT) alertDrawnMs = millis();
  Serial.printf("%s refresh %lu ms\n", full ? "Full" : "Quick", millis() - t0);
}

// Nightly deep clean: solid black, solid white, then the board, each as a full refresh
void deepClean() {
  Serial.println("Nightly deep clean");
  crumb.loopStep = LS_CLEAN;
  greyFadePending = false;                 // full refreshes wipe the screen anyway
  for (int pass = 0; pass < 2; pass++) {
    wdtPet();
    display.setFullWindow();
    display.firstPage();
    do { display.fillScreen(pass == 0 ? GxEPD_BLACK : GxEPD_WHITE); } while (display.nextPage());
  }
  wdtPet();
  display.setFullWindow();
  display.firstPage();
  do { drawBoard(); } while (display.nextPage());
  display.powerOff();
  lastFullMs = millis();
  lastCleanAt = time(nullptr);
}

// At DEEP_CLEAN_HOUR, once a night, when the board is idle on the main screen
void maybeDeepClean(unsigned long now) {
  struct tm t;
  if (!getLocalTime(&t, 0)) return;
  if (t.tm_hour != DEEP_CLEAN_HOUR || t.tm_yday == lastCleanDay) return;
  if (screenView != V_BOARD || now - lastInputMs < (unsigned long)DEEP_CLEAN_IDLE_MIN * 60000UL) return;
  lastCleanDay = t.tm_yday;
  deepClean();
}

// 4 hex digits unique to this board, e.g. "A4B8" (used in the WiFi name and the .local address)
String boardId() {
  uint64_t m = ESP.getEfuseMac();
  char b[8];
  snprintf(b, sizeof b, "%02X%02X", (unsigned)((m >> 32) & 0xFF), (unsigned)((m >> 40) & 0xFF));
  return String(b);
}

// Serial number for the data plate, from the board's factory MAC address, e.g. "FB-3F2A-91C4"
void serialNumber(char* buf, size_t len) {
  uint64_t m = ESP.getEfuseMac();
  snprintf(buf, len, "FB-%02X%02X-%02X%02X", (unsigned)((m >> 16) & 0xFF), (unsigned)((m >> 24) & 0xFF),
           (unsigned)((m >> 32) & 0xFF), (unsigned)((m >> 40) & 0xFF));
}

// ---- Smooth boot screens: the panel's 4-grey mode ----
// Setup sequence, panel voltages and data layout from Waveshare's 5.79" e-Paper demo (EPD_5in79.c,
// Waveshare team, V1.0 2024-03-05). Copyright (c) Waveshare. Permission is hereby granted, free of charge, to any
// person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the
// Software without restriction, including without limitation the rights to use, copy, modify, merge, publish,
// distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions: The above copyright notice and this permission notice
// shall be included in all copies or substantial portions of the Software. THE SOFTWARE IS PROVIDED "AS IS",
// WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT
// OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
// Panel voltages that go with the grey waveform (gate, source, VCOM, EOPT), as in Waveshare's table
const uint8_t WS_GREY_VOLTAGES[6] = { 0x22, 0x17, 0x41, 0xA8, 0x32, 0x40 };   // 0x3F, 0x03, 0x04 x3, 0x2C

// Our strobe-free grey waveform (Waveshare's shakes every pixel 16 times: the strobe). The table written with
// command 0x32 is 5 blocks of 6 steps, 7 bytes a step: up to 4 phases (2-bit voltage + 6-bit length in frames),
// byte 5 = how many times. Measured on the CrowPanel: voltage 01 pushes toward BLACK, 10 toward WHITE; block 0
// is VCOM, blocks 1-4 are pixel codes 11 (white), 10 (dark grey), 01 (light grey), 00 (black). Starting from
// black, a lightening push of 2/3/4/5/8/12 frames gave 18/38/56/72/89/95% of the way to white (v0.9.15 test card).
const uint8_t GREY_BASE_FRAMES = 25;     // 0.5 s: every pixel all the way to black or white
const uint8_t GREY_DARK_FRAMES = 3;      // then lighten: dark grey ~38%
const uint8_t GREY_LIGHT_FRAMES = 5;     //               light grey ~72%
void greyBuildLut(uint8_t* lut) {
  memset(lut, 0, 227);
  auto step = [&](int block, int g, uint8_t vs, uint8_t frames) {
    uint8_t* r = lut + block * 42 + g * 7;
    r[0] = 0x01;                                   // 1 frame at rest (as Waveshare's)
    r[1] = (uint8_t)((vs << 6) | (frames & 0x3F));
    r[5] = 1;                                      // once: no repeats, no strobe
  };
  const uint8_t HOLD = 0, BLACK = 1, WHITE = 2;
  step(1, 0, WHITE, GREY_BASE_FRAMES); step(1, 1, HOLD,  GREY_LIGHT_FRAMES);   // 11 white
  step(2, 0, BLACK, GREY_BASE_FRAMES); step(2, 1, WHITE, GREY_DARK_FRAMES);    // 10 dark grey
  step(3, 0, BLACK, GREY_BASE_FRAMES); step(3, 1, WHITE, GREY_LIGHT_FRAMES);   // 01 light grey
  step(4, 0, BLACK, GREY_BASE_FRAMES); step(4, 1, HOLD,  GREY_LIGHT_FRAMES);   // 00 black
  lut[224] = 0x02;                                 // 50 frames a second
}


void epdSend(bool data, uint8_t v) {
  digitalWrite(EPD_DC, data ? HIGH : LOW);
  digitalWrite(EPD_CS, LOW);
  SPI.transfer(v);
  digitalWrite(EPD_CS, HIGH);
}
void epdCmd(uint8_t c)  { epdSend(false, c); }
void epdData(uint8_t d) { epdSend(true, d); }
void epdDatas(std::initializer_list<uint8_t> l) { for (uint8_t d : l) epdData(d); }
bool epdWaitIdle(int maxMs) {                     // BUSY is high while the panel works
  for (int t = 0; t < maxMs; t += 10) {
    if (digitalRead(EPD_BUSY) == LOW) { delay(20); return true; }
    delay(10);
  }
  return false;
}

// One layer of the grey picture for one half of the panel. Each controller drives 396 columns; like Waveshare,
// the master gets columns 0-399 and the slave 392-791 (the 4-pixel overlap is how the halves line up).
// Layer 0x24/0xA4 takes the low bit of each pixel, 0x26/0xA6 the high bit.
void greySendPlane(uint8_t cmd, const uint8_t* img, bool slave, bool highBit) {
  epdCmd(cmd);
  digitalWrite(EPD_DC, HIGH);
  digitalWrite(EPD_CS, LOW);
  for (int j = 0; j < 272; j++) {
    const uint8_t* row = img + j * 198 + (slave ? 98 : 0);
    for (int i = 0; i < 50; i++) {
      uint8_t out = 0;
      for (int o = 0; o < 2; o++) {
        uint8_t b = row[i * 2 + o];
        for (int k = 0; k < 4; k++) {
          uint8_t v = b >> 6;
          out = (out << 1) | (highBit ? (v >> 1) : (v & 1));
          b <<= 2;
        }
      }
      SPI.transfer(out);
    }
  }
  digitalWrite(EPD_CS, HIGH);
}

void greySetBlack(uint8_t* buf, int x, int y) {
  if (x < 0 || x >= 792 || y < 0 || y >= 272) return;
  buf[y * 198 + x / 4] &= ~(3 << (6 - 2 * (x & 3)));
}
// Text in a GFX font, black, into the grey picture (the version number)
void greyText(uint8_t* buf, const GFXfont* f, const char* s, int x, int y) {
  for (; *s; s++) {
    uint8_t c = (uint8_t)*s;
    if (c < f->first || c > f->last) continue;
    const GFXglyph* g = &f->glyph[c - f->first];
    const uint8_t* bm = f->bitmap + g->bitmapOffset;
    uint8_t bits = 0, bit = 0;
    for (int yy = 0; yy < g->height; yy++)
      for (int xx = 0; xx < g->width; xx++) {
        if (!(bit++ & 7)) bits = *bm++;
        if (bits & 0x80) greySetBlack(buf, x + g->xOffset + xx, y + g->yOffset + yy);
        bits <<= 1;
      }
    x += g->xAdvance;
  }
}

// Turned over: the picture rotates 180 degrees (whole buffer backwards, 4 pixels a byte reversed too)
void greyRotate180(uint8_t* buf, int n) {
  auto rev = [](uint8_t b) -> uint8_t { return ((b & 0x03) << 6) | ((b & 0x0C) << 2) | ((b & 0x30) >> 2) | ((b & 0xC0) >> 6); };
  for (int i = 0, k = n - 1; i <= k; i++, k--) { uint8_t a = rev(buf[i]); buf[i] = rev(buf[k]); buf[k] = a; }
}

// Draws boot screen idx in greys. Returns false (nothing drawn) if there's no memory for the picture.
bool showBootScreenGrey(int idx) {
  uint8_t* buf = (uint8_t*)malloc(BOOT_GREY_BYTES);             // 53 KB: the ESP32 puts big blocks in PSRAM
  if (!buf) return false;
  memcpy(buf, BOOT_GREY[idx], BOOT_GREY_BYTES);
  char v[24];
  if (BOOT_SCREENS[idx].style == BOOT_STYLE_DRAWING) {
    greyText(buf, &BOOT_14, FW_VERSION, DWG_REV_X, DWG_REV_Y);
  } else {
    snprintf(v, sizeof v, "v%s", FW_VERSION);
    int16_t bx, by; uint16_t bw, bh;
    display.setFont(&BOOT_14R);
    display.getTextBounds(v, 0, VER_Y, &bx, &by, &bw, &bh);
    greyText(buf, &BOOT_14R, v, VER_RIGHT - bw - bx, VER_Y);
  }
  if (!screenFlip) greyRotate180(buf, BOOT_GREY_BYTES);      // this panel's grey RAM runs the other way round

  SPI.beginTransaction(SPISettings(4000000, MSBFIRST, SPI_MODE0));
  greyPanelSetup();
  greySendPlane(0x24, buf, false, false);
  greySendPlane(0x26, buf, false, true);
  greySendPlane(0xA4, buf, true, false);
  greySendPlane(0xA6, buf, true, true);
  bool done = greyRefresh();
  SPI.endTransaction();
  free(buf);
  if (!done) Serial.println("Smooth boot screen: the panel didn't finish in 15 s");
  greyFadePending = done;                          // the first flight list fades this away first
  display.init(115200, true, 2, false);            // back to the normal driver
  display.setRotation(screenFlip ? 2 : ROTATION);
  return true;
}

// Reset the panel and set it up for our waveform (Waveshare's setup sequence and voltages, our table)
void greyPanelSetup() {
  digitalWrite(EPD_RST, HIGH); delay(200); digitalWrite(EPD_RST, LOW); delay(2); digitalWrite(EPD_RST, HIGH); delay(200);
  epdWaitIdle(3000);
  epdCmd(0x12); epdWaitIdle(3000);                                   // soft reset
  epdCmd(0x0C); epdDatas({ 0x8B, 0x9C, 0xA6, 0x0F });                // booster
  epdCmd(0x3C); epdData(0x81);                                       // border
  epdWaitIdle(3000);
  epdCmd(0x11); epdData(0x01);                                       // master: x up, y down
  epdCmd(0x44); epdDatas({ 0x00, 0x31 });
  epdCmd(0x45); epdDatas({ 0x0F, 0x01, 0x00, 0x00 });
  epdCmd(0x4E); epdData(0x00);
  epdCmd(0x4F); epdDatas({ 0x0F, 0x01 });
  epdWaitIdle(3000);
  epdCmd(0x91); epdData(0x00);                                       // slave: mirrored
  epdCmd(0xC4); epdDatas({ 0x31, 0x00 });
  epdCmd(0xC5); epdDatas({ 0x0F, 0x01, 0x00, 0x00 });
  epdCmd(0xCE); epdData(0x31);
  epdCmd(0xCF); epdDatas({ 0x0F, 0x01 });
  uint8_t lut[227]; greyBuildLut(lut);
  epdCmd(0x32); for (int i = 0; i < 227; i++) epdData(lut[i]);           // our strobe-free grey waveform
  epdCmd(0x3F); epdData(WS_GREY_VOLTAGES[0]);
  epdCmd(0x03); epdData(WS_GREY_VOLTAGES[1]);
  epdCmd(0x04); for (int i = 2; i < 5; i++) epdData(WS_GREY_VOLTAGES[i]);
  epdCmd(0x2C); epdData(WS_GREY_VOLTAGES[5]);
}

bool greyRefresh() {                               // run the waveform on what's in the panel's memory
  epdCmd(0x22); epdData(0xCF);
  epdCmd(0x20);
  delay(100);
  return epdWaitIdle(15000);
}

void greyFill(uint8_t cmd, uint8_t v) {            // one layer, one half, all the same
  epdCmd(cmd);
  digitalWrite(EPD_DC, HIGH);
  digitalWrite(EPD_CS, LOW);
  for (int i = 0; i < 50 * 272; i++) SPI.transfer(v);
  digitalWrite(EPD_CS, HIGH);
}

// Before the first flight list after a grey boot screen: every pixel pushed to white, once (no flash, and it
// wipes the boot screen completely: the same one-way push took the old flight list to the test card with no
// ghost). Then the normal driver starts knowing the screen is white, so the list goes on with a quick update.
void greyFadeToWhite() {
  Serial.println("Fading the boot screen to white");
  SPI.beginTransaction(SPISettings(4000000, MSBFIRST, SPI_MODE0));
  greyPanelSetup();
  greyFill(0x24, 0xFF); greyFill(0x26, 0xFF);     // code 11 everywhere = white
  greyFill(0xA4, 0xFF); greyFill(0xA6, 0xFF);
  bool done = greyRefresh();
  SPI.endTransaction();
  display.init(115200, !done, 2, false);           // white and both pictures white in memory: quick update next
  display.setRotation(screenFlip ? 2 : ROTATION);
  if (!done) lastFullMs = 0;                       // didn't finish: let the next update be a full one
}

// Boot screens take turns, one per power-on, in order (see boot_screens.h).
// Version and serial number are drawn on top so they always match this code and this board.
void showBootScreen() {
  prefs.begin("fboard", false);
  uint32_t n = prefs.getUInt("boot", 0);
  prefs.putUInt("boot", n + 1);
  prefs.end();
  const BootScreen& bs = BOOT_SCREENS[n % NUM_BOOT_SCREENS];
  if (showBootScreenGrey(n % NUM_BOOT_SCREENS)) { lastFullMs = millis(); return; }   // plain one only if no memory

  char buf[24];
  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    display.drawBitmap(0, 0, bs.bitmap, BOOT_W, BOOT_H, GxEPD_BLACK);
    display.setTextColor(GxEPD_BLACK);
    if (bs.style == BOOT_STYLE_DRAWING) {            // after "REV" in the title block
      display.setFont(&BOOT_14);
      display.setCursor(DWG_REV_X, DWG_REV_Y);
      display.print(FW_VERSION);
    } else if (bs.style == BOOT_STYLE_PLATE) {       // serial number and firmware fields
      display.setFont(&BOOT_18T);
      serialNumber(buf, sizeof buf);
      display.setCursor(PLATE_X, PLATE_SERIAL_Y); display.print(buf);
      snprintf(buf, sizeof buf, "REV %s", FW_VERSION);
      display.setCursor(PLATE_X, PLATE_FW_Y);     display.print(buf);
    } else {                                         // "v0.5.3" bottom right
      snprintf(buf, sizeof buf, "v%s", FW_VERSION);
      display.setFont(&BOOT_14R);
      printRight(buf, VER_RIGHT, VER_Y);
    }
  } while (display.nextPage());
  display.powerOff();
  lastFullMs = millis();
}

void showMessage(const char* title, const char* line2, const char* line3) {
  greyFadePending = false;                 // full refresh: wipes the boot screen itself
  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    display.drawRect(0, 0, display.width(), display.height(), GxEPD_BLACK);
    display.setTextColor(GxEPD_BLACK);
    display.setFont(&DV_BIG);
    printCentered(title, 100);
    display.setFont(&DV_MID);
    if (line2) printCentered(line2, 150);
    display.setFont(&DV_ROW);
    if (line3) printCentered(line3, 200);
  } while (display.nextPage());
  display.powerOff();
  lastFullMs = millis();
}


void showSetupScreen() {
  greyFadePending = false;                 // full refresh: wipes the boot screen itself
  const int16_t W = display.width(), H = display.height();
  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    display.drawRect(0, 0, W, H, GxEPD_BLACK);
    display.fillRect(0, 0, W, 46, GxEPD_BLACK);
    display.setTextColor(GxEPD_WHITE);
    display.setFont(&DV_BIG);
    display.setCursor(14, 33); display.print("FLIGHT BOARD SETUP");
    display.setTextColor(GxEPD_BLACK);

    // Step 1: join the board's WiFi
    display.setFont(&DV_MID);
    display.setCursor(20, 82);  display.print("1.  On your phone, open Settings");
    { int16_t end = drawRouteArrow(display.getCursorX() + 8, 82, 22, 3, 6);
      display.setCursor(end + 8, 82); display.print("WiFi"); }
    display.setCursor(52, 108); display.print("and join this network:");
    display.drawRect(52, 118, 360, 52, GxEPD_BLACK);
    display.drawRect(53, 119, 358, 50, GxEPD_BLACK);
    display.setFont(&DV_ROW);
    display.setCursor(66, 140); display.print("Network:");
    display.setCursor(66, 162); display.print("Password:");
    display.setFont(&DV_MID);
    display.setCursor(160, 141); display.print(apName);
    display.setCursor(160, 163); display.print(apPass);

    // Step 2: the page opens by itself (or use the code / address)
    display.setCursor(20, 200); display.print("2.  The setup portal opens by itself.");
    display.setFont(&DV_ROW);
    display.setCursor(52, 224); display.print("If it doesn't, scan the QR code, or open " SETUP_IP);

    // One QR code: opens the setup portal. White margin all round for phone cameras.
    drawQr("http://" SETUP_IP, 592, 64, 7);
    display.setFont(&DV_ROW);
    display.setCursor(20, 258);
    display.print("After initial setup hold the EXIT button for 3 seconds to access settings from WiFi");
  } while (display.nextPage());
  display.powerOff();
}

