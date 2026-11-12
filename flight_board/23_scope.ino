// Flight Board: the Flight Board Scope (the round radar screen that sits beside the board).
// One tab of the flight_board sketch. Arduino joins the tabs in name order, after flight_board.ino.
//
// v0.9.48: pairing from the Settings page (no Wi-Fi to type on the Scope), and its settings live here:
//   - Pair a Scope: you type the 4-digit code the Scope shows. For 60 s the board calls out over ESP-NOW;
//     the Scope answers, and the board hands it the Wi-Fi details, locked with that code (see scope_pair.h).
//   - Once paired, the Scope asks /radar.json?sc=<its id>&... every 1.5 s: that tells the board it's there
//     (frame rate, signal, version), and the answer carries its settings (style, brightness, turn) back.
//   - Its own buttons (dim, turn, swipe for style) report back through /scope?do=set, so both stay in step.
// (esp_now.h, esp_wifi.h and scope_pair.h are included at the top of flight_board.ino)

enum ScpState : uint8_t { SCP_IDLE, SCP_WAIT, SCP_TALK, SCP_OK, SCP_WRONG, SCP_TIMEOUT, SCP_FAIL };
const char* const SCP_NAMES[] = { "idle", "wait", "talk", "ok", "wrong", "timeout", "fail" };
volatile uint8_t scpState = SCP_IDLE;
volatile bool scpStop = false, scpRunning = false;
uint32_t scpEnd = 0, scpTalkAt = 0;
char scpCode[5] = "";
SpKey scpKey;
bool scpKeyOk = false;
uint8_t scpPeer[6], scpPeerPub[SP_PUB], scpK[32], scpKeysMsg[SP_MAX];
int scpKeysLen = 0;

// The paired Scope (kept in its own Preferences space, "scope") and what it last said
bool scLoaded = false;
char scId[13] = "";                       // its radio address, 12 hex digits ("" = none paired)
uint8_t scSty = 1, scBri = 0, scRot = 0;  // Classic Sweep, full brightness, upright
uint32_t scFind = 0;                      // goes up for each "Find my Scope"
uint32_t scSeenMs = 0;
int scFps = 0, scRssi = 0;
char scIp[16] = "", scVer[10] = "";
char scForget[13] = "";                   // told to forget: the next time it asks, it's told so
uint32_t scForgetMs = 0;

void scLoad() {
  if (scLoaded) return;
  Preferences pr; pr.begin("scope", true);
  copyStr(scId, sizeof scId, pr.getString("scid", "").c_str());
  scSty = constrain(pr.getUChar("scsty", 1), 0, 2); scBri = constrain(pr.getUChar("scbri", 0), 0, 3); scRot = pr.getUChar("scrot", 0) & 3;
  pr.end(); scLoaded = true;
}
void scSave() {
  Preferences pr; pr.begin("scope", false);
  pr.putString("scid", scId); pr.putUChar("scsty", scSty); pr.putUChar("scbri", scBri); pr.putUChar("scrot", scRot);
  pr.end();
}

// ---- pairing: one message in; fills the answer to send straight back (returns its length, 0 = none)
void scpWipe() { memset(scpK, 0, sizeof scpK); memset(scpKeysMsg, 0, sizeof scpKeysMsg); scpKeysLen = 0; }
int scpOnMsg(const uint8_t* mac, const uint8_t* d, int n, uint8_t* out) {
  int t = spType(d, n);
  if (t == SP_HELLO && n == SP_HDR + SP_PUB && (scpState == SCP_WAIT || scpState == SCP_TALK) && scpKeyOk) {
    if (scpState == SCP_TALK && !memcmp(mac, scpPeer, 6) && !memcmp(d + SP_HDR, scpPeerPub, SP_PUB)) {   // asked again: same answer
      memcpy(out, scpKeysMsg, scpKeysLen); return scpKeysLen;
    }
    uint8_t z[32];
    if (!spShared(scpKey, d + SP_HDR, z)) return 0;
    spDerive(z, scpCode, d + SP_HDR, scpKey.pub, scpK);
    memset(z, 0, sizeof z);
    SpCreds c; memset(&c, 0, sizeof c);
    copyStr(c.ssid, sizeof c.ssid, WiFi.SSID().c_str());
    copyStr(c.pass, sizeof c.pass, WiFi.psk().c_str());
    serialNumber(c.id, sizeof c.id);
    copyStr(c.host, sizeof c.host, hostName.c_str());
    scpKeysLen = spMsgKeys(scpKeysMsg, scpK, scpKey.pub, c);
    memset(&c, 0, sizeof c);
    if (!scpKeysLen) return 0;
    memcpy(scpPeer, mac, 6); memcpy(scpPeerPub, d + SP_HDR, SP_PUB);
    scpState = SCP_TALK; scpTalkAt = millis();
    memcpy(out, scpKeysMsg, scpKeysLen); return scpKeysLen;
  }
  if (scpState == SCP_TALK && !memcmp(mac, scpPeer, 6)) {
    if (t == SP_DONE && spCheckDone(d, n, scpK)) {                 // it unlocked them: paired
      scLoad();
      snprintf(scId, sizeof scId, "%02X%02X%02X%02X%02X%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
      scForget[0] = 0; scSeenMs = 0; scSave();
      scpState = SCP_OK; scpWipe();
      Serial.printf("Scope: paired with %s\n", scId);
    } else if (t == SP_NOPE) {                                       // the code didn't match: stop (one try per Pair)
      scpState = SCP_WRONG; scpWipe();
      Serial.println("Scope: the code didn't match");
    }
  }
  return 0;
}
// Time passing: no proof within 3 s, listen again; the 60 s are up, stop
void scpTick(uint32_t now) {
  if (scpState == SCP_TALK && now - scpTalkAt > 3000) { scpState = SCP_WAIT; scpWipe(); }
  if ((scpState == SCP_WAIT || scpState == SCP_TALK) && (int32_t)(now - scpEnd) > 0) { scpState = SCP_TIMEOUT; scpWipe(); }
}
// Ready to pair (the radio part runs in scpTask). The board's one-off key is made here.
bool scpBegin(const char* code) {
  if (scpState == SCP_WAIT || scpState == SCP_TALK) return false;
  if (scpKeyOk) spKeyFree(scpKey);
  scpKeyOk = spKeyNew(scpKey);
  copyStr(scpCode, sizeof scpCode, code);
  scpWipe(); scpStop = false;
  scpEnd = millis() + 60000; scpState = scpKeyOk ? SCP_WAIT : SCP_FAIL;
  return scpKeyOk;
}

struct ScpRx { uint8_t mac[6]; uint8_t n; uint8_t d[SP_MAX]; };
QueueHandle_t scpQ = nullptr;
void scpRecv(const esp_now_recv_info_t* info, const uint8_t* d, int n) {
  if (!scpQ || n > SP_MAX || !spType(d, n)) return;
  ScpRx r; memcpy(r.mac, info->src_addr, 6); r.n = (uint8_t)n; memcpy(r.d, d, n);
  xQueueSend(scpQ, &r, 0);
}
bool scpAddPeer(const uint8_t* mac) {
  if (esp_now_is_peer_exist(mac)) return true;
  esp_now_peer_info_t p; memset(&p, 0, sizeof p);
  memcpy(p.peer_addr, mac, 6); p.channel = 0; p.ifidx = WIFI_IF_STA; p.encrypt = false;   // 0 = the channel the board's Wi-Fi is on
  return esp_now_add_peer(&p) == ESP_OK;
}
void scpTask(void*) {
  static const uint8_t BC[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
  scpRunning = true;
  wifi_ps_type_t ps = WIFI_PS_MIN_MODEM;
  esp_wifi_get_ps(&ps);
  esp_wifi_set_ps(WIFI_PS_NONE);                                   // listen all the time while pairing
  bool radio = esp_now_init() == ESP_OK;
  if (radio) { esp_now_register_recv_cb(scpRecv); radio = scpAddPeer(BC); }
  if (!radio) { scpState = SCP_FAIL; Serial.println("Scope: ESP-NOW didn't start"); }
  static uint8_t out[SP_MAX];
  uint32_t lastB = 0, lastK = 0;
  while (scpState == SCP_WAIT || scpState == SCP_TALK) {
    if (scpStop) { scpState = SCP_IDLE; break; }
    uint32_t now = millis();
    if (now - lastB >= 100) { lastB = now; int n = spMsgBeacon(out); esp_now_send(BC, out, n); }
    if (scpState == SCP_TALK && now - lastK >= 300) { lastK = now; esp_now_send(scpPeer, scpKeysMsg, scpKeysLen); }   // again, in case it was lost
    static ScpRx r;
    if (xQueueReceive(scpQ, &r, pdMS_TO_TICKS(20)) == pdTRUE) {
      int n = scpOnMsg(r.mac, r.d, r.n, out);
      if (n && scpAddPeer(r.mac)) { esp_now_send(r.mac, out, n); lastK = millis(); }
    }
    scpTick(millis());
  }
  scpWipe();
  if (radio) { esp_now_unregister_recv_cb(); esp_now_deinit(); }
  esp_wifi_set_ps(ps);
  if (scpKeyOk) { spKeyFree(scpKey); scpKeyOk = false; }
  scpRunning = false;
  vTaskDelete(nullptr);
}
bool scpStart(const char* code) {
  if (!scpQ) scpQ = xQueueCreate(4, sizeof(ScpRx));
  if (scpRunning || !scpQ) return false;                           // the last try is still finishing
  if (!scpBegin(code)) return false;
  scpRunning = true;
  xTaskCreatePinnedToCore(scpTask, "scope", 8192, nullptr, 1, nullptr, 0);
  Serial.println("Scope: pairing for 60 s");
  return true;
}

// ---- /scope: the card's status, and its buttons (do=pair|cancel|set|find|forget)
String scopeStatusJson() {
  scLoad();
  uint32_t now = millis();
  bool on = scId[0] && scSeenMs && now - scSeenMs < 6000;
  long left = (scpState == SCP_WAIT || scpState == SCP_TALK) ? ((long)(scpEnd - now) + 999) / 1000 : 0;
  char b[300];
  snprintf(b, sizeof b, "{\"p\":%d,\"id\":\"%s\",\"on\":%d,\"ago\":%ld,\"fps\":%d,\"rs\":%d,\"ip\":\"%s\",\"v\":\"%s\",\"sty\":%d,\"bri\":%d,\"rot\":%d,\"ps\":\"%s\",\"left\":%ld}",
           scId[0] ? 1 : 0, scId, on ? 1 : 0, scSeenMs ? (long)((now - scSeenMs) / 1000) : -1L, scFps, scRssi, scIp, scVer,
           scSty, scBri, scRot, SCP_NAMES[scpState], left < 0 ? 0L : left);
  return String(b);
}
void handleScope() {
  String d = server.arg("do");
  if (d.length() ? !fromOwnPage(true) : !hostOk()) { sendBlocked(); return; }
  scLoad();
  if (d == "pair") {
    String c = server.arg("code");
    bool ok = c.length() == 4;
    for (unsigned i = 0; i < c.length(); i++) if (c[i] < '0' || c[i] > '9') ok = false;
    if (ok) scpStart(c.c_str());
  } else if (d == "cancel") scpStop = true;
  else if (d == "find") scFind++;
  else if (d == "forget") {
    if (scId[0]) { copyStr(scForget, sizeof scForget, scId); scForgetMs = millis(); }
    scId[0] = 0; scSeenMs = 0; scSave();
  } else if (d == "set") {
    String sc = server.arg("sc"); sc.toUpperCase();
    if (!sc.length() || sc == scId) {                               // the card, or the Scope's own buttons
      if (server.hasArg("sty")) scSty = constrain((int)server.arg("sty").toInt(), 0, 2);
      if (server.hasArg("bri")) scBri = constrain((int)server.arg("bri").toInt(), 0, 3);
      if (server.hasArg("rot")) scRot = (int)server.arg("rot").toInt() & 3;
      scSave();
    }
  }
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", scopeStatusJson());
}

// ---- the part of /radar.json for the Scope that's asking (?sc=<its id>&v=..&fps=..&rs=..)
void scopeRadarPart(String& o) {
  if (!server.hasArg("sc")) return;
  scLoad();
  String id = server.arg("sc"); id.toUpperCase();
  if (scForget[0] && id == scForget) {                             // forgotten here: it goes back to "Pair me"
    o += ",\"sc\":{\"forget\":1}";
    if (millis() - scForgetMs > 10000) scForget[0] = 0;            // told it for a while (in case an answer was lost)
    return;
  }
  if (!scId[0] || id != scId) { o += ",\"sc\":{\"paired\":0}"; return; }
  scSeenMs = millis(); if (!scSeenMs) scSeenMs = 1;
  scFps = server.arg("fps").toInt(); scRssi = server.arg("rs").toInt();
  copyStr(scVer, sizeof scVer, server.arg("v").c_str());
  copyStr(scIp, sizeof scIp, IPAddress(server.client().remoteIP()).toString().c_str());
  char b[96];
  snprintf(b, sizeof b, ",\"sc\":{\"sty\":%d,\"bri\":%d,\"rot\":%d,\"find\":%lu}", scSty, scBri, scRot, (unsigned long)scFind);
  o += b;
}

// ---- the Settings page card
void scopeCard(String& b) {
  scLoad();
  b += F("<style>#c-scope .rst{display:grid;grid-template-columns:auto 1fr;gap:4px 14px;margin:0 0 6px;font-size:15px}"
         "#c-scope .rst>span{color:#7d8794;font:12px/1.7 GM,ui-monospace,monospace;letter-spacing:.08em;text-transform:uppercase}"
         ".rok{color:#3d7a00;font-weight:700}html.dark .rok{color:#c8ff3d}.roff{color:#b0571c;font-weight:700}html.dark .roff{color:#ffb03c}"
         "#c-scope .rbtn{display:grid;grid-template-columns:1fr 1fr;gap:10px;margin-top:14px}#c-scope .rbtn button{margin:0}"
         "#c-scope button.go2{width:100%;margin:12px 0 0;padding:12px;border:0;border-radius:3px;background:#c8ff3d;color:#0a0c0f;"
         "font:700 15px/1 IS,system-ui,sans-serif;letter-spacing:.12em;text-transform:uppercase}#c-scope button.go2:disabled{opacity:.45}"
         "#c-scope input.sccode{font:700 30px/1.2 GM,ui-monospace,monospace;letter-spacing:.6em;text-align:center;padding:10px 0 10px .6em}"
         "#c-scope .wait{display:flex;align-items:center;gap:8px;margin-top:10px;font:13px/1.4 GM,ui-monospace,monospace;letter-spacing:.04em}"
         "#c-scope .wait b{width:10px;height:10px;border-radius:50%;background:#c8ff3d;box-shadow:0 0 8px #c8ff3d;animation:scb 1s infinite}"
         "@keyframes scb{50%{opacity:.3}}#c-scope .steps{margin:8px 0 0;padding-left:20px;font-size:15px;line-height:1.55}"
         "#c-scope .scmsg{color:#b0571c;font-weight:700}html.dark #c-scope .scmsg{color:#ffb03c}#c-scope .scmsg:empty{display:none}</style>"
         "<div id=c-scope class=card><h2><span class=ic><svg class=i viewBox='0 0 24 24'><circle cx='12' cy='13' r='7.5'/><circle cx='12' cy='13' r='3'/>"
         "<path d='M8.5 3.5a7 7 0 0 1 7 0'/><path d='M10.2 6a3.6 3.6 0 0 1 3.6 0'/></svg></span>Flight Board Scope <span class=hint>(BETA)</span>"
         "<span class=remnow id=scs>");
  b += scId[0] ? "Paired" : "None paired";
  b += F("</span></h2>"
         "");
  b += scId[0] ? "<div id=scN style=display:none>" : "<div id=scN>";      // none paired
  b += F("<p class=hint style=\"margin:0\">A Flight Board Scope shows this board&rsquo;s radar on your desk, over WiFi. Tap a plane on it to open it here.</p>"
         "<ol class=steps><li>Plug in the Scope. It shows <b>Pair me</b> and a 4-digit code.</li><li>Tap <b>Pair a Scope</b> and type that code.</li></ol>"
         "<button type=button class=go2 onclick=scOpen()>Pair a Scope</button>"
         "<p class=hint style=\"margin-bottom:0\">The board sends it your WiFi. Nothing to type on the Scope.</p></div>"
         // pairing
         "<div id=scP style=display:none><label style=\"margin-top:0\">Code on the Scope&rsquo;s screen</label>"
         "<input id=scc class=sccode inputmode=numeric maxlength=4 autocomplete=off placeholder=\"&middot;&middot;&middot;&middot;\" "
         "onkeydown=\"if(event.key=='Enter'){event.preventDefault();scPair()}\">"
         "<button type=button class=go2 id=scpb onclick=scPair()>Pair</button>"
         "<div class=wait id=scw style=display:none><b></b><span id=scwt></span></div>"
         "<p class=\"hint scmsg\" id=scm></p>"
         "<button type=button class=sec style=\"margin-top:12px\" onclick=scCancel()>Cancel</button></div>"
         "");
  b += scId[0] ? "<div id=scY>" : "<div id=scY style=display:none>";      // paired
  b += F("<div class=rst><span>Status</span><div id=scst></div><span>Scope</span><div id=scid></div></div>"
         "<label>Style</label>");
  { const char* t[] = { "TRACON", "Classic", "Amber" }; b += segButtons("scsty", scSty, t, 3, nullptr, "scSty"); }
  b += F("<label>Brightness</label>");
  { const char* t[] = { "100%", "50%", "25%", "6%" }; b += segButtons("scbri", scBri, t, 4, nullptr, "scBri"); }
  b += F("<label>Screen turn</label>");
  { const char* t[] = { "0&deg;", "90&deg;", "180&deg;", "270&deg;" }; b += segButtons("scrot", scRot, t, 4, nullptr, "scRot"); }
  b += F("<p class=hint>The Scope&rsquo;s top button turns it too, and its bottom button dims it.</p>"
         "<div class=rbtn><button type=button class=sec onclick=\"scDo('find')\">Find my Scope</button>"
         "<button type=button class=\"sec danger\" onclick=\"if(confirm('Forget this Scope? It goes back to Pair me, and you can pair it again from here.'))scDo('forget')\">Forget this Scope</button></div>"
         "<p class=hint style=\"margin-bottom:0\"><b>Find</b> flashes its screen. <b>Forget</b> wipes its WiFi; it goes back to <b>Pair me</b>.</p></div></div>"
         "<script>(function(){var asked=0,hold=0,open=0,E=function(i){return document.getElementById(i)};"
         "function sig(r){return r>-67?'good':r>-75?'fair':'weak'}"
         "function q(d){return fetch('/scope'+(d?'?'+d:''),{headers:{'X-FB':'1'},cache:'no-store'}).then(function(r){return r.ok?r.json():null}).then(show).catch(function(){})}"
         "function rad(n,v){var e=document.querySelector('input[name='+n+'][value=\"'+v+'\"]');if(e)e.checked=true}"
         "function show(j){if(!j)return;var busy=j.ps=='wait'||j.ps=='talk',N=E('scN'),P=E('scP'),Y=E('scY'),m=E('scm');"
         "E('scs').textContent=busy?'Pairing\\u2026':j.p?(j.on?'Connected':'Not connected'):'None paired';"
         "if(busy){open=1;asked=1}"
         "if(j.p&&(!open||j.ps=='ok')){open=0;N.style.display='none';P.style.display='none';Y.style.display=''}"
         "else if(open){N.style.display='none';Y.style.display='none';P.style.display=''}"
         "else{Y.style.display='none';P.style.display='none';N.style.display=''}"
         "E('scw').style.display=busy?'':'none';E('scpb').disabled=busy;"
         "E('scwt').textContent=j.ps=='talk'?'The Scope answered\\u2026':'Listening for the Scope\\u2026 '+Math.floor(j.left/60)+':'+('0'+j.left%60).slice(-2)+' left';"
         "if(asked&&!busy)m.textContent=j.ps=='wrong'?'That code didn\\u2019t match. Check the code on the Scope and try again.':"
         "j.ps=='timeout'?'No Scope answered. Is it showing Pair me, near the board? Try again.':"
         "j.ps=='fail'?'The board\\u2019s radio couldn\\u2019t start pairing. Restart the board and try again.':'';"
         "if(j.p){E('scst').innerHTML=j.on?'<span class=rok>&#9679; Connected</span> &middot; '+j.fps+' fps &middot; signal '+sig(j.rs):"
         "'<span class=roff>&#9679; Not connected</span>'+(j.ago>=0?' &middot; last seen '+(j.ago<120?j.ago+' s':Math.round(j.ago/60)+' min')+' ago':'');"
         "E('scid').textContent=j.id.slice(-4)+(j.v?' \\u00b7 v'+j.v:'')+(j.ip&&j.on?' \\u00b7 '+j.ip:'');"
         "if(Date.now()-hold>4000){rad('scsty',j.sty);rad('scbri',j.bri);rad('scrot',j.rot)}}}"
         "window.scOpen=function(){open=1;E('scm').textContent='';E('scc').value='';E('scN').style.display='none';E('scP').style.display='';E('scc').focus()};"
         "window.scPair=function(){var c=E('scc').value.replace(/\\D/g,'');if(c.length!=4){E('scm').textContent='Type the 4 digits shown on the Scope.';return}"
         "asked=1;E('scm').textContent='';q('do=pair&code='+c)};"
         "window.scCancel=function(){open=0;asked=0;q('do=cancel')};"
         "window.scDo=function(a){q('do='+a)};"
         "function set(k,v){hold=Date.now();q('do=set&'+k+'='+v)}"
         "window.scSty=function(v){set('sty',v)};window.scBri=function(v){set('bri',v)};window.scRot=function(v){set('rot',v)};"
         "q('');setInterval(function(){if(!document.hidden)q('')},2000)})()</script>");
}
