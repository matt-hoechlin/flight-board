// Flight Board: Aircraft details screen.
// One tab of the flight_board sketch. Arduino joins the tabs in name order, after flight_board.ino.
// One instrument tile on the detail page: caption, big value, small unit
void drawTile(int16_t x, int16_t y, int16_t w, int16_t h, const char* cap, const char* val, const char* unit, bool degree) {
  display.drawRect(x, y, w, h, GxEPD_BLACK);
  display.drawRect(x + 1, y + 1, w - 2, h - 2, GxEPD_BLACK);
  display.setFont(&DV_SMALL);
  display.setCursor(x + 7, y + 16); display.print(cap);
  display.setFont(&DV_BIG);
  display.setCursor(x + 7, y + h - 9); display.print(val);
  int16_t vx = display.getCursorX() + 4;
  if (degree) { display.drawCircle(vx + 2, y + h - 29, 2, GxEPD_BLACK); vx += 8; }
  display.setFont(&DV_SMALL);
  display.setCursor(vx, y + h - 10); display.print(unit);
}

// Progress along the route: origin dot, solid line flown, plane, dotted line to go, destination ring
void drawRouteProgress(int16_t x0, int16_t x1, int16_t y, float frac, bool sure) {
  if (frac < 0) frac = 0;
  if (frac > 1) frac = 1;
  display.fillCircle(x0 + 4, y, 4, GxEPD_BLACK);
  display.drawCircle(x1 - 4, y, 4, GxEPD_BLACK);
  int16_t px = x0 + 10 + (int16_t)((x1 - x0 - 20) * frac);
  for (int16_t x = x0 + 8; x < px - 10; x++) {
    if (sure || (x / 4) % 2 == 0) display.drawFastVLine(x, y - 1, 3, GxEPD_BLACK);   // flown: solid (dashed if unsure)
  }
  for (int16_t x = px + 12; x < x1 - 10; x += 6) display.drawFastHLine(x, y, 2, GxEPD_BLACK);   // to go: dotted
  display.fillRect(px - 9, y - 9, 18, 18, GxEPD_WHITE);
  drawPlaneIcon(px - 7, y + 6);
}

void drawDetail() {
  const int16_t W = display.width(), H = display.height();
  const int16_t LEFT_W = 356, TILE_X = 382, TILE_W = 196, TILE_H = 52, TILE_GAP = 8, TILE_Y = 56;
  char buf[96], line[96];
  const Plane& p = view[detailIdx];
  Place place = places[curPlace];
  int shown = viewCount < ROUTE_LOOKUPS ? viewCount : ROUTE_LOOKUPS;

  display.fillScreen(GxEPD_WHITE);
  display.drawRect(0, 0, W, H, GxEPD_BLACK);
  snprintf(buf, sizeof buf, "%d of %d", detailIdx + 1, shown);
  drawHeader(p.callsign, airlineFullFor(p), buf, ICON_NONE);

  // ---- Left: route, cities, aircraft
  display.setFont(&DV_BIG);
  if (p.fits >= 0) {                                    // v0.9.32: the mark comes first
    int16_t rx = 14;
    if (p.fits == 1) { drawPlaneIcon(rx, 84); rx += 24; }
    else { display.setFont(&DV_MID); display.setCursor(rx, 86); display.print("~"); rx = display.getCursorX() + 10; display.setFont(&DV_BIG); }
    drawRoute(rx, 88, p.fromCode, p.toCode, true);
    display.setFont(&DV_SMALL);
    if (!p.fromCity[0] && p.toCity[0])      snprintf(line, sizeof line, "to  %s", p.toCity);
    else if (p.fromCity[0] && !p.toCity[0]) snprintf(line, sizeof line, "from  %s", p.fromCity);
    else snprintf(line, sizeof line, "%s  to  %s", p.fromCity, p.toCity);
    fitWords(line, LEFT_W);
    display.setCursor(14, 110); display.print(line);
  } else {
    display.setCursor(14, 88); display.print(p.legNote == LEG_MISMATCH ? "Route not confirmed" : "No route on file");
  }
  display.setFont(&DV_MID);
  const char* maker = makerName(p.type);
  if (p.type[0]) snprintf(line, sizeof line, "%s%s%s (%s)", maker, maker[0] ? " " : "", modelName(p.type), p.type);
  else snprintf(line, sizeof line, "Aircraft type unknown");
  fitWords(line, LEFT_W);
  display.setCursor(14, 142); display.print(line);
  display.setFont(&DV_ROW);
  line[0] = 0;
  if (p.reg[0] && strcmp(p.reg, p.callsign) != 0) snprintf(line, sizeof line, "%s", p.reg);
  if (place.isAirport) {                         // airport screens: what it's doing here
    size_t n = strlen(line);
    snprintf(line + n, sizeof line - n, "%s%s %s %s", n ? "   " : "", statusWord(p.status),
             p.status == ST_ARRIVING ? "at" : p.status == ST_DEPARTING ? "from" : p.status == ST_OVERHEAD ? "above" : "near",
             place.code);
  }
  fitWords(line, LEFT_W);
  display.setCursor(14, 166); display.print(line);

  // ---- Right: instrument tiles
  formatAlt(showAlt(p.altFt), buf, sizeof buf);
  int16_t tx[4], ty[4];
  for (int i = 0; i < 4; i++) { tx[i] = TILE_X + (i % 2) * (TILE_W + TILE_GAP); ty[i] = TILE_Y + (i / 2) * (TILE_H + 6); }
  drawTile(tx[0], ty[0], TILE_W, TILE_H, "ALTITUDE", buf, UN.alt, false);
  if (p.climbRate >  CLIMB_FPM) drawArrow(tx[0] + TILE_W - 20, ty[0] + 17, true);       // climbing / descending / level
  else if (p.climbRate < -CLIMB_FPM) drawArrow(tx[0] + TILE_W - 20, ty[0] + 17, false);
  else display.fillRect(tx[0] + TILE_W - 22, ty[0] + 9, 13, 3, GxEPD_BLACK);
  snprintf(buf, sizeof buf, "%d", showSpd(p.gs));
  drawTile(tx[1], ty[1], TILE_W, TILE_H, "SPEED", buf, UN.spd, false);
  snprintf(buf, sizeof buf, "%03d", (int)lround(p.track) % 360);
  drawTile(tx[2], ty[2], TILE_W, TILE_H, "HEADING", buf, compass(p.track), true);
  snprintf(buf, sizeof buf, "%.1f", showDist(p.distNm));
  snprintf(line, sizeof line, "%s %s", UN.dist, compass(p.bearing));
  drawTile(tx[3], ty[3], TILE_W, TILE_H, "DISTANCE", buf, line, false);

  // ---- Bottom: progress along the route
  const Airport* fa = p.fromCode[0] ? findAirport(p.fromCode) : nullptr;
  const Airport* ta = p.toCode[0] ? findAirport(p.toCode) : nullptr;
  display.setFont(&DV_ROW);
  if (p.fits >= 0 && fa && ta) {
    float flown  = distanceMiles(fa->lat, fa->lon, p.lat, p.lon) / 1.15078f;
    float togo   = distanceMiles(p.lat, p.lon, ta->lat, ta->lon) / 1.15078f;
    float total  = distanceMiles(fa->lat, fa->lon, ta->lat, ta->lon) / 1.15078f;
    float frac   = (flown + togo) > 1 ? flown / (flown + togo) : 0;
    snprintf(line, sizeof line, "%s  %s", p.fromCode, p.fromCity);
    fitWords(line, 360);
    display.setCursor(14, 192); display.print(line);
    snprintf(line, sizeof line, "%s  %s", p.toCity, p.toCode);
    fitWords(line, 360);
    printRight(line, W - 14, 192);
    drawRouteProgress(14, W - 14, 206, frac, p.fits == 1);
    if (p.fits == 1) {
      if (p.gs > 60) {
        int mins = (int)lroundf(togo / p.gs * 60.0f);
        snprintf(line, sizeof line, "%d of %d %s flown  -  %d %s / ~%d h %02d m to go", (int)lroundf(showDist(flown)), (int)lroundf(showDist(total)),
                 UN.dist, (int)lroundf(showDist(togo)), UN.dist, mins / 60, mins % 60);
      } else snprintf(line, sizeof line, "%d of %d %s flown  -  %d %s to go", (int)lroundf(showDist(flown)), (int)lroundf(showDist(total)),
                      UN.dist, (int)lroundf(showDist(togo)), UN.dist);
    } else if (p.legNote == LEG_RETURN)
      snprintf(line, sizeof line, "~ %s here: likely the return trip", p.status == ST_ARRIVING ? "landing" : "taking off");
    else snprintf(line, sizeof line, "~ may be a different leg");
    display.setFont(&DV_SMALL);
    printCentered(line, 232);
  } else {
    display.setFont(&DV_SMALL);
    if (p.fits >= 0 && p.legNote == LEG_OTHER_END_UNKNOWN && p.near) {      // v0.9.32: Home / other places
      const char* code = p.near == 1 ? p.toCode : p.fromCode;
      const char* city = p.near == 1 ? p.toCity : p.fromCity;
      snprintf(line, sizeof line, "~ %s %s %s: %s unknown", p.near == 1 ? "Arriving" : "Departing", code, city, p.near == 1 ? "origin" : "destination");
    } else if (p.fits >= 0 && p.legNote == LEG_OTHER_END_UNKNOWN)
      snprintf(line, sizeof line, "~ %s", p.status == ST_ARRIVING ? "landing here: origin unknown" : "taking off here: destination unknown");
    else if (p.fits >= 0 && p.fromCode[0] && p.toCode[0]) {               // v0.9.33: route known, an airport isn't in the list
      snprintf(line, sizeof line, "%s  %s", p.fromCode, p.fromCity); fitWords(line, 360);
      display.setFont(&DV_ROW); display.setCursor(14, 192); display.print(line);
      snprintf(line, sizeof line, "%s  %s", p.toCity, p.toCode); fitWords(line, 360);
      printRight(line, W - 14, 192);
      display.setFont(&DV_SMALL);
      snprintf(line, sizeof line, "Distance flown not available for this route");
      printCentered(line, 226);
      line[0] = 0;
    }
    else if (p.legNote == LEG_MISMATCH)                                    // v0.9.34
      snprintf(line, sizeof line, "The route on file for this flight number doesn't match where it is");
    else snprintf(line, sizeof line, "No published route for this flight");
    if (line[0]) printCentered(line, 206);
  }

  display.drawFastHLine(8, 246, W - 16, GxEPD_BLACK);
  display.setFont(&DV_SMALL);
  display.setCursor(12, 263);
  display.print("Knob: next/previous aircraft   Press or EXIT: back to board");
  if (p.routeFa && p.fits == 1) printRight("Route: FlightAware", W - 12, 263);   // v0.9.39
}


