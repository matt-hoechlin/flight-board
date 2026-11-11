// Flight Board: The web server's own task.
// One tab of the flight_board sketch. Arduino joins the tabs in name order, after flight_board.ino.
// =====================================================================
//  Settings page on the home network: http://flightboard-xxxx.local
// =====================================================================
void webTask(void*) {
  bool started = false;
  wdtWatchMe();
  for (;;) {
    wdtPet();
    if (!started && WiFi.status() == WL_CONNECTED) {
      if (MDNS.begin(hostName.c_str())) MDNS.addService("http", "tcp", 80);
      server.collectHeaders(FB_HEADERS, 3);
      server.on("/", HTTP_GET, handleRoot);
      server.on("/save", HTTP_POST, handleSave);
      server.on("/find", HTTP_GET, handleFind);
      server.on("/help", HTTP_GET, handleHelp);
      server.on("/bg.jpg", HTTP_GET, handleBg);
      server.on("/board", HTTP_GET, handleBoard);
      server.on("/clear", HTTP_GET, handleClear);
      server.on("/f/is.woff", HTTP_GET, handleFontIS);
      server.on("/f/isb.woff", HTTP_GET, handleFontISB);
      server.on("/f/bs.woff", HTTP_GET, handleFontBS);
      server.on("/f/gm.woff", HTTP_GET, handleFontGM);
      server.on("/legal", HTTP_GET, handleLegal);
      server.on("/scan", HTTP_GET, handleScan);
      server.on("/settings", HTTP_GET, handleSettings);
      server.on("/reboot", HTTP_POST, handleReboot);
      server.on("/reset", HTTP_POST, handleReset);
      server.on("/update", HTTP_POST, handleUpdateDone, handleUpdateUpload);
      server.on("/fwask", HTTP_POST, handleFwAsk);
      server.on("/fwask", HTTP_GET, handleFwAsk);
      server.begin();
      started = true;
      Serial.printf("Settings page: http://%s.local  (http://%s)\n", hostName.c_str(), WiFi.localIP().toString().c_str());
    }
    if (started) { crumb.webStep = WS_PAGE; server.handleClient(); crumb.webStep = WS_IDLE; }
    vTaskDelay(pdMS_TO_TICKS(5));
  }
}

