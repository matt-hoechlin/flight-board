// Flight Board: Knob and buttons.
// One tab of the flight_board sketch. Arduino joins the tabs in name order, after flight_board.ino.
// =====================================================================
//  Buttons
// =====================================================================
void IRAM_ATTR onButton(void* arg) {
  int i = (int)(intptr_t)arg;
  uint32_t now = millis();
  portENTER_CRITICAL_ISR(&btnMux);
  if (now - btnLastMs[i] > 60 && btnArmed[i]) {
    btnCount[i]++;
    btnLastMs[i] = now;
    if (BTN_NEEDS_RELEASE[i]) btnArmed[i] = false;
  }
  portEXIT_CRITICAL_ISR(&btnMux);
}


int takePresses(int i) {
  portENTER_CRITICAL(&btnMux);
  int n = btnCount[i];
  btnCount[i] = 0;
  if (i == B_MENU || i == B_EXIT || i == B_PRESS) { n = btr[i].presses; btr[i].presses = 0; }
  portEXIT_CRITICAL(&btnMux);
  return n;
}

// v0.9.25: MENU, EXIT, knob press. Reads the pin straight from the GPIO register (the Arduino helpers aren't safe in
// an interrupt while flash is being written, e.g. saving settings or a firmware update).
static inline bool IRAM_ATTR btnPinLow(int i) { return !((REG_READ(GPIO_IN_REG) >> BTN_PINS[i]) & 1u); }
static inline void IRAM_ATTR btnFinish(volatile BtnTrack& b) {
  uint32_t d = b.upAt - b.downAt;
  if (b.nDone < 4) { b.done[b.nDone].ms = (uint16_t)(d > 65535 ? 65535 : d); b.done[b.nDone].holdUsed = b.holdUsed; b.nDone++; }
  b.down = false;
}
void IRAM_ATTR onBtnEdge(void* arg) {
  int i = (int)(intptr_t)arg;
  uint32_t now = millis();
  bool low = btnPinLow(i);
  portENTER_CRITICAL_ISR(&btnMux);
  volatile BtnTrack& b = btr[i];
  if (low) {
    if (b.down && b.upSeen && now - b.upAt >= RELEASE_MS) btnFinish(b);   // the last press ended (loop hadn't looked yet)
    if (!b.down && now - b.upAt >= RELEASE_MS) {                          // a real new press (not a bounce)
      b.down = true; b.upSeen = false; b.holdUsed = false; b.downAt = now; b.presses++;
    }
  } else {
    b.upAt = now;                                                         // every edge up, bounces too
    if (b.down) b.upSeen = true;
  }
  portEXIT_CRITICAL_ISR(&btnMux);
}
// Loop: finish presses that have been let go (up, steadily, for RELEASE_MS)
void btnService(uint32_t now) {
  for (int i : { B_MENU, B_EXIT, B_PRESS }) {
    bool low = btnPinLow(i);
    portENTER_CRITICAL(&btnMux);
    volatile BtnTrack& b = btr[i];
    if (b.down && b.upSeen && !low && now - b.upAt >= RELEASE_MS) btnFinish(b);
    portEXIT_CRITICAL(&btnMux);
  }
}
// How long the button has been held down right now (0 if it isn't)
uint32_t btnHeldMs(int i, uint32_t now) {
  bool low = btnPinLow(i);
  portENTER_CRITICAL(&btnMux);
  uint32_t ms = (btr[i].down && low && !btr[i].holdUsed) ? now - btr[i].downAt : 0;
  portEXIT_CRITICAL(&btnMux);
  return ms;
}
// A hold acted on the current press: letting go must do nothing else
void btnHoldUsed(int i) { portENTER_CRITICAL(&btnMux); if (btr[i].down) btr[i].holdUsed = true; portEXIT_CRITICAL(&btnMux); }
// Finished taps (presses no hold acted on, shorter than holdMs; 0 = any length). Takes them all.
int takeTaps(int i, uint32_t holdMs) {
  int n = 0;
  portENTER_CRITICAL(&btnMux);
  volatile BtnTrack& b = btr[i];
  for (int k = 0; k < b.nDone; k++) if (!b.done[k].holdUsed && (!holdMs || b.done[k].ms < holdMs)) n++;
  b.nDone = 0;
  portEXIT_CRITICAL(&btnMux);
  return n;
}

