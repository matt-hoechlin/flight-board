// Flight Board: Web console: page building, banners, the Remote Control home page.
// One tab of the flight_board sketch. Arduino joins the tabs in name order, after flight_board.ino.
// Sent in pieces, so the whole page never has to sit in memory twice
void sendPage(const String& body) {
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "text/html", "");
  server.sendContent(webTheme ? "<!doctype html><html class=dark><head>" : "<!doctype html><html><head>");
  server.sendContent("<meta charset=utf-8><meta name=viewport content=\"width=device-width,initial-scale=1\"><title>Flight Board setup</title>");
  server.sendContent(PAGE_STYLE, strlen(PAGE_STYLE));
  server.sendContent("</head><body><div class=chart aria-hidden=true><i></i><i></i><i></i><i></i><i></i><i></i></div>");   // v0.9.33: phones' chart
  server.sendContent(body);
  String page;
  // Tap feedback: the button shows a spinner and a word, and a thin bar runs across the top
  page += F("<script>function busy(e,t){e.classList.add('busy');e.innerHTML='<span class=spin></span>'+t;"
            "}"
            "document.querySelectorAll('a.btn,a.lb').forEach(function(a){a.addEventListener('click',function(){busy(a,'Loading&hellip;')})});"
            "document.querySelectorAll('form').forEach(function(f){f.addEventListener('submit',function(){"
            "var b=f.querySelector('button:not([type=button])');if(b)busy(b,'Saving&hellip;')})});"
            "addEventListener('pageshow',function(e){if(e.persisted)location.reload()});</script>");
  page += F("</body></html>");
  server.sendContent(page);
  server.sendContent("");                   // end of the page
}

// Slate banner at the top of each page: title, a line under it, and optional chips
const char BANNER_RADAR[] PROGMEM = "<svg class=deco viewBox=\"0 0 300 180\" xmlns=\"http://www.w3.org/2000/svg\">"
  "<g fill=\"none\" stroke=\"#dfe6ee\"><circle cx=\"300\" cy=\"180\" r=\"60\" stroke-opacity=\"0.22\"/><circle cx=\"300\" cy=\"180\" r=\"120\" stroke-opacity=\"0.16\"/><circle cx=\"300\" cy=\"180\" r=\"180\" stroke-opacity=\"0.12\"/><circle cx=\"300\" cy=\"180\" r=\"240\" stroke-opacity=\"0.09\"/>"
  "<path d=\"M60 180L69 180M61 159L65 159M64 138L68 139M68 118L72 119M74 98L78 99M82 79L86 80M92 60L100 64M103 42L107 45M116 26L119 28M130 10L133 13M146 -4L148 -1M162 -17L165 -13M180 -28L184 -20M199 -38L200 -34M218 -46L219 -42M238 -52L239 -48M258 -56L259 -52M279 -59L279 -55M300 -60L300 -51\" stroke-opacity=\".35\"/></g>"
  "<g stroke=\"#c8ff3d\" fill=\"none\" stroke-width=\"1.3\"><path d=\"M225 123v-6h6M241 117h6v6M247 133v6h-6M231 139h-6v-6\"/>"
  "<path d=\"M236 128L190 154\" stroke-dasharray=\"3 4\" stroke-opacity=\".6\"/></g><circle cx=\"236\" cy=\"128\" r=\"2.3\" fill=\"#c8ff3d\"/></svg>";

// Header status line: the first place's position (or SETUP), and whether the board is online
String bannerStatus() {
  if (setupMode) return F("<div class=tag><span>FLIGHT BOARD</span><b>&#9679; SETUP</b></div>");
  String left = "FLIGHT BOARD";
  if (!setupMode && !noPlaces && numPlaces > 0) {
    const Place& p = places[0];
    char name[24], buf[80];
    copyStr(name, sizeof name, p.isAirport ? p.code : p.name);
    for (char* c = name; *c; c++) *c = toupper((unsigned char)*c);
    snprintf(buf, sizeof buf, "%s %.4f %c &middot; %.4f %c", name,
             fabs(p.lat), p.lat >= 0 ? 'N' : 'S', fabs(p.lon), p.lon >= 0 ? 'E' : 'W');
    left = htmlEscape(String(name)) + (buf + strlen(name));   // the name as plain text, then the position
  }
  bool on = WiFi.status() == WL_CONNECTED;
  return "<div class=tag><span>" + left + "</span><b" + String(on ? "" : " class=off") + ">&#9679; " + (on ? "ONLINE" : "OFFLINE") + "</b></div>";
}

// Corner label: the nearest aircraft's track and flight level, e.g. "UAL455 &middot; TRK 078 &middot; FL350"
String bannerContact() {
  if (setupMode || !dataLock || xSemaphoreTake(dataLock, pdMS_TO_TICKS(50)) != pdTRUE) return "";
  String out;
  if (shared.ok && shared.count > 0) {
    const Plane& p = shared.planes[0];
    char buf[64];
    snprintf(buf, sizeof buf, " &middot; TRK %03d &middot; FL%03ld", (int)lround(p.track) % 360, (p.altFt + 50) / 100);
    out = String("<span class=ctag>") + htmlEscape(String(p.callsign)) + buf + "</span>";
  }
  xSemaphoreGive(dataLock);
  return out;
}

String pageBanner(const String& title, const String& sub, const String& chips) {
  String b = "<div class=banner>" + String(FPSTR(BANNER_RADAR)) +
             "<i class=\"cb tl\"></i><i class=\"cb tr\"></i><i class=\"cb bl\"></i><i class=\"cb br\"></i>" +
             bannerStatus() + bannerContact() + "<h1>" + title + "</h1>";
  if (sub.length()) b += "<p class=sub>" + sub + "</p>";
  if (chips.length()) b += "<div class=chips>" + chips + "</div>";
  return b + "</div>";
}

String versionChips() {
  char sn[24];
  serialNumber(sn, sizeof sn);
  return "<span class=chip><svg class=i viewBox='0 0 24 24'><path d='M3 3.5h8.5l9 9-8 8-9-9z'/><circle cx='7.6' cy='8' r='1.6'/></svg>v" + String(FW_VERSION) + "</span><span class=chip><svg class=i viewBox='0 0 24 24'><rect x='2.5' y='5' width='19' height='14' rx='1.8'/><circle cx='8' cy='10.5' r='2.2'/><path d='M4.8 16c.6-1.8 1.8-2.6 3.2-2.6s2.6.8 3.2 2.6M14 10h5M14 13.5h4'/></svg>" + String(sn) + "</span>";
}

// Header chips: address, IP, WiFi, data source, version, serial (version and serial only during setup).
// onSettings: the WiFi chip jumps to the WiFi card on the same page; elsewhere it opens it on the settings page.
String headerChips(bool) {
  if (setupMode) return versionChips();
  return "<span class=chip><svg class=i viewBox='0 0 24 24'><circle cx='12' cy='12' r='9'/><ellipse cx='12' cy='12' rx='4' ry='9'/><path d='M3 12h18M4.5 7.5h15M4.5 16.5h15'/></svg>" + hostName + ".local</span>"
         "<span class=chip><svg class=i viewBox='0 0 24 24'><rect x='3' y='4' width='18' height='12' rx='1.5'/><path d='M8.5 20h7M12 16v4'/></svg>" + WiFi.localIP().toString() + "</span>"
         "<span class=chip><svg class=i viewBox='0 0 24 24'><path d='M2.5 9a14 14 0 0 1 19 0M5.5 12.3a9.5 9.5 0 0 1 13 0M8.6 15.5a5 5 0 0 1 6.8 0'/><circle cx='12' cy='19' r='1.2' fill='currentColor' stroke='none'/></svg>" + htmlEscape(wifiName) +
         " &middot; " + String(WiFi.RSSI()) + " dBm</span>"
         "<span class=chip><svg class=i viewBox='0 0 24 24'><path d='M4 20l3.5-6'/><path d='M3.5 9.5a7.5 7.5 0 0 0 11 10z'/><path d='M14 3.5a6.5 6.5 0 0 1 6.5 6.5M14 7a3 3 0 0 1 3 3'/></svg>" + String(viewOk ? SOURCE_WEB[viewSource] : "connecting") + "</span>" +
         versionChips();
}

// Short message pages (saved, restarting, erasing): banner plus one card
String messagePage(const String& title, const String& body) {
  return pageBanner("<svg class=i viewBox='0 0 24 24'><path d='M21 15.5l-8-5V4.2a1.5 1.5 0 0 0-3 0v6.3l-8 5v2l8-2.5V20l-2 1.5v1.2l3.5-1 3.5 1v-1.2L13 20v-5l8 2.5z' fill='currentColor' stroke='none'/></svg>Flight Board", title, "") + "<div class=card>" + body + "</div>";
}

// The two help buttons, at the bottom of the settings page
String helpButtons() {
  return F("<div class=card><h2><span class=ic><svg class=i viewBox='0 0 24 24'><circle cx='12' cy='12' r='9'/><path d='M12 11v6'/><circle cx='12' cy='7.6' r='1.1' fill='currentColor' stroke='none'/></svg></span>Info</h2>"
           "<a class=lb href=/help><svg class=i viewBox='0 0 24 24'><path d='M12 6.5C10 5 7 4.5 3 5v13.5c4-.5 7 0 9 1.5 2-1.5 5-2 9-1.5V5c-4-.5-7 0-9 1.5z'/><path d='M12 6.5V20'/></svg>Instructions &amp; Help</a>"
           "<a class=lb href=/legal><svg class=i viewBox='0 0 24 24'><path d='M12 3.5V20M7 20h10M5 7h14M8 4.8l4-1.3 4 1.3'/><path d='M5 7l-2.8 6.5a2.8 2.8 0 0 0 5.6 0z'/><path d='M19 7l-2.8 6.5a2.8 2.8 0 0 0 5.6 0z'/></svg>Credits &amp; Legal</a></div><div style=\"height:20px\"></div>");
}

void handleRoot() {
  if (!hostOk()) { sendBlocked(); return; }
  Preferences prefs;                       // its own handle: the web server runs in its own task
  prefs.begin("fboard", true);
  bool moved = prefs.getBool("v6", false);
  prefs.end();
  if (!moved) migrateSettings();

  prefs.begin("fboard", true);
  String ssid = prefs.getString("ssid", "");
  bool hasPass = prefs.getString("pass", "").length() > 0;
  String home = prefs.getString("h", "");
  int tz = prefs.getInt("tz", TZ_DEFAULT);
  String apt[MAX_AIRPORTS_SET], oq[MAX_OTHER], on[MAX_OTHER];
  for (int i = 0; i < MAX_AIRPORTS_SET; i++) {
    const Airport* a = findAirportByIdent(prefs.getString(("a" + String(i)).c_str(), "").c_str());
    if (a) apt[i] = airportLabel(a);
  }
  for (int i = 0; i < MAX_OTHER; i++) {
    oq[i] = prefs.getString(("o" + String(i)).c_str(), "");
    on[i] = prefs.getString(("on" + String(i)).c_str(), "");
  }
  prefs.end();
  if (server.hasArg("rescan") || (setupMode && !networkOptions.length())) scanNetworks(ssid);
  else if (!networkOptions.length())          // on the home network: don't scan unless asked
    networkOptions = "<option value=\"" + htmlEscape(ssid) + "\" selected>" + htmlEscape(ssid) + " (current)</option>";

  String b;
  b.reserve(9500);
  if (setupMode) {
    b += pageBanner("<svg class=i viewBox='0 0 24 24'><path d='M21 15.5l-8-5V4.2a1.5 1.5 0 0 0-3 0v6.3l-8 5v2l8-2.5V20l-2 1.5v1.2l3.5-1 3.5 1v-1.2L13 20v-5l8 2.5z' fill='currentColor' stroke='none'/></svg>Flight Board Setup", "", versionChips());
    b += F("<div class=card><p class=hint style=\"margin-top:0\">Later, you can change these anytime from your own WiFi: hold <b>EXIT</b> on the board "
           "for 3 seconds and it shows the address to open.</p>"
           "<p class=hint style=\"margin-bottom:0\">Android: if your phone asks whether to stay connected to a network with no internet, tap <b>Keep</b> or <b>Yes</b>.</p></div>");
  } else {
    b += pageBanner("<svg class=i viewBox='0 0 24 24'><path d='M21 15.5l-8-5V4.2a1.5 1.5 0 0 0-3 0v6.3l-8 5v2l8-2.5V20l-2 1.5v1.2l3.5-1 3.5 1v-1.2L13 20v-5l8 2.5z' fill='currentColor' stroke='none'/></svg>Flight Board Setup", "", headerChips(true));
  }
  b += F("<form method=post action=/save>");
  // 1. WiFi (folded away on the home network: changing it restarts the board)
  b += F("<div id=wf class=card>");
  b += setupMode ? F("<h2><span class=ic><svg class=i viewBox='0 0 24 24'><path d='M2.5 9a14 14 0 0 1 19 0M5.5 12.3a9.5 9.5 0 0 1 13 0M8.6 15.5a5 5 0 0 1 6.8 0'/><circle cx='12' cy='19' r='1.2' fill='currentColor' stroke='none'/></svg></span>1. WiFi</h2>") : F("<h2><span class=ic><svg class=i viewBox='0 0 24 24'><path d='M2.5 9a14 14 0 0 1 19 0M5.5 12.3a9.5 9.5 0 0 1 13 0M8.6 15.5a5 5 0 0 1 6.8 0'/><circle cx='12' cy='19' r='1.2' fill='currentColor' stroke='none'/></svg></span>WiFi</h2>");
  b += (WiFi.status() == WL_CONNECTED) ? F("<label>Network<span class=net><span class=ok></span>Connected</span></label>")
                                      : F("<label>Network<span class=net><span class=bad></span>Not connected</span></label>");
  b += F("<select name=ssid id=ssid>");
  b += networkOptions;
  b += F("<option value=\"\">Other (type it in)...</option></select>"
         "<input name=ssid2 id=ssid2 placeholder=\"Network name\" style=\"display:none;margin-top:8px\">"
         "<p class=hint><a href=\"/?rescan=1\" id=scn onclick=\"return rescan(this)\">Scan again</a> | 2.4 GHz networks only</p>"
         "<label>Password</label><input name=pass id=pass type=password autocomplete=off");
  if (hasPass) b += F(" placeholder=\"&#9679;&#9679;&#9679;&#9679;&#9679;&#9679;&#9679;&#9679;\" class=pdots");   // always 8: says nothing about its length
  b += F(">");
  b += setupMode ? F("</div>") : F("<p class=hint style=\"margin-bottom:0\">Changing WiFi restarts the board</p></div>");
  // 2. Time zone (setup only; afterwards it's on the Settings page)
  if (setupMode) {
  b += F("<div id=c-tz class=card><h2><span class=ic><svg class=i viewBox='0 0 24 24'><circle cx='12' cy='12' r='9'/><path d='M12 7v5l3.5 2'/></svg></span>");
  if (setupMode) b += F("2. ");
  b += F("Time Zone</h2><select name=tz>");
  for (int i = 0; i < NUM_TIME_ZONES; i++)
    b += "<option value=" + String(i) + (i == tz ? " selected>" : ">") + TIME_ZONES[i].label + "</option>";
  b += F("</select></div>");
  }
  // 3. Home
  b += F("<div id=c-home class=card><h2><span class=ic><svg class=i viewBox='0 0 24 24'><path d='M3.5 11L12 4l8.5 7'/><path d='M6 9.5V20h12V9.5'/><path d='M10 20v-5.5h4V20'/></svg></span>");
  if (setupMode) b += F("3. ");
  b += F("Your Home <span class=hint>(optional)</span></h2>"
         "<p class=hint>Shown as HOME on the board - use the Flight Board&rsquo;s physical location or leave blank to show airports only.</p>"
         "<input name=h autocomplete=off autocorrect=off placeholder=\"Zip code, town, or latitude, longitude\" value=\"");
  b += htmlEscape(home);
  b += F("\"><p class=hint>For example: 89511 &middot; Reno, NV &middot; 38.9606, -119.9409<br>"
         "Street addresses aren&rsquo;t supported. Latitude, longitude is the most precise.</p></div>");
  // 4. Airports
  b += F("<div id=c-apt class=card><h2><span class=ic><svg class=i viewBox='0 0 24 24'><path d='M3 20h18'/><path d='M4.5 14.5l3 2.5 13-4.5c1.2-.4 1.6-1.6.8-2.3-.6-.5-1.4-.6-2.1-.3L15 11.3 8 6.5 6 7.2l4.2 5.6-3.7 1.3-2-1.5-1.3.5z'/></svg></span>");
  if (setupMode) b += F("4. ");
  b += F("Airports</h2>"
         "<p class=hint>Start typing an IATA Airport code or city, then pick from the list</p>");
  for (int i = 0; i < MAX_AIRPORTS_SET; i++) {
    b += "<input class=apt name=a" + String(i) + " list=apts autocomplete=off autocorrect=off style=\"margin-bottom:8px\" placeholder=\"Airport " +
         String(i + 1) + "\" value=\"" + htmlEscape(apt[i]) + "\">";
  }
  b += F("<datalist id=apts></datalist></div>");
  // 5. Other places
  b += F("<div id=c-oth class=card><h2><span class=ic><svg class=i viewBox='0 0 24 24'><path d='M12 21.5s-6.5-6.2-6.5-11.5a6.5 6.5 0 0 1 13 0c0 5.3-6.5 11.5-6.5 11.5z'/><circle cx='12' cy='10' r='2.4'/></svg></span>");
  if (setupMode) b += F("5. ");
  b += F("Other Places <span class=hint>(up to 3)</span></h2>"
         "<p class=hint>Enter a name plus a zip code, town, or latitude, longitude</p>");
  for (int i = 0; i < MAX_OTHER; i++) {
    b += "<div class=row style=\"margin-bottom:8px\"><input name=on" + String(i) + " maxlength=14 style=\"max-width:36%\" placeholder=\"Name\" value=\"" +
         htmlEscape(on[i]) + "\"><input name=o" + String(i) + " autocomplete=off autocorrect=off placeholder=\"Zip, town, or lat, lon\" value=\"" +
         htmlEscape(oq[i]) + "\"></div>";
  }
  b += F("</div>");
  // The x buttons: remove a saved entry from the board straight away (home network only)
  if (!setupMode) b += F("<div id=undo class=undo><span id=undot></span><button type=button onclick=undoHome()>Undo</button></div>"
    "<script>(function(){"
    "function xb(inp,cb){var w=document.createElement('span');w.className='xw';inp.parentNode.insertBefore(w,inp);w.appendChild(inp);"
      "var x=document.createElement('button');x.type='button';x.className='xb';x.title='Remove from the board';x.innerHTML='&times;';x.onclick=cb;w.appendChild(x);"
      "x.hidden=!inp.defaultValue;return x;}"
    "function clr(q,ok){fetch('/clear?'+q,{headers:{'X-FB':'1'}}).then(function(r){if(r.ok){ok();if(window.bd)setTimeout(function(){bd('')},1200);}}).catch(function(){});}"
    "var ap=[].slice.call(document.querySelectorAll('input.apt'));"
    "ap.forEach(function(inp,i){inp._x=xb(inp,function(){clr('apt='+i,function(){"
      "for(var j=i;j<ap.length;j++){var n=ap[j+1];ap[j].value=ap[j].defaultValue=n?n.defaultValue:'';ap[j]._x.hidden=!ap[j].defaultValue;}});});});"
    "var ot=[];for(var k=0;k<3;k++){var o=document.querySelector('input[name=o'+k+']'),nm=document.querySelector('input[name=on'+k+']');if(o)ot.push([nm,o]);}"
    "ot.forEach(function(p,i){p[1]._x=xb(p[1],function(){clr('oth='+i,function(){"
      "for(var j=i;j<ot.length;j++){var n=ot[j+1];for(var m=0;m<2;m++){ot[j][m].value=ot[j][m].defaultValue=n?n[m].defaultValue:'';}ot[j][1]._x.hidden=!(ot[j][1].defaultValue||ot[j][0].defaultValue);}});});"
      "p[1]._x.hidden=!(p[1].defaultValue||p[0].defaultValue);});"
    "var h=document.querySelector('input[name=h]'),hv='',ht;"
    "if(h){h._x=xb(h,function(){hv=h.defaultValue;clr('home=1',function(){h.value=h.defaultValue='';h._x.hidden=true;"
      "undot.textContent='Removed Your Home';undo.classList.add('on');clearTimeout(ht);ht=setTimeout(function(){undo.classList.remove('on')},5000);});});}"
    "window.undoHome=function(){clearTimeout(ht);undo.classList.remove('on');"
      "clr('home=1&undo='+encodeURIComponent(hv),function(){h.value=h.defaultValue=hv;h._x.hidden=!hv;});};"
    "})()</script>");
  // Info & Settings: the Settings page, help and legal (on a phone it sits under Save)
  if (!setupMode)
    b += F("<div id=c-info class=card><h2><span class=ic><svg class=i viewBox='0 0 24 24'><circle cx='12' cy='12' r='9'/><path d='M12 11v6'/><circle cx='12' cy='7.6' r='1.1' fill='currentColor' stroke='none'/></svg></span>Info &amp; Settings</h2>"
           "<a class=lb href=/settings><svg class=i viewBox='0 0 24 24'><circle cx='12' cy='12' r='3'/><path d='M12 2.8l1.6 2.4 2.8-.9.5 2.9 2.9.5-.9 2.8 2.4 1.6-2.4 1.6.9 2.8-2.9.5-.5 2.9-2.8-.9-1.6 2.4-1.6-2.4-2.8.9-.5-2.9-2.9-.5.9-2.8L2.8 12l2.4-1.6-.9-2.8 2.9-.5.5-2.9 2.8.9z'/></svg>Settings</a>"
           "<a class=lb href=/help><svg class=i viewBox='0 0 24 24'><path d='M12 6.5C10 5 7 4.5 3 5v13.5c4-.5 7 0 9 1.5 2-1.5 5-2 9-1.5V5c-4-.5-7 0-9 1.5z'/><path d='M12 6.5V20'/></svg>Instructions &amp; Help</a>"
           "<a class=lb href=/legal><svg class=i viewBox='0 0 24 24'><path d='M12 3.5V20M7 20h10M5 7h14M8 4.8l4-1.3 4 1.3'/><path d='M5 7l-2.8 6.5a2.8 2.8 0 0 0 5.6 0z'/><path d='M19 7l-2.8 6.5a2.8 2.8 0 0 0 5.6 0z'/></svg>Credits &amp; Legal</a></div>");
  // Remote Control: everything the knob and buttons do (home network only, once there are places)
  if (!setupMode && !noPlaces) {
    b += F("<details id=c-board class=card><summary><span class=ic><svg class=i viewBox='0 0 24 24'><rect x='2.5' y='5' width='19' height='12' rx='1.5'/><path d='M8 20.5h8M12 17v3.5'/><path d='M10 8.5l4 2.5-4 2.5z' fill='currentColor' stroke='none'/></svg></span>Remote Control"
           "<span class=remnow>Displaying <b id=bnow></b></span></summary>"
           "<label style=\"margin-top:12px\">Tap any place or aircraft below to show it on the Flight Board</label><div class=rem id=bpl>");
    for (int i = 0; i < numPlaces; i++) {
      const Place& p = places[i];
      int8_t ic = placeIcon(p);
      b += "<button type=button data-i=" + String(i) + " onclick=\"bd('show=" + String(i) + "')\">";
      b += ic == ICON_HOUSE ? F("<svg class=i viewBox='0 0 24 24'><path d='M3.5 11L12 4l8.5 7'/><path d='M6 9.5V20h12V9.5'/><path d='M10 20v-5.5h4V20'/></svg>") : ic == ICON_PLANE ? F("<svg class=i viewBox='0 0 24 24'><path d='M21 15.5l-8-5V4.2a1.5 1.5 0 0 0-3 0v6.3l-8 5v2l8-2.5V20l-2 1.5v1.2l3.5-1 3.5 1v-1.2L13 20v-5l8 2.5z' fill='currentColor' stroke='none'/></svg>") : F("<svg class=i viewBox='0 0 24 24'><path d='M12 21.5s-6.5-6.2-6.5-11.5a6.5 6.5 0 0 1 13 0c0 5.3-6.5 11.5-6.5 11.5z'/><circle cx='12' cy='10' r='2.4'/></svg>");
      b += htmlEscape(String(p.isAirport ? p.code : p.name)) + "</button>";
    }
    b += F("</div><label>Aircraft</label><div class=rem id=bac></div><p class=hint id=bacn style=\"margin-bottom:0\"></p>"
           "<div class=bctl><div><label>Navigate</label><div class=rem id=bnav>"
           "<button type=button onclick=\"bd('key=prev')\">&larr; Prev</button>"
           "<button type=button onclick=\"bd('key=next')\">Next &rarr;</button>"
           "<button type=button onclick=\"bd('key=select')\">Select</button>"
           "<button type=button onclick=\"bd('key=back')\">Back</button></div></div>"
           "<div><label>Board</label><div class=rem>"
           "<button type=button onclick=\"bd('refresh=1');this.classList.add('on');var t=this;setTimeout(function(){t.classList.remove('on')},1500)\"><svg class=i viewBox='0 0 24 24'><path d='M20 12a8 8 0 1 1-2.4-5.7'/><path d='M20 4v5h-5'/></svg>Refresh now</button></div></div>"
           "<div><label>Layout</label><div class=rem id=blay>"
           "<button type=button data-l=1 onclick=\"bd('layout=1')\">List + Radar</button>"
           "<button type=button data-l=0 onclick=\"bd('layout=0')\">List</button></div></div></div>"
           "<script>function he(t){return String(t).replace(/[&<>\"']/g,function(c){return '&#'+c.charCodeAt(0)+';'})}"
           "function bu(t){var a=t.split('|'),c=+a[0],l=+a[1],v=+a[2],d=+a[3],cs=(a[4]||'').split(',').filter(String),p='';"
           "document.querySelectorAll('#bpl button').forEach(function(e){var on=+e.dataset.i==c;e.classList.toggle('on',on);if(on)p=e.textContent;});"
           "bnow.textContent=p+(v&&cs[d]?' \\u00b7 '+cs[d]:'');"
           "document.querySelectorAll('#blay button').forEach(function(e){e.classList.toggle('on',+e.dataset.l==l);});"
           "var s=document.querySelector('select[name=lay]');if(s)s.value=l;"
           "var h='';cs.forEach(function(x,i){h+='<button type=button class=\"'+(v&&i==d?'on':'')+'\" onclick=\"bd(\\'ac='+i+'\\')\"><b class=num>'+(i+1)+'</b>'+he(x)+'</button>';});"
           "bac.innerHTML=h;bacn.textContent=cs.length?'':'No aircraft on the board right now.';if(a[5]!==undefined){var ic={h:'<svg class=i viewBox=\\'0 0 24 24\\'><path d=\\'M3.5 11L12 4l8.5 7\\'/><path d=\\'M6 9.5V20h12V9.5\\'/><path d=\\'M10 20v-5.5h4V20\\'/></svg>',a:'<svg class=i viewBox=\\'0 0 24 24\\'><path d=\\'M21 15.5l-8-5V4.2a1.5 1.5 0 0 0-3 0v6.3l-8 5v2l8-2.5V20l-2 1.5v1.2l3.5-1 3.5 1v-1.2L13 20v-5l8 2.5z\\' fill=\\'currentColor\\' stroke=\\'none\\'/></svg>',p:'<svg class=i viewBox=\\'0 0 24 24\\'><path d=\\'M12 21.5s-6.5-6.2-6.5-11.5a6.5 6.5 0 0 1 13 0c0 5.3-6.5 11.5-6.5 11.5z\\'/><circle cx=\\'12\\' cy=\\'10\\' r=\\'2.4\\'/></svg>'},q='';a[5].split(';').filter(String).forEach(function(x,i){q+='<button type=button data-i='+i+' class=\"'+(i==c?'on':'')+'\" onclick=\"bd(\\'show='+i+'\\')\">'+(ic[x[0]]||'')+he(x.slice(2))+'</button>';});bpl.innerHTML=q;}}"
           "function bd(q){fetch('/board'+(q?'?'+q:''),{headers:{'X-FB':'1'}}).then(function(r){return r.text()}).then(bu).catch(function(){});}"
           "setInterval(function(){if(document.getElementById('c-board').open&&!document.hidden)bd('');},5000);</script></details>");
    b += "<script>bd('')</script>";
  }
  b += setupMode ? F("<button>Save and connect</button></form>") : F("<button>Save</button></form>");
  if (setupMode) b += helpButtons();
  b += F("<div class=foot>&copy; 2026 Volary Dynamics</div>");
  b += F(""
         "<script>var t;function find(e){var q=e.target.value;if(q.length<2||q.indexOf('(')>=0)return;clearTimeout(t);"
         "t=setTimeout(function(){fetch('/find?q='+encodeURIComponent(q)).then(function(r){return r.text()}).then(function(x){"
         "var d=document.getElementById('apts');d.innerHTML='';x.split('\\n').forEach(function(l){if(l){var o=document.createElement('option');"
         "o.value=l;d.appendChild(o);}});});},250);}"
         "document.querySelectorAll('.apt').forEach(function(i){i.addEventListener('input',find);});"
         "var s=document.getElementById('ssid');s.onchange=function(){document.getElementById('ssid2').style.display=s.value?'none':''};s.onchange();"
         "function rescan(a){if(a.dataset.on)return false;a.dataset.on=1;var sel=s.value;a.innerHTML='<span class=spin></span>Scanning&hellip; (about 5 s)';"
         "function done(t){a.textContent=t;delete a.dataset.on;}"
         "function poll(k){if(k>30){done('No answer. Tap to try again');return;}setTimeout(function(){fetch('/scan?sel='+encodeURIComponent(sel)).then(function(r){"
         "if(r.status==200)return r.text().then(function(h){s.innerHTML=h+'<option value=\"\">Other (type it in)...</option>';s.onchange();done('Scan again');});"
         "poll(k+1);}).catch(function(){poll(k+1);});},700);}"
         "fetch('/scan?start=1').then(function(){poll(0);}).catch(function(){poll(0);});return false;}"
         "</script>");
  sendPage(b);
}

