// Flight Board: Setup mode: the board's own WiFi network and setup page.
// One tab of the flight_board sketch. Arduino joins the tabs in name order, after flight_board.ino.
// =====================================================================
//  Setup mode: the board's own WiFi + setup web page
// =====================================================================
void scanNetworks(const String& selected) {
  Serial.println("Scanning for WiFi networks...");
  buildNetworkOptions(WiFi.scanNetworks(), selected);
}

// "Scan again": scans in the background while the page waits. ?start=1 begins a scan; then the page
// asks every 0.7 s: 202 = still scanning, 200 = the new network list (the dropdown's options).
// ---- Who may talk to the board ----
// Pages must be asked for by the board's own name or address (not another website's name pointing at the
// board: "DNS rebinding"). Changes must come from the board's own pages: browsers label requests that other
// websites send (Origin, Sec-Fetch-Site), and the board's own buttons add an X-FB header that other sites can't.
const char* FB_HEADERS[] = { "Origin", "Sec-Fetch-Site", "X-FB" };

bool hostOk() {
  if (setupMode) return true;                    // its own hotspot: the captive page may use any name
  String h = server.hostHeader(); h.toLowerCase();
  int c = h.lastIndexOf(':'); if (c > 0 && h.indexOf(']') < 0) h = h.substring(0, c);   // drop ":80"
  while (h.endsWith(".")) h.remove(h.length() - 1);
  if (h.length() == 0) return true;              // very old clients send no name
  if (h == WiFi.localIP().toString()) return true;
  String n = hostName; n.toLowerCase();
  return h == n || h.startsWith(n + ".");        // flightboard-xxxx, .local, or the router's own name for it
}

bool fromOwnPage(bool needX) {
  if (!hostOk()) return false;
  String o = server.header("Origin"); o.toLowerCase();
  String hh = server.hostHeader(); hh.toLowerCase();
  if (o.length() && o != "http://" + hh) return false;            // sent by another website's page
  String sf = server.header("Sec-Fetch-Site");
  if (sf == "cross-site" || sf == "same-site") return false;
  if (needX && server.header("X-FB") != "1") return false;         // the board's own buttons always say so
  return true;
}

void sendBlocked() {
  server.send(403, "text/plain", "Refused: this didn't come from the Flight Board's own page. Open the board's address directly and try again.");
}

void handleScan() {
  if (!hostOk()) { sendBlocked(); return; }
  if (server.hasArg("start")) {
    if (WiFi.scanComplete() != WIFI_SCAN_RUNNING) { WiFi.scanDelete(); WiFi.scanNetworks(true); }
    server.send(202, "text/plain", "scanning");
    return;
  }
  int n = WiFi.scanComplete();
  if (n == WIFI_SCAN_RUNNING) { server.send(202, "text/plain", "scanning"); return; }
  if (n < 0) { server.send(500, "text/plain", "no scan"); return; }
  buildNetworkOptions(n, server.arg("sel"));
  server.send(200, "text/html", networkOptions);
}

// The dropdown's options from scan results: strongest first, one per name, with the signal
void buildNetworkOptions(int n, const String& selected) {
  networkOptions = "";
  String seen[30];
  int seenCount = 0;
  bool selectedFound = false;
  // strongest first
  int order[64];
  int m = n < 64 ? n : 64;
  for (int i = 0; i < m; i++) order[i] = i;
  for (int i = 1; i < m; i++) {
    int k = order[i], j = i - 1;
    while (j >= 0 && WiFi.RSSI(order[j]) < WiFi.RSSI(k)) { order[j + 1] = order[j]; j--; }
    order[j + 1] = k;
  }
  for (int i = 0; i < m && seenCount < 30; i++) {
    String s = WiFi.SSID(order[i]);
    if (!s.length()) continue;
    bool dup = false;
    for (int j = 0; j < seenCount; j++) if (seen[j] == s) dup = true;
    if (dup) continue;
    seen[seenCount++] = s;
    int rssi = WiFi.RSSI(order[i]);
    const char* bars = rssi > -60 ? "strong" : rssi > -72 ? "good" : "weak";
    bool sel = (s == selected);
    if (sel) selectedFound = true;
    networkOptions += "<option value=\"" + htmlEscape(s) + "\"" + (sel ? " selected" : "") + ">" +
                      htmlEscape(s) + " (" + String(rssi) + " dBm, " + bars + ")</option>";
  }
  if (selected.length() && !selectedFound)
    networkOptions = "<option value=\"" + htmlEscape(selected) + "\" selected>" + htmlEscape(selected) +
                     " (saved)</option>" + networkOptions;
  WiFi.scanDelete();
  Serial.printf("Found %d networks\n", seenCount);
}

const char PAGE_STYLE[] PROGMEM = R"(<style>
body{font-family:-apple-system,system-ui,sans-serif;max-width:480px;margin:0 auto;padding:16px;background:#f4f4f2;color:#111}
h1{font-size:22px;margin:4px 0 12px}h2{font-size:17px;margin:22px 0 8px;border-top:1px solid #ccc;padding-top:14px}
label{display:block;margin:10px 0 4px;font-weight:600}input,select{width:100%;box-sizing:border-box;font-size:17px;padding:10px;border:1px solid #999;border-radius:8px;background:#fff}
.r label{font-weight:400;display:flex;align-items:center;gap:8px;margin:6px 0}.r input{width:auto}
.hint{color:#555;font-size:14px;margin:4px 0;font-weight:400}summary{font-weight:600;padding:6px 0}.row{display:flex;gap:8px}.err{background:#fdd;padding:10px;border-radius:8px}
button{width:100%;font-size:18px;padding:14px;margin:12px 0 20px;border:0;border-radius:10px;background:#111;color:#fff}a{color:#036}
button.sec{font-size:16px;padding:10px;margin:12px 0 0;background:#fff;color:#111;border:1px solid #999}
button.danger{color:#b00;border-color:#b00}
@media(min-width:700px){body{max-width:560px;padding:32px 16px}}
html{background:#dcd8cb;--tone:#dcd8cb;--tr:220,216,203}body{background:transparent}   /* v0.9.33: the page color IS the chart's color: Safari on iPhone paints its bars with it */
body::before{content:"";position:fixed;inset:0;z-index:-1;background:url(/bg.jpg?v=929) 10% 60%/cover no-repeat}   /* v0.9.29 wide chart: Reno + Lake Tahoe on phones; ?v= so browsers drop the cached old image */
.chart{display:none}@media(max-width:899px){html{position:relative}body::before{display:none}.chart{display:block;position:absolute;inset:0;overflow:hidden;z-index:-1;pointer-events:none}.chart i{display:block;height:170vh;background:url(/bg.jpg?v=929) 10% 60%/cover no-repeat}.chart i:nth-child(even){transform:scaleY(-1)}.chart::before,.chart::after{content:"";position:absolute;left:0;right:0;z-index:1}.chart::before{top:0;height:64px;background:linear-gradient(180deg,rgb(var(--tr)) 0%,rgba(var(--tr),.74) 22%,rgba(var(--tr),.42) 45%,rgba(var(--tr),.16) 70%,rgba(var(--tr),0) 100%)}.chart::after{bottom:0;height:88px;background:linear-gradient(0deg,rgb(var(--tr)) 0%,rgba(var(--tr),.74) 22%,rgba(var(--tr),.42) 45%,rgba(var(--tr),.16) 70%,rgba(var(--tr),0) 100%)}html.dark .chart i{filter:brightness(.2) saturate(.5) contrast(1.05)}}   /* v0.9.33: phones: the chart scrolls with the page and runs its whole length (tiles, every other one mirrored so the seams match), clipped to the page so it adds no scrolling. No fixed layer: iPhone Safari paints its bars from those. */
.banner{position:relative;overflow:hidden;background:linear-gradient(135deg,#2f3a48,#46566a);color:#fff;border-radius:18px;padding:18px 18px 16px;margin:0 0 16px;box-shadow:0 6px 18px rgba(30,40,60,.25)}
.banner h1{position:relative;margin:0 0 12px;color:#fff;font-size:clamp(16px,4.6vw,24px);white-space:nowrap;overflow:hidden;text-overflow:ellipsis}.banner h1+.sub{margin-top:-10px}.banner .sub{position:relative;color:#c9d6e3;font-size:13px;margin:0 0 14px}.banner a{color:#fff}
.banner .deco{position:absolute;right:-50px;top:-30px;width:230px;height:230px}
.chips{position:relative;display:flex;flex-wrap:wrap;gap:8px}.chip{background:rgba(255,255,255,.12);border:1px solid rgba(255,255,255,.22);border-radius:999px;padding:5px 11px;font-size:13px;color:#eef;text-decoration:none}
.card{background:rgba(255,255,255,.86);-webkit-backdrop-filter:blur(1.5px);backdrop-filter:blur(1.5px);border-radius:14px;padding:14px 16px;margin:0 0 12px;box-shadow:0 1px 3px rgba(30,40,60,.08),0 4px 12px rgba(30,40,60,.05)}
.card h2{border:0;padding:0;margin:0 0 8px}details.card summary{font-size:17px}html{scroll-behavior:smooth}
a.btn{display:block;box-sizing:border-box;width:100%;text-align:center;font-size:18px;padding:14px;margin:0 0 16px;border-radius:10px;background:#111;color:#fff;text-decoration:none}
button,a.btn,a.lb{transition:transform .08s,filter .08s;-webkit-tap-highlight-color:transparent}
button:active,a.btn:active,a.lb:active{transform:scale(.97);filter:brightness(.8)}
.busy{pointer-events:none;opacity:.9}.spin{display:inline-block;width:14px;height:14px;margin:0 10px -2px 0;border:2px solid currentColor;border-right-color:transparent;border-radius:50%;animation:sp .7s linear infinite}
@keyframes sp{to{transform:rotate(360deg)}}
.foot a{color:inherit}.by{display:flex;align-items:center;gap:14px;padding:10px 14px}.by .vdm{flex:none;width:40px;height:40px}.byt{flex:1;display:flex;flex-direction:column;line-height:1.3}.byt span{font-size:13px;color:#555}html.dark .byt span{color:#9eabb8}.by .mk{margin:0;font-size:14px}@media(max-width:640px){.by{flex-wrap:wrap}.by .mk{width:100%;padding-left:54px}}.mk{display:flex;flex-wrap:wrap;gap:6px 22px;margin:8px 0 0}.mk a{text-decoration:none;font-weight:600}.mk .i{margin-right:6px}
.foot{display:table;margin:16px auto 28px;background:rgba(255,255,255,.86);border-radius:999px;padding:6px 16px;font-size:13px;color:#555;box-shadow:0 1px 3px rgba(30,40,60,.08)}
.ic{display:inline-flex;align-items:center;justify-content:center;vertical-align:middle;width:26px;height:26px;border-radius:8px;background:#e7edf4;margin-right:8px;color:#2f3a48}
.i{width:16px;height:16px;vertical-align:-3px;fill:none;stroke:currentColor;stroke-width:1.8;stroke-linecap:round;stroke-linejoin:round}.ic .i{width:17px;height:17px;vertical-align:0}
.chip .i{width:14px;height:14px;margin-right:6px;vertical-align:-2px}.banner h1 .i{width:1em;height:1em;vertical-align:-.12em;margin-right:.25em}a.lb .i{margin-right:8px}
.net{font-weight:400;font-size:13px;margin-left:8px}.net .ok,.net .bad{width:9px;height:9px;margin-right:5px;vertical-align:0}.bad{display:inline-block;border-radius:50%;background:#d33}
.chip b{color:#fff}.ok{display:inline-block;width:8px;height:8px;border-radius:50%;background:#3c3;margin-right:6px;vertical-align:1px}
textarea.diag{width:100%;box-sizing:border-box;font:11px/1.35 ui-monospace,Menlo,Consolas,monospace;padding:8px;border:1px solid #bbb;border-radius:8px;background:#f7f9fb;color:#222;resize:vertical}
a.lb{display:block;text-align:center;font-size:16px;padding:12px;margin:10px 0 0;border:1px solid #999;border-radius:10px;background:#fff;color:#111;text-decoration:none}
ul{padding-left:18px;margin:6px 0}li{margin:5px 0;font-size:14px;line-height:1.35}.lic{color:#666;font-size:12px}
table{border-collapse:collapse;width:100%;font-size:14px}td{padding:5px 6px;border-bottom:1px solid #ddd;vertical-align:top}td:first-child{font-weight:600;width:38%}
.warn{background:#fff6e5;border:1px solid #e8c37a;border-radius:14px;padding:12px 14px;font-size:14px;margin:0 0 12px}
.legalese{font-size:11px;line-height:1.45;color:#444;text-align:justify}
.banner{border-radius:3px;background:#0a0c0f;border:1px solid #262c33;box-shadow:0 10px 30px rgba(0,0,0,.3);padding:20px 18px 18px}
.banner::before{content:"";position:absolute;inset:0;background-image:linear-gradient(rgba(255,255,255,.035) 1px,transparent 1px),linear-gradient(90deg,rgba(255,255,255,.035) 1px,transparent 1px);background-size:18px 18px;pointer-events:none}
.banner .deco{position:absolute;right:0;bottom:0;top:auto;width:300px;height:180px}.banner h1{font-weight:800;letter-spacing:.01em}
.chips{gap:6px}.chip{border-radius:2px;background:rgba(10,12,15,.72);border:1px solid rgba(255,255,255,.18);font:11px/1.2 ui-monospace,SFMono-Regular,Menlo,Consolas,monospace;padding:5px 8px;color:#d8dee5}.chip .i{width:13px;height:13px;opacity:.85}
.tag{position:relative;display:flex;justify-content:space-between;gap:10px;font:10px/1 ui-monospace,SFMono-Regular,Menlo,Consolas,monospace;letter-spacing:.14em;color:#7d8794;margin:0 0 10px;white-space:nowrap;overflow:hidden}.tag b{color:#c8ff3d;font-weight:500}.tag b.off{color:#ffb547}
.ctag{position:absolute;right:12px;bottom:9px;font:9px/1 ui-monospace,SFMono-Regular,Menlo,Consolas,monospace;letter-spacing:.14em;color:#7d8794}
.cb{position:absolute;width:10px;height:10px;border:0 solid #c9d1da;opacity:.7}.cb.tl{left:6px;top:6px;border-width:1px 0 0 1px}.cb.tr{right:6px;top:6px;border-width:1px 1px 0 0}.cb.bl{left:6px;bottom:6px;border-width:0 0 1px 1px}.cb.br{right:6px;bottom:6px;border-width:0 1px 1px 0}
.card,.warn{border-radius:4px}input,select,textarea.diag,.err{border-radius:3px}button,a.btn,a.lb,.foot{border-radius:3px}.ic{border-radius:3px}
.seg{display:flex;border:1px solid #999;border-radius:3px;overflow:hidden}.seg label{flex:1;margin:0;font-weight:500}.seg input{display:none}.seg span{display:block;text-align:center;padding:10px 0;background:#fff;cursor:pointer}.seg label+label span{border-left:1px solid #999}.seg input:checked+span{background:#0a0c0f;color:#fff;font-weight:700}
html.dark{color-scheme:dark;background:#272625;--tone:#272625;--tr:39,38,37}html.dark body{background:transparent;color:#e4e9ee}html.dark body::before{filter:brightness(.32) saturate(.6) contrast(1.05)}
html.dark .card{background:rgba(24,30,38,.92);box-shadow:0 1px 3px rgba(0,0,0,.4),0 4px 12px rgba(0,0,0,.3);border:1px solid #2a3441}
html.dark .hint,html.dark .lic{color:#9eabb8}html.dark .legalese{color:#aab4bf}html.dark input,html.dark select{background:#10151b;color:#e4e9ee;border-color:#3b4756}
html.dark .ic{background:#2a3441;color:#c9d3dd}html.dark a{color:#8cc4ff}html.dark .banner a,html.dark a.chip{color:#d8dee5}
html.dark button,html.dark a.btn{background:#e4e9ee;color:#0f1318}html.dark button.sec,html.dark a.lb{background:#18202a;color:#e4e9ee;border-color:#3b4756}html.dark button.danger{color:#ff7b7b;border-color:#ff7b7b}
html.dark td{border-bottom-color:#2f3a47}html.dark .foot{background:rgba(24,30,38,.92);color:#9eabb8}html.dark textarea.diag{background:#0c1015;color:#cfd8e1;border-color:#3b4756}
html.dark .warn{background:#2c2414;border:1px solid #7a6230;color:#f1e3c4}html.dark .err{background:#3a1a1a}
html.dark .seg{border-color:#3b4756}html.dark .seg span{background:#10151b}html.dark .seg label+label span{border-left-color:#3b4756}html.dark .seg input:checked+span{background:#e4e9ee;color:#0f1318}
@font-face{font-family:IS;src:url(/f/is.woff) format('woff');font-display:swap}@font-face{font-family:IS;font-weight:700;src:url(/f/isb.woff) format('woff');font-display:swap}
@font-face{font-family:BS;font-weight:700;src:url(/f/bs.woff) format('woff');font-display:swap}@font-face{font-family:GM;src:url(/f/gm.woff) format('woff');font-display:swap}
body,input,select,button,a.btn,a.lb{font-family:IS,-apple-system,system-ui,sans-serif}
.banner h1,.card h2,details.card summary{font-family:BS,IS,sans-serif;font-weight:700;text-transform:uppercase;letter-spacing:.05em}
.banner h1{font-size:clamp(17px,5vw,28px)}.card h2,details.card summary{font-size:19px}
.tag,.ctag,.chip,textarea.diag,label,.seg span{font-family:GM,ui-monospace,SFMono-Regular,Menlo,Consolas,monospace}
label{font-size:12px;text-transform:uppercase;letter-spacing:.08em;font-weight:500;color:#4a5563}html.dark label{color:#9eabb8}
label .hint{text-transform:none;letter-spacing:0;font-family:IS,system-ui,sans-serif}.r label{text-transform:none;letter-spacing:0;font-size:15px}
form>button:not([type]){background:#c8ff3d;color:#0a0c0f;border:0;font-weight:700;text-transform:uppercase;letter-spacing:.12em;font-size:15px}
a.btn{background:#0a0c0f}html.dark a.btn{background:transparent;color:#e4e9ee;border:1px solid #4a5868}
html.dark body::before{filter:brightness(.2) saturate(.5) contrast(1.05)}
@media(min-width:900px){
body{max-width:1120px;padding:28px 24px}
.banner{padding:22px 24px 20px}.banner h1{font-size:30px}.chips{gap:8px}.chip{font-size:12px;padding:6px 10px}.banner .deco{width:420px;height:240px}
form{display:grid;grid-template-columns:1fr 1fr;gap:14px 16px;align-items:start}form>.card,form>details{margin:0}
#wf{grid-column:1;grid-row:1}#c-home{grid-column:1;grid-row:2}#c-oth{grid-column:1;grid-row:3}#c-apt{grid-column:2;grid-row:1/span 2}#c-tz,#c-info{grid-column:2;grid-row:3}
form>details{grid-column:1/-1}form>details:not(#c-board)[open]:not(:has(textarea)){columns:2;column-gap:32px}form>details>summary{column-span:all}
form>details label{break-after:avoid}form>details select,form>details input,form>details .seg,form>details .hint{break-inside:avoid}
form>button:not([type]){grid-column:1/-1;position:sticky;bottom:14px;z-index:5;margin:4px 0 16px;box-shadow:0 8px 24px rgba(0,0,0,.35)}
form>button.sv-busy{opacity:.75;cursor:default}
form>button.sv-ok{background:#0a0c0f;color:#c8ff3d;box-shadow:0 0 0 2px #c8ff3d inset,0 8px 24px rgba(0,0,0,.35)}
form>button.sv-err{background:#3a1215;color:#ffc2c2;box-shadow:0 0 0 2px #ff5a5a inset,0 8px 24px rgba(0,0,0,.35)}
textarea.diag{font-size:12px}a.btn{max-width:320px}
form~.card{display:grid;grid-template-columns:1fr 1fr;column-gap:12px}form~.card h2{grid-column:1/-1}form~.card a.lb{margin-top:6px}
.cols{columns:2;column-gap:16px}.cols>.card,.cols>.warn{break-inside:avoid;display:block}
}
input,select{font-size:16px;padding:8px 10px}
.card,.warn{margin:0 0 16px}.warn{padding:14px 16px}.warn p{margin:0}
.warn h2{font-family:BS,IS,sans-serif;font-weight:700;text-transform:uppercase;letter-spacing:.05em;font-size:19px;margin:0 0 8px;border:0;padding:0;color:#7a4b00}
.warn .ic{background:#f6d58e;color:#7a4b00}html.dark .warn h2{color:#ffcf6b}html.dark .warn .ic{background:#4a3a14;color:#ffcf6b}
@media(min-width:900px){body{max-width:980px}input,select{font-size:15px}form{gap:16px}#wf,#c-tz,#c-home,#c-apt,#c-oth,#c-info{align-self:stretch}
.cols{column-gap:16px}.cols>.card,.cols>.warn{display:inline-block;width:100%;box-sizing:border-box;margin:0 0 16px;vertical-align:top}}
.pdots::placeholder{font-size:13px;letter-spacing:.3em;color:#8a939e;opacity:1}
.remnow{float:right;margin-top:4px;font:11px/1 GM,ui-monospace,monospace;letter-spacing:.1em;color:#7d8794;text-transform:uppercase}.remnow b{color:#5b8a00;font-weight:700}html.dark .remnow b{color:#c8ff3d}
.rem{display:flex;flex-wrap:wrap;gap:8px}.rem button{width:auto;margin:0;padding:9px 12px;font:12px/1 GM,ui-monospace,monospace;letter-spacing:.06em;text-transform:uppercase;background:#fff;color:#2f3a48;border:1px solid #999;border-radius:3px;display:inline-flex;align-items:center;gap:7px}
.rem button .i{width:15px;height:15px}.rem button.on{background:#c8ff3d;color:#0a0c0f;border-color:#c8ff3d;font-weight:700}
html.dark .rem button{background:#18202a;color:#cfd8e1;border-color:#3b4756}html.dark .rem button.on{background:#c8ff3d;color:#0a0c0f;border-color:#c8ff3d}
.bctl{display:flex;flex-wrap:wrap;gap:4px 24px;margin-top:6px}
.rem b.num{display:inline-flex;align-items:center;justify-content:center;min-width:18px;height:18px;border-radius:50%;background:#0a0c0f;color:#fff;font-size:10px;font-weight:700}.rem button.on b.num{background:#0a0c0f;color:#c8ff3d}html.dark .rem b.num{background:#e4e9ee;color:#0f1318}html.dark .rem button.on b.num{background:#0a0c0f;color:#c8ff3d}
.xw{position:relative;display:block;flex:1;min-width:0}.xw>input.apt{margin-bottom:0!important}.xw:has(>input.apt){margin-bottom:8px}.row .xw{flex:1 1 auto}.xw input{padding-right:34px}
.xb{position:absolute;right:6px;top:50%;transform:translateY(-50%);width:26px;height:26px;margin:0;padding:0;border:0;border-radius:3px;background:transparent;color:#7d8794;font:20px/26px IS,system-ui,sans-serif;cursor:pointer}
html .xb,html.dark .xb{background:transparent;color:#7d8794;border:0}html .xb:hover,html.dark .xb:hover{background:rgba(127,127,127,.18);color:#e5484d}.xb[hidden]{display:none}.apt+.xb{}
.undo{position:fixed;left:50%;bottom:84px;transform:translate(-50%,20px);opacity:0;pointer-events:none;transition:.2s;z-index:20;display:flex;align-items:center;gap:14px;background:#0a0c0f;color:#e4e9ee;border:1px solid #3b4756;border-radius:3px;padding:10px 12px 10px 16px;font:13px IS,system-ui,sans-serif;box-shadow:0 8px 24px rgba(0,0,0,.4)}
.undo.on{opacity:1;transform:translate(-50%,0);pointer-events:auto}@media(max-width:899px){form>button:not([type]),form>button.sv-ok,form>button.sv-err,.undo{background-color:transparent!important}
form>button:not([type]){background-image:linear-gradient(#c8ff3d,#c8ff3d)}form>button.sv-ok{background-image:linear-gradient(#0a0c0f,#0a0c0f)}
form>button.sv-err{background-image:linear-gradient(#3a1215,#3a1215)}.undo{background-image:linear-gradient(#0a0c0f,#0a0c0f)}}   /* v0.9.33: Safari on iPhone tints its bars from fixed / sticky elements' background-color */
html .undo button,html.dark .undo button{width:auto;margin:0;padding:6px 12px;background:#c8ff3d;color:#0a0c0f;border:0;border-radius:3px;font:700 12px/1 GM,ui-monospace,monospace;letter-spacing:.1em;text-transform:uppercase}
#c-info .lb{margin-top:10px}
@media(max-width:899px){form:has(#c-info){display:flex;flex-direction:column}#c-info{order:9}form:has(#c-info)>button:not([type]){order:8}}
#c-nav{display:flex;flex-wrap:wrap;justify-content:center;gap:10px}#c-nav .lb{margin:0;padding:10px 18px;font-size:15px}
@media(max-width:899px){#c-nav{flex-direction:column}}
@media(min-width:900px){.scol{display:flex;flex-direction:column;gap:16px;align-self:stretch}.scol>.card{margin:0}
#c-sys,#c-diag{grid-column:1/-1}#c-sys .two{display:grid;grid-template-columns:1fr 1fr;gap:12px}#c-sys .two button{margin:0}}
h3.sub{font:600 13px/1.2 GM,ui-monospace,monospace;letter-spacing:.08em;text-transform:uppercase;margin:16px 0 6px;color:inherit;opacity:.8}
p.legal2{margin:0;font-size:14px}
</style>)";

