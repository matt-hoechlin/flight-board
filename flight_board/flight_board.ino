// =====================================================================
//  Flight Board v0.9.47
//  Elecrow CrowPanel ESP32-S3 5.79" e-paper (792 x 272, 2x SSD1683)
//  (c) 2026 Matt Hoechlin
//
//  New in v0.9.47:
//    - Flight Board Round support: http://<board>/radar.json gives what the radar shows (place, range, airports,
//      aircraft with their hex, position, track, speed, altitude, climb, squawk; and the one open on the board).
//      The board also announces itself on the network as "_flightboard._tcp", so a round screen finds it.
//      /board?acx=<hex> opens that aircraft (same rules as the Remote Control buttons).
//
//  New in v0.9.46:
//    - Radar LCD: new style "Modern TRACON", after today's approach-control scopes (STARS): black, grey range rings
//      and nearby airports, blue history dots, green data blocks (callsign / altitude + speed, swapping with the
//      type every 2 s; one line for private aircraft). The aircraft open on the board turns white.
//    - Classic Radar: the small labels (N E S W, place and range, ring distances) are white; lime blended in.
//    - Overhead alert: squawk line under the route.
//    - Ordinary ATC codes show bare ("SQUAWK 2153"); only VFR, GLIDER, fire, rescue, military and emergencies
//      get a tag.
//    - Route log: the FlightAware note is no longer cut off ("free was wrong, fixed").
//    - Settings page (wide screens): Time Zone moved to the left column and cards keep their own height,
//      so there are no stretched empty gaps.
//
//  New in v0.9.45:
//    - Fix: safety restarts every few minutes on v0.9.44 ("stopped: data, fetching aircraft from adsb.lol").
//      A request could hold the data part past the 40 s limit: the secure handshake was allowed 120 s, and an
//      answer that trickled in was read in one go. Now the handshake gets the request's own time limit, the
//      answer is read in pieces (checking in as it arrives), and the whole request stops after 15 s
//      (Diagnostic Log: "too slow or cut off").
//    - The emergency squawk checks use adsb.fi, then airplanes.live, so adsb.lol only gets the board's own
//      updates (it had started saying "too many requests").
//    - Diagnostic Log names two more steps if the data part ever stops: "checking the overhead alert" and
//      "checking emergency squawks".
//    - Details page on airport screens: the squawk was missing (no room next to "DEPARTING from ATL"). The
//      squawk now comes first; then "DEPARTING" (or the full "DEPARTING from ATL") if there's room.
//
//  New in v0.9.44:
//    - Better Routes > Full asks FlightAware about every airliner, even when the free route fits; the route log
//      notes "agreed with free" or "free was wrong, fixed".
//    - Squawk codes. Emergencies (7700 emergency, 7600 radio failure, 7500 hijack, 7400 drone lost link) pop up a
//      full screen once per flight (60 s, or any button), then a white-on-black note in the footer while it lasts.
//      Overhead Alert card > Emergency alerts: Off / Scan area / 50 / 150 / 300 NM from Home. A code must show in
//      two separate updates first. Special flight tags (FIRE 1255, SAR 1277, MIL 4000/7777) in the airline column.
//      Every aircraft's details page shows its squawk and what it means (talking to ATC, VFR, glider...).
//    - Radar LCD style "ATC Data Blocks": square targets with heading lines and three-line tags (callsign,
//      flight level + speed in tens of knots, type), placed so they don't overlap.
//    - The notes at the top of this file list the current files and libraries.
//
//  New in v0.9.43:
//    - Better Routes > How much FlightAware: Light (~$5 a month: planes taking off, landing or overhead, spread over
//      the month) / Full (every plane that needs a route, all the time, safety stop at 10,000 lookups ~ $50) /
//      Custom (the Which flights and Monthly limit choices, as before). Boards that were on the old defaults start on
//      Light; anything else starts on Custom with the same settings.
//    - An overhead alert plane is looked up even when the month's lookups are being paced (still within the limit).
//
//  New in v0.9.42:
//    - The sketch is now split into tabs (01_helpers.ino ... 22_setup_loop.ino) next to this file. No code
//      changed: Arduino joins the tabs back into one program, in name order after this file, exactly as before.
//      Open this file in the Arduino IDE and the others appear as tabs. The folder is now just "flight_board".
//    - This file keeps the notes, the libraries, the settings and the shared data; startup and the main loop
//      are in 22_setup_loop.ino (last, so everything they use is already defined).
//
//  New in v0.9.41:
//    - Settings > Better Routes on a computer: Which flights and the details switch sit side by side on their own
//      row under the first two columns, and the Low / Overhead notes are separate paragraphs.
//
//  New in v0.9.40:
//    - Better Routes > Which flights: All flights / Low + overhead (default) / Low only. "Low" is the board's
//      best guess at a plane taking off or landing nearby (below 18,000 ft, or climbing / descending 1,000 fpm or
//      more); "overhead" uses the Overhead Alert distance and height. High planes just passing by keep the free route.
//    - Better Routes > When I open a plane's details: Look it up (default) / Don't. Opening a plane the board
//      hasn't asked FlightAware about asks right away (still within the monthly limit).
//    - A saved FlightAware answer that no longer fits (same flight number, a different trip later in the day)
//      is asked once more instead of leaving the route blank.
//    - A free route the board turned down isn't accepted a few minutes later just because the plane climbed.
//    - Board list: a plane with no aircraft type shows "--", like the route column.
//    - Route log: "not asked: high, passing by" when Which flights skipped a plane.
//
//  New in v0.9.39:
//    - Board footer reads "Source: ADSB + FlightAware" while Better Routes is on for the place shown.
//    - Aircraft details: "Route: FlightAware" (bottom right) on routes FlightAware supplied.
//    - Overhead alert icon in the header: a properly shaped airliner (swept wings, engines) over the ground line.
//    - Settings on a phone: the four "Use FlightAware for" buttons sit 2 x 2 instead of squeezing onto one row.
//
//  New in v0.9.38:
//    - Better Routes: "Use FlightAware for: Off / Home only / Home + airports / All places" (Home only by default).
//      Off pauses it with the key still saved.
//    - Never ask twice: the board remembers the last 128 flights it asked FlightAware about (4 hours) and reuses the
//      answer, even after its route memory has moved on. The Diagnostic Log counts the repeats avoided.
//    - One retry: when FlightAware has no airborne leg yet (a flight that just took off), the board asks once more
//      3 minutes later instead of giving up.
//    - A "~ RNO ->" guess is kept for that flight (20 minutes) once the board has made it, instead of vanishing the
//      moment the plane leaves the take-off / landing window. Dropped if the plane does the opposite (a go-around).
//    - Route log (for testing, on by default): the Diagnostic Log lists the last 60 route decisions, one line each:
//      the free route and why it passed or failed, what FlightAware said or why it wasn't asked, and what was shown.
//      Kept in memory only (cleared on restart); no positions.
//    - Phone pages: the chart's top and bottom fades are tighter (64 / 88 px) and eased.
//    - Credits & Legal: FlightAware AeroAPI under Where the Data Comes From; the trademark paragraph now follows
//      the FlightAware notice.
//
//  New in v0.9.37:
//    - Routes are checked harder before they get the plane icon: away from both ends the plane must head within 60 deg
//      of the destination (was 90), and a plane below 18,000 ft must be near one end of the route (a plane descending
//      through 13,000 ft over Reno isn't in the middle of San Jose -> Salt Lake). On a day of LAS traffic every route
//      these catch was wrong.
//    - Better Routes also asks FlightAware when the free database has no route at all (the "--" rows), not only when
//      its route looks wrong.
//    - Up to 8 FlightAware lookups a minute (was one every 7 s, so a busy airport got one a minute), and the monthly
//      limit is spread over the month (a bit more up front), so it doesn't run out in the first few days.
//
//  New in v0.9.36:
//    - Better Routes folds away like Firmware (closed by default); "On" glows when FlightAware is on; "FlightAware API key";
//      prices read "~$5 a month" (FlightAware sets them). Credits & Legal: a FlightAware notice (not affiliated, no payment,
//      your own agreement and fees), and Privacy lists what goes to FlightAware when you add a key.
//
//  New in v0.9.35:
//    - Better Routes (optional): Settings has a box for the owner's own FlightAware AeroAPI key (their free Personal
//      plan). When the free route for a flight doesn't match where the plane is, the board asks FlightAware for the
//      real route. Only those flights use a lookup; a monthly limit (250 / 1,000 / no limit, 1,000 by default, about
//      FlightAware's free $5) stops lookups until next month. The box shows this month's lookups, rough cost and
//      routes fixed; the Diagnostic Log does too. No key = exactly as before.
//
//  New in v0.9.34:
//    - Routes: a route that doesn't match the plane is no longer shown at all (it was, with "~"). Flight numbers get
//      reused on other routes (SkyWest especially), so "SKW3911 ~ MSP -> MDW" over Reno is now blank, and the detail
//      page says the route on file doesn't match. A route also has to point the way the plane is flying (away from
//      both ends), so a return trip or a reused number isn't taken for this flight. On a day of LAS traffic: 187
//      wrong "~" routes gone, plus 31 flights whose "confirmed" route was really flying the other way.
//
//  New in v0.9.33:
//    - Airport mode, helicopters: ARRIVING / DEPARTING only when one is actually coming down to or lifting off
//      from a pad near the field (tuned on a day of Las Vegas tour helicopters). One just passing by, or
//      hovering a few miles out, stays OVERFLIGHT, and a returning tour no longer shows DEPARTING.
//    - Web pages on iPhone: no more dark bars above and below the page. Safari (iOS 26) paints its bars with the
//      page's own color or a fixed element's, so: the page color is now the chart's own tone (light and dark), the
//      chart scrolls with the page, runs its whole length and fades into the page color at the top and bottom,
//      and nothing fixed or sticky gives Safari a color.
//    - Settings > Overhead Alert On / Off now updates the icon next to the clock straight away (it waited for a restart).
//    - Routes through airports whose airline code differs from the board's list (Mesa Gateway AZA/IWA, Kalispell
//      FCA/GPI, Yuma YUM/NYL, Palm Beach PBI, ...): the detail page showed "No published route for this flight"
//      even with the route on screen, and airport mode couldn't match them. The board now matches them by ICAO code.
//    - Settings page (computer): Time Zone & Clock moved to the right column, so the two columns end evenly and the
//      Radar LCD card no longer has a block of empty space. Board Setup footer is plain text (no link).
//
//  New in v0.9.32:
//    - Safety restart: every part of the board (data, web page, radar, screen) checks in with the chip's
//      watchdog. If one stops for 40 s the board restarts itself, and the Diagnostic Log names the part that
//      was stuck and what it was doing. Slow or dead data sources never cause this (each request has its own
//      time limit and the data part checks in between requests).
//    - Overhead alert status on the HOME page: a small plane over a ground line next to the clock, crossed
//      out when the alert is off.
//    - Routes: the "~" (and the plane mark for a confirmed route) now come first on the alert and detail
//      screens. "Route unknown" is now "No route on file".
//    - Routes on Home and in the alert, for airliners clearly taking off from or landing at a nearby airport
//      (seen doing so on two updates in a row, near an airport that airline routes around you use):
//      the real leg if that airport is on the flight's schedule, otherwise "~ Departing RNO Reno".
//    - Settings > Display > Screen updates: every 20, 40 or 60 s (60 s by default).
//
//  New in v0.9.31:
//    - Credits & Legal tidied: "Made by Volary Dynamics" is a slim bar with the logo (website + email on the right),
//      the Notice runs the full width, and the Warranty card no longer repeats who made the board.
//    - Instructions & Help: the "Questions or Ideas?" card is gone.
//
//  New in v0.9.30:
//    - Links to the maker: the Board Setup footer links to volarydynamics.com (v0.9.33: plain text again); Credits & Legal opens with a
//      "Made by Volary Dynamics" card (website + email); Instructions & Help ends with "Questions or ideas?".
//      (In setup mode the phone has no internet yet, so these links only work once the board is on Wi-Fi.)
//
//  New in v0.9.29:
//    - Sharper page background: the FAA sectional behind the web pages is now about twice the detail and
//      covers more country (Pyramid Lake to Walker Lake, Lake Tahoe to Fallon). Same soft look.
//
//  New in v0.9.28:
//    - Volary Dynamics: maker's name on the boot screens, the page footers and Credits & Legal (boot_screens.cpp changed).
//    - Settings > Time Zone: Clock 12-hour / 24-hour (the board's header clock).
//    - Settings > Firmware: "Reset to factory settings" tick box. The board's MENU question says so too; after the
//      install the new firmware starts as a new board (setup hotspot).
//    - The "Next, the page header shows v..." line after an install is gone.
//
//  New in v0.9.27:
//    - Smarter ARRIVING / DEPARTING / OVERHEAD / OVERFLIGHT (tuned on a day of recorded SFO and RNO traffic):
//      the board remembers each aircraft from the last update, so a plane rolling down the runway keeps its label
//      (it was OVERHEAD before); fast jets climbing or descending well above 6,000 ft near the field count;
//      small planes using a neighbouring airfield (San Carlos, Stead, ...) are no longer called arriving or
//      departing here; the flight schedule only decides when what the plane is doing doesn't.
//    - Uses the GPS climb rate when an aircraft doesn't send the barometric one.
//    - EXIT back to the first place shows "Refreshing..." in the header too, until its aircraft arrive.
//
//  New in v0.9.26:
//    - Help: "The E-Ink Paper Screen" is now "The E-Ink Screen".
//    - An aircraft with no flight number and no registration shows its transponder code in capitals (2B3EA0).
//
//  New in v0.9.25:
//    - Buttons: MENU, EXIT and the knob press are timed by the chip itself (press and release moments), so a tap
//      made while the screen is updating still counts (it acts as soon as the update ends), quick taps count, and
//      contact bounce can't make one press count twice. Holds work as before.
//    - MENU shows "Refreshing..." in the header right away, until the new data arrives.
//    - Settings: "Optional GC9A01 LCD, see Instructions for pinout" now sits under the Radar style selector.
//    - Settings > Firmware: the button says "Install" (a .bin can be newer or older), or "Install v..." once one is chosen;
//      before a file is picked it says "No file selected".
//    - Diagnostic Log: "E-paper:" is now "E-ink screen:".
//
//  New in v0.9.24:
//    - Board Setup: a saved WiFi password shows as dots (always 8, whatever its length) instead of "Leave blank to
//      keep the saved password". Leaving it alone still keeps the saved password; it is never sent to the page.
//    - Board Setup: the "Show password" box is gone (the saved password is never on the page to show).
//    - Board Setup: Your Home says street addresses aren't supported and latitude, longitude is the most precise.
//    - Settings: "Firmware Update" is now just "Firmware" (its "Installed v..." fits beside it on phones), and it
//      says "Flash from a .bin file. All settings are retained."
//    - Settings: Diagnostic Log now sits under Firmware, with System at the bottom.
//    - Help: the TCAS line now just says "+50" = 5,000 ft (no "above the ground there").
//    - Credits & Legal: the warranty paragraph ends "By using the device, you accept these terms."
//
//  New in v0.9.23:
//    - Settings save in place: the Save button shows Saving..., then "Saved" for 2 seconds, and you stay where you
//      were on the page. If the board can't be reached it says "Not saved - tap to try again" and keeps your changes.
//      (Browsers without JavaScript still get the old Saved page. Board Setup keeps its page: it restarts the board.)
//
//  v0.9.22 (test build, not released):
//    - Grey clock, take 4: before each clock update, load the panel's factory settings so the clock would use the
//      panel's own common voltage. Result: the white greyed again, as in v0.9.19 (loading the factory settings
//      does not set that voltage on this chip). Grey clock shelved; back to v0.9.18.
//
//  v0.9.21 (test build, not installed):
//    - Grey clock, take 3: longer push to black (40 frames) and a double push when leaving the flight list, to stop
//      faint clock digits showing on the next screen. Skipped: its longer pushes would have made v0.9.20's fading worse.
//
//  v0.9.20 (test build, not released):
//    - Grey clock, take 2: common voltage 0 V during clock updates. Result: the white stayed white, but the black
//      header bar slowly faded to dark grey, and faint clock digits showed on the details screen.
//
//  v0.9.19 (test build, not released):
//    - Grey clock test: the flight list clock drawn smooth (4 greys) with seconds, updated every 5 s, changing only
//      the clock's corner of the panel. Result: the digits changed cleanly, but every update slowly greyed the whole
//      screen (each update runs over every pixel, and the -1.6 V common voltage nudged them toward black).
//
//  New in v0.9.18:
//    - Faster compiles: the big data (boot screens, airports, page background, web fonts) moved from the .h files
//      into airports.cpp, bg_image.cpp, boot_screens.cpp and web_fonts.cpp. The IDE compiles those once and
//      reuses them, so changing the .ino only recompiles the .ino. Keep all the files together in the sketch folder.
//      Nothing else changed.
//
//  New in v0.9.17:
//    - No ghost of the boot screen on the first flight list: just before it, the screen fades to white in one
//      smooth step (our one-way waveform, no flash), then the list is drawn with a quick update on clean white
//    - Smooth boot screens are the only kind: Settings > Display > Boot screens is gone (the plain black/white
//      boot screen is used only if the board can't get memory for the grey picture)
//
//  New in v0.9.16:
//    - Smooth boot screens without the strobe: our own grey waveform pushes each pixel one way only (black,
//      white, or black then a short lighten for the greys), measured on the panel: dark grey 3 frames, light 5
//    - Smooth boot screens the right way up (they were 180 degrees off), and never lighter than Classic
//      (every pixel black on Classic stays black; greys only soften the outside edges)
//    - No flash going from the boot screen to the flight list: the board tells the panel what's on screen, so the
//      first update is a quick (partial) one instead of a full black-and-white refresh
//    - (v0.9.15 was a test build: 4 trial waveforms on a test card)
//
//  New in v0.9.14:
//    - Smooth boot screens (BETA): drawn in the panel's 4 grey levels, so slanted lines, curves and big text have
//      soft edges. Uses the 4-grey mode from Waveshare's 5.79" e-Paper demo (MIT licence). Settings > Display >
//      Boot screens: Smooth / Classic. boot_screens.h changed (grey copies of the 4 screens added).
//
//  New in v0.9.13:
//    - Boot screens: the PFD is busier (short final into RNO 16R at minimums: numbered tapes, flight director,
//      ILS dots, radio altitude, VSI needle, compass rose). boot_screens.h changed.
//    - Legal: total liability capped at one U.S. dollar
//
//  New in v0.9.12:
//    - Place names and callsigns always show as plain text on the web pages (text that looks like web code
//      can't run). Callsigns, registrations and types keep only letters, digits and dashes.
//    - Odd numbers from the flight feeds (headings below 0 or over 360, impossible positions, speeds or
//      heights) are cleaned up or skipped, so they can't crash the board. Place lookups that answer with an
//      impossible position are treated as "not found".
//
//  New in v0.9.11:
//    - Firmware Update must be allowed at the board: it asks "Allow firmware update?" and only MENU says yes
//      (EXIT or 60 s says no). Only the page that asked can then send the file, within 2 minutes, once.
//    - The board only takes commands from its own pages: requests sent by other websites (cross-site) or
//      under a strange address (DNS rebinding) are refused
//    - Firmware Update is a section that folds open, like the Diagnostic Log
//
//  New in v0.9.10:
//    - Settings > Firmware Update: install a new version from a .bin file over WiFi (Sketch > Export Compiled
//      Binary in the Arduino IDE makes it). WiFi, places and settings are kept. The page reads the version inside
//      the file first; files that aren't Flight Board firmware are refused; older versions install with a warning.
//      If the upload is cut off or the file is damaged, the board keeps the version it has.
//
//  New in v0.9.9:
//    - Saving Settings no longer starts the board over ("Scanning for aircraft near...") unless a scan range
//      changed. Radar look, radar brightness, overhead alert and web theme apply without touching the e-paper;
//      units, layout and time zone redraw the same flights; Screen upside down does one full refresh
//
//  New in v0.9.8:
//    - Radar LCD up to 25 frames a second (was 20); every frame still leaves a short gap for WiFi and the web pages
//    - Diagnostic Log shows the radar's real, measured frame rate
//
//  New in v0.9.7:
//    - Radar LCD faster again: the circle is sent in 16 bands (16 address windows instead of 240), each row
//      copied to internal RAM and handed to the SPI hardware in one burst
//    - Radar LCD overhead alert: smaller text that always fits ("OVERHEAD 0:45" / "PASSING"); the board has the detail
//
//  New in v0.9.6:
//    - Radar LCD faster: only the round part of each frame is sent (the corners are never seen), and the
//      wire runs at 40 MHz (was 26). If the picture glitches on long jumper wires, set RADAR_SPI_HZ back to 26000000
//
//  New in v0.9.5:
//    - Radar LCD: new Classic look (navy scope, lime rings, wide sweep, trails, pulsing red target on the
//      closest aircraft, big callsign) and a new "TCAS Traffic" look; both show the overhead alert on the scope.
//      Diagnostic Log shows the radar's frame rate
//    - Boot screens: busier HUD, bigger nav display; the data plate is retired (4 screens now)
//    - Web: Trademarks inside the warranty box; Help and Legal go back to Board Setup and have the button row;
//      Remote Control order (places, aircraft, then controls) in one heading style; WiFi chip no longer a link;
//      System gets a power icon; Save is a docked bar on desktop
//    - Settings > Display > Screen: Normal / Upside down (e-paper turned over, knob direction follows)
//    - Airline names: Air Canada Rouge (ROU), Porter (POE), Cargojet (CJT) built in (adsbdb had ROU wrong)
//
//  New in v0.9.4:
//    - Overhead alert (Home only): when an aircraft is about to pass close over Your Home, the board
//      shows where to look, how high, a sky view and a countdown to the closest point; also on the
//      radar LCD. Settings: on/off, distance, only aircraft below a height, heads-up time
//    - Web pages: Board Setup (WiFi, places, Remote Control) and a separate Settings page
//      (Display, Time Zone, Scan Range, Overhead Alert, Radar LCD Add-On, System, Diagnostic Log)
//    - Up to 8 airports; Other Places get their own scan range
//    - Units and Board layout are buttons; switching units updates every distance and height on the page
//
//  New in v0.9.3:
//    - Settings: an x in each saved airport, Your Home and Other Places box removes it from the board
//      straight away (no Save needed); Your Home offers Undo for 5 seconds
//    - Remote Control: shorter wording; its place buttons follow changes to your places
//
//  New in v0.9.2:
//    - "On the Board" is now "Remote Control": Prev / Next / Select / Back work like the knob and EXIT,
//      and the nearby aircraft are listed so any of them can be opened on the board; it follows the
//      board while open (place, layout, the aircraft on show)
//    - Help: radar wiring shows which header row VCC and GND are on again
//
//  New in v0.9.1:
//    - Diagnostic Log: why each data source last failed (too many requests, server error, timed out,
//      couldn't connect / low memory...), per-source counts, route lookup failures, PSRAM in use
//    - Restart log: also keeps the data feed totals, the last fetch failure and free memory from just
//      before a crash or watchdog restart
//    - Web pages are sent in pieces instead of one big block (less memory at once)
//    - Wording on all three web pages revised; "This Settings Page" help card removed
//
//  New in v0.9.0:
//    - "On the Board" (settings page, above Advanced): show any saved place on the e-paper,
//      refresh now, and switch between List + Radar and List, from a phone or computer
//
//  New in v0.8.9:
//    - Up to 7 airports (was 6)
//
//  New in v0.8.8:
//    - Computer layout: Airports stretches to line up with Time Zone (no gap above Other Places);
//      Your Home and Other Places are the same height
//
//  New in v0.8.7:
//    - Web pages: narrower centred layout on computers (980 px), smaller entry text (16 px phones, 15 px computers),
//      the same 16 px gap between all panels, no panels touching in the two-column pages
//    - Credits & Legal: the warning is a "Safety Notice" panel with a heading and icon
//
//  New in v0.8.6:
//    - New type on the web pages, stored on the board (web_fonts.h): Big Shoulders for titles,
//      Instrument Sans for text, Geist Mono for data and labels
//    - Lime SAVE button; dark mode: darker map, outlined Back button, no hint under Appearance
//    - Computer screens (900 px and wider): two-column settings, Advanced in two columns,
//      SAVE stays at the bottom of the window, Help and Credits & Legal in two columns
//
//  New in v0.8.5:
//    - Web pages: sharper look. Near-black header with range rings, corner brackets and a fine grid;
//      a status line (first place's position or SETUP, ONLINE / OFFLINE); square monospace chips;
//      the nearest aircraft's track and flight level in the corner; squarer cards and buttons
//    - Appearance: Light or Dark (Advanced), saved on the board, for every phone and computer
//
//  New in v0.8.4:
//    - "About, Credits & Legal" is now "Credits & Legal" (button and page)
//    - Page titles stay on one line on phones (size adapts to the screen width)
//
//  New in v0.8.3:
//    - Settings header: "Flight Board Setup & Settings", no subtitle
//    - Help and About pages: same header chips as the settings page (address, IP, WiFi, source, version, serial)
//
//  New in v0.8.2:
//    - Diagnostic Log: access point (BSSID) and channel, data feed requests per source, last fetch
//      time / size, route lookups, chip temperature, PSRAM, e-paper refresh counts, last deep clean,
//      clock sync, build info, and "since start" sky stats (unique aircraft, closest, highest, fastest)
//    - "Back to Settings"
//
//  New in v0.8.1:
//    - Help: plain-language ARRIVING / DEPARTING / OVERHEAD / OVERFLIGHT, plus an altitude tip
//
//  New in v0.8.0:
//    - "Scan again" for WiFi scans in the background: a spinner shows, only the network list
//      refreshes, nothing typed is lost; each network shows its signal in dBm
//    - Up to 6 airports (was 10)
//    - Settings and help: Title Case headings, clearer wording, "LCD" for the radar add-on,
//      a List row and a radius tip; layout list shows "List + Radar (default)" first
//    - List + radar: an aircraft's number can't spill into the footer or the ring legend
//
//  New in v0.7.9:
//    - New or factory-reset boards start in List + radar (switching is remembered until a factory reset)
//    - Hold MENU + EXIT 5 s also works on the setup screen
//
//  New in v0.7.8:
//    - List + radar: tighter trails, 3 dots at 15 / 30 / 45 seconds behind each arrow; the number
//      sits beside the plane, on the side away from its trail
//    - Settings: "Your WiFi" is now "WiFi"; the diagnostics have their own "Diagnostic log" section
//
//  New in v0.7.7:
//    - Hold MENU + EXIT 5 s while running: factory reset (asks first; press the knob to erase)
//    - Saving settings no longer restarts the board when the browser auto-fills the same WiFi password
//    - Diagnostics (own section from v0.7.8): why the board last restarted (power dip, crash, watchdog, power on...),
//      what it was doing, lowest free memory, spare task stack, WiFi disconnects and their reasons.
//      Also printed on the Serial Monitor at every start. Copy button for sending it along.
//
//  New in v0.7.6:
//    - Units setting (Advanced): Aviation (ft, kt, NM), US (ft, mph, mi) or Metric (m, km/h, km)
//      for the board, detail page, radar rings and the round radar; help page updated
//    - Scan radius is kept exact, so switching units shows the same distance (20 NM = 23 mi = 37 km)
//
//  New in v0.7.5:
//    - Web pages: all emoji replaced with drawn line icons (look the same on every
//      phone and computer); tag icon on the version, ID-card icon on the serial
//    - WiFi card: green "Connected" / red "Not connected" next to Network
//
//  New in v0.7.4:
//    - Settings header: computer icon for the IP, ID-card icon for the serial
//    - Help page updated: screen icons and start-up, aircraft details page, ring legend,
//      radar brightness, the settings page, weak-WiFi tip
//
//  New in v0.7.3:
//    - Always starts on HOME (or the first airport) after power-on
//    - List + radar: radar moved right (12 px margin), more room for the list, columns re-spaced;
//      ring distances moved out of the radar into the panel's bottom-left corner
//    - Detail page: instrument tiles (altitude, speed, heading, distance) and a route
//      progress bar with distance and time to go
//
//  New in v0.7.2:
//    - Fix: pressing the knob on the detail page went back to the board and then reopened
//      the details (press-down and release were both counted). It now acts once, on release.
//    - Plane header icon: no stray dot at the tail
//
//  New in v0.7.1:
//    - Header icon in front of the name: house (HOME), plane (airports), map pin (other places)
//    - 747 boot screen: cutaway side view with Boeing sections 41-48; windshield on the front view
//
//  New in v0.7.0:
//    - WiFi is its own card on the settings page (the WiFi chip jumps to it)
//    - Both layouts: columns spaced evenly
//    - List + radar: bigger, centred radar; "Scanning..." message stays in the left panel
//
//  New in v0.6.9:
//    - No blue loading bar (press + spinner stay); copyright back on the settings page
//    - New board layout "List + radar": numbered rows plus a radar scope with trails.
//      Hold the knob 1 s to switch (or Advanced > Board layout); in the help page
//    - Settings header: WiFi signal (dBm) and the data source; no serial on Legal
//
//  New in v0.6.8:
//    - Radar brightness setting (Advanced): 100 / 75 / 50 / 25 %
//    - "Back to settings" is a full button; buttons show a press, a spinner and a
//      progress bar while the board answers; save page returns after 5 s
//    - Saved / restarting / erasing pages in a banner and card
//    - Header chips no longer bold; copyright in a small pill
//    - Legal page: data sources tidied, IP-address line removed
//
//  New in v0.6.7:
//    - Legal page: even spacing under the "Not for navigation" box
//    - Page background shows more of Lake Tahoe on phones
//
//  New in v0.6.6:
//    - Web pages: Reno VFR sectional chart as the background, gradient banner with a
//      radar drawing, every section on its own card with an icon
//    - "Serial" label on the serial chip; Trademarks after Privacy; e-paper help reworded
//
//  New in v0.6.5:
//    - Footer: "7 of 10 Aircraft" when more are in range than fit on the board
//    - Radar add-on is Off until someone picks a look (kept once saved)
//    - Settings page: slate banner with address, IP, WiFi, version and serial
//    - New pages on the board: Instructions & Help, About & Legal (credits,
//      licenses, privacy, warranty and liability)
//
//  New in v0.6.4:
//    - Radar: labels no longer pile up (nearest aircraft first, overlaps skipped);
//      ring distances on the diagonal; softer sweep
//    - Radar drill-down: the aircraft open on the detail page turns yellow with a ring,
//      big yellow callsign plus distance and altitude; the others dim
//    - Settings page: version and serial at the top; after saving, counts down 10 s
//      and returns to settings by itself
//
//  New in v0.6.3:
//    - Radar add-on: 1.28" round screen (GC9A01) shows the aircraft around the current
//      place. Three looks (Classic green, ATC scope, Amber sweep) or Off, chosen on the
//      settings page. Needs the "Adafruit GC9A01A" library (Library Manager).
//
//  New in v0.6.2:
//    - Web pages show the firmware version and serial number at the bottom
//    - Footer: "9 Aircraft"
//
//  New in v0.6.1:
//    - "aircraft" instead of "planes", "WiFi" spelling, "setup portal", clearer hints
//    - House icon next to HOME; detail page says "of CABIN" for other places
//    - Time zone picker (all US zones) on the setup portal and settings page
//    - Setup hotspot: random password made once per board, address 10.47.0.1
//    - Starts on the screen you were last looking at
//    - "No places set up" screen instead of quietly watching Reno
//    - Factory reset: hold MENU + EXIT while plugging in, or from the settings page (v0.7.7: or hold both 5 s)
//      (Advanced), which also has a Restart button
//    - Fix: Cessna 172s (and 177, 402, Beech 1900, Bell 212...) were flagged as military,
//      because "C17" matched the start of "C172". Type codes now must match exactly.
//
//  New in v0.6.0:
//    - Airport mode: when a plane is clearly landing or taking off at your airport
//      but its schedule shows another trip, the board shows your airport as that
//      end of the leg ("BOS -> RNO", "-> RNO") and says how sure it is
//
//  New in v0.5.9:
//    - WiFi signal bars in the footer
//    - No full-screen flash on button presses: full refresh only with an automatic
//      update, at most every 15 min; nightly deep clean at 1 AM; panel powered off
//      between updates
//
//  New in v0.5.8:
//    - Fixed: pressing the knob opened a plane's details and closed them again
//
//  New in v0.5.7:
//    - Routes drawn with an arrow: OAK -> RNO
//    - Settings page on your home WiFi: hold EXIT 3 s to see the address
//      (http://flightboard-xxxx.local); place changes apply without a restart
//
//  New in v0.5.6:
//    - Setup: optional HOME (15 NM), up to 10 airports (10 NM, search as you type),
//      up to 3 other places (cabin, office...). Old settings move over automatically.
//    - 7,805 airports: every US public airport incl. Alaska & Hawaii + major international
//    - Header shows "SEA Seattle" (code + city); names never cut mid-word
//    - Any airline's name (from adsbdb); military / DoW aircraft show as "Secret Squirrel"
//
//  New in v0.5.5:
//    - Setup page: longitude can be typed with a minus on iPhone (or as "119.6 W")
//
//  New in v0.5.4:
//    - Easter eggs on every boot screen
//    - Setup screen: one QR code (opens the setup page) and clearer steps
//
//  New in v0.5.3:
//    - DejaVu Sans Bold on the board (fully hinted: crisp on e-paper)
//    - Five boot screens in turn: HUD, 747 drawing, PFD, nav display, data plate
//      (the data plate shows a serial number unique to each board)
//    - Airport mode: OVERHEAD when a plane is within 1 NM of the airport
//    - Footer items spaced evenly across the screen
//
//  New in v0.5.2:
//    - Font strokes fitted to the pixel grid so every letter has the same weight
//    - Boot screen stays up 15 s; footer shows the WiFi network name
//
//  New in v0.5.1:
//    - Board shows 7 planes (smaller row font); new GS KT and HDG columns
//    - Header: "DEN Denver Intl - 10NM Scan Radius"; all distances in NM
//    - Switching places: "Scanning for planes near Denver Intl (DEN)..."
//
//  New in v0.5:
//    - Boot screen stays up 12 s (press any button to skip)
//    - Font redrawn so every character has the same stroke weight
//
//  New in v0.4 (v3 had QR setup, places, airport mode and the detail page):
//    - Boot screen alternates between a HUD and an engineering drawing
//    - Up to 10 extra places for the knob; each can be an airport code,
//      a ZIP code, a town, or "latitude, longitude", with an optional name
//    - 657 built-in airports: every public airport in NV, CA, OR, WA, CO
//      plus major US and international airports
//    - Footer: "Source: API ADSB", the date, and "Pg. 1/11"
//    - Switching places shows "Scanning for planes near ..."
//
//  Buttons: knob up/down = change place, knob press = plane details,
//           MENU = refresh, EXIT = first place, hold EXIT 3 s = settings address,
//           hold MENU 5 s = setup mode
//
//  Files in this sketch folder (all needed): flight_board.ino, the tabs 01_helpers.ino ... 22_setup_loop.ino,
//    airports.cpp/.h, bg_image.cpp/.h, boot_screens.cpp/.h, web_fonts.cpp/.h, fonts.h, qr_code.h
//  Libraries: GxEPD2 (+ Adafruit GFX), ArduinoJson v7, Adafruit GC9A01A (radar add-on)
//  Board: "ESP32S3 Dev Module", USB CDC On Boot: Disabled, Flash 8MB,
//         Partition "8M with spiffs (3MB APP/1.5MB SPIFFS)", PSRAM "OPI PSRAM"
//  Serial Monitor: 115200 baud
// =====================================================================

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <Update.h>          // Firmware Update on the Settings page
#include <ArduinoJson.h>
#include <SPI.h>
#include <GxEPD2_BW.h>
#include <Adafruit_GC9A01A.h>     // radar add-on (Library Manager: "Adafruit GC9A01A")
#include <time.h>
#include <math.h>
#include <initializer_list>
#include <esp_heap_caps.h>   // free-memory figures for Diagnostics
#include <esp_task_wdt.h>    // v0.9.32: safety restart if any part of the board stops
#include "soc/soc.h"          // REG_READ: the buttons' interrupt reads the pins directly (safe while flash is busy)
#include "soc/gpio_reg.h"
#include "qr_code.h"
#include "fonts.h"
#include "airports.h"
#include "boot_screens.h"
#include "bg_image.h"        // page background (Reno sectional chart)
#include "web_fonts.h"       // fonts for the web pages

const char* FW_VERSION = "0.9.47";        // shown on the boot screens
// Marks a .bin file as Flight Board firmware: Firmware Update looks for it. Keep the version the same as above.
const char FW_MARK[] = "FLIGHTBOARD-FW:0.9.47;";
const int  FW_MARK_PRE = 15;              // length of "FLIGHTBOARD-FW:"

// ================= SETTINGS =================
const int  BOOT_SCREEN_MS     = 15000; // how long the boot screen stays up (any button skips it)
int        fetchSeconds       = 60;    // how often to update: 20, 40 or 60 s (Settings > Display > Screen updates)
const int  UPD_SECONDS[3]     = { 20, 40, 60 };
const int  FULL_REFRESH_MIN   = 15;    // full (flashing) refresh at most this often, only with an automatic update
const int  DEEP_CLEAN_HOUR    = 1;     // nightly deep clean against ghosting at 1 AM...
const int  DEEP_CLEAN_IDLE_MIN = 5;    // ...once nobody has touched the board for this long
const int  CLIMB_FPM          = 300;   // faster than this counts as climbing/descending
const int  AIRPORT_LOW_AGL_FT = 6000;
const float OVERHEAD_NM       = 1.0;   // airport mode: closer than this and not arriving/departing = OVERHEAD  // airport mode: below this (above the field) = arriving/departing
const int  KNOB_SETTLE_MS     = 600;   // wait this long after the last knob click before switching place
const int  DETAIL_SETTLE_MS   = 350;   // same, on the detail page
const int  DETAIL_TIMEOUT_S   = 120;   // detail page goes back to the board after this long untouched
const int  SETUP_HOLD_S       = 5;     // hold MENU this long while running to start setup
const int  SETTINGS_HOLD_MS   = 3000;  // hold EXIT this long to show the settings address
#define SETUP_IP "10.47.0.1"              // the board's own setup hotspot (unlikely to clash with a home network)

// US time zones for the picker: name on the page, rule for the clock
struct TimeZone { const char* label; const char* posix; };
const TimeZone TIME_ZONES[] = {
  { "Eastern",                     "EST5EDT,M3.2.0,M11.1.0" },
  { "Central",                     "CST6CDT,M3.2.0,M11.1.0" },
  { "Mountain",                    "MST7MDT,M3.2.0,M11.1.0" },
  { "Arizona (no daylight saving)", "MST7" },
  { "Pacific",                     "PST8PDT,M3.2.0,M11.1.0" },
  { "Alaska",                      "AKST9AKDT,M3.2.0,M11.1.0" },
  { "Hawaii",                      "HST10" },
  { "Puerto Rico / Virgin Islands", "AST4" },
  { "Guam / Northern Mariana Islands", "ChST-10" },
  { "American Samoa",              "SST11" },
};
const int NUM_TIME_ZONES = sizeof(TIME_ZONES) / sizeof(TIME_ZONES[0]);
const int TZ_DEFAULT     = 4;             // Pacific
int tzIndex = TZ_DEFAULT;
// (tzRule() is further down: the Arduino IDE puts its made-up function declarations
//  above the first function in the file, so no function may come before the structs)
const char* USER_AGENT = "Mozilla/5.0 (compatible; FlightBoard/0.9.47; +https://volarydynamics.com)";
// ============================================

// ---------- CrowPanel 5.79" pins ----------
#define EPD_PWR   7
#define EPD_SCK   12
#define EPD_MOSI  11
#define EPD_CS    45
#define EPD_DC    46
#define EPD_RST   47
#define EPD_BUSY  48

#define BTN_EXIT  1
#define BTN_MENU  2
#define ROT_DOWN  4
#define ROT_PRESS 5
#define ROT_UP    6

// Radar add-on (1.28" round GC9A01) on the 2x10 header
#define RADAR_SCK  15
#define RADAR_MOSI 17
#define RADAR_DC   21
#define RADAR_CS   9

const uint8_t ROTATION = 0;             // normal; 2 = upside down (Settings > Display > Screen)
bool clock24 = false;                    // v0.9.28: 24-hour clock in the header
bool screenFlip = false;                 // the board is mounted the other way up
bool greyFadePending = false;            // a grey boot screen is up: fade to white before the first flight list

GxEPD2_BW<GxEPD2_579_GDEY0579T93, GxEPD2_579_GDEY0579T93::HEIGHT>
  display(GxEPD2_579_GDEY0579T93(EPD_CS, EPD_DC, EPD_RST, EPD_BUSY));

// =====================================================================
//  Structs and tables (above the functions that use them)
// =====================================================================

struct Airline { const char* icao; const char* shortName; const char* fullName; };  // short max 10 chars
const Airline AIRLINES[] = {
  {"AAL","American","American Airlines"}, {"UAL","United","United Airlines"},
  {"DAL","Delta","Delta Air Lines"},      {"SWA","Southwest","Southwest Airlines"},
  {"ASA","Alaska","Alaska Airlines"},     {"JBU","JetBlue","JetBlue"},
  {"NKS","Spirit","Spirit Airlines"},     {"FFT","Frontier","Frontier Airlines"},
  {"HAL","Hawaiian","Hawaiian Airlines"}, {"AAY","Allegiant","Allegiant Air"},
  {"SCX","SunCountry","Sun Country"},     {"MXY","Breeze","Breeze Airways"},
  {"VXP","Avelo","Avelo Airlines"},       {"SKW","SkyWest","SkyWest Airlines"},
  {"QXE","Horizon","Horizon Air"},        {"ENY","Envoy","Envoy Air"},
  {"RPA","Republic","Republic Airways"},  {"EDV","Endeavor","Endeavor Air"},
  {"JIA","PSA","PSA Airlines"},           {"ASH","Mesa","Mesa Airlines"},
  {"GJS","GoJet","GoJet Airlines"},       {"CPZ","Compass","Compass Airlines"},
  {"AWI","Air Wisc","Air Wisconsin"},     {"PDT","Piedmont","Piedmont Airlines"},
  {"UPS","UPS","UPS Airlines"},           {"FDX","FedEx","FedEx Express"},
  {"ABX","ABX Air","ABX Air"},            {"GTI","Atlas Air","Atlas Air"},
  {"CKS","Kalitta","Kalitta Air"},        {"ATN","ATI","Air Transport Intl"},
  {"AMF","Ameriflght","Ameriflight"},     {"EJA","NetJets","NetJets"},
  {"LXJ","Flexjet","Flexjet"},            {"XOJ","XOJet","XOJet"},
  {"JSX","JSX","JSX"},                    {"CNS","Contour","Contour Airlines"},
  {"SIL","Silver","Silver Airways"},      {"ACA","Air Canada","Air Canada"},
  {"JZA","Jazz","Air Canada Jazz"},       {"WJA","WestJet","WestJet"},
  {"FLE","Flair","Flair Airlines"},       {"TSC","Transat","Air Transat"},
  {"AMX","Aeromexico","Aeromexico"},      {"VOI","Volaris","Volaris"},
  {"VIV","VivaAero","VivaAerobus"},       {"BAW","British","British Airways"},
  {"VIR","Virgin Atl","Virgin Atlantic"}, {"AFR","Air France","Air France"},
  {"KLM","KLM","KLM"},                    {"DLH","Lufthansa","Lufthansa"},
  {"SWR","Swiss","Swiss"},                {"IBE","Iberia","Iberia"},
  {"EIN","Aer Lingus","Aer Lingus"},      {"ICE","Icelandair","Icelandair"},
  {"SAS","SAS","SAS"},                    {"FIN","Finnair","Finnair"},
  {"THY","Turkish","Turkish Airlines"},   {"UAE","Emirates","Emirates"},
  {"QTR","Qatar","Qatar Airways"},        {"ETD","Etihad","Etihad Airways"},
  {"ANA","ANA","All Nippon Airways"},     {"JAL","JAL","Japan Airlines"},
  {"KAL","Korean Air","Korean Air"},      {"AAR","Asiana","Asiana Airlines"},
  {"CPA","Cathay","Cathay Pacific"},      {"EVA","EVA Air","EVA Air"},
  {"CAL","China Air","China Airlines"},   {"SIA","Singapore","Singapore Airlines"},
  {"QFA","Qantas","Qantas"},              {"ANZ","Air NZ","Air New Zealand"},
  {"PAL","Philippine","Philippine Airlines"}, {"CES","China East","China Eastern"},
  {"CSN","China Sth","China Southern"},   {"CCA","Air China","Air China"},
  {"FJI","Fiji","Fiji Airways"},          {"LAN","LATAM","LATAM"},
  {"AVA","Avianca","Avianca"},            {"CMP","Copa","Copa Airlines"},
  {"CFG","Condor","Condor"},              {"EDW","Edelweiss","Edelweiss Air"},
  {"ROU","AC Rouge","Air Canada Rouge"},  {"POE","Porter","Porter Airlines"},
  {"CJT","Cargojet","Cargojet"},
};
const int NUM_AIRLINES = sizeof(AIRLINES) / sizeof(AIRLINES[0]);

struct TypeName { const char* code; const char* name; };   // friendly model names
const TypeName TYPE_NAMES[] = {
  {"A319","A319"}, {"A320","A320"}, {"A20N","A320neo"}, {"A321","A321"}, {"A21N","A321neo"},
  {"A332","A330-200"}, {"A333","A330-300"}, {"A339","A330-900neo"}, {"A359","A350-900"},
  {"A35K","A350-1000"}, {"A388","A380"}, {"BCS1","A220-100"}, {"BCS3","A220-300"},
  {"B712","717"}, {"B737","737-700"}, {"B738","737-800"}, {"B739","737-900"},
  {"B37M","737 MAX 7"}, {"B38M","737 MAX 8"}, {"B39M","737 MAX 9"}, {"B3XM","737 MAX 10"},
  {"B744","747-400"}, {"B748","747-8"}, {"B752","757-200"}, {"B753","757-300"},
  {"B763","767-300"}, {"B764","767-400"}, {"B772","777-200"}, {"B77L","777-200LR"},
  {"B77W","777-300ER"}, {"B788","787-8"}, {"B789","787-9"}, {"B78X","787-10"},
  {"CRJ2","CRJ-200"}, {"CRJ7","CRJ-700"}, {"CRJ9","CRJ-900"}, {"E170","E170"},
  {"E75L","E175"}, {"E75S","E175"}, {"E190","E190"}, {"E195","E195"}, {"E290","E190-E2"},
  {"DH8D","Dash 8-400"}, {"AT76","ATR 72-600"}, {"MD11","MD-11"},
  {"C172","172 Skyhawk"}, {"C182","182 Skylane"}, {"C208","208 Caravan"},
  {"PC12","PC-12"}, {"SR22","SR22"}, {"B350","King Air 350"}, {"BE20","King Air 200"},
};
const int NUM_TYPE_NAMES = sizeof(TYPE_NAMES) / sizeof(TYPE_NAMES[0]);

// A place to watch: HOME, an airport, or another place (cabin, office...)
struct Place {
  char   name[24];      // shown in the header: "HOME", "SEA Seattle", "CABIN"
  char   longName[48];  // for "Scanning for planes near ...": "Seattle-Tacoma Intl (SEA)"
  char   code[5];       // airport code ("" if not an airport)
  char   gkey[6];       // where a ZIP/town lookup is saved ("hg", "og0".."og2")
  double lat, lon;
  float  radiusNm;      // scan radius, NM (kept exact so switching units shows the same distance)
  bool   isAirport;
  int    elevFt;
  bool   resolved;      // false until a ZIP/town has been looked up
  bool   customName;    // name typed on the setup page (don't replace it)
  char   query[48];     // what was typed, for looking up a ZIP/town
};
const int MAX_AIRPORTS_SET = 8;           // airports on the knob
const int MAX_OTHER        = 3;           // other places on the knob
const int MAX_PLACES = 1 + MAX_AIRPORTS_SET + MAX_OTHER;
Place places[MAX_PLACES];
int   numPlaces = 0;

enum PlaneStatus : int8_t { ST_NONE, ST_ARRIVING, ST_DEPARTING, ST_OVERFLIGHT, ST_OVERHEAD };
// fits = 2: the plane is clearly landing or taking off at your airport, but the
// schedule shows a different trip. Your airport becomes that end of the leg.
enum LegNote : int8_t { LEG_NONE, LEG_RETURN, LEG_OTHER_END_UNKNOWN, LEG_MISMATCH };   // v0.9.34: MISMATCH = route on file, not this flight

struct Plane {
  uint32_t icao;        // 24-bit aircraft address (the "hex"), for counting unique aircraft
  char   callsign[9];
  char   reg[11];
  char   type[5];
  long   altFt;
  int    climbRate;     // feet per minute
  bool   rateOk;        // v0.9.27: a climb rate was sent (or worked out from the last update)
  float  gs;            // knots
  float  track;         // degrees
  double lat, lon;
  float  distNm;        // nautical miles from the place
  float  bearing;       // from you, 0 = north
  // filled in by the background task after the route lookup
  int8_t fits;          // 1 = route fits, 0 = doubtful, -1 = unknown, 2 = one end seen here
  int8_t legNote;       // why fits = 2 (LEG_...), for the detail page
  char   fromCode[5], toCode[5];
  char   fromCity[22], toCity[22];
  int8_t status;        // airport mode
  bool   routeFa;       // v0.9.39: the route shown came from FlightAware
  int8_t near;          // v0.9.32, Home / other places: 1 = landing at, 2 = taking off from a nearby airport (in from/toCode)
  bool   mil;           // military / DoW: shown as "Secret Squirrel"
  bool   heli;          // v0.9.33: helicopter (ADS-B category A7, or a helicopter type code)
  int16_t squawk;       // v0.9.44: transponder code as its 4 digits (7700), -1 = not sent
  char   airline[40];   // airline name from adsbdb (any carrier)
};

// Remembered routes: up to 3 airports per flight (origin, stopover, destination)
struct RouteCache {
  char   callsign[9];
  int    n;
  char   code[3][5];
  char   city[3][22];
  double lat[3], lon[3];
  char   airline[40];   // airline name, if adsbdb knows it
  bool   faTried;       // v0.9.35: FlightAware already asked about this flight
  bool   fromFa;        // v0.9.35: the route came from FlightAware
  uint32_t freeBadMs;   // v0.9.40: when the free route was last turned down (0 = never)
};

// Airline names looked up by their 3-letter code (for flights without a known route)
struct AirlineCache { char icao[4]; char name[40]; };
const int AIRLINE_CACHE_SIZE = 30;
AirlineCache airlineCache[AIRLINE_CACHE_SIZE];
int airlineCacheNext = 0;
const int CACHE_SIZE = 40;
RouteCache routeCache[CACHE_SIZE];
// v0.9.35: Better Routes (FlightAware), see faRoute()
int  faUseI = 1;                             // v0.9.38 "fau": 0 off, 1 Home only, 2 Home + airports, 3 all places
int  faWhichI = 1;                           // v0.9.40 "faw": 0 all flights, 1 low + overhead, 2 low only
int  faDetailI = 1;                          // v0.9.40 "fad": 1 = look up a plane when its details are opened
char faAskCs[9] = "";                        // v0.9.40: the plane whose details were just opened...
volatile uint32_t faAskMs = 0;               // ...and when (the network task asks FlightAware for it)
const long FA_LOW_FT = 18000, FA_VS_FPM = 1000;     // v0.9.40: "low" = below this, or climbing / descending this fast
const uint32_t FREE_BAD_MS = 30UL * 60000UL;        // v0.9.40: a free route turned down stays turned down this long
struct Place; const Place* faPlace = nullptr;       // v0.9.40: the place addRoute() is working for
bool faPlaceOk = true;                       // set per place by addRoute()
uint32_t faRepeats = 0;                      // lookups saved by the memory below, since power-on
// v0.9.38: never ask twice. What FlightAware said about each flight, kept 4 hours (PSRAM).
enum : uint8_t { FAM_NONE, FAM_ROUTE, FAM_NOROUTE, FAM_NOTYET };
struct FaMemo { char cs[9]; uint8_t state, tries; uint32_t ms; char from[5], to[5]; };
const int FAMEMO_N = 128;
const uint32_t FAMEMO_KEEP_MS = 4UL * 3600000UL, FA_RETRY_MS = 180000UL;
FaMemo* faMemo = nullptr;
int faMemoNext = 0;
// v0.9.38: route log, the last route decisions (PSRAM, cleared on restart, no positions)
struct RLog { time_t t; char cs[9]; char type[6]; int32_t alt; float nm; char freeR[48]; char fa[52]; char shown[24]; };   // v0.9.46: fa 40 -> 52 ("free was wrong, fixed" was cut off)
const int RLOG_N = 60;
RLog* rlog = nullptr;
int rlogNext = 0, rlogCount = 0;
bool rlogOn = true;                          // "rlo" = 1 turns it off
char rlFree[48], rlFa[52], fitWhy[28];       // filled while one plane's route is worked out
void faMemoClear();
void formatAlt(long a, char* buf, size_t len);
void rlogText(String& s);
char faKey[72] = "";                         // from Settings ("fak"); never sent back to any page
int  faLimitI = 1;                           // in use: 0 = 250, 1 = 1,000, 2 = no limit, 3 = Full (safety stop, no pacing)
const int FA_LIMITS[4] = { 250, 1000, 0, 10000 };
int  faPlanI = 0;                            // v0.9.43 "fap": 0 Light, 1 Full, 2 Custom
int  faWhichC = 1, faLimitC = 1;             // v0.9.43: the Custom choices ("faw", "fal")
char faUrgentCs[9] = ""; uint32_t faUrgentMs = 0;   // v0.9.43: the overhead alert plane (skips the pacing)
const float FA_COST = 0.005f;                // dollars per lookup, list price
const int FA_PER_MIN = 8;                    // v0.9.37: Personal plan allows 10 a minute; keep under it
unsigned long faTimes[FA_PER_MIN] = { 0 };   // when the last few lookups were made
int faTimesNext = 0;
uint32_t faMonth = 0, faCount = 0, faFixed = 0;   // this month (YYYYMM), lookups, routes fixed ("fam", "fan", "faf")
bool faLoaded = false;
int  faLastErr = 0;                          // last answer: 200, 401/403 = key not accepted, 0 = none yet
unsigned long faLastAt = 0;
int cacheNext = 0;

const int MAX_PLANES     = 60;
const int ROUTE_LOOKUPS  = 10;   // closest planes that get a route (and can be opened in detail)
const int ROWS           = 7;    // rows on the board

// ---------- Shared between the background task and the screen ----------
SemaphoreHandle_t dataLock;
struct Shared {
  Plane    planes[MAX_PLANES];
  int      count;
  bool     ok;
  char     status[64];
  char     updated[16];
  int      source;
  uint32_t gen;          // bumps every time new data is ready
} shared;

volatile int      curPlace  = 0;
volatile uint32_t placeGen  = 0;     // bumps when the place changes (old fetches are thrown away)
volatile bool     fetchNow  = true;
unsigned long     menuRefreshAt = 0;        // v0.9.25: MENU pressed: header says "Refreshing..." until new data (0 = no)
const unsigned long MENU_REFRESH_MAX_MS = 20000;   // give up showing it after this (no network, say)

const char* SOURCE_NAMES[] = { "API ADSB", "API ADSB", "API Airplanes" };   // adsb.lol, adsb.fi, airplanes.live

// ---------- Overhead alert (Home only) ----------
// The settings are saved as which button was picked; what each button means depends on the units shown.
const float ALERT_DIST_NM[3][3]   = { { 1, 2, 3 }, { 1 / 1.15078f, 2 / 1.15078f, 3 / 1.15078f }, { 2 / 1.852f, 4 / 1.852f, 6 / 1.852f } };
const char* ALERT_DIST_LABEL[3][3] = { { "1 NM", "2 NM", "3 NM" }, { "1 mi", "2 mi", "3 mi" }, { "2 km", "4 km", "6 km" } };
const long  ALERT_BELOW_FT[3][5]  = { { 5000, 10000, 15000, 20000, 0 }, { 5000, 10000, 15000, 20000, 0 },
                                      { 4921, 9843, 14764, 19685, 0 } };          // 0 = any height; metric = 1,500-6,000 m
const char* ALERT_BELOW_LABEL[3][5] = { { "5,000", "10,000", "15,000", "20,000", "Any" }, { "5,000", "10,000", "15,000", "20,000", "Any" },
                                        { "1,500", "3,000", "4,500", "6,000", "Any" } };
const int   ALERT_WARN_S[3]    = { 30, 60, 120 };   // heads-up: the countdown starts this long before the closest point
const float ALERT_FETCH_NM     = 15;     // extra look around Home (only when the board shows another place)
const int   ALERT_NOW_S        = 5;      // this close to the closest point: "NOW"
const int   ALERT_PASS_S       = 20;     // then "passing" this long, then back to the board
const unsigned long ALERT_REDRAW_MS = 5000;           // countdown redraw (quick refresh)
const unsigned long ALERT_REPEAT_MS = 10UL * 60000UL; // the same aircraft alerts at most once in 10 minutes
const char* UNIT_SHORT[3] = { "Aviation", "US", "Metric" };
bool alertOn = true;
int  alertDistI = 1, alertBelowI = 1, alertWarnI = 1;   // 2 NM, below 10,000 ft, 1 minute
struct AlertInfo {
  bool  valid;
  Plane p;                  // the aircraft, as last reported
  float x, y, vx, vy;       // from Home: NM east / north, and NM per second
  float tca;                // seconds from 'at' to the closest point
  float dmin;               // NM at the closest point
  long  homeFt;             // Home's ground height (nearest airport's), ft
  unsigned long at;         // millis() when the position was current
};
AlertInfo alertShared;                    // from the background task (under dataLock)
volatile uint32_t alertGen = 0;           // bumps when alertShared changes
AlertInfo alertCur;                       // main loop: the alert on show (or about to be)
uint32_t  alertSeenGen = 0;
uint32_t  lastAlertIcao = 0;
unsigned long lastAlertMs = 0, alertDrawnMs = 0;
uint32_t  alertsShown = 0;                // since start, for the Diagnostic Log

// ---------- v0.9.44: squawk codes ----------
int  emergI = 2;                          // "oe": emergency alerts 0 off, 1 scan area, 2 50 NM, 3 150 NM, 4 300 NM
bool sqTagsOn = true;                     // "ot": FIRE / SAR / MIL tags on the board
const float EMERG_NM[5] = { 0, 0, 50, 150, 300 };
const unsigned long EMERG_POLL_MS = 30000;            // one squawk code asked about every 30 s
const unsigned long EMERG_SHOW_MS = 60000;            // the emergency screen stays this long
struct Emerg {
  uint32_t icao; int16_t code; uint8_t hits;          // hits: separate updates it was seen in (2 = confirmed)
  uint32_t firstMs, lastMs;
  float homeNm, homeBrg;                              // from Home
  bool  routed;                                       // route looked up
  Plane p;
};
const int MAX_EMERG = 6;
Emerg emergShared[MAX_EMERG]; int emergSharedN = 0;   // from the background task (under dataLock)
volatile uint32_t emergGen = 0;
Emerg emergCur[MAX_EMERG]; int emergCurN = 0;         // main loop's copy
Emerg emergShow;                                      // the one on the emergency screen
uint32_t emergAlerted[8]; int emergAlertedN = 0;      // alerted already (once per flight)
unsigned long emergShownMs = 0;
uint32_t emergsShown = 0;

// ---------- Screen state (main loop only) ----------
Plane    view[MAX_PLANES];
int      viewCount = 0;
bool     viewOk = false;
char     viewStatus[96] = "Scanning for aircraft...";
char     viewUpdated[16] = "--:--";
int      viewSource = 0;
uint32_t seenGen = 0;

enum View { V_BOARD, V_DETAIL, V_SETTINGS, V_ALERT, V_EMERG };   // v0.9.44: V_EMERG
View     screenView = V_BOARD;
int      detailIdx = 0;
char     detailCallsign[9] = "";
unsigned long lastInputMs = 0;
unsigned long lastFullMs = 0;       // last full (flashing) refresh
int      lastCleanDay = -1;          // day of the year of the last nightly deep clean

int      knobDelta = 0;              // clicks waiting to be applied
unsigned long lastKnobMs = 0;

// ---------- Buttons (counted by interrupts, so presses during a screen refresh aren't lost) ----------
enum { B_MENU, B_EXIT, B_UP, B_DOWN, B_PRESS, NUM_BTN };
const uint8_t BTN_PINS[NUM_BTN] = { BTN_MENU, BTN_EXIT, ROT_UP, ROT_DOWN, ROT_PRESS };
volatile uint16_t btnCount[NUM_BTN];
volatile uint32_t btnLastMs[NUM_BTN];
// Knob press and MENU only count again once the button has been let go for a moment.
// (Contacts "bounce" on release; a bounce during a screen refresh used to count as a second press.)
volatile bool     btnArmed[NUM_BTN] = { true, true, true, true, true };
const bool        BTN_NEEDS_RELEASE[NUM_BTN] = { true, false, false, false, true };   // MENU, EXIT, UP, DOWN, PRESS
const uint32_t    RELEASE_MS = 80;           // must read "released" this long before the next press counts
uint32_t          btnUpSince[NUM_BTN];
portMUX_TYPE btnMux = portMUX_INITIALIZER_UNLOCKED;
// v0.9.25: MENU, EXIT and the knob press. The interrupt runs on every edge and notes when the button went down and
// when it came back up, so the loop can be busy (a screen update takes up to ~4 s) without missing a tap, and can still
// tell a tap from a hold afterwards. A press only counts if the button had been up, steadily, for RELEASE_MS
// (contacts bounce for a few ms on press and on release). A press is finished once it has stayed up RELEASE_MS.
struct BtnPress { uint16_t ms; bool holdUsed; };      // a finished press: how long it was down; did a hold act on it
struct BtnTrack {
  uint32_t downAt, upAt;                              // last press start, last edge back up
  bool     down, upSeen, holdUsed;                    // a press is going on; it has come up since; a hold acted on it
  uint16_t presses;                                   // presses started (MENU acts on these)
  uint8_t  nDone; BtnPress done[4];                   // finished presses not yet handled
};
volatile BtnTrack btr[NUM_BTN];
unsigned long menuDownSince = 0;
unsigned long bothDownSince = 0;            // MENU + EXIT held together (factory reset)
const int     RESET_HOLD_S  = 5;

// ---------- Setup mode ----------
Preferences prefs;
WebServer   server(80);
DNSServer   dns;
String      apName, apPass;
String      networkOptions;
unsigned long restartAt = 0;
String      wifiName;                    // shown in the footer
String      hostName;                    // "flightboard-a4b8": settings page at http://flightboard-a4b8.local
bool        noPlaces = false;            // nothing set up: show "No places set up"
int8_t      restartKind = 0;             // what restartAt does: RESTART_...
enum { RESTART_WIFI = 1, RESTART_PLAIN, RESTART_FACTORY, RESTART_UPDATE };
enum HeaderIcon : int8_t { ICON_NONE, ICON_HOUSE, ICON_PLANE, ICON_PIN };   // icon in front of the header name

// ---- Radar add-on settings and shared data (functions are further down) ----
const int RADAR_SIZE = 240, RADAR_C = 120, RADAR_R = 114;   // screen, centre, scope radius
const uint32_t RADAR_SPI_HZ = 40000000;   // 40 MHz; if the picture glitches on long jumper wires, use 26000000
const uint16_t RADAR_SWEEP_MS = 4000;     // one turn of the sweep
const uint16_t RADAR_FRAME_MS = 40;       // 25 frames a second at most
const uint16_t RADAR_GAP_MS   = 4;        // always rest this long between frames (WiFi, web pages, e-paper)
const uint8_t  RADAR_ROTATION = 0;        // 0-3: turn the picture if the screen is mounted sideways
const char* RADAR_STYLES[] = { "Off / Not Connected", "Classic Radar", "ATC Scope", "Amber Sweep", "TCAS Traffic", "ATC Data Blocks", "Modern TRACON" };
const int   NUM_RADAR_STYLES = 7;
const int   RADAR_DEFAULT = 0;            // Off until someone picks a look (and it's saved)
volatile int radarStyle = RADAR_DEFAULT;
volatile int radarBright = 100;            // percent: the screen has no backlight control, so colours are drawn darker

SPIClass radarSPI(HSPI);                  // second SPI bus: the e-paper keeps the first one to itself
Adafruit_GC9A01A radarTft(&radarSPI, RADAR_DC, RADAR_CS, -1);   // -1: reset by command (RST not wired)
GFXcanvas16* radarCanvas = nullptr;
bool radarStarted = false;

struct RadarBlip { char cs[9]; char type[5]; float x, y, gs, trk, dist; long alt; int vs; bool airliner; int16_t squawk; uint32_t icao; };   // v0.9.47: icao (round screen)   // v0.9.46: squawk   // v0.9.44: type   // x east, y north, in NM; vs ft/min
struct RadarSnap {
  RadarBlip b[MAX_PLANES];
  int n, sel;                             // sel = aircraft open on the detail page, or -1
  float range;
  char name[24];
  unsigned long t0;                       // when these positions were current (millis)
  bool active;                            // false: nothing to show (no places)
  bool alert;                             // overhead alert on show: the sky view instead
  float ax, ay, avx, avy, atca, atrk;     // the alert aircraft (see AlertInfo)
  long  aAbove; int aClimb;               // ft above Home when reported, ft/min
  unsigned long aAt;
  char  acs[9];
  long  ground;                           // ground height at the place shown, ft (TCAS heights are above this)
  bool  atHome;                           // the board is showing Home (alert: draw the others too)
  int   aIdx;                             // the alert aircraft among b[], or -1
  struct { char code[5]; float x, y; } ap[6]; int nAp;   // v0.9.46: nearby airports (NM east / north), for Modern TRACON
};
RadarSnap radarShared, radarLocal;
volatile uint32_t radarDrawMs = 0, radarSendMs = 0, radarFrames = 0, radarPeriodMs = 0;   // frame timing (Diagnostic Log)
SemaphoreHandle_t radarLock;
unsigned long saveScreenAt = 0;          // remember the current screen a few seconds after the knob stops
bool        setupMode = false;           // true while the board runs its own setup WiFi
volatile bool placesChanged = false;     // settings page saved new places: reload them
volatile bool settingsChanged = false;   // settings page saved display/radar/alert settings only: apply in place
volatile bool fwUpdating = false;        // a firmware file is being received: fetching pauses, the board says so
char fwNewVer[16] = "";                  // version just installed (for the restart message)
enum { FWA_NONE, FWA_ASK, FWA_YES, FWA_NO };
volatile uint8_t fwAsk = FWA_NONE;       // Firmware Update asking at the board / allowed / refused
volatile unsigned long fwAskAt = 0, fwYesAt = 0;
volatile uint32_t fwAskIp = 0;           // only the computer that asked may send the file
bool fwAskReset = false;                 // v0.9.28: the page asked for a factory reset with this install
bool fwResetAfter = false;               // ...and it was installed: erase before restarting
char fwAskVer[16] = "";                  // version the page says it will send (shown on the board)
bool fwShown = false;                    // a Firmware Update message is on the e-paper
const unsigned long FW_ASK_MS = 60000;   // time to press MENU
const unsigned long FW_YES_MS = 120000;  // after MENU, time for the file to start arriving
volatile int8_t webShow = -1;             // "Remote Control": place to show (-1 = none asked)
volatile int8_t webKey = 0;               // "Remote Control": a button press, as if on the board (WK_...)
volatile int8_t webAircraft = -1;         // "Remote Control": aircraft to open in details (-1 = none asked)
enum WebKey : int8_t { WK_NONE, WK_PREV, WK_NEXT, WK_SELECT, WK_BACK };
volatile int8_t webLayout = -1;           // "Remote Control": layout to switch to (-1 = none asked)
volatile bool   webRefresh = false;       // "Remote Control": refresh now

// Screen layout
const int16_t ROW_Y0 = 92, ROW_STEP = 22;            // baselines of the plane rows
// "List + radar" layout: numbered rows on the left, a radar scope on the right
// Columns spaced evenly (about 21 px between the widest entries in each column);
// the radar keeps a 12 px margin from the right edge
const int16_t A_BADGE = 12, A_FLIGHT = 49, A_AIRLINE = 153, A_TYPE = 283, A_ICON = 352, A_ROUTE = 370,
              A_ALT_R = 546, A_ARROW = 551, A_DIVIDER = 576, A_SCOPE_X = 682, A_SCOPE_Y = 144, A_SCOPE_R = 90;   // centred between header and footer
const int  LAYOUT_HOLD_MS = 1000;                    // hold the knob this long to switch layouts
// Units shown on the board, radar and settings. Inside, the board always works in feet, knots and NM.
enum UnitsMode : uint8_t { U_AVIATION, U_US, U_METRIC };
struct UnitSet {
  const char* label;                      // settings dropdown
  const char *alt, *spd, *dist;           // after numbers: "ft", "kt", "NM"
  const char *altH, *spdH, *distH;        // column headers
  float altK, spdK, distK;                // multiply ft / kt / NM by these
  float rings[3];                         // radar rings, in these units
};
const UnitSet UNIT_SETS[3] = {
  { "Aviation: feet, knots, nautical miles", "ft", "kt",   "NM", "FT", "KT",   "NM", 1.0f,    1.0f,     1.0f,     { 2, 5, 10 } },
  { "USCS: feet, mph, miles",                  "ft", "mph",  "mi", "FT", "MPH",  "MI", 1.0f,    1.15078f, 1.15078f, { 2, 5, 10 } },
  { "Metric: metres, km/h, kilometres",      "m",  "km/h", "km", "M",  "KM/H", "KM", 0.3048f, 1.852f,   1.852f,   { 5, 10, 20 } },
};
uint8_t unitsMode = U_AVIATION;
#define UN (UNIT_SETS[unitsMode])

// ---- Diagnostics ----
// A few notes in RTC memory: they survive a crash, watchdog or software restart (not a power cut),
// so after a crash the board can say what it was doing.
struct Crumb {
  uint32_t magic;
  uint32_t upS;                                         // seconds since start
  uint32_t minHeap;                                     // lowest free internal memory, bytes
  uint16_t stackNet, stackWeb, stackRadar, stackLoop;   // least spare stack seen, bytes
  uint16_t wifiDrops;                                   // WiFi disconnects (and failed tries) this run
  uint8_t  wifiReason;                                  // the router's reason for the last one
  uint8_t  netStep, webStep, loopStep;                  // what each part was doing
  uint8_t  planned;                                     // PLANNED_... when the board restarted itself
  uint16_t feedOk, feedFail;                            // aircraft fetches this run
  int16_t  lastErr;                                     // last fetch failure (HTTP code, or <0 = ours)
  uint8_t  lastErrSrc;                                  // which source (0-2), 9 = route lookup
  uint32_t lastErrUpS;                                  // when (seconds since start)
  uint32_t heapNow, largestNow;                         // free internal memory and largest block, now
  uint32_t pet[4];                                      // v0.9.32: when each part last checked in (s since start): data, web, radar, screen
  uint8_t  radarStep, netSrc;                           // v0.9.32: what the radar was doing; which data source
};
RTC_NOINIT_ATTR Crumb crumb;
const uint32_t CRUMB_MAGIC = 0xFB093201;                // changed with the layout, so old notes aren't misread
enum NetStep  : uint8_t { NS_IDLE, NS_WIFI, NS_LOOKUP, NS_FETCH, NS_ROUTES, NS_OVERHEAD, NS_SQUAWK };   // v0.9.45: last two
enum WebStep  : uint8_t { WS_IDLE, WS_PAGE };
enum LoopStep : uint8_t { LS_IDLE, LS_DRAW, LS_CLEAN };
enum RadarStep : uint8_t { RS_IDLE, RS_DRAW };
enum PetSlot : uint8_t { PET_NET, PET_WEB, PET_RADAR, PET_LOOP };
enum Planned  : uint8_t { PLANNED_NONE, PLANNED_WIFI, PLANNED_BUTTON, PLANNED_SETUP, PLANNED_SETUP_SAVED, PLANNED_FACTORY, PLANNED_UPDATE };
// One line of the restart log, newest first, saved in settings as "diag"
struct DiagEntry {
  uint32_t when;                                        // unix time of the restart (0 = not known)
  uint32_t upS, minHeap;                                // about the run before it (0 = not known)
  uint16_t stackNet, stackWeb, stackRadar, stackLoop;
  uint16_t wifiDrops;
  uint8_t  reason, planned, wifiReason, netStep, webStep, loopStep;
  uint8_t  crumbOk;
  uint8_t  lastErrSrc;
  int16_t  lastErr;
  uint16_t feedOk, feedFail;
  uint32_t lastErrAgoS;                                 // how long before the restart the last failure was
  uint32_t heapNow, largestNow;
  uint8_t  stuck;                                       // v0.9.32: safety restart: 1 + PetSlot of the part that stopped
  uint8_t  radarStep, netSrc;
};
const int DIAG_N = 5;
DiagEntry diagLog[DIAG_N];
volatile uint16_t wifiDropsNow = 0;
volatile uint8_t  wifiReasonNow = 0;
volatile uint32_t wifiDropAt = 0, wifiUpAt = 0;
int      wifiUpRssi = 0;
bool     diagStamped = false;
TaskHandle_t netTaskH = nullptr, webTaskH = nullptr, radarTaskH = nullptr, loopTaskH = nullptr;
// v0.9.32: safety restart. Each part checks in with the chip's task watchdog; if one doesn't for WDT_S
// seconds, the board restarts and the Diagnostic Log names it. Each data request has its own (shorter) time
// limit and the data part checks in between requests, so slow or dead data sources never trigger this.
const uint32_t WDT_S = 40;
void wdtPet() {
  TaskHandle_t me = xTaskGetCurrentTaskHandle();
  int slot = !me ? -1 : me == netTaskH ? PET_NET : me == webTaskH ? PET_WEB : me == radarTaskH ? PET_RADAR : me == loopTaskH ? PET_LOOP : -1;
  if (slot < 0) return;                                 // a part that isn't watched (setup, say)
  esp_task_wdt_reset();
  crumb.pet[slot] = millis() / 1000;
}
void wdtWatchMe() { esp_task_wdt_add(nullptr); wdtPet(); }
// Counters for the Diagnostic Log (memory only; they start again at every restart)
struct NetStats {
  uint32_t requests, ok[3], fail[3];             // per data source
  uint32_t lastAt, lastMs, lastBytes;            // the last successful fetch
  int8_t   lastSource = -1;
  uint32_t routesAsked, routesFromMemory, routesOnline;
  int16_t  lastErr[3];                           // why each source last failed (see failText)
  uint32_t lastErrAt[3], limited[3], lastErrLargest[3];
  uint32_t routesFailed, routesUnknown;          // adsbdb: failed vs "no route known"
  int16_t  routeLastErr; uint32_t routeLastErrAt;
  uint32_t faAsked;                              // v0.9.35: FlightAware lookups since power-on
};
const int16_t ERR_GARBLED = 1000;                // "lastErr" for a reply that wasn't valid JSON
const int16_t ERR_SLOW    = 1001;                // v0.9.45: the answer trickled in too slowly, or was cut off
volatile int16_t  lastHttpCode = 0;              // from httpGet: HTTP status, or <0 for a client-side error
volatile uint32_t lastHttpLargest = 0;           // largest free internal block when it failed
NetStats netStats;
const int SEEN_MAX = 1024;                       // unique aircraft remembered since start
struct SkyStats {
  uint32_t seen[SEEN_MAX];
  int      unique;
  char     closestCs[9], closestAt[24]; float closestNm = 1e9f;
  char     highestCs[9];  long  highestFt;
  char     fastestCs[9];  float fastestKt;
};
SkyStats sky;
uint32_t epdQuick = 0, epdFull = 0;              // e-paper refreshes since start
time_t   lastCleanAt = 0, clockSetAt = 0;

uint8_t webTheme = 0;                              // settings pages: 0 light, 1 dark
int boardLayout = 0;                                 // 0 = full list, 1 = list + radar
const char* SOURCE_WEB[] = { "adsb.lol", "adsb.fi", "airplanes.live" };
// Columns spaced evenly (about 30 px between the widest entries in each column)
const int16_t C_FLIGHT = 12, C_AIRLINE = 125, C_TYPE = 264, C_ICON = 342, C_ROUTE = 360,
              C_ALT_R = 546, C_ARROW = 551, C_GS_R = 628, C_HDG = 658, C_DIST_R = 778;

