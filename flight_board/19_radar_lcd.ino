// Flight Board: Radar LCD add-on (1.28" round GC9A01).
// One tab of the flight_board sketch. Arduino joins the tabs in name order, after flight_board.ino.
// =====================================================================
//  Radar add-on: 1.28" round GC9A01 screen on its own SPI bus
//  Wiring (cable colours): orange VCC->3V3, red GND->GND, olive SCL->IO15,
//  black SDA->IO17, white DC->IO21, grey CS->IO9, purple RST not connected.
//  Draws into a 240x240 picture in PSRAM, then sends it in one go (~15-20 frames/s).
//  Aircraft glide between the 60 s updates using their speed and track.
// =====================================================================

uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) { return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3); }
uint16_t rgbScale(uint8_t r, uint8_t g, uint8_t b, float k) {
  if (k < 0) k = 0;
  if (k > 1) k = 1;
  return rgb((uint8_t)(r * k), (uint8_t)(g * k), (uint8_t)(b * k));
}

// Called by the main loop when the aircraft, the place or the detail page changes
void radarPublish(bool newData) {
  if (!radarLock) return;
  xSemaphoreTake(radarLock, portMAX_DELAY);
  RadarSnap& s = radarShared;
  s.active = !noPlaces;
  s.range = places[curPlace].radiusNm;
  copyStr(s.name, sizeof s.name, places[curPlace].isAirport ? places[curPlace].code : places[curPlace].name);
  s.n = 0;
  for (int i = 0; i < viewCount && s.n < MAX_PLANES; i++) {
    const Plane& p = view[i];
    RadarBlip& b = s.b[s.n++];
    copyStr(b.cs, sizeof b.cs, p.callsign[0] ? p.callsign : "?");
    copyStr(b.type, sizeof b.type, p.type);                       // v0.9.44: for data blocks
    float a = p.bearing * DEG_TO_RAD;
    b.x = p.distNm * sinf(a);
    b.y = p.distNm * cosf(a);
    b.gs = p.gs;
    b.trk = p.track;
    b.airliner = isAirlineCallsign(p.callsign);
    b.dist = p.distNm;
    b.alt = p.altFt;
    b.vs = p.climbRate;
    b.squawk = p.squawk;
    b.icao = p.icao;
  }
  s.sel = (screenView == V_DETAIL && detailIdx < viewCount) ? detailIdx : -1;
  { // v0.9.46: the airports on the scope, nearest first (looked up again only when the place changes)
    static double aLat = 999, aLon = 999; static float aRange = -1;
    const Place& pl = places[curPlace];
    if (pl.lat != aLat || pl.lon != aLon || pl.radiusNm != aRange) {
      aLat = pl.lat; aLon = pl.lon; aRange = pl.radiusNm; s.nAp = 0;
      float k = cosf(pl.lat * DEG_TO_RAD), dmax = pl.radiusNm * 0.95f, best[6];
      for (int i = 0; i < NUM_AIRPORTS; i++) {
        const Airport& a = AIRPORTS[i];
        if (strlen(a.code) != 3 || !isalpha(a.code[0]) || !isalpha(a.code[1]) || !isalpha(a.code[2])) continue;   // airline-style codes only
        float x = (a.lon - pl.lon) * 60.0f * k, y = (a.lat - pl.lat) * 60.0f, d = sqrtf(x * x + y * y);
        if (d > dmax) continue;
        int at = s.nAp < 6 ? s.nAp++ : 6;
        while (at > 0 && best[at - 1] > d) { if (at < 6) { best[at] = best[at - 1]; s.ap[at] = s.ap[at - 1]; } at--; }
        if (at < 6) { best[at] = d; copyStr(s.ap[at].code, sizeof s.ap[at].code, a.code); s.ap[at].x = x; s.ap[at].y = y; }
      }
    }
  }
  if (newData || !s.t0) s.t0 = millis();
  {                                       // ground height where the board is looking (cached per place)
    static int gPlace = -1; static double gLat = 999; static long gFt = 0;
    const Place& pl = places[curPlace];
    if (gPlace != curPlace || gLat != pl.lat) { gPlace = curPlace; gLat = pl.lat; gFt = pl.isAirport ? pl.elevFt : groundFtNear(pl.lat, pl.lon); }
    s.ground = gFt;
  }
  s.atHome = homeIndex() == curPlace;
  s.alert = screenView == V_ALERT && alertCur.valid;
  s.aIdx = -1;
  if (s.alert && s.atHome)
    for (int i = 0; i < viewCount; i++) if (view[i].icao == alertCur.p.icao) { s.aIdx = i; break; }
  if (s.alert) {
    s.ax = alertCur.x; s.ay = alertCur.y; s.avx = alertCur.vx; s.avy = alertCur.vy; s.atca = alertCur.tca;
    s.atrk = alertCur.p.track; s.aAbove = alertCur.p.altFt - alertCur.homeFt; s.aClimb = alertCur.p.climbRate;
    s.aAt = alertCur.at; copyStr(s.acs, sizeof s.acs, alertCur.p.callsign);
    copyStr(s.afrom, sizeof s.afrom, alertCur.p.fromCode); copyStr(s.ato, sizeof s.ato, alertCur.p.toCode);   // v0.9.51: for the Scope
    copyStr(s.atype, sizeof s.atype, alertCur.p.type); s.ags = alertCur.p.gs; s.admin = alertCur.dmin; s.aToday = alertsToday;
    copyStr(s.aair, sizeof s.aair, airlineFullFor(alertCur.p)); s.aid = alertCur.p.icao;      // v0.9.52
    { const char* mk = makerName(alertCur.p.type); const char* md = modelName(alertCur.p.type);
      if (mk[0] && strcmp(mk, "Military")) snprintf(s.atn, sizeof s.atn, "%s %s", mk, md);   // v0.9.53: "Airbus A320" too
      else copyStr(s.atn, sizeof s.atn, md); }
  }
  xSemaphoreGive(radarLock);
}

// Where an aircraft is now: last known position moved along its track (at most 2 minutes)
void radarPos(const RadarBlip& b, unsigned long t0, float& x, float& y) {
  float dt = (millis() - t0) / 1000.0f;
  if (dt > 120) dt = 120;
  float nm = b.gs * dt / 3600.0f, a = b.trk * DEG_TO_RAD;
  x = b.x + nm * sinf(a);
  y = b.y + nm * cosf(a);
}

// Scope coordinates; false if outside the scope
bool radarXY(float xNm, float yNm, float range, int16_t& sx, int16_t& sy) {
  float k = RADAR_R / range;
  float px = xNm * k, py = yNm * k;
  if (px * px + py * py > (RADAR_R - 3) * (RADAR_R - 3)) return false;
  sx = RADAR_C + (int16_t)lroundf(px);
  sy = RADAR_C - (int16_t)lroundf(py);
  return true;
}


void radarText(GFXcanvas16& g, int16_t x, int16_t y, const char* s, uint16_t col) {
  g.setCursor(x, y); g.setTextColor(col); g.print(s);
}

// Aircraft symbol pointing along its track
void radarArrow(GFXcanvas16& g, int16_t x, int16_t y, float trk, uint16_t col, float s) {
  float a = trk * DEG_TO_RAD, ca = cosf(a), sa = sinf(a);
  auto P = [&](float px, float py, int16_t& ox, int16_t& oy) {
    ox = x + (int16_t)lroundf(px * ca - py * sa);
    oy = y + (int16_t)lroundf(px * sa + py * ca);
  };
  int16_t nx, ny, lx, ly, rx, ry, kx, ky;
  P(0, -s, nx, ny); P(-0.7f * s, 0.8f * s, lx, ly); P(0.7f * s, 0.8f * s, rx, ry); P(0, 0.35f * s, kx, ky);
  g.fillTriangle(nx, ny, rx, ry, kx, ky, col);
  g.fillTriangle(nx, ny, kx, ky, lx, ly, col);
}

// Fading wedge behind the sweep line, blended into the background colour (bR, bG, bB).
// Drawn before the rings so they stay visible through it.
void radarSweep(GFXcanvas16& g, float ang, uint8_t r, uint8_t gr, uint8_t b, float strength,
                uint8_t bR, uint8_t bG, uint8_t bB) {
  const int SLICES = 30; const float STEP = 2.0f;
  for (int i = SLICES - 1; i >= 0; i--) {
    float k = powf(1.0f - (float)i / SLICES, 1.6f) * strength;
    float a1 = (ang - i * STEP) * DEG_TO_RAD, a2 = (ang - (i + 1) * STEP) * DEG_TO_RAD;
    uint16_t c = rgb(bR + (uint8_t)((r > bR ? r - bR : 0) * k), bG + (uint8_t)((gr > bG ? gr - bG : 0) * k),
                     bB + (uint8_t)((b > bB ? b - bB : 0) * k));
    g.fillTriangle(RADAR_C, RADAR_C,
                   RADAR_C + RADAR_R * sinf(a1), RADAR_C - RADAR_R * cosf(a1),
                   RADAR_C + RADAR_R * sinf(a2), RADAR_C - RADAR_R * cosf(a2), c);
  }
}

// The bright sweep line itself (drawn after the rings)
void radarSweepLine(GFXcanvas16& g, float ang, uint8_t r, uint8_t gr, uint8_t b) {
  float a = ang * DEG_TO_RAD;
  int16_t ex = RADAR_C + RADAR_R * sinf(a), ey = RADAR_C - RADAR_R * cosf(a);
  uint16_t line = rgb(r, gr, b);
  g.drawLine(RADAR_C, RADAR_C, ex, ey, line);
  g.drawLine(RADAR_C + 1, RADAR_C, ex + 1, ey, line);
}

// How bright a blip is: full when the sweep has just passed it, fading over a turn
float radarGlow(float sweep, float bx, float by, float minGlow) {
  float brg = atan2f(bx, by) * RAD_TO_DEG;
  float since = fmodf(sweep - brg + 720.0f, 360.0f);
  float g = 1.0f - since / 300.0f;
  return g < minGlow ? minGlow : g;
}



// ---- Labels that don't pile up: each one claims its box; a label that would overlap is skipped
struct RadarBox { int16_t x, y, w, h; };
RadarBox radarBoxes[MAX_PLANES + 4];
int radarNumBoxes = 0;

bool radarClaim(int16_t x, int16_t y, int16_t w, int16_t h) {
  for (int i = 0; i < radarNumBoxes; i++) {
    const RadarBox& b = radarBoxes[i];
    if (x < b.x + b.w && b.x < x + w && y < b.y + b.h && b.y < y + h) return false;
  }
  if (radarNumBoxes < (int)(sizeof radarBoxes / sizeof radarBoxes[0])) radarBoxes[radarNumBoxes++] = { x, y, w, h };
  return true;
}

// Callsign beside an aircraft, if there's room (right side first, then left)
void radarLabel(GFXcanvas16& g, int16_t sx, int16_t sy, const char* cs, uint16_t col) {
  int16_t w = strlen(cs) * 6;
  auto inside = [](int16_t px, int16_t py) {
    long dx = px - RADAR_C, dy = py - RADAR_C;
    return dx * dx + dy * dy <= (long)(RADAR_R - 2) * (RADAR_R - 2);
  };
  int16_t xs[2] = { (int16_t)(sx + 7), (int16_t)(sx - 7 - w) };
  for (int16_t x : xs) {
    if (!inside(x, sy - 8) || !inside(x + w, sy - 8) || !inside(x, sy) || !inside(x + w, sy)) continue;
    if (radarClaim(x - 1, sy - 9, w + 2, 10)) { radarText(g, x, sy - 8, cs, col); return; }
  }
}

// Every aircraft's symbol claims a small box first, so labels avoid covering other aircraft
void radarClaimDots(const RadarSnap& s) {
  for (int i = 0; i < s.n; i++) {
    float x, y; int16_t sx, sy;
    radarPos(s.b[i], s.t0, x, y);
    if (radarXY(x, y, s.range, sx, sy)) radarClaim(sx - 3, sy - 3, 7, 7);
  }
}

// The selected aircraft's label, just outside its ring (always shown)
void radarLabelNear(GFXcanvas16& g, int16_t sx, int16_t sy, const char* cs, uint16_t col) {
  int16_t w = strlen(cs) * 6;
  int16_t x = sx + 12;
  long dx = x + w - RADAR_C, dy = sy - RADAR_C;
  if (dx * dx + dy * dy > (long)(RADAR_R - 2) * (RADAR_R - 2)) x = sx - 12 - w;
  radarClaim(x - 1, sy - 5, w + 2, 10);
  radarText(g, x, sy - 4, cs, col);
}

// Drawing order: the selected aircraft first, then nearest first (they get the labels)
int radarOrder[MAX_PLANES];
void radarSortOrder(const RadarSnap& s) {
  for (int i = 0; i < s.n; i++) radarOrder[i] = i;
  for (int i = 1; i < s.n; i++) {                 // insertion sort, small lists
    int v = radarOrder[i], j = i - 1;
    auto key = [&](int k) { return k == s.sel ? -1.0f : s.b[k].dist; };
    while (j >= 0 && key(radarOrder[j]) > key(v)) { radarOrder[j + 1] = radarOrder[j]; j--; }
    radarOrder[j + 1] = v;
  }
}

const uint16_t RADAR_SEL_YELLOW = 0xFF00;          // rgb(255, 224, 0)

// Drill-down: big yellow callsign with distance and altitude, on the half away from the aircraft
void radarSelectedBlock(GFXcanvas16& g, const RadarSnap& s) {
  if (s.sel < 0 || s.sel >= s.n) return;
  const RadarBlip& b = s.b[s.sel];
  float x, y; int16_t sx, sy;
  radarPos(b, s.t0, x, y);
  bool top = !radarXY(x, y, s.range, sx, sy) || sy >= RADAR_C;   // aircraft low -> block on top
  int16_t y1 = top ? RADAR_C - 78 : RADAR_C + 34;
  g.setTextSize(2);
  int16_t w = strlen(b.cs) * 12;
  g.fillRect(RADAR_C - w / 2 - 3, y1 - 2, w + 6, 30, 0);          // black behind the text so it reads cleanly
  radarText(g, RADAR_C - w / 2, y1, b.cs, RADAR_SEL_YELLOW);
  g.setTextSize(1);
  char alt[16], line[32];
  formatAlt(showAlt(b.alt), alt, sizeof alt);
  snprintf(line, sizeof line, "%.1f %s  %s %s", showDist(b.dist), UN.dist, alt, UN.alt);
  int16_t w2 = strlen(line) * 6;
  radarText(g, RADAR_C - w2 / 2, y1 + 18, line, RADAR_SEL_YELLOW);
}

// Keep labels out of where the big drill-down block will go
void radarSelectedBlockArea(const RadarSnap& s) {
  if (s.sel < 0 || s.sel >= s.n) return;
  float x, y; int16_t sx, sy;
  radarPos(s.b[s.sel], s.t0, x, y);
  bool top = !radarXY(x, y, s.range, sx, sy) || sy >= RADAR_C;
  int16_t y1 = top ? RADAR_C - 78 : RADAR_C + 34;
  radarClaim(RADAR_C - 66, y1 - 2, 132, 30);
}

// Place name under the centre, only if no aircraft label is already there
void radarCentreNameFree(GFXcanvas16& g, const char* name, uint16_t col) {
  int16_t w = strlen(name) * 6, x = RADAR_C - w / 2, y = RADAR_C + (int16_t)(RADAR_R * 0.70f);
  if (radarClaim(x - 1, y - 1, w + 2, 10)) radarText(g, x, y, name, col);
}

// Ring distances along the lower-right diagonal, clear of the N
void radarRingLabels(GFXcanvas16& g, float range, uint16_t col) {
  for (int k = 2; k <= 4; k += 2) {
    char t[8];
    snprintf(t, sizeof t, "%d", (int)lroundf(showDist(range * k / 4)));
    float r = RADAR_R * k / 4.0f - 4;
    int16_t tx = RADAR_C + (int16_t)(r * 0.7071f) - (int16_t)strlen(t) * 6, ty = RADAR_C + (int16_t)(r * 0.7071f) - 8;
    radarClaim(tx - 1, ty - 1, strlen(t) * 6 + 2, 10);
    radarText(g, tx, ty, t, col);
  }
}


void radarDrawAtc(GFXcanvas16& g, const RadarSnap& s) {
  g.fillScreen(rgb(8, 16, 32));
  uint16_t ring = rgb(40, 70, 110), tick = rgb(90, 130, 170), white = rgb(235, 240, 245),
           cyan = rgb(80, 200, 255), amber = rgb(255, 190, 60);
  for (int k = 1; k <= 4; k++) g.drawCircle(RADAR_C, RADAR_C, RADAR_R * k / 4, ring);
  for (int b = 0; b < 360; b += 10) {
    float a = b * DEG_TO_RAD; int r1 = RADAR_R - (b % 30 == 0 ? 6 : 3);
    g.drawLine(RADAR_C + r1 * sinf(a), RADAR_C - r1 * cosf(a), RADAR_C + RADAR_R * sinf(a), RADAR_C - RADAR_R * cosf(a),
               b % 30 == 0 ? tick : ring);
  }
  const char* NESW[] = { "N", "E", "S", "W" };
  for (int i = 0; i < 4; i++) {
    float a = i * 90 * DEG_TO_RAD;
    radarText(g, RADAR_C + (RADAR_R - 14) * sinf(a) - 2, RADAR_C - (RADAR_R - 14) * cosf(a) - 3, NESW[i], rgb(150, 180, 210));
  }
  radarNumBoxes = 0;
  radarRingLabels(g, s.range, tick);
  g.drawLine(RADAR_C, RADAR_C - 4, RADAR_C + 4, RADAR_C, white); g.drawLine(RADAR_C + 4, RADAR_C, RADAR_C, RADAR_C + 4, white);
  g.drawLine(RADAR_C, RADAR_C + 4, RADAR_C - 4, RADAR_C, white); g.drawLine(RADAR_C - 4, RADAR_C, RADAR_C, RADAR_C - 4, white);
  (void)amber;
  radarSelectedBlockArea(s);
  radarClaimDots(s);
  radarSortOrder(s);
  bool focus = s.sel >= 0;
  for (int o = 0; o < s.n; o++) {
    int i = radarOrder[o];
    float x, y; int16_t sx, sy;
    radarPos(s.b[i], s.t0, x, y);
    if (!radarXY(x, y, s.range, sx, sy)) continue;
    bool sel = i == s.sel;
    uint16_t col = sel ? RADAR_SEL_YELLOW : (s.b[i].airliner ? white : cyan);
    if (focus && !sel) col = (col >> 1) & 0x7BEF;                    // dim the others
    float a = (s.b[i].trk + 180) * DEG_TO_RAD, L = s.b[i].gs / 180.0f * RADAR_R / s.range;   // 20 seconds of trail
    if (L > 16) L = 16;
    g.drawLine(sx, sy, sx + L * sinf(a), sy - L * cosf(a), (col >> 1) & 0x7BEF);
    radarArrow(g, sx, sy, s.b[i].trk, col, 6);
    if (sel) { g.drawCircle(sx, sy, 9, RADAR_SEL_YELLOW); radarLabelNear(g, sx, sy, s.b[i].cs, col); }
    else if (!focus) radarLabel(g, sx, sy, s.b[i].cs, col);
  }
  if (!focus) radarCentreNameFree(g, s.name, tick);
  radarSelectedBlock(g, s);
}

// v0.9.44: ATC Data Blocks: a controller's scope. Each aircraft is a small square target with a line showing
// where it's heading, and a three-line tag: callsign / flight level (arrow when climbing or descending) and
// speed in tens of knots / aircraft type. Tags go on whichever side of the target is clear.
void radarDrawBlocks(GFXcanvas16& g, const RadarSnap& s) {
  g.fillScreen(rgb(3, 16, 9));
  uint16_t ring = rgb(30, 80, 45), dimr = rgb(20, 55, 30), green = rgb(124, 255, 138), dimg = rgb(70, 150, 82);
  g.drawCircle(RADAR_C, RADAR_C, RADAR_R, ring);
  for (int k = 1; k <= 3; k++) {
    int16_t r = RADAR_R * k / 4;
    for (int t = 0; t < 360; t += 4) g.drawPixel(RADAR_C + r * sinf(t * DEG_TO_RAD), RADAR_C - r * cosf(t * DEG_TO_RAD), dimr);
  }
  for (int b = 0; b < 360; b += 30) {
    float a = b * DEG_TO_RAD;
    g.drawLine(RADAR_C + (RADAR_R - 5) * sinf(a), RADAR_C - (RADAR_R - 5) * cosf(a), RADAR_C + RADAR_R * sinf(a), RADAR_C - RADAR_R * cosf(a), ring);
  }
  radarText(g, RADAR_C - 2, 8, "N", dimg);
  radarNumBoxes = 0;
  radarRingLabels(g, s.range, dimr);
  g.drawFastHLine(RADAR_C - 4, RADAR_C, 9, dimg); g.drawFastVLine(RADAR_C, RADAR_C - 4, 9, dimg);
  struct Box { int16_t x, y, w, h; } used[MAX_PLANES]; int nu = 0;
  auto clear = [&](int16_t x, int16_t y, int16_t w, int16_t h) {
    if (x < 6 || x + w > RADAR_SIZE - 6 || y < 6 || y + h > RADAR_SIZE - 6) return false;
    int16_t dx = x + w / 2 - RADAR_C, dy = y + h / 2 - RADAR_C;
    if (dx * dx + dy * dy > (RADAR_R - 14) * (RADAR_R - 14)) return false;
    for (int k = 0; k < nu; k++)
      if (x < used[k].x + used[k].w && used[k].x < x + w && y < used[k].y + used[k].h && used[k].y < y + h) return false;
    return true;
  };
  radarSortOrder(s);
  bool focus = s.sel >= 0;
  for (int o = 0; o < s.n; o++) {
    int i = radarOrder[o];
    const RadarBlip& b = s.b[i];
    float x, y; int16_t sx, sy;
    radarPos(b, s.t0, x, y);
    if (!radarXY(x, y, s.range, sx, sy)) continue;
    bool sel = i == s.sel;
    uint16_t col = sel ? RADAR_SEL_YELLOW : green;
    if (focus && !sel) col = dimg;
    g.fillRect(sx - 2, sy - 2, 5, 5, col);                                           // the target
    float a = b.trk * DEG_TO_RAD;
    g.drawLine(sx, sy, sx + 12 * sinf(a), sy - 12 * cosf(a), col);                   // where it's heading
    char l1[10], l2[12], l3[6];
    copyStr(l1, sizeof l1, b.cs);
    long fl = (b.alt + 50) / 100; if (fl < 0) fl = 0; if (fl > 999) fl = 999;
    int sp = (int)lroundf(b.gs / 10.0f); if (sp > 99) sp = 99;
    snprintf(l2, sizeof l2, "%03ld%c %02d", fl, b.vs > 500 ? 0x18 : b.vs < -500 ? 0x19 : ' ', sp);   // 0x18/0x19: arrows
    copyStr(l3, sizeof l3, b.type);
    int16_t w = 6 * (int16_t)(strlen(l1) > strlen(l2) ? strlen(l1) : strlen(l2)), h = l3[0] ? 26 : 17;
    int16_t cand[4][2] = { { (int16_t)(sx + 10), (int16_t)(sy - 20) }, { (int16_t)(sx - 10 - w), (int16_t)(sy - 20) },
                           { (int16_t)(sx + 10), (int16_t)(sy + 4) },  { (int16_t)(sx - 10 - w), (int16_t)(sy + 4) } };
    int c = -1;
    for (int k = 0; k < 4 && c < 0; k++) if (clear(cand[k][0], cand[k][1], w, h)) c = k;
    if (c < 0) {                                                                    // crowded: callsign only, or nothing
      if (sel || !focus) for (int k = 0; k < 4 && c < 0; k++) if (clear(cand[k][0], cand[k][1], 6 * (int16_t)strlen(l1), 8)) { c = k; h = 8; }
      if (c < 0) continue;
    }
    int16_t tx = cand[c][0], ty = cand[c][1];
    g.drawLine(sx + (c % 2 ? -3 : 3), sy, c % 2 ? tx + w + 1 : tx - 2, ty + (c < 2 ? 12 : 4), col == green ? dimg : col);   // leader line
    radarText(g, tx, ty, l1, col);
    if (h > 8) radarText(g, tx, ty + 9, l2, col);
    if (h > 17) radarText(g, tx, ty + 18, l3, col == green ? dimg : col);
    if (nu < MAX_PLANES) used[nu++] = { tx, ty, w, h };
  }
  if (!focus) radarCentreNameFree(g, s.name, dimg);
}

// v0.9.46: Modern TRACON, after the scopes in today's approach-control rooms (STARS): black, grey range rings and
// nearby airports, blue history dots behind each aircraft, green data blocks on a leader line. An airline flight
// gets a full block (callsign / altitude in hundreds + speed in tens of knots, swapping with the type every 2 s);
// everything else a one-line block (altitude + speed). The aircraft open on the board turns white.
void radarDrawTracon(GFXcanvas16& g, const RadarSnap& s) {
  g.fillScreen(0);
  uint16_t ring = rgb(52, 56, 62), map = rgb(95, 100, 108), green = rgb(80, 235, 90), dimg = rgb(40, 120, 48),
           white = rgb(240, 240, 240), target = rgb(120, 190, 255);
  const uint16_t hist[5] = { rgb(40, 90, 255), rgb(32, 72, 210), rgb(26, 58, 170), rgb(20, 44, 130), rgb(14, 32, 95) };
  for (int k = 1; k <= 4; k++) {
    int16_t r = RADAR_R * k / 4;
    if (k < 4) for (int t = 0; t < 360; t += 2) g.drawPixel(RADAR_C + r * sinf(t * DEG_TO_RAD), RADAR_C - r * cosf(t * DEG_TO_RAD), ring);
    else g.drawCircle(RADAR_C, RADAR_C, r, ring);
  }
  for (int b = 0; b < 360; b += 10) {                                               // compass rose on the edge
    float a = b * DEG_TO_RAD; int16_t r1 = RADAR_R - (b % 30 == 0 ? 6 : 3);
    g.drawLine(RADAR_C + r1 * sinf(a), RADAR_C - r1 * cosf(a), RADAR_C + RADAR_R * sinf(a), RADAR_C - RADAR_R * cosf(a), b % 30 == 0 ? map : ring);
  }
  radarNumBoxes = 0;
  struct Box { int16_t x, y, w, h; } used[MAX_PLANES + 8]; int nu = 0;
  auto clear = [&](int16_t x, int16_t y, int16_t w, int16_t h) {
    int16_t dx = x + w / 2 - RADAR_C, dy = y + h / 2 - RADAR_C;
    if (dx * dx + dy * dy > (RADAR_R - 12) * (RADAR_R - 12)) return false;
    for (int k = 0; k < nu; k++)
      if (x < used[k].x + used[k].w && used[k].x < x + w && y < used[k].y + used[k].h && used[k].y < y + h) return false;
    return true;
  };
  for (int i = 0; i < s.nAp; i++) {                                                 // the airports, like the map's
    int16_t ax, ay;
    if (!radarXY(s.ap[i].x, s.ap[i].y, s.range, ax, ay)) continue;
    g.drawCircle(ax, ay, 3, map); g.drawFastHLine(ax - 5, ay, 11, map); g.drawFastVLine(ax, ay - 5, 11, map);
    if (clear(ax + 6, ay - 3, 18, 8)) { radarText(g, ax + 6, ay - 3, s.ap[i].code, map); used[nu++] = { (int16_t)(ax + 6), (int16_t)(ay - 3), 18, 8 }; }
  }
  { char h[32]; snprintf(h, sizeof h, "%s %ld%s", s.name, lroundf(showDist(s.range)), UN.dist);   // top: place and range
    int16_t w = strlen(h) * 6; radarText(g, RADAR_C - w / 2, 18, h, map); used[nu++] = { (int16_t)(RADAR_C - w / 2), 18, w, 8 }; }
  g.drawFastHLine(RADAR_C - 3, RADAR_C, 7, map); g.drawFastVLine(RADAR_C, RADAR_C - 3, 7, map);   // the board
  radarSortOrder(s);
  bool focus = s.sel >= 0, typeNow = (millis() / 2000) & 1;
  for (int o = 0; o < s.n; o++) {
    int i = radarOrder[o];
    const RadarBlip& b = s.b[i];
    float x, y; int16_t sx, sy;
    radarPos(b, s.t0, x, y);
    if (!radarXY(x, y, s.range, sx, sy)) continue;
    bool sel = i == s.sel;
    uint16_t col = sel ? white : (focus ? dimg : green);
    float a = b.trk * DEG_TO_RAD, step = b.gs / 3600.0f * 5.0f;                   // history: where it was every 5 s
    for (int h = 5; h >= 1; h--) {
      int16_t hx, hy;
      if (radarXY(x - step * h * sinf(a), y - step * h * cosf(a), s.range, hx, hy)) g.fillRect(hx - 1, hy - 1, 2, 2, hist[h - 1]);
    }
    g.fillCircle(sx, sy, 2, sel ? white : target);                                  // the aircraft
    char l1[10], l2[12];
    long fl = (b.alt + 50) / 100; if (fl < 0) fl = 0; if (fl > 999) fl = 999;
    int sp = (int)lroundf(b.gs / 10.0f); if (sp > 99) sp = 99;
    bool full = b.airliner || sel;
    if (full) {
      copyStr(l1, sizeof l1, b.cs);
      if (typeNow && b.type[0]) snprintf(l2, sizeof l2, "%03ld %s", fl, b.type);
      else snprintf(l2, sizeof l2, "%03ld %02d", fl, sp);
    } else {
      snprintf(l1, sizeof l1, "%03ld %02d", fl, sp); l2[0] = 0;
    }
    int16_t w = 6 * (int16_t)(strlen(l1) > strlen(l2) ? strlen(l1) : strlen(l2)), h = l2[0] ? 17 : 8;
    // leader line 14 px out; try NE, NW, SE, SW
    const int8_t dir[4][2] = { { 1, -1 }, { -1, -1 }, { 1, 1 }, { -1, 1 } };
    int c = -1; int16_t tx = 0, ty = 0;
    for (int k = 0; k < 4 && c < 0; k++) {
      int16_t ex = sx + dir[k][0] * 10, ey = sy + dir[k][1] * 10;
      tx = dir[k][0] > 0 ? ex + 2 : ex - 2 - w; ty = dir[k][1] < 0 ? ey - h + 3 : ey - 3;
      if (clear(tx, ty, w, h)) c = k;
    }
    if (c < 0 && l2[0]) {                                                         // crowded: try the top line only
      l2[0] = 0; h = 8; w = 6 * (int16_t)strlen(l1);
      for (int k = 0; k < 4 && c < 0; k++) {
        int16_t ex = sx + dir[k][0] * 10, ey = sy + dir[k][1] * 10;
        tx = dir[k][0] > 0 ? ex + 2 : ex - 2 - w; ty = dir[k][1] < 0 ? ey - h + 3 : ey - 3;
        if (clear(tx, ty, w, h)) c = k;
      }
    }
    if (c < 0) { if (!sel) continue; c = 0; tx = sx + 12; ty = sy - h - 7; }
    if (focus && !sel && !full) continue;                                          // keep it quiet while one is open
    g.drawLine(sx + dir[c][0] * 3, sy + dir[c][1] * 3, sx + dir[c][0] * 10, sy + dir[c][1] * 10, col);
    radarText(g, tx, ty, l1, col);
    if (l2[0]) radarText(g, tx, ty + 9, l2, col);
    if (nu < MAX_PLANES + 8) used[nu++] = { tx, ty, w, h };
  }
}

void radarDrawAmber(GFXcanvas16& g, const RadarSnap& s, float sweep) {
  g.fillScreen(rgb(10, 5, 0));
  uint16_t dim = rgb(110, 65, 10), amber = rgb(255, 170, 40);
  radarSweep(g, sweep, 255, 170, 40, 0.28f, 10, 5, 0);
  for (int k = 1; k <= 4; k++) g.drawCircle(RADAR_C, RADAR_C, RADAR_R * k / 4, dim);
  for (int b = 0; b < 360; b += 30) {
    float a = b * DEG_TO_RAD;
    g.drawLine(RADAR_C + (RADAR_R - 6) * sinf(a), RADAR_C - (RADAR_R - 6) * cosf(a),
               RADAR_C + RADAR_R * sinf(a), RADAR_C - RADAR_R * cosf(a), amber);
  }
  radarSweepLine(g, sweep, 255, 170, 40);
  radarText(g, RADAR_C - 2, 8, "N", amber);
  radarNumBoxes = 0;
  radarRingLabels(g, s.range, dim);
  g.fillTriangle(RADAR_C, RADAR_C - 4, RADAR_C + 4, RADAR_C, RADAR_C, RADAR_C + 4, amber);
  g.fillTriangle(RADAR_C, RADAR_C - 4, RADAR_C, RADAR_C + 4, RADAR_C - 4, RADAR_C, amber);
  radarSelectedBlockArea(s);
  radarClaimDots(s);
  radarSortOrder(s);
  bool focus = s.sel >= 0;
  for (int o = 0; o < s.n; o++) {
    int i = radarOrder[o];
    float x, y; int16_t sx, sy;
    radarPos(s.b[i], s.t0, x, y);
    if (!radarXY(x, y, s.range, sx, sy)) continue;
    bool sel = i == s.sel;
    float glow = radarGlow(sweep, x, y, 0.45f);
    uint16_t col = sel ? RADAR_SEL_YELLOW : rgbScale(255, 170, 40, focus ? glow * 0.45f : glow);
    radarArrow(g, sx, sy, s.b[i].trk, col, 5);
    if (sel) { g.drawCircle(sx, sy, 8, RADAR_SEL_YELLOW); radarLabelNear(g, sx, sy, s.b[i].cs, col); }
    else if (!focus) radarLabel(g, sx, sy, s.b[i].cs, col);
  }
  if (!focus) radarCentreNameFree(g, s.name, dim);
  radarSelectedBlock(g, s);
}


// Overhead alert on the round screen: sky view (centre = straight up), where to look, and the countdown

// ---------------------------------------------------------------------
//  Classic Radar (v0.9.5): navy scope, lime rings, wide sweep, trails, red blinking target, big callsign
// ---------------------------------------------------------------------
const uint8_t CL_BG_R = 14, CL_BG_G = 18, CL_BG_B = 36;
int radarFeatured(const RadarSnap& s) {           // the aircraft in the big label: the one open, else the closest
  if (s.sel >= 0 && s.sel < s.n) return s.sel;
  int best = -1; float bd = 1e9f;
  for (int i = 0; i < s.n; i++) if (s.b[i].dist < bd) { bd = s.b[i].dist; best = i; }
  return best;
}
// Red target rings that pulse outward (period ms); fast = overhead alert
void radarTarget(GFXcanvas16& g, int16_t x, int16_t y, bool fast) {
  unsigned long t = millis();
  int period = fast ? 600 : 1200;
  float ph = (t % period) / (float)period;                    // 0..1
  uint16_t red = rgb(255, 40, 40), dark = rgb(150, 20, 24);
  g.drawCircle(x, y, 5, red); g.drawCircle(x, y, 6, red);
  int r2 = 8 + (int)(ph * 8);                                 // the outer ring grows and fades
  g.drawCircle(x, y, r2, ph < 0.5f ? red : dark);
  if (fast && ((t / 300) & 1)) g.fillCircle(x, y, 3, red);    // blink
}
// Centred text in one of the board's fonts; baseline y
void radarCentred(GFXcanvas16& g, const GFXfont* f, int16_t y, const char* t, uint16_t col) {
  g.setFont(f);
  int16_t x1, y1; uint16_t w, h;
  g.getTextBounds(t, 0, y, &x1, &y1, &w, &h);
  g.setTextColor(col); g.setCursor(RADAR_C - (int16_t)w / 2 - x1, y); g.print(t);
  g.setFont(nullptr);
}
// Ring labels on the north line: "4mi" "8mi"; the outer ring's value goes in the header
void radarNorthLabels(GFXcanvas16& g, float range, uint16_t col) {
  for (int k = 1; k <= 2; k++) {
    char t[12];
    snprintf(t, sizeof t, "%ld%s", lroundf(showDist(range * k / 3)), UN.dist);
    int16_t y = RADAR_C - RADAR_R * k / 3 + 3;
    radarClaim(RADAR_C + 3, y - 1, strlen(t) * 6 + 2, 10);
    radarText(g, RADAR_C + 4, y, t, col);
  }
}
// Where the big two-line label goes: low on the scope, or high if its aircraft is down there
int16_t radarBlockY(bool have, int16_t sx, int16_t sy) {
  return (have && sy > RADAR_C + 22 && sy < RADAR_C + 100 && abs(sx - RADAR_C) < 95) ? RADAR_C - 58 : RADAR_C + 62;
}
// Dead-reckoned alert aircraft, relative to Home
void radarAlertPos(const RadarSnap& s, float& x, float& y, float& secs) {
  float dt = (long)(millis() - s.aAt) / 1000.0f;
  x = s.ax + s.avx * dt; y = s.ay + s.avy * dt; secs = s.atca - dt;
}
void radarAlertLines(const RadarSnap& s, float x, float y, float secs, char* l1, size_t n1, char* l2, size_t n2, const char* word) {
  if (secs > ALERT_NOW_S) snprintf(l1, n1, "%s %d:%02d", word, (int)secs / 60, (int)secs % 60);
  else if (secs >= -ALERT_NOW_S) snprintf(l1, n1, "%s NOW", word);
  else snprintf(l1, n1, "PASSING");
  float dist = sqrtf(x * x + y * y), above = s.aAbove + s.aClimb * ((long)(millis() - s.aAt) / 60000.0f);
  if (above < 0) above = 0;
  float elev = atan2f(above, dist * 6076.12f) * RAD_TO_DEG;
  if (elev >= 70) snprintf(l2, n2, "%s LOOK UP", s.acs);
  else snprintf(l2, n2, "%s LOOK %s %d UP", s.acs, compass(fmodf(atan2f(x, y) * RAD_TO_DEG + 360.0f, 360.0f)), (int)lroundf(elev));
}

void radarDrawClassic(GFXcanvas16& g, const RadarSnap& s, float sweep) {
  uint16_t ring = rgb(110, 150, 28), lime = rgb(190, 240, 40), white = rgb(235, 240, 245), yellow = rgb(255, 228, 40);
  g.fillScreen(rgb(CL_BG_R, CL_BG_G, CL_BG_B));
  radarSweep(g, sweep, 170, 255, 40, 0.62f, CL_BG_R, CL_BG_G, CL_BG_B);
  for (int k = 1; k <= 3; k++) g.drawCircle(RADAR_C, RADAR_C, RADAR_R * k / 3, k == 3 ? lime : ring);
  g.drawFastHLine(RADAR_C - RADAR_R, RADAR_C, 2 * RADAR_R, ring);
  g.drawFastVLine(RADAR_C, RADAR_C - RADAR_R, 2 * RADAR_R, ring);
  radarSweepLine(g, sweep, 225, 255, 140);
  radarNumBoxes = 0;
  bool alert = s.alert;
  float range = alert ? 6.0f : s.range;                       // alert: a close-in view around Home
  const char* NESW[] = { "N", "E", "S", "W" };
  for (int i = 0; i < 4; i++) {
    float a = i * 90 * DEG_TO_RAD;
    int16_t tx = RADAR_C + (RADAR_R - 8) * sinf(a) - 2, ty = RADAR_C - (RADAR_R - 8) * cosf(a) - 3;
    radarClaim(tx - 1, ty - 1, 8, 10);
    radarText(g, tx, ty, NESW[i], white);                     // v0.9.46: white labels (lime blended into the scope)
  }
  radarNorthLabels(g, range, white);
  { char h[32];                                                // header: place and outer ring
    snprintf(h, sizeof h, "%s  %ld%s", alert ? "HOME" : s.name, lroundf(showDist(range)), UN.dist);
    int16_t w = strlen(h) * 6;
    radarClaim(RADAR_C - w / 2 - 1, 25, w + 2, 10);
    radarText(g, RADAR_C - w / 2, 26, h, white); }
  radarArrow(g, RADAR_C, RADAR_C, 0, white, 5);               // where the board is
  int feat = alert ? -1 : radarFeatured(s);
  // ---- the aircraft: diamond + a short trail behind it
  radarClaimDots(s);
  radarSortOrder(s);
  bool showOthers = !alert || s.atHome;
  for (int o = 0; o < s.n && showOthers; o++) {
    int i = radarOrder[o];
    if (alert && i == s.aIdx) continue;                        // drawn below, from the alert's own track
    float x, y; int16_t sx, sy;
    radarPos(s.b[i], s.t0, x, y);
    if (!radarXY(x, y, range, sx, sy)) continue;
    float glow = radarGlow(sweep, x, y, 0.55f);
    uint16_t col = rgbScale(200, 245, 60, alert ? 0.45f : glow);
    float a = (s.b[i].trk + 180) * DEG_TO_RAD, L = s.b[i].gs / 180.0f * RADAR_R / range;   // 20 s of travel
    if (L > 14) L = 14;
    if (L < 4) L = 4;
    g.drawLine(sx, sy, sx + L * sinf(a), sy - L * cosf(a), rgbScale(200, 245, 60, glow * 0.55f));
    g.fillTriangle(sx, sy - 3, sx + 3, sy, sx, sy + 3, col); g.fillTriangle(sx, sy - 3, sx - 3, sy, sx, sy + 3, col);
    if (i == feat) { radarTarget(g, sx, sy, false); radarLabelNear(g, sx, sy, s.b[i].cs, yellow); }
  }
  // ---- the big label at the bottom
  if (alert) {
    float x, y, secs; int16_t sx = 0, sy = 0;
    radarAlertPos(s, x, y, secs);
    bool on = radarXY(x, y, range, sx, sy);
    char l1[24], l2[40];
    radarAlertLines(s, x, y, secs, l1, sizeof l1, l2, sizeof l2, "OVERHEAD");
    int16_t by = radarBlockY(on, sx, sy);
    radarCentred(g, &DV_SMALL, by, l1, rgb(255, 70, 60));
    radarText(g, RADAR_C - (int16_t)strlen(l2) * 3, by + 6, l2, white);
    if (on) {
      float a = (s.atrk + 180) * DEG_TO_RAD;
      g.drawLine(sx, sy, sx + 12 * sinf(a), sy - 12 * cosf(a), rgb(255, 120, 120));
      radarTarget(g, sx, sy, true);
    }
  } else if (feat >= 0) {
    const RadarBlip& b = s.b[feat];
    char alt[16], l2[40];
    formatAlt(showAlt(b.alt), alt, sizeof alt);
    snprintf(l2, sizeof l2, "%s%s  %.1f%s", alt, UN.alt, showDist(b.dist), UN.dist);
    float x, y; int16_t sx = 0, sy = 0;
    radarPos(b, s.t0, x, y);
    int16_t by = radarBlockY(radarXY(x, y, range, sx, sy), sx, sy);
    radarCentred(g, &DV_MID, by, b.cs, yellow);
    radarCentred(g, &DV_SMALL, by + 20, l2, white);
  } else {
    radarText(g, RADAR_C - 33, RADAR_C + 56, "No aircraft", ring);
  }
}

// ---------------------------------------------------------------------
//  TCAS Traffic: the cockpit traffic display. Symbols: hollow diamond = other traffic, filled diamond =
//  close, amber circle = close and low, red square = the overhead alert. +84 = 8,400 ft above the ground here.
// ---------------------------------------------------------------------
void radarDrawTcas(GFXcanvas16& g, const RadarSnap& s) {
  uint16_t white = rgb(240, 240, 240), grey = rgb(70, 74, 80), cyan = rgb(60, 220, 255), amber = rgb(255, 180, 0),
           red = rgb(255, 40, 40), green = rgb(40, 230, 90);
  g.fillScreen(0);
  bool alert = s.alert;
  float range = alert ? 6.0f : s.range;
  g.drawCircle(RADAR_C, RADAR_C, RADAR_R, grey);
  for (int b = 0; b < 360; b += 30) {                          // compass ticks on the outer ring
    float a = b * DEG_TO_RAD;
    g.drawLine(RADAR_C + (RADAR_R - 5) * sinf(a), RADAR_C - (RADAR_R - 5) * cosf(a), RADAR_C + RADAR_R * sinf(a), RADAR_C - RADAR_R * cosf(a), white);
  }
  for (int b = 0; b < 360; b += 30) {                          // 12 dots: the half-range ring
    float a = b * DEG_TO_RAD; int16_t r = RADAR_R / 2;
    g.fillCircle(RADAR_C + r * sinf(a), RADAR_C - r * cosf(a), 1, white);
  }
  radarNumBoxes = 0;
  radarText(g, RADAR_C - 2, 12, "N", cyan);
  { char t[16]; snprintf(t, sizeof t, "%ld", lroundf(showDist(range / 2)));
    radarText(g, RADAR_C + RADAR_R / 2 * 0.7071f + 3, RADAR_C - RADAR_R / 2 * 0.7071f - 10, t, cyan); }
  if (!alert) {
    radarText(g, 46, 40, "TFC", green);
    char t[16]; snprintf(t, sizeof t, "%ld%s", lroundf(showDist(range)), UN.dist); radarText(g, 194 - strlen(t) * 6, 40, t, cyan);
  }
  // own position: the white aircraft
  { int16_t c = RADAR_C;
    g.fillTriangle(c, c - 7, c - 2, c + 6, c + 2, c + 6, white);
    g.fillRect(c - 7, c - 1, 15, 3, white); g.fillRect(c - 3, c + 5, 7, 2, white); }
  radarClaim(RADAR_C - 8, RADAR_C - 8, 17, 16);
  int feat = alert ? -1 : radarFeatured(s);
  auto symbol = [&](int16_t sx, int16_t sy, int kind, long rel, int vs, uint16_t sel) {
    // kind 0 other, 1 close, 2 close+low, 3 alert
    if (kind == 3) g.fillRect(sx - 4, sy - 4, 9, 9, red);
    else if (kind == 2) g.fillCircle(sx, sy, 4, amber);
    else {
      uint16_t c = cyan;
      g.drawLine(sx, sy - 5, sx + 5, sy, c); g.drawLine(sx + 5, sy, sx, sy + 5, c);
      g.drawLine(sx, sy + 5, sx - 5, sy, c); g.drawLine(sx - 5, sy, sx, sy - 5, c);
      if (kind == 1) { g.fillTriangle(sx, sy - 4, sx + 4, sy, sx, sy + 4, c); g.fillTriangle(sx, sy - 4, sx - 4, sy, sx, sy + 4, c); }
    }
    uint16_t tc = kind == 3 ? red : kind == 2 ? amber : cyan;
    char t[8]; long h = rel / 100; if (h > 999) h = 999; if (h < 0) h = 0;
    snprintf(t, sizeof t, "+%02ld", h);
    radarText(g, sx - 8, sy - 15, t, tc);                       // above: it's above the ground here
    if (vs > CLIMB_FPM || vs < -CLIMB_FPM) {                    // climbing / descending arrow
      int16_t ax = sx + 8, d = vs > 0 ? -1 : 1;
      g.drawFastVLine(ax, sy - 4, 9, tc);
      g.drawLine(ax, sy + 4 * d, ax - 2, sy + 2 * d, tc); g.drawLine(ax, sy + 4 * d, ax + 2, sy + 2 * d, tc);
    }
    if (sel) g.drawRect(sx - 8, sy - 8, 17, 17, sel);
  };
  bool showOthers = !alert || s.atHome;
  for (int i = 0; i < s.n && showOthers; i++) {
    if (alert && i == s.aIdx) continue;
    float x, y; int16_t sx, sy;
    radarPos(s.b[i], s.t0, x, y);
    if (!radarXY(x, y, range, sx, sy)) continue;
    long rel = s.b[i].alt - s.ground;
    int kind = s.b[i].dist <= range * 0.25f && rel < 3000 ? 2 : s.b[i].dist <= range * 0.5f ? 1 : 0;
    symbol(sx, sy, kind, rel, s.b[i].vs, i == feat ? RADAR_SEL_YELLOW : 0);
  }
  if (alert) {
    float x, y, secs; int16_t sx, sy;
    radarAlertPos(s, x, y, secs);
    long rel = s.aAbove + (long)(s.aClimb * ((long)(millis() - s.aAt) / 60000.0f));
    bool on = radarXY(x, y, range, sx, sy);
    char l1[24], l2[40];
    radarAlertLines(s, x, y, secs, l1, sizeof l1, l2, sizeof l2, "TRAFFIC");
    int16_t by = radarBlockY(on, sx, sy);
    radarCentred(g, &DV_SMALL, by, l1, red);
    radarText(g, RADAR_C - (int16_t)strlen(l2) * 3, by + 6, l2, white);
    if (on) { symbol(sx, sy, 3, rel, s.aClimb, 0); if ((millis() / 300) & 1) g.drawRect(sx - 8, sy - 8, 17, 17, red); }
  } else if (feat >= 0) {
    const RadarBlip& b = s.b[feat];
    char l[40], alt[16];
    formatAlt(showAlt(b.alt), alt, sizeof alt);
    snprintf(l, sizeof l, "%s  %s%s", b.cs, alt, UN.alt);
    radarCentred(g, &DV_SMALL, RADAR_C + 86, l, white);
  }
}

void radarDrawAlert(GFXcanvas16& g, const RadarSnap& s, int style) {
  uint16_t bg, fg, dim;
  if (style == 2)      { bg = rgb(0, 0, 0);  fg = rgb(220, 235, 255); dim = rgb(45, 65, 95); }
  else if (style == 3) { bg = rgb(10, 5, 0); fg = rgb(255, 176, 40);  dim = rgb(95, 58, 12); }
  else                 { bg = rgb(0, 8, 0);  fg = rgb(70, 255, 110);  dim = rgb(20, 90, 35); }
  g.fillScreen(bg);
  float dt = (long)(millis() - s.aAt) / 1000.0f, secs = s.atca - dt;
  float x = s.ax + s.avx * dt, y = s.ay + s.avy * dt, dist = sqrtf(x * x + y * y);
  float above = s.aAbove + s.aClimb * dt / 60.0f; if (above < 0) above = 0;
  float elev = atan2f(above, dist * 6076.12f) * RAD_TO_DEG, bear = atan2f(x, y);
  g.drawCircle(RADAR_C, RADAR_C, RADAR_R, fg);
  for (int e = 30; e <= 60; e += 30) {
    int16_t r = RADAR_R * (90 - e) / 90;
    for (int t = 0; t < 360; t += 6) g.drawPixel(RADAR_C + r * sinf(t * DEG_TO_RAD), RADAR_C - r * cosf(t * DEG_TO_RAD), dim);
  }
  const char* L[] = { "N", "E", "S", "W" };
  for (int i = 0; i < 4; i++) {
    float a = i * 90 * DEG_TO_RAD;
    radarText(g, RADAR_C + (RADAR_R - 10) * sinf(a) - 3, RADAR_C - (RADAR_R - 10) * cosf(a) - 4, L[i], dim);
  }
  g.fillCircle(RADAR_C, RADAR_C, 2, dim);
  float r = RADAR_R * (90 - (elev < 0 ? 0 : elev)) / 90.0f;
  int16_t px = RADAR_C + r * sinf(bear), py = RADAR_C - r * cosf(bear);
  g.fillCircle(px, py, 5, fg);
  radarArrow(g, px, py, s.atrk, fg, 8);
  // words: callsign above the centre, countdown and where to look below
  char t[24];
  g.setTextSize(1);
  int16_t w = strlen(s.acs) * 6;
  radarText(g, RADAR_C - w / 2, RADAR_C - 44, s.acs, fg);
  if (secs > ALERT_NOW_S) snprintf(t, sizeof t, "%d:%02d", (int)secs / 60, (int)secs % 60);
  else snprintf(t, sizeof t, secs >= -ALERT_NOW_S ? "NOW" : "PASSING");
  g.setTextSize(2);
  w = strlen(t) * 12;
  radarText(g, RADAR_C - w / 2, RADAR_C + 22, t, fg);
  g.setTextSize(1);
  if (elev >= 70) snprintf(t, sizeof t, "LOOK UP");
  else snprintf(t, sizeof t, "LOOK %s %d", compass(fmodf(bear * RAD_TO_DEG + 360.0f, 360.0f)), (int)lroundf(elev));
  w = strlen(t) * 6 + (elev >= 70 ? 0 : 5);
  radarText(g, RADAR_C - w / 2, RADAR_C + 44, t, fg);
  if (elev < 70) g.drawCircle(RADAR_C - w / 2 + strlen(t) * 6 + 2, RADAR_C + 45, 1, fg);
}

void radarDraw(GFXcanvas16& g, const RadarSnap& s, int style, float sweep) {
  g.setFont(nullptr);                      // the small built-in font: crisp at this size
  g.setTextSize(1);
  g.setTextWrap(false);
  if (!s.active) { g.fillScreen(0); return; }
  if (style == 4)      { radarDrawTcas(g, s); return; }          // these two show the alert on the scope itself
  if (style == 1)      { radarDrawClassic(g, s, sweep); return; }
  if (s.alert) { radarDrawAlert(g, s, style); g.setTextSize(1); return; }
  if (style == 2)      radarDrawAtc(g, s);
  else if (style == 5) radarDrawBlocks(g, s);                     // v0.9.44
  else if (style == 6) radarDrawTracon(g, s);                     // v0.9.46
  else if (style == 3) radarDrawAmber(g, s, sweep);
  else                 radarDrawClassic(g, s, sweep);
}

// Darken the whole picture (the module's backlight is always on, so this is the only "brightness")
void radarDim(uint16_t* px, int n, int pct) {
  uint32_t k = pct * 256 / 100;
  for (int i = 0; i < n; i++) {
    uint16_t c = px[i];
    if (!c) continue;
    uint16_t r = (((c >> 11) & 31) * k) >> 8, g = (((c >> 5) & 63) * k) >> 8, b = ((c & 31) * k) >> 8;
    px[i] = (r << 11) | (g << 5) | b;
  }
}

// Send only the circle, fast: 16 bands of rows, each band one address window as wide as its widest row.
// Each row is copied from the canvas (PSRAM) to a small buffer in internal RAM and sent in one SPI burst;
// the SPI driver swaps the bytes to the order the screen wants.
const int RADAR_BANDS = 16;
void radarSendRound(uint16_t* buf) {
  static uint8_t bx[RADAR_BANDS];                        // left edge of each band
  static bool ready = false;
  static uint16_t line[RADAR_SIZE];                      // one row, internal RAM
  const int BH = RADAR_SIZE / RADAR_BANDS;
  if (!ready) {
    for (int k = 0; k < RADAR_BANDS; k++) {
      int a = RADAR_SIZE / 2;
      for (int y = k * BH; y < (k + 1) * BH; y++) {       // the widest row in the band decides
        float dy = y + 0.5f - RADAR_SIZE / 2.0f, h = sqrtf(RADAR_SIZE * RADAR_SIZE / 4.0f - dy * dy);
        int e = (int)floorf(RADAR_SIZE / 2.0f - h) - 1;
        if (e < a) a = e;
      }
      bx[k] = (uint8_t)constrain(a, 0, RADAR_SIZE / 2 - 1);
    }
    ready = true;
  }
  radarTft.startWrite();
  for (int k = 0; k < RADAR_BANDS; k++) {
    int a = bx[k], w = RADAR_SIZE - 2 * a, y0 = k * BH;
    radarTft.setAddrWindow(a, y0, w, BH);                // leaves the screen ready for pixel data
    for (int y = y0; y < y0 + BH; y++) {
      memcpy(line, buf + y * RADAR_SIZE + a, w * 2);
      radarSPI.writePixels(line, w * 2);
    }
  }
  radarTft.endWrite();
}

bool radarBegin() {
  if (radarStarted) return true;
  if (!radarCanvas) radarCanvas = new GFXcanvas16(RADAR_SIZE, RADAR_SIZE);   // ~115 KB, goes to PSRAM
  if (!radarCanvas || !radarCanvas->getBuffer()) { Serial.println("Radar: not enough memory"); return false; }
  radarSPI.begin(RADAR_SCK, -1, RADAR_MOSI, -1);
  radarTft.begin(RADAR_SPI_HZ);
  radarTft.setRotation(RADAR_ROTATION);
  radarTft.fillScreen(0);
  radarStarted = true;
  Serial.println("Radar: started");
  return true;
}

void radarTask(void*) {
  int lastStyle = -1;
  wdtWatchMe();
  for (;;) {
    wdtPet();
    crumb.radarStep = RS_IDLE;
    int style = radarStyle;
    if (style <= 0) {                      // Off / not connected
      if (radarStarted && lastStyle != 0) radarTft.fillScreen(0);   // blank it once
      lastStyle = 0;
      vTaskDelay(pdMS_TO_TICKS(500));
      continue;
    }
    if (!radarBegin()) { vTaskDelay(pdMS_TO_TICKS(5000)); continue; }
    lastStyle = style;
    unsigned long t = millis();
    xSemaphoreTake(radarLock, portMAX_DELAY);
    memcpy(&radarLocal, &radarShared, sizeof radarLocal);
    xSemaphoreGive(radarLock);
    float sweep = (t % RADAR_SWEEP_MS) * 360.0f / RADAR_SWEEP_MS;
    crumb.radarStep = RS_DRAW;
    radarDraw(*radarCanvas, radarLocal, style, sweep);
    if (radarBright < 100) radarDim(radarCanvas->getBuffer(), RADAR_SIZE * RADAR_SIZE, radarBright);
    unsigned long drawn = millis();
    radarSendRound(radarCanvas->getBuffer());
    unsigned long took = millis() - t;
    radarDrawMs = (radarDrawMs * 7 + (drawn - t)) / 8;          // smoothed, for the Diagnostic Log
    radarSendMs = (radarSendMs * 7 + (millis() - drawn)) / 8;
    static unsigned long lastStart = 0;                // measured time from one frame to the next
    if (lastStart) radarPeriodMs = radarPeriodMs ? (radarPeriodMs * 7 + (t - lastStart)) / 8 : (t - lastStart);
    lastStart = t;
    radarFrames++;
    vTaskDelay(pdMS_TO_TICKS(took + RADAR_GAP_MS < RADAR_FRAME_MS ? RADAR_FRAME_MS - took : RADAR_GAP_MS));
  }
}

