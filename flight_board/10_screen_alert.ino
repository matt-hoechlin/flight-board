// Flight Board: Overhead alert screen.
// One tab of the flight_board sketch. Arduino joins the tabs in name order, after flight_board.ino.
// ---------------------------------------------------------------------
//  Overhead alert screen
// ---------------------------------------------------------------------
// Sky view: centre = straight up, edge = horizon; the dot is the aircraft, the arrow its heading
void drawSkyDial(int16_t cx, int16_t cy, int16_t R, float bearing, float elev, float track) {
  display.drawCircle(cx, cy, R, GxEPD_BLACK); display.drawCircle(cx, cy, R - 1, GxEPD_BLACK);
  for (int e = 30; e <= 60; e += 30) {                       // dotted 30 and 60 degree rings
    float r = R * (90 - e) / 90.0f;
    for (int t = 0; t < 360; t += 8) display.drawPixel(cx + r * sinf(t * DEG_TO_RAD), cy - r * cosf(t * DEG_TO_RAD), GxEPD_BLACK);
  }
  display.setFont(&DV_SMALL);
  const char* L[] = {"N", "E", "S", "W"};
  for (int i = 0; i < 4; i++) {
    float a = i * 90 * DEG_TO_RAD; int16_t bx, by; uint16_t bw, bh;
    display.getTextBounds(L[i], 0, 0, &bx, &by, &bw, &bh);
    int16_t tx = cx + (R - 11) * sinf(a) - bw / 2, ty = cy - (R - 11) * cosf(a) + bh / 2;
    display.setCursor(tx, ty); display.print(L[i]);
  }
  display.fillCircle(cx, cy, 2, GxEPD_BLACK);               // straight up
  if (elev < 0) elev = 0;
  float r = R * (90 - elev) / 90.0f, a = bearing * DEG_TO_RAD;
  int16_t px = cx + r * sinf(a), py = cy - r * cosf(a);
  display.fillCircle(px, py, 6, GxEPD_BLACK);
  float ta = track * DEG_TO_RAD;                              // where it's heading
  display.drawLine(px, py, px + 22 * sinf(ta), py - 22 * cosf(ta), GxEPD_BLACK);
  display.drawLine(px + 1, py, px + 1 + 22 * sinf(ta), py - 22 * cosf(ta), GxEPD_BLACK);
  int16_t hx = px + 26 * sinf(ta), hy = py - 26 * cosf(ta);
  display.fillTriangle(hx, hy, hx - 7 * sinf(ta - 0.5f), hy + 7 * cosf(ta - 0.5f), hx - 7 * sinf(ta + 0.5f), hy + 7 * cosf(ta + 0.5f), GxEPD_BLACK);
}

// A small degree ring after the text just printed (baseline y, ring raised by up px)
void printDegree(int16_t y, int16_t up, int16_t r) {
  int16_t dx = display.getCursorX() + r + 1;
  display.drawCircle(dx, y - up, r, GxEPD_BLACK);
  if (r > 2) display.drawCircle(dx, y - up, r - 1, GxEPD_BLACK);
  display.setCursor(dx + r + 2, y);
}

float alertSecs(unsigned long now) { return alertCur.tca - (long)(now - alertCur.at) / 1000.0f; }

void drawOverhead() {
  const int16_t W = display.width();
  char buf[64], line[112], small[64];
  const AlertInfo& a = alertCur;
  const Plane& p = a.p;
  float dt = (long)(millis() - a.at) / 1000.0f;
  float secs = a.tca - dt;
  int state = secs > ALERT_NOW_S ? 0 : secs >= -ALERT_NOW_S ? 1 : 2;   // approaching, now, passing

  // Where it is now (straight-line estimate from the last report)
  float x = a.x + a.vx * dt, y = a.y + a.vy * dt, dist = sqrtf(x * x + y * y);
  float bearing = fmodf(atan2f(x, y) * RAD_TO_DEG + 360.0f, 360.0f);
  long altNow = p.altFt + (long)(p.climbRate * dt / 60.0f); if (altNow < 0) altNow = 0;
  long above = altNow - a.homeFt; if (above < 0) above = 0;
  float elev = atan2f((float)above, dist * 6076.12f) * RAD_TO_DEG;
  // ...and at the closest point
  float xc = a.x + a.vx * a.tca, yc = a.y + a.vy * a.tca;
  float bearC = fmodf(atan2f(xc, yc) * RAD_TO_DEG + 360.0f, 360.0f);
  long aboveC = p.altFt + (long)(p.climbRate * a.tca / 60.0f) - a.homeFt; if (aboveC < 0) aboveC = 0;
  float elevC = atan2f((float)aboveC, a.dmin * 6076.12f) * RAD_TO_DEG;

  display.fillScreen(GxEPD_WHITE);
  display.drawRect(0, 0, W, display.height(), GxEPD_BLACK);
  if (state == 0) snprintf(buf, sizeof buf, "in %d:%02d", (int)secs / 60, (int)secs % 60);
  else snprintf(buf, sizeof buf, state == 1 ? "NOW" : "passing");
  const char* who = p.mil ? "Secret Squirrel" : airlineFullFor(p);
  snprintf(small, sizeof small, "%s%s%s", p.callsign, who && who[0] ? "  " : "", who ? who : "");
  display.setFont(&DV_MID);
  fitWords(small, 400);
  drawHeader("OVERHEAD", small, buf, ICON_HOUSE);
  display.setTextColor(GxEPD_BLACK);

  // ---- left: where to look
  bool up = elev >= 70;
  display.setFont(&DV_BIG);
  display.setCursor(14, 90);
  if (up) display.print("LOOK UP"); else { display.print("LOOK "); display.print(compass(bearing)); }
  display.setFont(&DV_MID);
  display.setCursor(14, 120);
  if (up) display.print("Almost straight up");
  else {
    snprintf(line, sizeof line, "%d", (int)lroundf(elev)); display.print(line);
    printDegree(120, 14, 3);
    snprintf(line, sizeof line, "up  -  %.1f %s away", showDist(dist), UN.dist); display.print(line);
  }
  display.setFont(&DV_ROW);
  const char* maker = makerName(p.type);
  if (p.type[0]) snprintf(line, sizeof line, "%s%s%s (%s)", maker, maker[0] ? " " : "", modelName(p.type), p.type);
  else snprintf(line, sizeof line, "Aircraft type unknown");
  if (p.reg[0] && strcmp(p.reg, p.callsign) != 0) { size_t n = strlen(line); snprintf(line + n, sizeof line - n, "  -  %s", p.reg); }
  fitWords(line, 360);
  display.setCursor(14, 146); display.print(line);
  if (p.fits >= 0 && p.fromCode[0] && p.toCode[0]) {         // route, with the board's arrow; the mark comes first (v0.9.32)
    char from[32], to[32];
    snprintf(from, sizeof from, "%s %s", p.fromCode, p.fromCity);
    snprintf(to, sizeof to, "%s %s", p.toCode, p.toCity);
    int16_t rx = 14;
    if (p.fits == 1) { drawPlaneIcon(rx, 165); rx += 22; }
    else { display.setCursor(rx, 168); display.print("~"); rx = display.getCursorX() + 8; }
    if (rx + textWidth(from) + textWidth(to) + 34 > 374) { copyStr(from, sizeof from, p.fromCode); copyStr(to, sizeof to, p.toCode); }
    display.setCursor(rx, 168); display.print(from);
    int16_t end = drawRouteArrow(display.getCursorX() + 8, 168, 18, 3, 5);
    display.setCursor(end + 8, 168); display.print(to);
  } else if (p.fits >= 0 && p.near && (p.near == 1 ? p.toCode[0] : p.fromCode[0])) {   // "~ Departing RNO Reno"
    snprintf(line, sizeof line, "~ %s %s %s", p.near == 1 ? "Arriving" : "Departing",
             p.near == 1 ? p.toCode : p.fromCode, p.near == 1 ? p.toCity : p.fromCity);
    fitWords(line, 360);
    display.setCursor(14, 168); display.print(line);
  } else {
    display.setCursor(14, 168); display.print(p.legNote == LEG_MISMATCH ? "Route not confirmed" : "No route on file");
  }

  // ---- middle: 2 x 3 facts
  char v[6][24]; const char* k[6];
  formatAlt(showAlt(altNow), buf, sizeof buf);  snprintf(v[0], sizeof v[0], "%s %s", buf, UN.alt); k[0] = "ALTITUDE";
  formatAlt(showAlt(above), buf, sizeof buf);   snprintf(v[1], sizeof v[1], "%s %s", buf, UN.alt); k[1] = "ABOVE YOU";
  snprintf(v[2], sizeof v[2], "%d %s", showSpd(p.gs), UN.spd); k[2] = "SPEED";
  snprintf(v[3], sizeof v[3], "%.1f %s", showDist(a.dmin), UN.dist); k[3] = state == 2 ? "PASSED AT" : "CLOSEST";
  snprintf(v[4], sizeof v[4], "%03d", (int)lroundf(p.track) % 360); k[4] = "HEADING";
  k[5] = p.climbRate > CLIMB_FPM ? "CLIMBING" : p.climbRate < -CLIMB_FPM ? "DESCENDING" : "VERTICAL";
  if (p.climbRate >= -CLIMB_FPM && p.climbRate <= CLIMB_FPM) snprintf(v[5], sizeof v[5], "Level");
  else if (unitsMode == U_METRIC) snprintf(v[5], sizeof v[5], "%+.1f m/s", p.climbRate * 0.00508f);
  else { formatAlt(labs((long)p.climbRate), buf, sizeof buf); snprintf(v[5], sizeof v[5], "%c%s fpm", p.climbRate > 0 ? '+' : '-', buf); }
  for (int i = 0; i < 6; i++) {
    int16_t fx = (i % 2) ? 498 : 392, fy = 84 + (i / 2) * 46;
    display.setFont(&DV_SMALL); display.setCursor(fx, fy - 18); display.print(k[i]);
    display.setFont(&DV_ROW);   display.setCursor(fx, fy + 4);  display.print(v[i]);
    if (i == 4) { printDegree(fy + 4, 10, 2); display.print(" "); display.print(compass(p.track)); }
  }
  display.drawFastVLine(380, 60, 150, GxEPD_BLACK);

  // ---- right: sky view
  drawSkyDial(698, 136, 76, bearing, elev, p.track);

  // ---- bottom: countdown to the closest point
  const int16_t bx = 14, bw = W - 28, by = 228, bh = 14;
  int warn = ALERT_WARN_S[alertWarnI];
  float frac = state == 0 ? secs / warn : state == 1 ? 0 : (ALERT_NOW_S + ALERT_PASS_S + secs) / ALERT_PASS_S;
  frac = constrain(frac, 0.0f, 1.0f);
  display.drawRect(bx, by, bw, bh, GxEPD_BLACK);
  int16_t fw = (int16_t)(bw * frac);
  if (state == 0) display.fillRect(bx + 2, by + 2, fw > 4 ? fw - 4 : 0, bh - 4, GxEPD_BLACK);
  else if (state == 2) for (int px = bx + 2; px < bx + fw - 2; px += 6) display.fillRect(px, by + 4, 3, bh - 8, GxEPD_BLACK);
  for (int t = 0; t <= 4; t++) display.drawFastVLine(bx + bw * t / 4, by + bh, 4, GxEPD_BLACK);
  display.setFont(&DV_SMALL);
  display.setCursor(14, 221);
  if (state == 0) {
    snprintf(line, sizeof line, "Closest in %d:%02d  -  step outside and look ", (int)secs / 60, (int)secs % 60);
    display.print(line);
    if (elevC >= 70) display.print("straight up");
    else {
      snprintf(line, sizeof line, "%s, about %d", compass(bearC), (int)lroundf(elevC)); display.print(line);
      printDegree(221, 8, 2); display.print("up");
    }
  } else if (state == 1) {
    snprintf(line, sizeof line, "Overhead now  -  closest pass %.1f %s", showDist(a.dmin), UN.dist); display.print(line);
  } else {
    int left = (int)(ALERT_NOW_S + ALERT_PASS_S + secs); if (left < 0) left = 0;
    snprintf(line, sizeof line, "Passing  -  back to the board in %d s", left); display.print(line);
  }
  display.setCursor(14, 264); display.print("Any button: back to the board");
  long below = ALERT_BELOW_FT[unitsMode][alertBelowI];
  if (below) snprintf(line, sizeof line, "Alert: within %s, below %s %s", ALERT_DIST_LABEL[unitsMode][alertDistI], ALERT_BELOW_LABEL[unitsMode][alertBelowI], UN.alt);
  else snprintf(line, sizeof line, "Alert: within %s, any height", ALERT_DIST_LABEL[unitsMode][alertDistI]);
  printRight(line, W - 14, 264);
}

