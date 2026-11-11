// Flight Board: Web console: the Settings page, airport search, saving.
// One tab of the flight_board sketch. Arduino joins the tabs in name order, after flight_board.ino.
// A row of buttons that work like radio buttons: values 0..n-1
String segButtons(const char* name, int checked, const char* const* labels, int n, const char* spanClass, const char* onchange) {
  String o = "<div class=seg>";
  for (int i = 0; i < n; i++) {
    o += "<label><input type=radio name=" + String(name) + " value=" + String(i);
    if (onchange) o += " onchange=" + String(onchange) + "(" + String(i) + ")";
    if (i == checked) o += " checked";
    o += "><span";
    if (spanClass) o += " class=" + String(spanClass);
    o += ">" + String(labels[i]) + "</span></label>";
  }
  return o + "</div>";
}

// The Settings page: Display, Time Zone, Scan Range | Overhead Alert, Radar LCD | System, Diagnostic Log
void handleSettings() {
  if (!hostOk()) { sendBlocked(); return; }
  Preferences pr;                          // its own handle: the web server runs in its own task
  pr.begin("fboard", true);
  float hrad = readRadius(pr, "hradf", "hrad", HOME_RADIUS_DEFAULT);
  float arad = readRadius(pr, "aradf", "arad", AIRPORT_RADIUS_DEFAULT);
  float orad = pr.isKey("oradf") ? pr.getFloat("oradf", hrad) : hrad;
  int tz = pr.getInt("tz", TZ_DEFAULT);
  int rstyle = constrain(pr.getInt("radar", RADAR_DEFAULT), 0, NUM_RADAR_STYLES - 1);
  int rbri = pr.getInt("rbri", 100);
  int lay = pr.getInt("lay", 1);
  bool hasHome = pr.getString("h", "").length() > 0;
  pr.end();
  int u = unitsMode;
  String r = String(UN.dist);
  String b;
  b.reserve(10000);
  b += pageBanner("<svg class=i viewBox='0 0 24 24'><path d='M21 15.5l-8-5V4.2a1.5 1.5 0 0 0-3 0v6.3l-8 5v2l8-2.5V20l-2 1.5v1.2l3.5-1 3.5 1v-1.2L13 20v-5l8 2.5z' fill='currentColor' stroke='none'/></svg>Flight Board Settings", "", headerChips(false));
  b += F("<a class=btn href=/>&larr;&nbsp; Back to Board Setup</a>"
         "<form method=post action=/save><input type=hidden name=pg value=settings>");
  b += "<input type=hidden name=ru id=ru value=" + String(u) + "><div class=scol>";
  // Display
  b += F("<div id=c-disp class=card><h2><span class=ic><svg class=i viewBox='0 0 24 24'><path d='M2.5 12s3.5-6.5 9.5-6.5 9.5 6.5 9.5 6.5-3.5 6.5-9.5 6.5S2.5 12 2.5 12z'/><circle cx='12' cy='12' r='3'/></svg></span>Display<span class=remnow id=dsum></span></h2><label style=\"margin-top:0\">Appearance</label>");
  { const char* t[] = { "Light", "Dark" }; b += segButtons("theme", webTheme, t, 2, nullptr, "thm"); }
  b += F("<label>Units</label>");
  b += segButtons("units", u, UNIT_SHORT, 3, nullptr, "un");
  b += F("<label>Board layout</label><div class=seg>");
  b += String("<label><input type=radio name=lay value=1") + (lay == 1 ? " checked" : "") + "><span>List + Radar</span></label>"
       "<label><input type=radio name=lay value=0" + (lay == 0 ? " checked" : "") + "><span>List</span></label></div>";
  b += F("<p class=hint>Or hold the knob for 1 second to switch.</p><label>Screen</label>");
  { const char* t[] = { "Normal", "Upside down" }; b += segButtons("flip", screenFlip ? 1 : 0, t, 2, nullptr, nullptr); }
  b += F("<p class=hint>For mounting the Flight Board the other way up. The knob follows.</p><label>Screen updates</label>");
  { const char* t[] = { "20 s", "40 s", "60 s" }; int ui = fetchSeconds <= 20 ? 0 : fetchSeconds <= 40 ? 1 : 2;
    b += segButtons("upd", ui, t, 3, nullptr, "upw");
    b += F("<p class=hint style=\"margin-bottom:0\">How often the flight list and radar refresh. Faster updates wear the e-ink panel sooner, so 60&nbsp;s is the gentlest.</p>"
           "<p class=hint id=updw style=\"margin:6px 0 0;color:#e0a43a\"></p>"
           "<script>function upw(i){var w=document.getElementById('updw');w.innerHTML=i==0?'&#9888;&nbsp;20 s triples the screen updates: fresher positions, but the panel wears out about three times faster.':i==1?'&#9888;&nbsp;40 s: fresher positions, but the panel wears about 1.5&times; faster.':'';w.style.display=i<2?'':'none'}upw(");
    b += String(ui) + F(")</script>"); }
  b += F("</div>");
  // Scan Range: the boxes are in the units shown; ru says which, so a units change in the same save is understood
  b += F("<div id=c-range class=card><h2><span class=ic><svg class=i viewBox='0 0 24 24'><circle cx='12' cy='12' r='9'/><circle cx='12' cy='12' r='4.5'/><circle cx='12' cy='12' r='1' fill='currentColor' stroke='none'/></svg></span>Scan Range<span class=remnow id=rsum></span></h2>");
  String mx = String(showRadius(60));
  b += "<label style=\"margin-top:0\">Home (<span class=un>" + r + "</span>)</label>"
       "<input name=hrad id=hrad type=number min=3 max=" + mx + " value=\"" + String(showRadius(hrad)) + "\">"
       "<p class=hint>Use a wider radius (<span data-nm=15>" + String(showRadius(15)) + "</span>&ndash;<span data-nm=30>" +
       String(showRadius(30)) + "</span> <span class=un>" + r + "</span>) if you&rsquo;re far from an airport.</p>";
  b += "<label>Airports (<span class=un>" + r + "</span>)</label>"
       "<input name=arad id=arad type=number min=3 max=" + mx + " value=\"" + String(showRadius(arad)) + "\">"
       "<p class=hint>Busy airports usually need only <span data-nm=10>" + String(showRadius(10)) + "</span> <span class=un>" + r + "</span>. "
       "Reduce to under <span data-nm=10>" + String(showRadius(10)) + "</span> <span class=un>" + r + "</span> if the radar gets too crowded.</p>";
  b += "<label>Other places (<span class=un>" + r + "</span>)</label>"
       "<input name=orad id=orad type=number min=3 max=" + mx + " value=\"" + String(showRadius(orad)) + "\"></div></div><div class=scol>";
  // Overhead Alert
  b += F("<div id=c-alert class=card><h2><span class=ic><svg class=i viewBox='0 0 24 24'><path d='M6 16V11a6 6 0 0 1 12 0v5l1.5 2h-15z'/><path d='M10 20.5a2 2 0 0 0 4 0'/><path d='M12 3v2'/></svg></span>Overhead Alert<span class=remnow id=asum></span></h2>"
         "<p class=hint style=\"margin-top:0\">When an aircraft is about to pass over Your Home, the Flight Board switches to an alert screen "
         "with a countdown, so you can step outside and look up. Home only, since that is where the board is.</p>");
  if (!hasHome) b += F("<p class=hint><b>Add Your Home on Board Setup to use this.</b></p>");
  b += F("<label>Alert</label>");
  { const char* t[] = { "On", "Off" }; b += segButtons("oa", alertOn ? 0 : 1, t, 2, nullptr, nullptr); }
  b += F("<label>Distance</label>");
  b += segButtons("od", alertDistI, ALERT_DIST_LABEL[u], 3, "od", nullptr);
  b += F("<p class=hint>How close the aircraft has to pass.</p>");
  b += "<label>Only aircraft below (<span id=ua>" + String(UN.alt) + "</span>)</label>";
  b += segButtons("oh", alertBelowI, ALERT_BELOW_LABEL[u], 5, "oh", nullptr);
  b += F("<p class=hint>Height above your home. Lower aircraft are easier to see and hear.</p><label>Heads-up</label>");
  { const char* t[] = { "30 s", "1 min", "2 min" }; b += segButtons("ow", alertWarnI, t, 3, nullptr, nullptr); }
  b += F("<p class=hint>How early the countdown starts before the closest pass.</p>"
         "<p class=hint style=\"margin-bottom:0\">Also shows on the Radar LCD when one is connected. The board goes back to normal "
         "about 20 seconds after the aircraft passes, or press any button.</p></div>");
  // Time Zone
  b += F("<div id=c-tz class=card><h2><span class=ic><svg class=i viewBox='0 0 24 24'><circle cx='12' cy='12' r='9'/><path d='M12 7v5l3.5 2'/></svg></span>Time Zone &amp; Clock</h2><style>.tzr{display:grid;grid-template-columns:1fr 1fr;gap:12px;align-items:center}.tzr select,.tzr .seg{margin:0}@media(max-width:700px){.tzr{grid-template-columns:1fr}}</style><div class=tzr><select name=tz>");
  for (int i = 0; i < NUM_TIME_ZONES; i++)
    b += "<option value=" + String(i) + (i == tz ? " selected>" : ">") + TIME_ZONES[i].label + "</option>";
  b += F("</select>");
  { const char* t[] = { "12-hour", "24-hour" }; b += segButtons("c24", clock24 ? 1 : 0, t, 2, nullptr, nullptr); }
  b += F("</div></div>");
  // Radar LCD Add-On
  b += "<div id=c-lcd class=card><h2><span class=ic><svg class=i viewBox='0 0 24 24'><circle cx='12' cy='12' r='9'/><circle cx='12' cy='12' r='5'/><path d='M12 12l6.3-6.3'/><circle cx='15.5' cy='15' r='1.1' fill='currentColor' stroke='none'/></svg></span>Radar LCD Add-On <span class=hint>(BETA)</span><span class=remnow>" +
       String(rstyle ? RADAR_STYLES[rstyle] : "Off") + "</span></h2>";
  b += F("<label style=\"margin-top:0\">Radar style</label><select name=radar>");
  for (int i = 0; i < NUM_RADAR_STYLES; i++)
    b += "<option value=" + String(i) + (i == rstyle ? " selected>" : ">") + RADAR_STYLES[i] + "</option>";
  b += F("</select><p class=hint>Optional GC9A01 LCD, see <a href=/help>Instructions</a> for pinout</p><label>Radar brightness</label><select name=rbri>");
  for (int p = 100; p >= 25; p -= 25)
    b += "<option value=" + String(p) + (p == rbri ? " selected>" : ">") + String(p) + "%</option>";
  b += F("</select><p class=hint style=\"margin-bottom:0\">Dims the radar image. The LCD backlight always stays on.</p></div></div>");
  // v0.9.35: Better Routes (FlightAware key). The saved key is never sent back to the page.
  // v0.9.36: folds away like Firmware; "On" glows.
  {
    faLoadCounts(); faRollMonth();
    bool on = faKey[0] && faUseI && !faKeyRefused() && !faLimitReached();
    b += F("<style>#c-routes{grid-column:1/-1;display:block}#c-routes#c-routes[open]{columns:auto}#c-routes .rin{margin-top:12px}"
           ".rg{display:grid;grid-template-columns:minmax(0,1.4fr) minmax(0,1fr);gap:16px;align-items:start}"
           "@media(max-width:600px){.rg{grid-template-columns:1fr;gap:0}#c-routes .opt{display:none}}#c-routes .opt{font-weight:400;opacity:.7;font-size:.8em}"
           "#c-routes .use{margin-bottom:0;padding-top:8px;border-top:1px solid rgba(128,140,155,.25)}"
           "#c-routes .rg2{margin-top:6px;padding-top:4px;border-top:1px solid rgba(128,140,155,.25)}#c-routes .hint.fw{margin-bottom:8px}"
           "#c-routes .rmk{display:flex;align-items:center;gap:8px;margin:6px 0 0;font:400 14px/1.4 IS,system-ui,sans-serif;letter-spacing:0;text-transform:none}"
           "#c-routes .rmk input{width:auto;margin:0}"
           ".s4 .seg span{white-space:nowrap}@media(max-width:600px){.s4 .seg{display:grid;grid-template-columns:1fr 1fr}"
           ".s4 .seg label:nth-child(-n+2) span{border-bottom:1px solid #999}.s4 .seg label:nth-child(3) span{border-left:0}"
           "html.dark .s4 .seg label:nth-child(-n+2) span{border-bottom-color:#3b4756}"
           ".s3v .seg{display:grid;grid-template-columns:1fr}.s3v .seg label+label span{border-left:0;border-top:1px solid #999}html.dark .s3v .seg label+label span{border-top-color:#3b4756}}"
           "#c-routes input.saved::placeholder{color:inherit;opacity:.85;letter-spacing:.12em}"
           ".faon{color:#3d7a00;font-weight:700;text-shadow:0 0 6px rgba(61,122,0,.45)}"
           ".faon i{display:inline-block;width:7px;height:7px;border-radius:50%;background:currentColor;margin-right:6px;vertical-align:1px;box-shadow:0 0 6px currentColor,0 0 12px currentColor}"
           "html.dark .faon{color:#c8ff3d;text-shadow:0 0 8px rgba(200,255,61,.85),0 0 2px rgba(200,255,61,.9)}"
           ".faoff{color:#d97a00;font-weight:700}</style>"
           "<details id=c-routes class=card><summary><span class=ic><svg class=i viewBox='0 0 24 24'><path d='M3 12h4l3-7 4 14 3-7h4'/></svg></span>"
           "Better Routes <span class=opt>(optional)</span><span class=remnow>");
    if (on) b += F("FlightAware &middot; <b class=faon><i></i>On</b>");
    else if (!faKey[0]) b += F("Off");
    else if (!faUseI) b += F("<b class=faoff>Paused</b>");
    else if (faKeyRefused()) b += F("<b class=faoff>Key not accepted</b>");
    else b += F("<b class=faoff>Limit reached</b>");
    b += F("</span></summary><div class=rin><p class=hint style=\"margin-top:0\">Now and then a route on the board looks wrong, like a "
           "Minneapolis &rarr; Chicago flight over Reno. The free route database falls out of date because airlines reuse flight numbers. "
           "The board works hard to catch and correct these on its own, but it isn&rsquo;t perfect. Add your own FlightAware key and the "
           "board asks FlightAware for the real route when the free one looks wrong or is missing, <b>only for those flights</b>.</p><div class=rg><div>"
           "<label>FlightAware API key</label><input type=password name=fak autocomplete=off spellcheck=false placeholder=\"");
    b += faKey[0] ? F("&bull;&bull;&bull;&bull;&bull;&bull;&bull;&bull;&bull;&bull;&bull;&bull;&bull;&bull;&bull;&bull;&bull;&bull;&bull;&bull;&bull;&bull;&bull;&bull;&bull;&bull;&bull;&bull;&bull;&bull;&bull;&bull;\" class=saved aria-label=\"FlightAware API key (saved, paste a new one to replace it)")
                    : F("Paste your key");   // saved: dots, like a stored password (the key itself never comes back)
    b += F("\"><p class=hint>FlightAware&rsquo;s <b>Personal</b> plan includes ~$5 a month of free lookups (FlightAware sets the price and may "
           "change it). Get a key at <a href=https://flightaware.com/aeroapi target=_blank rel=noopener>flightaware.com/aeroapi</a>. "
           "Leave blank to use free routes only.</p>");
    if (faKey[0]) b += F("<label class=rmk><input type=checkbox name=fakx value=1>Remove the saved key</label>");
    if (faKeyRefused()) b += F("<p class=hint style=\"color:#d97a00\"><b>FlightAware didn&rsquo;t accept this key.</b> Check it on your My AeroAPI page and paste it again.</p>");
    b += F("<label>Use FlightAware for</label>");                // v0.9.38
    { const char* t[] = { "Off", "Home only", "Home + airports", "All places" };
      b += F("<div class=s4>"); b += segButtons("fau", faUseI, t, 4, nullptr, nullptr); b += F("</div>"); }   // v0.9.39: 2 x 2 on phones
    b += F("<p class=hint>Airports see many more flights and use far more lookups. Off pauses it and keeps your key.</p>");
    b += F("</div><div><label>Monthly limit</label>");
    { const char* t[] = { "250", "1,000", "No limit" }; b += segButtons("fal", faLimitI, t, 3, nullptr, nullptr); }
    b += F("<p class=hint>1,000 lookups is ~$5 a month at today&rsquo;s price. Lookups are spread over the month; at the limit the board goes back to free routes until next month.</p>");
    b += F("<label>Route log (for testing)</label>");              // v0.9.38
    { const char* t[] = { "On", "Off" }; b += segButtons("rlo", rlogOn ? 0 : 1, t, 2, nullptr, nullptr); }
    b += F("<p class=hint>Lists the board&rsquo;s last route decisions in the Diagnostic Log. Memory only; no positions.</p></div></div>");
    // v0.9.41: Which flights and the details switch get their own row, side by side under the first two columns
    // v0.9.40: Which flights, and a lookup when a plane's details are opened
    b += F("<div class='rg rg2'><div><label>Which flights</label><div class=s3v>");
    { const char* t[] = { "All flights", "Low + overhead", "Low only" }; b += segButtons("faw", faWhichI, t, 3, nullptr, nullptr); }
    b += "</div><p class='hint fw'><b>Low:</b> below <span id=flo>" + String(unitsMode == 2 ? "5,500 m" : "18,000 ft") + "</span>, or climbing or descending. "
         "The board can&rsquo;t know where a plane is going until it asks, but near home a low plane is nearly always taking off or landing.</p>"
         "<p class='hint fw'><b>Overhead:</b> within your Overhead Alert distance (<span id=fod>" + String(ALERT_DIST_LABEL[unitsMode][alertDistI]) + "</span>)<span id=foh>"
         + (ALERT_BELOW_FT[unitsMode][alertBelowI] ? " and below its height (" + String(ALERT_BELOW_LABEL[unitsMode][alertBelowI]) + " " + String(UN.alt) + ")" : String(", at any height"))
         + "</span>.<span id=foa" + String(alertOn ? " style=display:none" : "") + "> The alert itself is off; these numbers still apply.</span></p>"
         "<p class=hint>Other flights keep the free route if it fits, or stay blank.</p>";
    b += F("</div><div><label>When I open a plane&rsquo;s details</label>");
    { const char* t[] = { "Look it up", "Don&rsquo;t" }; b += segButtons("fad", faDetailI ? 0 : 1, t, 2, nullptr, nullptr); }   // 0 = look it up
    b += F("<p class=hint>One lookup when you open a plane the board hasn&rsquo;t already asked about, so the plane you&rsquo;re curious about gets a real route.</p>"
           "<script>function fawh(){var d=document.querySelector('input[name=od]:checked'),h=document.querySelector('input[name=oh]:checked'),"
           "o=document.querySelector('input[name=oa]:checked'),u=document.querySelector('input[name=units]:checked');if(!d||!h)return;"
           "fod.textContent=d.nextElementSibling.textContent;foh.textContent=+h.value==4?', at any height':' and below its height ('+h.nextElementSibling.textContent+' '+ua.textContent+')';"
           "foa.style.display=(o&&+o.value)?'':'none';flo.textContent=(u&&+u.value==2)?'5,500 m':'18,000 ft'}"
           "addEventListener('DOMContentLoaded',function(){document.querySelectorAll('input[name=od],input[name=oh],input[name=oa],input[name=units]').forEach(function(e){e.addEventListener('change',function(){setTimeout(fawh,0)})})})</script></div></div>");
    if (faKey[0]) {
      char u[140];
      snprintf(u, sizeof u, "<p class='hint use'><b>This month:</b> %lu lookup%s &middot; ~$%.2f &middot; %lu route%s fixed</p>",
               (unsigned long)faCount, faCount == 1 ? "" : "s", faCount * FA_COST, (unsigned long)faFixed, faFixed == 1 ? "" : "s");
      b += u;
      if (faRepeats) { snprintf(u, sizeof u, "<p class=hint style='margin:2px 0 0'>%lu repeat lookup%s avoided since power-on</p>", (unsigned long)faRepeats, faRepeats == 1 ? "" : "s"); b += u; }
    }
    b += F("</div></details>");
  }
  // Firmware Update, folded away (works on its own; the file box has no name, so Save never sends it)
  b += F("<style>#c-fw{grid-column:1/-1;display:block}#c-fw#c-fw[open]{columns:auto}#c-fw .fwin{margin-top:12px}"
         ".fwrow{display:grid;grid-template-columns:1fr 1fr;gap:12px;align-items:end}.fwrow>*{min-width:0}"
         ".fw .pick{display:flex;align-items:center;gap:10px;padding:10px 12px;border:1px dashed #999;border-radius:3px;font-size:15px;min-height:24px}"
         "html.dark .fw .pick{border-color:#3b4756}"
         ".fw .pick b{font-family:GM,ui-monospace,monospace;font-size:13px;letter-spacing:.04em;font-weight:400}"
         ".fw .grow{flex:1;min-width:0;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}"
         ".fw .pick button.sec{width:auto;margin:0;padding:7px 12px;font-size:14px}"
         ".fw .ver{display:grid;grid-template-columns:auto 1fr;gap:4px 14px;margin:10px 0 0;font-size:15px}"
         ".fw .ver>span{color:#7d8794;font:12px/1.6 GM,ui-monospace,monospace;letter-spacing:.08em;text-transform:uppercase}"
         ".fwok{color:#3d7a00;font-weight:600}.fwbad{color:#b00;font-weight:600}"
         "html.dark .fwok{color:#c8ff3d}html.dark .fwbad{color:#ff7b7b}"
         ".fw button.go{width:100%;margin:0;padding:12px;border:0;border-radius:3px;background:#c8ff3d;color:#0a0c0f;font:700 15px/1 IS,system-ui,sans-serif;letter-spacing:.12em;text-transform:uppercase}"
         ".fw button.go[disabled]{background:#d6dbe0;color:#8a939d}"
         "html.dark .fw button.go[disabled]{background:#26303c;color:#6b7785}"
         ".fw .bar{height:12px;margin:12px 0 4px;border-radius:3px;background:#e3e7eb;overflow:hidden}"
         "html.dark .fw .bar{background:#26303c}"
         ".fw .bar i{display:block;height:100%;width:0;background:#c8ff3d}"
         ".fw .stat{font:13px/1.4 GM,ui-monospace,monospace;letter-spacing:.04em}"
         ".fw .askb{margin:12px 0 0;padding:14px;border:2px solid #c8ff3d;border-radius:3px;font-size:16px;text-align:center}"
         ".fw .askb b{font-size:20px;letter-spacing:.06em}"
         "@media(max-width:700px){.fwrow{grid-template-columns:1fr}}</style>"
         "<details id=c-fw class='card fw'><summary><span class=ic><svg class=i viewBox='0 0 24 24'><path d='M12 15V4'/><path d='M7.5 8.5L12 4l4.5 4.5'/>"
         "<path d='M4 14v4.5A1.5 1.5 0 0 0 5.5 20h13a1.5 1.5 0 0 0 1.5-1.5V14'/></svg></span>Firmware<span class=remnow>Installed <b>v");
  b += FW_VERSION;
  b += F("</b></span></summary><div class=fwin><p class=hint style='margin:0 0 10px'>Flash from a <b>.bin</b> file. All settings are retained unless you tick Reset to factory settings.</p>"
         "<input type=file id=fwf accept='.bin' style='display:none'>"
         "<div id=fwp class=fwrow><div class=pick><span id=fwn class='grow hint' style='margin:0'>No file selected</span>"
         "<button type=button class=sec id=fwc onclick='fwf.click()'>Choose file&hellip;</button></div>"
         "<button type=button class=go id=fwb disabled onclick='fwGo()'>Install</button></div>"
         "<label id=fwro class=fwr><input type=checkbox id=fwr><span>Reset to factory settings<br><span class=hint>Erases WiFi and all settings. The board starts as new.</span></span></label>"
         "<div id=fwv class=ver style='display:none'></div>"
         "<div id=fwa class=askb style='display:none'></div>"
         "<div id=fwbar style='display:none'><div class=bar><i id=fwi></i></div><div id=fws class=stat></div></div>"
         "<p id=fwh class=hint style='margin:8px 0 0;display:none'></p></div></details>"
         "<style>.fwr{display:flex;gap:10px;align-items:flex-start;margin:12px 0 0;font-size:15px;text-transform:none;letter-spacing:0;cursor:pointer}"
         ".fwr input{width:18px;height:18px;margin:2px 0 0;flex:none;accent-color:#c8ff3d}.fwr .hint{display:block;margin:2px 0 0}</style>"
         "<script>var FWV='");
  b += FW_VERSION;
  b += F("',fwOk=0,fwVH='',fwT=0;"
         "function fwe(t){var d=document.createElement('div');d.textContent=t;return d.innerHTML}"
         "function fwc2(a,b){a=a.split('.');b=b.split('.');for(var i=0;i<3;i++){var x=+(a[i]||0),y=+(b[i]||0);if(x!=y)return x-y}return 0}"
         "function fwHint(t){fwh.innerHTML=t;fwh.style.display=''}"
         "function fwX(u,m){return fetch(u,{method:m||'GET',headers:{'X-FB':'1'},cache:'no-store'}).then(function(r){return r.text()})}"
         "fwf.onchange=function(){var f=fwf.files[0];if(!f)return;fwOk=0;fwb.disabled=true;"
         "fwn.className='grow';fwn.innerHTML='<b>'+fwe(f.name)+'</b> <span class=hint>'+(f.size/1048576).toFixed(2)+' MB</span>';fwc.textContent='Change';"
         "var r=new FileReader();r.onload=function(){var u=new Uint8Array(r.result),v=null,"
         "app=u.length>36&&u[0]==233&&(u[12]|u[13]<<8)==9&&u[32]==50&&u[33]==84&&u[34]==205&&u[35]==171;"
         "if(app){var s=new TextDecoder('latin1').decode(u),k='FLIGHT'+'BOARD-FW:',i=0,m;"
         "while((i=s.indexOf(k,i))>=0){m=/^([0-9]+\\.[0-9]+\\.[0-9]+);/.exec(s.substr(i+k.length,16));if(m){v=m[1];break}i++}}"
         "fwv.style.display='';"
         "if(!v){fwv.innerHTML='<span>This file</span><div><span class=fwbad>&#10007; Not Flight Board firmware</span></div>';"
         "fwb.textContent='Install';fwHint('Choose the .bin file made for Flight Board. Nothing was changed.');return}"
         "var c=fwc2(v,FWV);fwOk=1;fwb.disabled=false;fwb.dataset.v=v;"
         "fwv.innerHTML='<span>This file</span><div><b>v'+v+'</b> &nbsp;'+(c>0?'<span class=fwok>&#10003; Flight Board firmware, newer</span>':"
         "c<0?'<span class=fwbad>Older than installed</span>':'<span class=hint>Same as installed</span>')+'</div><span>Installed</span><div>v'+FWV+'</div>';"
         "fwb.textContent=(c==0?'Reinstall v':'Install v')+v+(c<0?' anyway':'');fwVH=fwv.innerHTML;"
         "fwHint('Takes about a minute. The board asks you to press <b>MENU</b> first, then restarts by itself; keep it plugged in.')};"
         "r.readAsArrayBuffer(f)};"
         "function fwFail(t){clearTimeout(fwT);fwa.style.display='none';fwbar.style.display='none';fwv.innerHTML=fwVH;fwp.style.display='';fwro.style.display='';"
         "fwHint('<span class=fwbad>&#10007; Not installed.</span> '+fwe(t)+' The board is still on v'+FWV+'; nothing was changed.')}"
         "function fwGo(){if(!fwOk)return;var v=fwb.dataset.v;"
         "if(fwc2(v,FWV)<0&&!confirm('v'+v+' is older than the installed v'+FWV+'. Install it anyway?'))return;"
         "var fr=fwr.checked;if(fr&&!confirm('Install v'+v+' and erase ALL settings, including WiFi? The board restarts as new and you will need to set it up again from scratch.'))return;"
         "fwp.style.display='none';fwro.style.display='none';fwh.style.display='none';fwv.innerHTML='<span>Installing</span><div><b>v'+v+'</b></div>';"
         "fwa.style.display='';fwa.innerHTML='Asking the board&hellip;';fwb.dataset.fr=fr?1:0;"
         "fwX('/fwask?v='+encodeURIComponent(v)+(fr?'&fr=1':''),'POST').then(function(t){if(t=='busy')return fwFail('The board is already busy with an update.');"
         "if(t!='ask')return fwFail(t||'The board said no.');fwWait(v,0)}).catch(function(){fwFail('The board didn\\'t answer.')})}"
         "function fwWait(v,k){fwX('/fwask').then(function(t){"
         "if(t.indexOf('ask')==0){fwa.innerHTML='Press <b>MENU</b> on the Flight Board<br><span class=hint>to allow v'+v+' &middot; '+(t.split(' ')[1]||'')+' s left &middot; EXIT cancels</span>';"
         "fwT=setTimeout(function(){fwWait(v,k+1)},1000)}"
         "else if(t=='yes'){fwa.style.display='none';fwSend(v)}"
         "else fwFail(t=='no'?'Cancelled at the board (EXIT, or no answer in 60 seconds).':'The board stopped asking.')})"
         ".catch(function(){if(k<70)fwT=setTimeout(function(){fwWait(v,k+1)},1000);else fwFail('The board didn\\'t answer.')})}"
         "function fwSend(v){var x=new XMLHttpRequest(),d=new FormData();d.append('fw',fwf.files[0]);"
         "fwbar.style.display='';fwi.style.width='0';fws.textContent='Sending\\u2026';"
         "fwHint('Keep this page open and the board plugged in. If anything goes wrong the board keeps v'+FWV+'.');"
         "x.upload.onprogress=function(e){if(e.lengthComputable){var p=Math.round(e.loaded*100/e.total);fwi.style.width=p+'%';"
         "fws.textContent='Sending\\u2026 '+p+'%  '+(e.loaded/1048576).toFixed(2)+' of '+(e.total/1048576).toFixed(2)+' MB'}};"
         "x.onload=function(){var t=x.responseText||'';if(x.status==200&&t.indexOf('OK')==0){var n=25;fwi.style.width='100%';"
         "if(fwb.dataset.fr==1){fws.innerHTML='<span class=fwok>&#10003; Installed.</span> All settings erased; the board is restarting as new.';return}"
         "fws.innerHTML='<span class=fwok>&#10003; Installed.</span> Restarting&hellip; this page reloads in <b id=fwt>'+n+'</b> s';"
         "var iv=setInterval(function(){n--;if(n<=0){clearInterval(iv);location.href='/settings'}else fwt.textContent=n},1000)}"
         "else fwFail(t.replace(/^ERR /,'')||('Error '+x.status))};"
         "x.onerror=function(){fwFail('The connection dropped.')};"
         "x.open('POST','/update');x.send(d)}</script>");
  // Diagnostic Log, folded away
  b += F("<details id=c-diag class=card><summary><span class=ic><svg class=i viewBox='0 0 24 24'><rect x='3.5' y='4' width='17' height='16' rx='2'/><path d='M6.5 12.5h3l1.5-4 3 8 1.5-4h2'/></svg></span>Diagnostic Log</summary>"
         "<p class=hint style=\"margin-top:10px\">Log for diagnostic purposes. Tap Copy to paste into email, debugger, etc.</p>"
         "<textarea id=dg class=diag readonly rows=20>");
  b += htmlEscape(diagText());
  b += F("</textarea><button type=button class=sec onclick=\"dg.focus();dg.select();dg.setSelectionRange(0,1e5);document.execCommand('copy');this.textContent='Copied'\">Copy</button></details>"
         // System (buttons, not submits: Enter in a box must still mean Save) - last, under the Diagnostic Log
         "<div id=c-sys class=card><h2><span class=ic><svg class=i viewBox='0 0 24 24'><path d='M12 3v8.5'/><path d='M7 5.8a8 8 0 1 0 10 0'/></svg></span>System</h2><div class=two>"
         "<button type=button class=sec onclick=\"if(confirm('Restart the Flight Board now?'))rb.submit()\">Restart Flight Board</button>"
         "<button type=button class=\"sec danger\" onclick=\"if(confirm('Erase ALL settings, including WiFi? "
         "The board restarts as new and you will need to set it up again from scratch.'))fr.submit()\">Reset Flight Board to factory settings</button></div></div>"
         "<button id=sv>Save</button></form>"
         "<form id=rb method=post action=/reboot></form><form id=fr method=post action=/reset></form>"
         "<div id=c-nav class=card><a class=lb href=/>&larr; Back to Board Setup</a><a class=lb href=/help><svg class=i viewBox='0 0 24 24'><path d='M12 6.5C10 5 7 4.5 3 5v13.5c4-.5 7 0 9 1.5 2-1.5 5-2 9-1.5V5c-4-.5-7 0-9 1.5z'/><path d='M12 6.5V20'/></svg>Instructions &amp; Help</a>"
         "<a class=lb href=/legal><svg class=i viewBox='0 0 24 24'><path d='M12 3.5V20M7 20h10M5 7h14M8 4.8l4-1.3 4 1.3'/><path d='M5 7l-2.8 6.5a2.8 2.8 0 0 0 5.6 0z'/><path d='M19 7l-2.8 6.5a2.8 2.8 0 0 0 5.6 0z'/></svg>Credits &amp; Legal</a></div><div class=foot>&copy; 2026 Volary Dynamics</div>"
         // Units: every distance and height on the page follows the buttons straight away
         "<script>var K=[1,1.15078,1.852],U=['NM','mi','km'],AU=['ft','ft','m'],UN=['Aviation','US','Metric'],"
         "OD=[['1 NM','2 NM','3 NM'],['1 mi','2 mi','3 mi'],['2 km','4 km','6 km']],"
         "OH=[['5,000','10,000','15,000','20,000','Any'],['5,000','10,000','15,000','20,000','Any'],['1,500','3,000','4,500','6,000','Any']];"
         "function q(s){return document.querySelectorAll(s)}"
         "function ck(n){var e=document.querySelector('input[name='+n+']:checked');return e?+e.value:0}"
         "function sums(){var u=ck('units');dsum.textContent=(ck('theme')?'Dark':'Light')+' \\u00b7 '+UN[u];"
         "rsum.textContent=hrad.value+' \\u00b7 '+arad.value+' \\u00b7 '+orad.value+' '+U[u];"
         "asum.innerHTML=ck('oa')?'Off':'On \\u00b7 <b>Home</b>';}"
         "function thm(d){document.documentElement.classList.toggle('dark',!!d);sums()}"
         "function un(n){var o=+ru.value;[hrad,arad,orad].forEach(function(e){if(e.value)e.value=Math.round(e.value/K[o]*K[n]);e.max=Math.round(60*K[n]);});"
         "q('.un').forEach(function(e){e.textContent=U[n]});q('[data-nm]').forEach(function(e){e.textContent=Math.round(e.dataset.nm*K[n])});"
         "q('.od').forEach(function(e,i){e.textContent=OD[n][i]});q('.oh').forEach(function(e,i){e.textContent=OH[n][i]});"
         "ua.textContent=AU[n];ru.value=n;sums();}"
         "q('#hrad,#arad,#orad').forEach(function(e){e.addEventListener('input',sums)});"
         "q('input[name=oa]').forEach(function(e){e.addEventListener('change',sums)});sums();"
         // v0.9.23: save in place. The X-FB label tells the board to answer "ok" instead of sending the Saved page.
         "(function(){var f=sv.form,busy=0,tm=0;if(!window.fetch||!window.FormData||!window.URLSearchParams)return;"
         "function st(c,h){sv.className=c;sv.innerHTML=h}"
         "function idle(){clearTimeout(tm);if(!busy)st('','Save')}"
         "f.addEventListener('input',idle);f.addEventListener('change',idle);"
         "f.addEventListener('submit',function(e){e.preventDefault();if(busy)return;busy=1;clearTimeout(tm);"
         "st('sv-busy','<span class=spin></span>Saving&hellip;');"
         "fetch('/save',{method:'POST',headers:{'X-FB':'1'},body:new URLSearchParams(new FormData(f))})"
         ".then(function(r){return r.ok?r.text():''}).then(function(t){busy=0;if(t!='ok')throw 0;"
         "st('sv-ok','Saved &#10003;');tm=setTimeout(idle,2000);"
         "var k=f.elements.fak,x=f.elements.fakx;if((k&&k.value)||(x&&x.checked))setTimeout(function(){location.reload()},1500)})"   // v0.9.35: show the key's new state
         ".catch(function(){busy=0;st('sv-err','Not saved &mdash; tap to try again')})})})();</script>");
  sendPage(b);
}

// Case-insensitive "does text contain word"
bool containsNoCase(const char* text, const char* word) {
  size_t n = strlen(word);
  for (; *text; text++) if (strncasecmp(text, word, n) == 0) return true;
  return false;
}

// Case-insensitive "does a word in text start with word" ("Reno Tahoe", "Lake Tahoe", not "Grenoble")
bool containsWordNoCase(const char* text, const char* word) {
  size_t n = strlen(word);
  for (const char* t = text; *t; t++)
    if ((t == text || !isalnum((unsigned char)t[-1])) && strncasecmp(t, word, n) == 0) return true;
  return false;
}

// Airport search for the setup page: /find?q=reno -> up to 12 lines of "SEA  Seattle-Tacoma Intl - Seattle, WA  (KSEA)"
void handleFind() {
  if (!hostOk()) { sendBlocked(); return; }
  String q = server.arg("q"); q.trim();
  String out;
  if (q.length() >= 2) {
    String Q = q; Q.toUpperCase();
    int found[12], nf = 0;
    for (int tier = 0; tier < 6 && nf < 12; tier++) {
      for (int i = 0; i < NUM_AIRPORTS && nf < 12; i++) {
        const Airport& a = AIRPORTS[i];
        bool hit;
        if (tier == 0)      hit = strcmp(Q.c_str(), a.code) == 0;                      // exact code
        else if (tier == 1) hit = strcasecmp(Q.c_str(), a.ident) == 0;                // exact id (KSEA)
        else if (tier == 2) hit = strncasecmp(a.city, q.c_str(), q.length()) == 0 &&   // city starts with it, and so
                                  containsWordNoCase(a.name, q.c_str());                //   does a word of the name (the main airport)
        else if (tier == 3) hit = strncasecmp(a.city, q.c_str(), q.length()) == 0;     // city starts with it
        else if (tier == 4) hit = containsWordNoCase(a.name, q.c_str()) || containsWordNoCase(a.city, q.c_str());   // a word starts with it
        else                hit = containsNoCase(a.name, q.c_str()) || containsNoCase(a.city, q.c_str());           // anywhere
        if (!hit) continue;
        bool dup = false;
        for (int k = 0; k < nf; k++) if (found[k] == i) dup = true;
        if (!dup) found[nf++] = i;
      }
    }
    for (int k = 0; k < nf; k++) { out += airportLabel(&AIRPORTS[found[k]]); out += "\n"; }
  }
  server.send(200, "text/plain", out);
}

// Settings page: display, time zone, scan ranges, overhead alert, radar LCD. Anything not sent stays as it was.
void saveSettings() {
  Preferences pr;
  pr.begin("fboard", false);
  int units = server.hasArg("units") ? constrain((int)server.arg("units").toInt(), 0, 2) : unitsMode;
  int ru = server.hasArg("ru") ? constrain((int)server.arg("ru").toInt(), 0, 2) : unitsMode;
  float oldH = readRadius(pr, "hradf", "hrad", HOME_RADIUS_DEFAULT), oldA = readRadius(pr, "aradf", "arad", AIRPORT_RADIUS_DEFAULT);
  float oldO = pr.isKey("oradf") ? pr.getFloat("oradf", oldH) : oldH;
  float k = UNIT_SETS[ru].distK;
  // Radius boxes: typed in the units the page showed (ru). Unchanged boxes keep the exact saved distance.
  auto radius = [&](const char* name, float old) -> float {
    if (!server.hasArg(name)) return old;
    long t = server.arg(name).toInt();
    if (t <= 0) return old;
    return t == lroundf(old * k) ? old : constrain(t / k, 3.0f, 60.0f);
  };
  float hrad = radius("hrad", oldH), arad = radius("arad", oldA), orad = radius("orad", oldO);
  pr.putFloat("hradf", hrad); pr.putInt("hrad", (int)lroundf(hrad));   // older firmware reads the whole numbers
  pr.putFloat("aradf", arad); pr.putInt("arad", (int)lroundf(arad));
  pr.putFloat("oradf", orad);
  pr.putInt("units", units);
  if (server.hasArg("tz"))    pr.putInt("tz", constrain((int)server.arg("tz").toInt(), 0, NUM_TIME_ZONES - 1));
  if (server.hasArg("lay"))   pr.putInt("lay", constrain((int)server.arg("lay").toInt(), 0, 1));
  if (server.hasArg("flip"))  pr.putBool("flip", server.arg("flip") == "1");
  if (server.hasArg("upd"))   pr.putInt("upd", constrain((int)server.arg("upd").toInt(), 0, 2));
  if (server.hasArg("c24"))   pr.putBool("c24", server.arg("c24") == "1");
  if (server.hasArg("radar")) pr.putInt("radar", constrain((int)server.arg("radar").toInt(), 0, NUM_RADAR_STYLES - 1));
  if (server.hasArg("rbri"))  pr.putInt("rbri", constrain((int)server.arg("rbri").toInt(), 25, 100));
  if (server.hasArg("theme")) { webTheme = constrain((int)server.arg("theme").toInt(), 0, 1); pr.putInt("theme", webTheme); }
  if (server.hasArg("oa"))    pr.putBool("oa", server.arg("oa") != "1");          // 0 = On, 1 = Off
  if (server.hasArg("od"))    pr.putInt("od", constrain((int)server.arg("od").toInt(), 0, 2));
  if (server.hasArg("oh"))    pr.putInt("oh", constrain((int)server.arg("oh").toInt(), 0, 4));
  if (server.hasArg("ow"))    pr.putInt("ow", constrain((int)server.arg("ow").toInt(), 0, 2));
  if (server.hasArg("fal"))   pr.putInt("fal", constrain((int)server.arg("fal").toInt(), 0, 2));   // v0.9.35: Better Routes
  if (server.hasArg("fau"))   pr.putInt("fau", constrain((int)server.arg("fau").toInt(), 0, 3));   // v0.9.38
  if (server.hasArg("faw"))   pr.putInt("faw", constrain((int)server.arg("faw").toInt(), 0, 2));   // v0.9.40
  if (server.hasArg("fad"))   pr.putInt("fad", server.arg("fad") == "0" ? 1 : 0);   // button 0 = Look it up
  if (server.hasArg("rlo"))   pr.putInt("rlo", constrain((int)server.arg("rlo").toInt(), 0, 1));
  if (server.arg("fakx") == "1") pr.remove("fak");
  else if (server.hasArg("fak")) {                                   // blank = keep the saved key
    String k = server.arg("fak"); k.trim();
    String clean;
    for (unsigned i = 0; i < k.length() && clean.length() < 64; i++) if (isalnum((unsigned char)k[i])) clean += k[i];
    if (clean.length()) pr.putString("fak", clean);
  }
  pr.end();
  // A new scan range changes which aircraft belong on the board: start over. Anything else is applied in place.
  if (hrad != oldH || arad != oldA || orad != oldO) placesChanged = true;
  else settingsChanged = true;
  if (server.header("X-FB") == "1") { server.send(200, "text/plain", "ok"); return; }   // saved in place (v0.9.23)
  sendPage(messagePage("Settings", "<h2 style=\"margin-top:0\">Saved &#10003;</h2><p style=\"margin-bottom:0\">Returning to Settings in <b id=n>5</b> seconds.</p>") +
           "<a class=btn href=/settings>&larr;&nbsp; Back to Settings</a>"
           "<script>var n=5;setInterval(function(){n--;if(n<=0)location.href='/settings';"
           "else document.getElementById('n').textContent=n;},1000);</script>");
}

// Board Setup page (and first-time setup): WiFi, Home, airports, other places (and time zone during setup)
void handleSave() {
  if (!fromOwnPage(false)) { sendBlocked(); return; }
  if (server.arg("pg") == "settings") { saveSettings(); return; }
  Preferences prefs;                       // its own handle: the web server runs in its own task
  String ssid = server.arg("ssid");
  if (!ssid.length()) ssid = server.arg("ssid2");
  ssid.trim();
  String pass = server.arg("pass");
  String home = server.arg("h"); home.trim();
  String err;

  String apt[MAX_AIRPORTS_SET]; int na = 0;
  for (int i = 0; i < MAX_AIRPORTS_SET; i++) {
    String t = server.arg("a" + String(i)); t.trim();
    if (!t.length()) continue;
    const Airport* a = findAirport(t.c_str());
    if (!a) { err = "Couldn't find the airport \"" + t + "\". Start typing its code or city and pick it from the list."; break; }
    apt[na++] = a->ident;
  }
  String oq[MAX_OTHER], on[MAX_OTHER]; int no = 0;
  for (int i = 0; i < MAX_OTHER; i++) {
    String q = server.arg("o" + String(i)); q.trim();
    String n = server.arg("on" + String(i)); n.trim();
    if (!q.length()) continue;
    oq[no] = q; on[no] = n.substring(0, 14); no++;
  }
  if (!ssid.length()) err = "Please pick your WiFi network.";
  else if (!err.length() && !home.length() && na == 0 && no == 0) err = "Please add your home, an airport, or another place.";
  if (err.length()) {
    sendPage("<h1>Almost&hellip;</h1><p class=err>" + htmlEscape(err) + "</p><p><a href=\"javascript:history.back()\">&larr; Back</a></p>");
    return;
  }

  prefs.begin("fboard", false);
  String oldSsid = prefs.getString("ssid", "");
  String oldPass = prefs.getString("pass", "");
  bool wifiChanged = ssid != oldSsid || (pass.length() > 0 && pass != oldPass);   // the same password, auto-filled, isn't a change
  prefs.putString("ssid", ssid);
  if (pass.length() || ssid != oldSsid) prefs.putString("pass", pass);   // blank = keep (same network)
  if (prefs.getString("h", "") != home) prefs.remove("hg");             // forget an old lookup
  prefs.putString("h", home);
  for (int i = 0; i < MAX_AIRPORTS_SET; i++) prefs.putString(("a" + String(i)).c_str(), i < na ? apt[i] : String(""));
  for (int i = MAX_AIRPORTS_SET; i < 10; i++) prefs.remove(("a" + String(i)).c_str());   // slots 8-10 from before v0.8.0
  for (int i = 0; i < MAX_OTHER; i++) {
    String key = "o" + String(i);
    String val = i < no ? oq[i] : String("");
    if (prefs.getString(key.c_str(), "") != val) prefs.remove(("og" + String(i)).c_str());
    prefs.putString(key.c_str(), val);
    prefs.putString(("on" + String(i)).c_str(), i < no ? on[i] : String(""));
  }
  if (server.hasArg("tz")) prefs.putInt("tz", constrain((int)server.arg("tz").toInt(), 0, NUM_TIME_ZONES - 1));   // first-time setup
  prefs.putBool("v6", true);
  if (setupMode) prefs.putBool("fresh", true);
  prefs.putBool("setup", false);
  prefs.end();

  String later = "<p class=hint>To change settings later: hold <b>EXIT</b> on the board for 3 seconds, then open "
                 "<b>http://" + hostName + ".local</b> on any phone or computer on your WiFi.</p>";
  if (setupMode || wifiChanged) {
    sendPage(messagePage(setupMode ? "Setup" : "Settings", "<h2 style=\"margin-top:0\">Saved &#10003;</h2><p>The board is restarting and will connect to <b>" + htmlEscape(ssid) +
             "</b>.</p><p>You can close this page" + String(setupMode ? " and switch your phone back to your normal WiFi" : "") + ".</p>" +
             later + "<p class=hint>If the board says it can't connect, hold MENU while plugging it in to run setup again.</p>"));
    restartKind = RESTART_WIFI;
    restartAt = millis() + 3000;
  } else {
    placesChanged = true;                  // the main loop reloads the places
    sendPage(messagePage("Board Setup", "<h2 style=\"margin-top:0\">Saved &#10003;</h2><p style=\"margin-bottom:0\">Returning to Board Setup in <b id=n>5</b> seconds.</p>") +
             "<a class=btn href=/>&larr;&nbsp; Back to Board Setup</a>"
             "<script>var n=5;setInterval(function(){n--;if(n<=0)location.href='/';"
             "else document.getElementById('n').textContent=n;},1000);</script>");
  }
}

void handleReboot() {
  if (!fromOwnPage(false)) { sendBlocked(); return; }
  sendPage(messagePage("Settings", "<h2 style=\"margin-top:0\">Restarting&hellip;</h2><p>The board will be back in about 20 seconds.</p>"
           "<p class=hint style=\"margin-bottom:0\">Reload after it restarts.</p>") + "<a class=btn href=/settings>&larr;&nbsp; Back to Settings</a>");
  restartKind = RESTART_PLAIN;
  restartAt = millis() + 2000;
}

void handleReset() {
  if (!fromOwnPage(false)) { sendBlocked(); return; }
  sendPage(messagePage("Settings", "<h2 style=\"margin-top:0\">Erasing&hellip;</h2><p>All settings are being erased and the board is restarting as new.</p>"
           "<p style=\"margin-bottom:0\">To set it up again, join its <b>FlightBoard-" + boardId() + "</b> network (the board shows a new password) "
           "and follow the steps on its screen.</p>"));
  restartKind = RESTART_FACTORY;
  restartAt = millis() + 2000;
}

