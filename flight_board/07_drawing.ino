// Flight Board: Drawing helpers for the e-ink screen.
// One tab of the flight_board sketch. Arduino joins the tabs in name order, after flight_board.ino.
// =====================================================================
//  Drawing helpers
// =====================================================================
int16_t textWidth(const char* s) {
  int16_t bx, by; uint16_t bw, bh;
  display.getTextBounds(s, 0, 100, &bx, &by, &bw, &bh);
  return bw + bx;
}

void printRight(const char* s, int16_t rightX, int16_t y) {
  int16_t bx, by; uint16_t bw, bh;
  display.getTextBounds(s, 0, y, &bx, &by, &bw, &bh);
  display.setCursor(rightX - bw - bx, y);
  display.print(s);
}

// Centred between x0 and x1; wraps onto a second line at a space if it's too wide
void printCenteredIn(const char* s, int16_t x0, int16_t x1, int16_t y, int16_t lineH) {
  int16_t bx, by; uint16_t bw, bh;
  display.getTextBounds(s, 0, y, &bx, &by, &bw, &bh);
  if ((int16_t)bw <= x1 - x0) { display.setCursor(x0 + (x1 - x0 - (int16_t)bw) / 2 - bx, y); display.print(s); return; }
  char a[100]; copyStr(a, sizeof a, s);
  size_t n = strlen(a);
  int split = -1;                                // the space closest to the middle
  for (size_t i = 0; i < n; i++) if (a[i] == ' ' && (split < 0 || abs((int)i - (int)n / 2) < abs(split - (int)n / 2))) split = i;
  if (split < 0) { display.setCursor(x0, y); display.print(s); return; }
  a[split] = 0;
  const char* lines[2] = { a, a + split + 1 };
  for (int k = 0; k < 2; k++) {
    display.getTextBounds(lines[k], 0, y, &bx, &by, &bw, &bh);
    display.setCursor(x0 + (x1 - x0 - (int16_t)bw) / 2 - bx, y - lineH / 2 + k * lineH);
    display.print(lines[k]);
  }
}

void printCentered(const char* s, int16_t y) {
  int16_t bx, by; uint16_t bw, bh;
  display.getTextBounds(s, 0, y, &bx, &by, &bw, &bh);
  display.setCursor((display.width() - bw) / 2 - bx, y);
  display.print(s);
}

// Small plane pointing right; y is the text baseline
void drawPlaneIcon(int16_t x, int16_t y) {
  int16_t cy = y - 6;
  display.fillRect(x, cy - 1, 14, 3, GxEPD_BLACK);
  display.fillTriangle(x + 5, cy, x + 9, cy, x + 4, cy - 7, GxEPD_BLACK);
  display.fillTriangle(x + 5, cy, x + 9, cy, x + 4, cy + 7, GxEPD_BLACK);
  display.fillRect(x, cy - 3, 2, 7, GxEPD_BLACK);
}

void drawArrow(int16_t x, int16_t y, bool up) {
  if (up) display.fillTriangle(x, y, x + 10, y, x + 5, y - 11, GxEPD_BLACK);
  else    display.fillTriangle(x, y - 11, x + 10, y - 11, x + 5, y, GxEPD_BLACK);
}

void formatAlt(long a, char* buf, size_t len) {
  if (a >= 1000) snprintf(buf, len, "%ld,%03ld", a / 1000, a % 1000);
  else snprintf(buf, len, "%ld", a);
}

// The same numbers in the chosen units
long  showAlt(long ft)   { long v = lround(ft * UN.altK); return unitsMode == U_METRIC ? (v + 5) / 10 * 10 : v; }   // metres to the nearest 10
int   showSpd(float kt)  { return (int)lround(kt * UN.spdK); }
float showDist(float nm) { return nm * UN.distK; }
int   showRadius(float nm) { return (int)lroundf(nm * UN.distK); }

// Saved scan radius in NM: the exact one if there is one, else the older whole-number setting
float readRadius(Preferences& pr, const char* exactKey, const char* oldKey, int def) {
  if (pr.isKey(exactKey)) return pr.getFloat(exactKey, def);
  return pr.getInt(oldKey, def);
}

// Right-pointing arrow, centred on the capital letters of the current font; y = text baseline
int16_t drawRouteArrow(int16_t x, int16_t y, int16_t length, int16_t stroke, int16_t head) {
  int16_t bx, by; uint16_t bw, bh;
  display.getTextBounds("H", 0, y, &bx, &by, &bw, &bh);
  int16_t cy = y - bh / 2 - 1;
  display.fillRect(x, cy - stroke / 2, length - head, stroke, GxEPD_BLACK);
  display.fillTriangle(x + length - head - 1, cy - head, x + length, cy, x + length - head - 1, cy + head, GxEPD_BLACK);
  return x + length;
}

// "OAK -> RNO" with a drawn arrow, in the current font. big = detail page size.
void drawRoute(int16_t x, int16_t y, const char* from, const char* to, bool big) {
  bool shortCodes = strlen(from) <= 3 && strlen(to) <= 3;
  int16_t gap = big ? 10 : (shortCodes ? 5 : 3);
  display.setCursor(x, y);
  display.print(from);
  int16_t ax = display.getCursorX() + gap;
  int16_t end = big ? drawRouteArrow(ax, y, 28, 5, 9) : drawRouteArrow(ax, y, shortCodes ? 15 : 13, 3, 5);
  display.setCursor(end + gap, y);
  display.print(to);
}

const char* statusWord(int8_t s) {
  switch (s) {
    case ST_ARRIVING:   return "ARRIVING";
    case ST_DEPARTING:  return "DEPARTING";
    case ST_OVERFLIGHT: return "OVERFLIGHT";
    case ST_OVERHEAD:   return "OVERHEAD";
    default:            return "";
  }
}

// v0.9.32: overhead alert status, white on the black header: a small plane (seen from below) over a ground
// line; crossed out when the alert is off. cx, cy = centre; fits inside the 46 px header bar.
const int16_t ALERT_ICON_W = 30;
// v0.9.39: a properly shaped airliner seen from below (swept wings, engines, tailplane) over the ground line
static void tri2(float x0, float y0, float x1, float y1, float x2, float y2, uint16_t c) {
  display.fillTriangle(lroundf(x0), lroundf(y0), lroundf(x1), lroundf(y1), lroundf(x2), lroundf(y2), c);
}
static void quad2(float ax, float ay, float bx, float by, float cx, float cy, float dx, float dy, uint16_t c) {
  tri2(ax, ay, bx, by, cx, cy, c); tri2(ax, ay, cx, cy, dx, dy, c);
}
void drawAlertIcon(int16_t cx, int16_t cy, bool on) {
  const uint16_t c = GxEPD_WHITE;
  const float L = 25, S = 13, y0 = cy - 16, x = cx + .5f;      // length, half span, nose
  display.fillRect(cx - 1, lroundf(y0 + L * .10f), 4, lroundf(L * .80f), c);                     // fuselage
  tri2(cx - 1.5f, y0 + L * .11f, cx + 2.5f, y0 + L * .11f, x, y0, c);                           // nose
  tri2(cx - 1.5f, y0 + L * .88f, cx + 2.5f, y0 + L * .88f, x, y0 + L, c);                       // tail cone
  for (int sg = -1; sg <= 1; sg += 2) {
    quad2(x, y0 + L * .36f, x + sg * S, y0 + L * .60f, x + sg * S, y0 + L * .68f, x, y0 + L * .56f, c);          // swept wing
    display.fillRect(lroundf(x + sg * S * .42f - 1), lroundf(y0 + L * .50f), 3, 5, c);                         // engine
    quad2(x, y0 + L * .80f, x + sg * S * .38f, y0 + L * .94f, x + sg * S * .38f, y0 + L * .99f, x, y0 + L * .92f, c);   // tailplane
  }
  display.fillRect(cx - 12, cy + 13, 25, 2, c);                 // the ground
  if (!on) {                                                    // crossed out: black edge, white bar
    for (int o = -3; o <= 3; o++) display.drawLine(cx - 15 + o, cy + 16, cx + 15 + o, cy - 16, GxEPD_BLACK);
    for (int o = -1; o <= 1; o++) display.drawLine(cx - 15 + o, cy + 16, cx + 15 + o, cy - 16, c);
  }
}

// Small house, white on the black header; x = left edge, y = text baseline
const int16_t HOUSE_W = 22;
void drawHouseIcon(int16_t x, int16_t y) {
  display.fillTriangle(x, y - 11, x + 21, y - 11, x + 10, y - 22, GxEPD_WHITE);   // roof
  display.fillRect(x + 15, y - 21, 3, 6, GxEPD_WHITE);                             // chimney
  display.fillRect(x + 3, y - 11, 16, 11, GxEPD_WHITE);                            // walls
  display.fillRect(x + 9, y - 7, 5, 7, GxEPD_BLACK);                               // door
}

// Small airliner seen from above, nose up (airport screens)
void drawPlaneHeaderIcon(int16_t x, int16_t y) {
  int16_t cx = x + 11, cy = y - 12;
  display.fillRect(cx - 2, cy - 11, 4, 23, GxEPD_WHITE);                                   // fuselage, down into the tail
  display.fillTriangle(cx - 2, cy - 11, cx + 1, cy - 11, cx, cy - 13, GxEPD_WHITE);        // nose
  display.fillTriangle(cx - 11, cy + 1, cx - 2, cy - 4, cx - 2, cy, GxEPD_WHITE);          // left wing
  display.fillTriangle(cx - 11, cy + 1, cx - 2, cy, cx - 11, cy + 3, GxEPD_WHITE);
  display.fillTriangle(cx + 11, cy + 1, cx + 1, cy - 4, cx + 1, cy, GxEPD_WHITE);          // right wing
  display.fillTriangle(cx + 11, cy + 1, cx + 1, cy, cx + 11, cy + 3, GxEPD_WHITE);
  display.fillRect(cx - 5, cy + 10, 11, 2, GxEPD_WHITE);                                   // tailplane
  display.fillTriangle(cx - 5, cy + 10, cx - 2, cy + 7, cx - 2, cy + 10, GxEPD_WHITE);
  display.fillTriangle(cx + 5, cy + 10, cx + 1, cy + 7, cx + 1, cy + 10, GxEPD_WHITE);
}

// Map pin (other places)
void drawPinIcon(int16_t x, int16_t y) {
  int16_t cx = x + 11;
  display.fillCircle(cx, y - 15, 9, GxEPD_WHITE);
  display.fillTriangle(cx - 7, y - 10, cx + 7, y - 10, cx, y, GxEPD_WHITE);
  display.fillCircle(cx, y - 15, 3, GxEPD_BLACK);
}

bool isHomePlace(const Place& p) { return strcmp(p.gkey, "hg") == 0; }

HeaderIcon placeIcon(const Place& p) { return isHomePlace(p) ? ICON_HOUSE : p.isAirport ? ICON_PLANE : ICON_PIN; }

// Black title bar: [icon] Name  small text ................ right text
void drawHeader(const char* left, const char* small, const char* right, int8_t icon) {
  const int16_t W = display.width();
  display.fillRect(0, 0, W, 46, GxEPD_BLACK);
  display.setTextColor(GxEPD_WHITE);
  int16_t x = 14;
  if (icon == ICON_HOUSE)      drawHouseIcon(x, 33);
  else if (icon == ICON_PLANE) drawPlaneHeaderIcon(x, 33);
  else if (icon == ICON_PIN)   drawPinIcon(x, 33);
  if (icon != ICON_NONE) x += HOUSE_W + 10;
  display.setFont(&DV_BIG);
  display.setCursor(x, 33);
  display.print(left);
  int16_t after = display.getCursorX();
  if (small && small[0]) {
    display.setFont(&DV_MID);
    display.setCursor(after + 10, 32);
    display.print(small);
  }
  display.setFont(&DV_MID);
  if (right && right[0]) printRight(right, W - 14, 32);
  display.setTextColor(GxEPD_BLACK);
}

