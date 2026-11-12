// Flight Board: The board: flight list, radar scope, header and footer.
// One tab of the flight_board sketch. Arduino joins the tabs in name order, after flight_board.ino.
// =====================================================================
//  Screens
// =====================================================================
// Cut text at a word (or letter) so it fits in maxW pixels in the current font
void fitWords(char* s, int16_t maxW) {
  int16_t bx, by; uint16_t bw, bh;
  for (;;) {
    display.getTextBounds(s, 0, 50, &bx, &by, &bw, &bh);
    if ((int16_t)(bw + bx) <= maxW) return;
    char* sp = strrchr(s, ' ');
    if (sp && sp != s) *sp = '\0';
    else { size_t n = strlen(s); if (n <= 1) return; s[n - 1] = '\0'; }
  }
}

// ---- "List + radar" layout ----
// Number in a black dot (rows and scope use the same numbers)
void drawBadge(int16_t cx, int16_t cy, int n) {
  char t[4]; snprintf(t, sizeof t, "%d", n);
  display.fillCircle(cx, cy, 8, GxEPD_BLACK);
  display.setFont(&DV_SMALL);
  int16_t bx, by; uint16_t bw, bh;
  display.getTextBounds(t, 0, 0, &bx, &by, &bw, &bh);
  display.setTextColor(GxEPD_WHITE);
  display.setCursor(cx - (int16_t)(bw / 2) - bx, cy - by - (int16_t)(bh / 2));
  display.print(t);
  display.setTextColor(GxEPD_BLACK);
}

void drawRowA(const Plane& p, int idx, int16_t y, bool airportMode) {
  char buf[24];
  drawBadge(A_BADGE + 8, y - 6, idx + 1);
  display.setFont(&DV_ROW);
  display.setCursor(A_FLIGHT, y); display.print(p.callsign);
  display.setCursor(A_AIRLINE, y);
  if (airportMode) display.print(statusWord(p.status));
  else {
    char al[40];
    const char* tag = nullptr; char sq[6];                     // v0.9.44: squawk tags
    if (sqEmergency(p.squawk) && emergI && emergConfirmed(p.icao)) { snprintf(sq, sizeof sq, "%04d", p.squawk); tag = sq; }
    else if (sqTagsOn) tag = sqTag(p.squawk);
    if (tag) { drawTag(A_AIRLINE, y, tag, &DV_SMALL); display.setFont(&DV_ROW); }
    else {
      airlineShortFor(p, al, sizeof al);
      fitWords(al, A_TYPE - A_AIRLINE - 8);
      display.print(al);
    }
  }
  display.setCursor(A_TYPE, y); display.print(p.type[0] ? p.type : "--");   // v0.9.40
  if (p.fits == 1) drawPlaneIcon(A_ICON, y);
  else if (p.fits == 0 || p.fits == 2) { display.setCursor(A_ICON, y); display.print("~"); }
  if (p.fits >= 0) drawRoute(A_ROUTE, y, p.fromCode, p.toCode, false);
  else { display.setCursor(A_ROUTE, y); display.print("--"); }
  formatAlt(showAlt(p.altFt), buf, sizeof buf);
  printRight(buf, A_ALT_R, y);
  if (p.climbRate >  CLIMB_FPM) drawArrow(A_ARROW, y, true);
  if (p.climbRate < -CLIMB_FPM) drawArrow(A_ARROW, y, false);
}

// Distance from the centre is stretched (square root) so nearby traffic doesn't bunch up
float scopeR(float nm, float range) { if (nm > range) nm = range; return A_SCOPE_R * sqrtf(nm / range); }

void scopeXY(float xNm, float yNm, float range, int16_t& sx, int16_t& sy) {
  float d = sqrtf(xNm * xNm + yNm * yNm), a = atan2f(xNm, yNm), r = scopeR(d, range);
  sx = A_SCOPE_X + (int16_t)lroundf(r * sinf(a));
  sy = A_SCOPE_Y - (int16_t)lroundf(r * cosf(a));
}

void scopeArrow(int16_t x, int16_t y, float trk, float s) {
  float a = trk * DEG_TO_RAD, ca = cosf(a), sa = sinf(a);
  auto P = [&](float px, float py, int16_t& ox, int16_t& oy) {
    ox = x + (int16_t)lroundf(px * ca - py * sa); oy = y + (int16_t)lroundf(px * sa + py * ca);
  };
  int16_t nx, ny, lx, ly, rx, ry, kx, ky;
  P(0, -s, nx, ny); P(-0.7f * s, 0.8f * s, lx, ly); P(0.7f * s, 0.8f * s, rx, ry); P(0, 0.35f * s, kx, ky);
  display.fillTriangle(nx, ny, rx, ry, kx, ky, GxEPD_BLACK);
  display.fillTriangle(nx, ny, kx, ky, lx, ly, GxEPD_BLACK);
}

void drawScope(const Place& p) {
  const int16_t cx = A_SCOPE_X, cy = A_SCOPE_Y, R = A_SCOPE_R;
  float range = p.radiusNm;
  display.drawCircle(cx, cy, R, GxEPD_BLACK);
  display.drawCircle(cx, cy, R - 1, GxEPD_BLACK);
  for (float ru : UN.rings) {                    // dotted rings inside the scan radius (round numbers in the chosen units)
    float nm = ru / UN.distK;
    if (nm >= range) continue;
    float r = scopeR(nm, range);
    for (int t = 0; t < 360; t += 6) display.drawPixel(cx + r * sinf(t * DEG_TO_RAD), cy - r * cosf(t * DEG_TO_RAD), GxEPD_BLACK);
  }
  for (int t = 0; t < 360; t += 30) {            // compass ticks
    float a = t * DEG_TO_RAD; int r1 = R - (t % 90 == 0 ? 7 : 4);
    display.drawLine(cx + r1 * sinf(a), cy - r1 * cosf(a), cx + R * sinf(a), cy - R * cosf(a), GxEPD_BLACK);
  }
  display.setFont(&DV_SMALL);
  display.setCursor(cx - 4, cy - R + 20); display.print("N");
  // Ring distances, one short line in the empty bottom-left corner (the scan radius is in the header)
  {
    char t[24]; t[0] = 0;
    for (float ru : UN.rings) {
      if (ru / UN.distK >= range) break;
      size_t n = strlen(t); snprintf(t + n, sizeof t - n, "%s%d", n ? "/" : "", (int)ru);
    }
    display.setCursor(A_DIVIDER + 7, 215); display.print(UN.dist);      // unit above, so the numbers stay clear of the circle
    display.setCursor(A_DIVIDER + 7, 231); display.print(t);
  }
  // you, in the middle
  display.drawLine(cx, cy - 5, cx + 5, cy, GxEPD_BLACK); display.drawLine(cx + 5, cy, cx, cy + 5, GxEPD_BLACK);
  display.drawLine(cx, cy + 5, cx - 5, cy, GxEPD_BLACK); display.drawLine(cx - 5, cy, cx, cy - 5, GxEPD_BLACK);
  for (int i = ROWS - 1; i >= 0; i--) {          // nearest last, so its number sits on top
    if (i >= viewCount) continue;
    const Plane& q = view[i];
    float a = q.bearing * DEG_TO_RAD, xNm = q.distNm * sinf(a), yNm = q.distNm * cosf(a);
    int16_t sx, sy;
    scopeXY(xNm, yNm, range, sx, sy);
    float bt = (q.track + 180) * DEG_TO_RAD;
    for (int m = 1; m <= 3; m++) {               // where it was 15, 30 and 45 seconds ago
      float nm = q.gs * (m * 15) / 3600.0f;
      float px = xNm + nm * sinf(bt), py = yNm + nm * cosf(bt);
      if (px * px + py * py >= range * range) break;
      int16_t tx, ty; scopeXY(px, py, range, tx, ty);
      display.fillCircle(tx, ty, 1, GxEPD_BLACK);
    }
    scopeArrow(sx, sy, q.track, 7);
    // Number beside the plane, off its trail: right of travel, else left, else ahead, and behind only as a last resort;
    // always inside the radar panel, clear of the footer and the ring legend in the bottom-left corner
    auto badgeOk = [](int16_t x, int16_t y) {
      if (x > 774 || x < A_DIVIDER + 12 || y < 61 || y > 222) return false;
      if (x < A_DIVIDER + 52 && y > 196) return false;          // "NM / 2/5" legend
      if (abs(x - A_SCOPE_X) < 16 && y < A_SCOPE_Y - A_SCOPE_R + 32) return false;   // the "N" at the top
      return true;
    };
    const float tryDeg[] = { 90, -90, 45, -45, 0, 135, -135, 180 };   // relative to the way it's flying
    int16_t bx = sx, by = sy;
    bool placed = false;
    for (float d : tryDeg) {
      float ba = (q.track + d) * DEG_TO_RAD;
      bx = sx + (int16_t)lroundf(17 * sinf(ba));
      by = sy - (int16_t)lroundf(17 * cosf(ba));
      if (badgeOk(bx, by)) { placed = true; break; }
    }
    if (!placed) {                               // nothing fits: keep it inside the panel anyway
      bx = constrain(sx + 17, A_DIVIDER + 12, 774);
      by = constrain(sy - 17, 61, 222);
    }
    drawBadge(bx, by, i + 1);
  }
}

void drawRow(const Plane& p, int16_t y, bool airportMode) {
  char buf[24];
  display.setFont(&DV_ROW);
  display.setCursor(C_FLIGHT, y);  display.print(p.callsign);

  display.setCursor(C_AIRLINE, y);
  if (airportMode) display.print(statusWord(p.status));
  else {
    char al[40];
    const char* tag = nullptr; char sq[6];                     // v0.9.44: squawk tags
    if (sqEmergency(p.squawk) && emergI && emergConfirmed(p.icao)) { snprintf(sq, sizeof sq, "%04d", p.squawk); tag = sq; }
    else if (sqTagsOn) tag = sqTag(p.squawk);
    if (tag) { drawTag(C_AIRLINE, y, tag, &DV_SMALL); display.setFont(&DV_ROW); }
    else {
      airlineShortFor(p, al, sizeof al);
      fitWords(al, C_TYPE - C_AIRLINE - 8);
      display.print(al);
    }
  }

  display.setCursor(C_TYPE, y);    display.print(p.type[0] ? p.type : "--");   // v0.9.40

  if (p.fits == 1) drawPlaneIcon(C_ICON, y);
  else if (p.fits == 0 || p.fits == 2) { display.setCursor(C_ICON, y); display.print("~"); }
  if (p.fits >= 0) drawRoute(C_ROUTE, y, p.fromCode, p.toCode, false);
  else { display.setCursor(C_ROUTE, y); display.print("--"); }

  formatAlt(showAlt(p.altFt), buf, sizeof buf);
  printRight(buf, C_ALT_R, y);
  if (p.climbRate >  CLIMB_FPM) drawArrow(C_ARROW, y, true);
  if (p.climbRate < -CLIMB_FPM) drawArrow(C_ARROW, y, false);

  snprintf(buf, sizeof buf, "%d", showSpd(p.gs));
  printRight(buf, C_GS_R, y);

  // heading = the way the plane is flying, e.g. "283W"
  snprintf(buf, sizeof buf, "%03d%s", (int)lround(p.track) % 360, compass(p.track));
  display.setCursor(C_HDG, y);     display.print(buf);

  snprintf(buf, sizeof buf, "%.1f", showDist(p.distNm));
  printRight(buf, C_DIST_R, y);
}

// "Seattle-Tacoma Intl (SEA)" for airports, otherwise the place's name
void placeLongName(const Place& p, char* buf, size_t len) {
  copyStr(buf, len, p.longName[0] ? p.longName : p.name);
}

// WiFi signal: 4 rising bars, filled up to the signal level; a small x when offline. y = text baseline.
const int16_t WIFI_ICON_W = 16, WIFI_ICON_GAP = 4;
void drawWifiIcon(int16_t x, int16_t y) {
  int level = 0;
  bool online = WiFi.status() == WL_CONNECTED;
  if (online) {
    int rssi = WiFi.RSSI();
    level = rssi > -55 ? 4 : rssi > -65 ? 3 : rssi > -75 ? 2 : rssi > -85 ? 1 : 0;
  }
  for (int i = 0; i < 4; i++) {
    int16_t h = 3 + i * 3, bx = x + i * 4;
    if (i < level) display.fillRect(bx, y - h, 3, h + 1, GxEPD_BLACK);
    else           display.drawRect(bx, y - h, 3, h + 1, GxEPD_BLACK);
  }
  if (!online) {                                    // small x over the bars
    display.drawLine(x + 9, y - 12, x + 15, y - 6, GxEPD_BLACK);
    display.drawLine(x + 15, y - 12, x + 9, y - 6, GxEPD_BLACK);
  }
}

// Draws footer items spread evenly from margin to margin, with a | centred in each gap.
// If they don't fit, the WiFi name is shortened with "..", and left out if it still won't fit.
int footerInvert = -1;                     // v0.9.44: this footer item is drawn white on black
void drawSpreadFooter(char item[][64], int n, int wifiItem, int16_t y) {
  const int16_t W = display.width(), left = 12, right = W - 12, minGap = 22;
  int16_t bx, by; uint16_t bw, bh;
  int16_t w[6];
  bool cut = false;
  for (;;) {
    int16_t total = 0;
    for (int i = 0; i < n; i++) {
      display.getTextBounds(item[i], 0, y, &bx, &by, &bw, &bh);
      w[i] = bw + bx;
      if (i == wifiItem) w[i] += WIFI_ICON_W + WIFI_ICON_GAP;   // room for the signal bars
      total += w[i];
    }
    int16_t gap = n > 1 ? (right - left - total) / (n - 1) : 0;
    if (gap >= minGap || wifiItem < 0) {
      int16_t x = left;
      for (int i = 0; i < n; i++) {
        if (i == n - 1 && n > 1) x = right - w[i];          // last item flush right
        if (i == wifiItem) {
          drawWifiIcon(x, y - 1);
          display.setCursor(x + WIFI_ICON_W + WIFI_ICON_GAP, y);
        } else {
          display.setCursor(x, y);
        }
        if (i == footerInvert) {                               // v0.9.44: white on black
          display.fillRect(x - 5, y - 14, w[i] + 10, 19, GxEPD_BLACK);
          display.setTextColor(GxEPD_WHITE); display.print(item[i]); display.setTextColor(GxEPD_BLACK);
        } else display.print(item[i]);
        x += w[i];
        if (i < n - 1) {
          display.drawFastVLine(x + gap / 2, y - 11, 13, GxEPD_BLACK);   // the | separator
          x += gap;
        }
      }
      return;
    }
    // too wide: shorten the WiFi name, or drop it
    size_t len = strlen(item[wifiItem]);
    size_t base = 3;
    if (cut) len -= 2;                                     // remove the old ".."
    if (len <= base) {
      for (int i = wifiItem; i < n - 1; i++) strcpy(item[i], item[i + 1]);
      n--;
      wifiItem = -1;
    } else {
      item[wifiItem][len - 1] = '\0';
      strcat(item[wifiItem], "..");
      cut = true;
    }
  }
}

void drawNoPlaces() {
  const int16_t W = display.width(), H = display.height();
  char clk[12];
  clockText(clk, sizeof clk, false);
  display.fillScreen(GxEPD_WHITE);
  display.drawRect(0, 0, W, H, GxEPD_BLACK);
  drawHeader("FLIGHT BOARD", nullptr, clk, ICON_NONE);
  display.setFont(&DV_BIG);
  printCentered("No places set up", 120);
  display.setFont(&DV_MID);
  printCentered("Hold EXIT for 3 seconds to add one.", 165);
}

void drawBoard() {
  if (noPlaces) { drawNoPlaces(); return; }
  const int16_t W = display.width(), H = display.height();
  char buf[100], small[40], clk[16];       // v0.9.25: room for "Refreshing..."
  Place p = places[curPlace];

  display.fillScreen(GxEPD_WHITE);
  display.drawRect(0, 0, W, H, GxEPD_BLACK);
  snprintf(small, sizeof small, "- %d%s Scan Radius", showRadius(p.radiusNm), UN.dist);
  clockText(clk, sizeof clk, false);
  if (menuRefreshAt) copyStr(clk, sizeof clk, "Refreshing...");   // MENU: until the new data arrives
  char shortName[24];
  copyStr(shortName, sizeof shortName, p.name);
  int16_t bx, by; uint16_t bw, bh;
  display.setFont(&DV_MID);
  display.getTextBounds(clk, 0, 32, &bx, &by, &bw, &bh);
  int16_t clkW = bw + bx;
  display.getTextBounds(small, 0, 32, &bx, &by, &bw, &bh);
  int16_t smallW = bw + bx;
  display.setFont(&DV_BIG);
  bool alertIcon = isHomePlace(p);                       // v0.9.32: alert status on the HOME page
  fitWords(shortName, W - 14 - clkW - 20 - smallW - 10 - 14 - (HOUSE_W + 10) - (alertIcon ? ALERT_ICON_W + 12 : 0));   // never hits the clock
  drawHeader(shortName, small, clk, placeIcon(p));
  if (alertIcon) drawAlertIcon(W - 14 - clkW - 12 - ALERT_ICON_W / 2, 23, alertOn);

  display.setFont(&DV_SMALL);
  if (boardLayout == 1) {                      // list + radar
    display.setCursor(A_BADGE + 4, 64);  display.print("#");
    display.setCursor(A_FLIGHT, 64);     display.print("FLIGHT");
    display.setCursor(A_AIRLINE, 64);    display.print(p.isAirport ? "STATUS" : "AIRLINE");
    display.setCursor(A_TYPE, 64);       display.print("TYPE");
    display.setCursor(A_ROUTE, 64);      display.print("ROUTE");
    { char h[12]; snprintf(h, sizeof h, "ALT %s", UN.altH); printRight(h, A_ALT_R, 64); }
    display.drawFastHLine(8, 70, A_DIVIDER - 14, GxEPD_BLACK);
    display.drawFastVLine(A_DIVIDER, 52, 182, GxEPD_BLACK);
    if (viewCount > 0)
      for (int i = 0; i < ROWS && i < viewCount; i++) drawRowA(view[i], i, ROW_Y0 + i * ROW_STEP, p.isAirport);
    drawScope(p);
  }
  if (boardLayout == 0) {
  display.setCursor(C_FLIGHT, 64);  display.print("FLIGHT");
  display.setCursor(C_AIRLINE, 64); display.print(p.isAirport ? "STATUS" : "AIRLINE");
  display.setCursor(C_TYPE, 64);    display.print("TYPE");
  display.setCursor(C_ROUTE, 64);   display.print("ROUTE");
  { char h[12]; snprintf(h, sizeof h, "ALT %s", UN.altH); printRight(h, C_ALT_R, 64);
    snprintf(h, sizeof h, "GS %s", UN.spdH); printRight(h, C_GS_R, 64); }
  display.setCursor(C_HDG, 64);     display.print("HDG");
  printRight(UN.distH, C_DIST_R, 64);
  display.drawFastHLine(8, 70, W - 16, GxEPD_BLACK);

  }
  const char* msg = nullptr;
  if (viewCount > 0) {
    if (boardLayout == 0)
      for (int i = 0; i < ROWS && i < viewCount; i++) drawRow(view[i], ROW_Y0 + i * ROW_STEP, p.isAirport);
  } else if (viewOk) {
    snprintf(buf, sizeof buf, "No aircraft within %d%s right now", showRadius(p.radiusNm), UN.dist);
    msg = buf;
  } else {
    msg = viewStatus;
  }
  if (msg && boardLayout == 1) {               // list + radar: keep the message in the left panel
    display.setFont(&DV_MID);
    printCenteredIn(msg, 12, A_DIVIDER - 12, 155, 28);
    msg = nullptr;
  }
  if (msg) {                                   // big if it fits, smaller if it's a long name
    int16_t bx, by; uint16_t bw, bh;
    display.setFont(&DV_MID);
    display.getTextBounds(msg, 0, 160, &bx, &by, &bw, &bh);
    if (bw > W - 24) display.setFont(&DV_ROW);
    printCentered(msg, 160);
  }

  // Footer: items spread evenly across the screen with a | between each
  display.drawFastHLine(8, 240, W - 16, GxEPD_BLACK);
  display.setFont(&DV_SMALL);
  char item[6][64];
  int n = 0;
  if (viewOk) {
    snprintf(item[n++], 64, "Updated %s", viewUpdated);
    if (viewCount > ROWS) snprintf(item[n++], 64, "%d of %d Aircraft", ROWS, viewCount);   // only the nearest fit
    else                  snprintf(item[n++], 64, "%d Aircraft", viewCount);
    if (faOnFor(places[curPlace]))                     // v0.9.39
      snprintf(item[n++], 64, "Source: %s + FlightAware", viewSource == 2 ? "Airplanes" : "ADSB");
    else snprintf(item[n++], 64, "Source: %s", SOURCE_NAMES[viewSource]);
    int ec; const Emerg* e = emergI ? emergBest(&ec) : nullptr;         // v0.9.44: an emergency squawk nearby
    if (e) { snprintf(item[n - 1], 64, "! %04d  %s  %.0f %s%s", e->code, e->p.callsign, showDist(e->homeNm), UN.dist, ec > 1 ? "  +1" : ""); footerInvert = n - 1; }
  } else if (viewCount > 0) {
    snprintf(item[n++], 64, "%s", viewStatus);
    snprintf(item[n++], 64, "Shown: %s", viewUpdated);
  } else {
    snprintf(item[n++], 64, "Knob: change screens   Press: aircraft details");
  }
  int wifiItem = n;
  snprintf(item[n++], 64, "%s", wifiName.c_str());       // drawn after the signal bars
  char date[16];
  dateText(date, sizeof date);
  if (date[0]) snprintf(item[n++], 64, "%s", date);
  snprintf(item[n++], 64, "Pg. %d/%d", curPlace + 1, numPlaces);
  drawSpreadFooter(item, n, wifiItem, 260);
  footerInvert = -1;
}

