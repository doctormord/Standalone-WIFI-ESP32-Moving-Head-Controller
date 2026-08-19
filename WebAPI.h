#pragma once
#include <Arduino.h>

extern void loadAllChaserScenes();

static File fsUploadFile; 

void setupAPI() {
  
  if (!LittleFS.exists("/index.html")) {
    server.on("/", HTTP_GET, []() {
      String html = "<!DOCTYPE html><html><head><title>Setup Mode</title></head><body style='background:#121212;color:white;text-align:center;'><h2>SYSTEM SETUP</h2><p>No index.html found. Please upload Web-GUI.</p><form method='POST' action='/upload_gui' enctype='multipart/form-data'><input type='file' name='update'><input type='submit' value='INSTALL'></form></body></html>";
      server.send(200, "text/html", html);
    });

    server.on("/upload_gui", HTTP_POST, []() {
      server.send(200, "text/plain", "Successful! Rebooting...");
      delay(1000); ESP.restart();
    }, []() {
      HTTPUpload& upload = server.upload();
      if (upload.status == UPLOAD_FILE_START) { if (LittleFS.exists("/index.html")) LittleFS.remove("/index.html"); fsUploadFile = LittleFS.open("/index.html", "w"); }
      else if (upload.status == UPLOAD_FILE_WRITE) { if (fsUploadFile) fsUploadFile.write(upload.buf, upload.currentSize); }
      else if (upload.status == UPLOAD_FILE_END) { if (fsUploadFile) fsUploadFile.close(); }
    });
  } else {
    server.serveStatic("/", LittleFS, "/index.html");
  }

  // React/ReactDOM/Babel, stored gzip-compressed on LittleFS and served locally so the
  // UI works with no internet uplink (e.g. the WiFi AP fallback at a venue with no WLAN).
  server.on("/vendor/react.js", []() {
    File f = LittleFS.open("/vendor/react.production.min.js.gz", "r");
    if (!f) { server.send(404, "text/plain", "not found"); return; }
    // streamFile() detects the ".gz" filename itself and adds the
    // Content-Encoding header automatically — an explicit sendHeader() call
    // here would duplicate it (seen live: "Content-Encoding: gzip, gzip",
    // which makes browsers attempt to gunzip the body twice and fail).
    server.streamFile(f, "application/javascript");
    f.close();
  });
  server.on("/vendor/react-dom.js", []() {
    File f = LittleFS.open("/vendor/react-dom.production.min.js.gz", "r");
    if (!f) { server.send(404, "text/plain", "not found"); return; }
    // streamFile() detects the ".gz" filename itself and adds the
    // Content-Encoding header automatically — an explicit sendHeader() call
    // here would duplicate it (seen live: "Content-Encoding: gzip, gzip",
    // which makes browsers attempt to gunzip the body twice and fail).
    server.streamFile(f, "application/javascript");
    f.close();
  });
  server.on("/vendor/babel.js", []() {
    File f = LittleFS.open("/vendor/babel.min.js.gz", "r");
    if (!f) { server.send(404, "text/plain", "not found"); return; }
    // streamFile() detects the ".gz" filename itself and adds the
    // Content-Encoding header automatically — an explicit sendHeader() call
    // here would duplicate it (seen live: "Content-Encoding: gzip, gzip",
    // which makes browsers attempt to gunzip the body twice and fail).
    server.streamFile(f, "application/javascript");
    f.close();
  });

  server.on("/api/get_dmx", []() {
    String json; json.reserve(4000);
    json += "{";
    json += "\"1\":" + String((int)dimSmoothTarget) + ",";
    for (int i = 2; i <= 18; i++) json += "\"" + String(i) + "\":" + String(dmxData[i]) + ",";
    json += "\"cp\":" + String(centerPan16) + ",\"ct\":" + String(centerTilt16) + ",\"bpm\":" + String(globalBPM) + ",\"pr\":" + String(activePresetSlot) + ",\"chA\":" + String(chaserActive ? 1 : 0) + ",";
    json += "\"pn\":["; for(int i=0; i<10; i++) { json += "\"" + presetNames[i] + "\"" + (i<9?",":""); } json += "],";
    json += "\"dSm\":" + String(dimSmoothVal) + ",\"fO\":" + String(fadeStateOut ? 1 : 0) + ",";
    json += "\"mB\":" + String(masterBrightness) + ",";
    json += "\"dip\":" + String(dipToBlack ? 1 : 0) + ",\"hwA\":" + String(hwAudioEnabled?1:0) + ",";

    json += "\"fA\":" + String(moveFX.active?1:0) + ",\"fT\":" + String(moveFX.type) + ",\"fR\":" + String(moveFX.rot) + ",\"fTr\":" + String(moveFX.trigger) + ",\"fSy\":" + String(moveFX.sync) + ",\"fSS\":" + String(moveFX.spdSt) + ",\"fSE\":" + String(moveFX.spdEn) + ",\"fZS\":" + String(moveFX.szSt) + ",\"fZE\":" + String(moveFX.szEn) + ",\"fMM\":" + String(moveFX.modMo) + ",\"fMC\":" + String(moveFX.modCu) + ",\"fMS\":" + String(moveFX.modSp) + ",";
    json += "\"dA\":" + String(dimFX.active?1:0) + ",\"dSt\":" + String(dimFX.startVal) + ",\"dEn\":" + String(dimFX.endVal) + ",\"dMo\":" + String(dimFX.mode) + ",\"dCu\":" + String(dimFX.curve) + ",\"dSp\":" + String(dimFX.speed) + ",\"dTr\":" + String(dimFX.trigger) + ",\"dSy\":" + String(dimFX.sync) + ",";
    json += "\"grA\":" + String(gRotFX.active?1:0) + ",\"grSt\":" + String(gRotFX.startVal) + ",\"grEn\":" + String(gRotFX.endVal) + ",\"grMo\":" + String(gRotFX.mode) + ",\"grCu\":" + String(gRotFX.curve) + ",\"grSp\":" + String(gRotFX.speed) + ",\"grTr\":" + String(gRotFX.trigger) + ",\"grSy\":" + String(gRotFX.sync) + ",";
    json += "\"prA\":" + String(pRotFX.active?1:0) + ",\"prSt\":" + String(pRotFX.startVal) + ",\"prEn\":" + String(pRotFX.endVal) + ",\"prMo\":" + String(pRotFX.mode) + ",\"prCu\":" + String(pRotFX.curve) + ",\"prSp\":" + String(pRotFX.speed) + ",\"prTr\":" + String(pRotFX.trigger) + ",\"prSy\":" + String(pRotFX.sync) + ",";
    json += "\"cA\":" + String(colFX.active?1:0) + ",\"cSt\":" + String(colFX.startVal) + ",\"cEn\":" + String(colFX.endVal) + ",\"cHo\":" + String(colFX.holdTime) + ",\"cTr\":" + String(colFX.trigger) + ",\"cSy\":" + String(colFX.sync) + ",";
    json += "\"sgA\":" + String(sgobFX.active?1:0) + ",\"sgSt\":" + String(sgobFX.startVal) + ",\"sgEn\":" + String(sgobFX.endVal) + ",\"sgHo\":" + String(sgobFX.holdTime) + ",\"sgTr\":" + String(sgobFX.trigger) + ",\"sgSy\":" + String(sgobFX.sync) + ",\"sgSc\":" + String(sgobFX.scratch?1:0) + ",\"sgSp\":" + String(sgobFX.scratchSpeed) + ",\"sgRng\":" + String(sgobFX.scratchRange) + ",";
    json += "\"rgA\":" + String(rgobFX.active?1:0) + ",\"rgSt\":" + String(rgobFX.startVal) + ",\"rgEn\":" + String(rgobFX.endVal) + ",\"rgHo\":" + String(rgobFX.holdTime) + ",\"rgTr\":" + String(rgobFX.trigger) + ",\"rgSy\":" + String(rgobFX.sync) + ",\"rgSc\":" + String(rgobFX.scratch?1:0) + ",\"rgSp\":" + String(rgobFX.scratchSpeed) + ",";
    json += "\"chSS\":" + String(chaserStartSlot) + ",\"chES\":" + String(chaserEndSlot) + ",\"chF\":" + String(fadeTime) + ",\"chH\":" + String(holdTime) + ",\"chTr\":" + String(chaserTrigger) + ",\"chSy\":" + String(chaserSync) + ",\"chOrd\":" + String(chaserOrder) + ",\"chFTr\":" + String(chaserFadeTrigger) + ",\"chFSy\":" + String(chaserFadeSync);
    json += "}"; server.send(200, "application/json", json);
  });

  server.on("/api/state", []() {
    String json; json.reserve(600);
    json += "{\"pr\":" + String(activePresetSlot) + ",\"bpm\":" + String(globalBPM) + ",\"chA\":" + String(chaserActive?1:0);
    json += ",\"hwA\":" + String(hwAudioEnabled?1:0) + ",\"fO\":" + String(fadeStateOut?1:0);
    json += ",\"trB\":" + String(guiBass?1:0) + ",\"trM\":" + String(guiMid?1:0) + ",\"trH\":" + String(guiHigh?1:0);
    json += ",\"pn\":["; for(int i=0; i<10; i++) { json += "\"" + presetNames[i] + "\"" + (i<9?",":""); } json += "]}"; 
    server.send(200, "application/json", json);
    guiBass = false; guiMid = false; guiHigh = false;
  });

  server.on("/save", []() {
    int s = server.arg("slot").toInt();
    if (s < 1 || s > 10) { server.send(400, "text/plain", "invalid slot"); return; }
    String n = server.arg("n");
    presetNames[s-1] = n;
    
    dmxData[CH_DIMMER] = (byte)dimSmoothTarget;
    dmxData[CH_PAN] = (byte)(centerPan16 >> 8); dmxData[CH_PAN_FINE] = (byte)(centerPan16 & 0xFF);
    dmxData[CH_TILT] = (byte)(centerTilt16 >> 8); dmxData[CH_TILT_FINE] = (byte)(centerTilt16 & 0xFF);

    if(colFX.active) dmxData[CH_COLOR] = wheelMap[constrain(colFX.currentIdx, 0, 19)];
    if(sgobFX.active) dmxData[CH_GOBO] = sGoboMap[constrain(sgobFX.currentIdx, 0, 9)];
    if(rgobFX.active) dmxData[CH_GOBO_ROT] = rGoboMap[constrain(rgobFX.currentIdx, 0, 6)];

    SceneData sd;
    memset(&sd, 0, sizeof(SceneData));

    for(int i=1; i<=18; i++) sd.dmx[i] = dmxData[i];
    
    sd.fA = moveFX.active; sd.fT = moveFX.type; sd.fR = moveFX.rot; 
    sd.fSS = moveFX.spdSt; sd.fSE = moveFX.spdEn; sd.fZS = moveFX.szSt; sd.fZE = moveFX.szEn; 
    sd.fMM = moveFX.modMo; sd.fMC = moveFX.modCu; sd.fMS = moveFX.modSp; 
    sd.fTr = moveFX.trigger; sd.fSy = moveFX.sync;

    sd.dA = dimFX.active; sd.dSt = dimFX.startVal; sd.dEn = dimFX.endVal; 
    sd.dMo = dimFX.mode; sd.dCu = dimFX.curve; sd.dSp = dimFX.speed; 
    sd.dTr = dimFX.trigger; sd.dSy = dimFX.sync;

    sd.grA = gRotFX.active; sd.grSt = gRotFX.startVal; sd.grEn = gRotFX.endVal; 
    sd.grMo = gRotFX.mode; sd.grCu = gRotFX.curve; sd.grSp = gRotFX.speed; 
    sd.grTr = gRotFX.trigger; sd.grSy = gRotFX.sync;

    sd.prA = pRotFX.active; sd.prSt = pRotFX.startVal; sd.prEn = pRotFX.endVal; 
    sd.prMo = pRotFX.mode; sd.prCu = pRotFX.curve; sd.prSp = pRotFX.speed; 
    sd.prTr = pRotFX.trigger; sd.prSy = pRotFX.sync;

    sd.cA = colFX.active; sd.cSt = colFX.startVal; sd.cEn = colFX.endVal; 
    sd.cHo = (uint32_t)colFX.holdTime; sd.cTr = colFX.trigger; sd.cSy = colFX.sync;

    sd.sgA = sgobFX.active; sd.sgSt = sgobFX.startVal; sd.sgEn = sgobFX.endVal; 
    sd.sgHo = (uint32_t)sgobFX.holdTime; sd.sgTr = sgobFX.trigger; sd.sgSy = sgobFX.sync; sd.sgSc = sgobFX.scratch;

    sd.rgA = rgobFX.active; sd.rgSt = rgobFX.startVal; sd.rgEn = rgobFX.endVal; 
    sd.rgHo = (uint32_t)rgobFX.holdTime; sd.rgTr = rgobFX.trigger; sd.rgSy = rgobFX.sync; sd.rgSc = rgobFX.scratch;

    prefs.begin(("sc" + String(s)).c_str(), false);
    prefs.clear();
    prefs.putString("n", n);
    size_t written = prefs.putBytes("data", &sd, sizeof(SceneData));
    prefs.end();

    if (written != sizeof(SceneData)) { server.send(500, "text/plain", "storage error"); return; }

    // sd already holds exactly what was just persisted, and presetNames[s-1]
    // was set above — update in-memory state directly instead of reloading
    // all 10 NVS slots from flash.
    chaserScenes[s - 1] = sd;
    server.send(200, "OK");
  });

  server.on("/joy_cfg", []() {
    joyMaxSpeed = constrain(server.arg("spd").toInt(), 1, 20000);
    joyCurve = constrain(server.arg("crv").toFloat(), 0.0f, 5.0f);
    joyMomentum = constrain(server.arg("mom").toFloat(), 0.0f, 99.0f) / 100.0f;
    joyPanRev = server.arg("pr") == "1"; joyTiltRev = server.arg("tr") == "1";
    auto axisLimits = [](const String& minArg, const String& maxArg, int &minOut, int &maxOut) {
      minOut = constrain(minArg.toInt(), 0, 255) << 8;
      maxOut = (constrain(maxArg.toInt(), 0, 255) << 8) | 0xFF;
      if (minOut > maxOut) { int t = minOut; minOut = maxOut; maxOut = t; }
    };
    axisLimits(server.arg("pmin"), server.arg("pmax"), panMinLimit, panMaxLimit);
    axisLimits(server.arg("tmin"), server.arg("tmax"), tiltMinLimit, tiltMaxLimit);
    prefs.begin("sys", false);
    prefs.putInt("j_msp", joyMaxSpeed); prefs.putFloat("j_crv", joyCurve); prefs.putFloat("j_mom", joyMomentum);
    prefs.putBool("j_prv", joyPanRev); prefs.putBool("j_trv", joyTiltRev);
    prefs.putInt("j_pmi", panMinLimit); prefs.putInt("j_pma", panMaxLimit);
    prefs.putInt("j_tmi", tiltMinLimit); prefs.putInt("j_tma", tiltMaxLimit);
    prefs.end();
    server.send(200, "OK");
  });

  server.on("/joy_in", []() {
    joyInputX = server.arg("x").toFloat(); 
    joyInputY = server.arg("y").toFloat();
    server.send(200, "OK");
  });

  server.on("/set_all", []() {
    chaserActive = false; activePresetSlot = 0;
    for (int i = 1; i <= 18; i++) {
      String arg = "c" + String(i);
      if (server.hasArg(arg)) {
        int v = constrain(server.arg(arg).toInt(), 0, 255);
        dmxData[i] = (byte)v;
        if(i==CH_DIMMER) { dimFX.stop(); dimSmoothTarget = v; }
        if(i==CH_PAN) centerPan16 = (v << 8) | (centerPan16 & 0xFF);
        if(i==CH_PAN_FINE) centerPan16 = (centerPan16 & 0xFF00) | v;
        if(i==CH_TILT) centerTilt16 = (v << 8) | (centerTilt16 & 0xFF);
        if(i==CH_TILT_FINE) centerTilt16 = (centerTilt16 & 0xFF00) | v;
      }
    }
    server.send(200, "OK");
  });

  server.on("/fx", []() {
      bool startFresh = !moveFX.active && (server.arg("a") == "1");
      moveFX.active = (server.arg("a") == "1");
      moveFX.type = server.arg("t").toInt(); moveFX.rot = server.arg("r").toFloat();
      // Clamped to 1 (not 0): a 0% size collapses the movement pattern's amplitude to a single point
      // (fixture sits static at dead center) with the FX still reporting "running" -- looks identical
      // to a stuck/broken FX. Frontend sliders already enforce min=1, this is defense-in-depth for
      // values that can also arrive via NVS-persisted presets, per this project's established pattern.
      moveFX.spdSt = constrain(server.arg("ss").toInt(), 1, 100); moveFX.spdEn = constrain(server.arg("se").toInt(), 1, 100);
      moveFX.szSt = constrain(server.arg("zs").toInt(), 1, 100); moveFX.szEn = constrain(server.arg("ze").toInt(), 1, 100);
      moveFX.modMo = server.arg("mm").toInt(); moveFX.modCu = server.arg("mc").toInt();
      moveFX.modSp = server.arg("ms").toFloat(); moveFX.trigger = server.arg("tr").toInt();
      moveFX.sync = constrain(server.arg("sy").toInt(), 0, 7);
      if(startFresh) moveFX.start(); else if (!moveFX.active) moveFX.stop();
      server.send(200, "OK");
  });

  server.on("/modfx", []() {
      String pfx = server.arg("pfx");
      Modulator* m = nullptr;
      int ch = -1; // CH9/CH11 for gr/pr -- dim restores via dimSmoothTarget instead, see below
      if (pfx == "dim") m = &dimFX; else if (pfx == "gr") { m = &gRotFX; ch = 9; } else if (pfx == "pr") { m = &pRotFX; ch = 11; }
      if(m) {
        bool startFresh = !m->active && (server.arg("a") == "1");
        m->active = (server.arg("a") == "1");
        m->startVal = server.arg("st").toInt(); m->endVal = server.arg("en").toInt();
        m->speed = server.arg("sp").toFloat(); m->mode = server.arg("mo").toInt();
        m->curve = server.arg("cu").toInt(); m->trigger = server.arg("tr").toInt();
        m->sync = constrain(server.arg("sy").toInt(), 0, 6);
        if(startFresh) m->start(); else if (!m->active) m->stop();
        // On stop, land on the Programmer tab's manual value immediately instead of leaving the
        // modulator's last live output in place. For gr/pr that means writing CH9/CH11 directly; for
        // dim, updateEngines() reads dimSmoothTarget (not dmxData[CH_DIMMER]) as its restore target,
        // since dimFX.process() had been overwriting dimSmoothTarget itself every tick while active.
        // Reported live 2026-08-18: dimmer/gobo-rot/prism-rot FX "changes not taking" after stop.
        if (!m->active && server.hasArg("mv")) {
          int mv = constrain(server.arg("mv").toInt(), 0, 255);
          if (ch >= 0) dmxData[ch] = (byte)mv; else dimSmoothTarget = mv;
        }
      }
      server.send(200, "OK");
  });

  server.on("/chaser", []() {
    bool startFresh = !chaserActive && (server.arg("act") == "1");
    chaserActive = (server.arg("act") == "1");
    if (server.hasArg("start")) chaserStartSlot = constrain(server.arg("start").toInt(), 0, 9);
    if (server.hasArg("end")) chaserEndSlot = constrain(server.arg("end").toInt(), 0, 9);
    if (chaserStartSlot > chaserEndSlot) { int t = chaserStartSlot; chaserStartSlot = chaserEndSlot; chaserEndSlot = t; }
    if (server.hasArg("fade")) fadeTime = server.arg("fade").toInt();
    if (server.hasArg("hold")) holdTime = server.arg("hold").toInt();
    if (server.hasArg("trg")) chaserTrigger = server.arg("trg").toInt();
    if (server.hasArg("sync")) chaserSync = constrain(server.arg("sync").toInt(), 0, 6);
    if (server.hasArg("ord")) chaserOrder = server.arg("ord").toInt();
    if (server.hasArg("f_trg")) chaserFadeTrigger = server.arg("f_trg").toInt();
    if (server.hasArg("f_sync")) chaserFadeSync = constrain(server.arg("f_sync").toInt(), 0, 6);

    // Persist here — /chaser_cfg used to own this but was dead code (never
    // called from the frontend), so chaser config never actually survived
    // a reboot until this was moved to the endpoint that's really in use.
    prefs.begin("sys", false);
    prefs.putInt("c_st", chaserStartSlot); prefs.putInt("c_en", chaserEndSlot);
    prefs.putInt("c_fd", fadeTime); prefs.putInt("c_hd", holdTime);
    prefs.putInt("c_tr", chaserTrigger); prefs.putInt("c_sy", chaserSync);
    prefs.putInt("c_or", chaserOrder); prefs.putInt("c_ftr", chaserFadeTrigger);
    prefs.putInt("c_fsy", chaserFadeSync);
    prefs.end();

    if (startFresh) { currentSlot = chaserStartSlot; nextSlot = chaserStartSlot; stepStartTime = millis(); inFade = false; executeChaserSlot(currentSlot); }
    else if (!chaserActive) { activePresetSlot = 0; }
    server.send(200, "OK");
  });

  server.on("/recall", []() { triggerLoad(1, server.arg("slot").toInt()); server.send(200, "OK"); });
  server.on("/kill_fx", []() { moveFX.stop(); dimFX.stop(); colFX.active = false; sgobFX.active = false; rgobFX.active = false; gRotFX.stop(); pRotFX.stop(); chaserActive = false; activePresetSlot = 0; server.send(200, "OK"); });
  server.on("/bump", []() { String t = server.arg("t"); bool s = (server.arg("s") == "1"); if(t=="blinder") bumpBlinder=s; if(t=="strobeF") bumpStrobeF=s; if(t=="strobe50") bumpStrobe50=s; if(t=="blackout") bumpBlackout=s; server.send(200, "OK"); });
  server.on("/masterdim", []() { masterBrightness = server.arg("v").toFloat() / 100.0f; prefs.begin("sys", false); prefs.putFloat("mdim", masterBrightness); prefs.end(); server.send(200, "OK"); });
  server.on("/smooth", []() { dimSmoothVal = constrain(server.arg("v").toInt(), 0, 100); prefs.begin("sys", false); prefs.putInt("ds", dimSmoothVal); prefs.end(); server.send(200, "OK"); });
  server.on("/autofade", []() { fadeDuration = constrain(server.arg("t").toInt(), 1, 3600000); fadeCurve = server.arg("c").toInt(); fadeStateOut = !fadeStateOut; fadeStartTime = millis(); autoFading = true; server.send(200, "OK"); });
  server.on("/unmute", []() { autoFading = false; fadeStateOut = false; fadeMultiplier = 1.0f; server.send(200, "OK"); });
  server.on("/trans", []() { dipToBlack = (server.arg("dip") == "1"); prefs.begin("sys", false); prefs.putBool("dip", dipToBlack); prefs.end(); server.send(200, "OK"); });
  server.on("/hwaudio", []() { hwAudioEnabled = (server.arg("en") == "1"); hwAudioSensitivity = constrain(server.arg("sens").toInt(), 0, 100); server.send(200, "OK"); });
  
  server.on("/colfx", []() {
      colFX.active = (server.arg("a") == "1"); colFX.startVal = constrain(server.arg("st").toInt(), 0, 19); colFX.endVal = constrain(server.arg("en").toInt(), 0, 19);
      if (colFX.startVal > colFX.endVal) { int t = colFX.startVal; colFX.startVal = colFX.endVal; colFX.endVal = t; }
      colFX.holdTime = server.arg("ho").toInt(); colFX.trigger = server.arg("tr").toInt(); colFX.sync = constrain(server.arg("sy").toInt(), 0, 6);
      updateColFXStep();
      if(colFX.active) { colFX.lastStepTime = millis(); colFX.currentIdx = colFX.startVal; }
      server.send(200, "OK");
  });

  server.on("/sgobfx", []() {
      sgobFX.active = (server.arg("a") == "1"); sgobFX.startVal = constrain(server.arg("st").toInt(), 0, 9); sgobFX.endVal = constrain(server.arg("en").toInt(), 0, 9);
      if (sgobFX.startVal > sgobFX.endVal) { int t = sgobFX.startVal; sgobFX.startVal = sgobFX.endVal; sgobFX.endVal = t; }
      sgobFX.holdTime = server.arg("ho").toInt(); sgobFX.trigger = server.arg("tr").toInt(); sgobFX.sync = constrain(server.arg("sy").toInt(), 0, 6); sgobFX.scratch = (server.arg("sc") == "1");
      if (server.hasArg("spd")) sgobFX.scratchSpeed = constrain(server.arg("spd").toFloat(), 0.2f, 10.0f);
      if (server.hasArg("rng")) sgobFX.scratchRange = constrain(server.arg("rng").toInt(), 0, 100);
      if(sgobFX.active) { sgobFX.lastStepTime = millis(); sgobFX.currentIdx = sgobFX.startVal; }
      // On stop, land on whatever the Programmer tab's manual CH7 control is currently set to (sent
      // as "mv" alongside a=0) instead of the chaser's own last wheel position -- matches what the
      // user expects to see once the chaser hands control back to the manual slider. runStep()'s own
      // stop-reset (the plain, non-shake gobo value) still runs as a fallback for stop paths that
      // don't know a manual value (e.g. /kill_fx) -- clear sgWasActive here so THIS restore isn't
      // immediately overwritten by that fallback on the very next runStep() call.
      if (!sgobFX.active && server.hasArg("mv")) { dmxData[CH_GOBO] = (byte)constrain(server.arg("mv").toInt(), 0, 255); sgWasActive = false; }
      server.send(200, "OK");
  });

  server.on("/rgobfx", []() {
      rgobFX.active = (server.arg("a") == "1"); rgobFX.startVal = constrain(server.arg("st").toInt(), 0, 6); rgobFX.endVal = constrain(server.arg("en").toInt(), 0, 6);
      if (rgobFX.startVal > rgobFX.endVal) { int t = rgobFX.startVal; rgobFX.startVal = rgobFX.endVal; rgobFX.endVal = t; }
      rgobFX.holdTime = server.arg("ho").toInt(); rgobFX.trigger = server.arg("tr").toInt(); rgobFX.sync = constrain(server.arg("sy").toInt(), 0, 6); rgobFX.scratch = (server.arg("sc") == "1");
      if (server.hasArg("spd")) rgobFX.scratchSpeed = constrain(server.arg("spd").toInt(), 1, 5);
      if(rgobFX.active) { rgobFX.lastStepTime = millis(); rgobFX.currentIdx = rgobFX.startVal; }
      // See /sgobfx above: on stop, land on the Programmer tab's manual CH8 value instead of the
      // chaser's own last wheel position, and clear rgWasActive so runStep() doesn't undo it.
      if (!rgobFX.active && server.hasArg("mv")) { dmxData[CH_GOBO_ROT] = (byte)constrain(server.arg("mv").toInt(), 0, 255); rgWasActive = false; }
      server.send(200, "OK");
  });

  server.on("/save_patch", HTTP_POST, []() {
      int n = constrain(server.arg("n").toInt(), 1, 8); prefs.begin("patch", false); prefs.putInt("n", n);
      for(int i=0; i<n; i++) {
        int addr = constrain(server.arg("a"+String(i)).toInt(), 1, 495); // leaves room for the fixture's 18 channels within the 512-channel universe
        prefs.putInt(("a"+String(i)).c_str(), addr); prefs.putBool(("ip"+String(i)).c_str(), server.arg("ip"+String(i)) == "1");
        prefs.putBool(("it"+String(i)).c_str(), server.arg("it"+String(i)) == "1"); prefs.putInt(("ph"+String(i)).c_str(), server.arg("ph"+String(i)).toInt());
        fixtures[i].addr = addr; fixtures[i].invP = server.arg("ip"+String(i)) == "1"; fixtures[i].invT = server.arg("it"+String(i)) == "1"; fixtures[i].phase = server.arg("ph"+String(i)).toInt();
      }
      numFixtures = n; maxDmxChannel = 0; for(int i=0; i<numFixtures; i++) { int endChan = fixtures[i].addr + 17; if(endChan > maxDmxChannel) maxDmxChannel = endChan; }
      if(maxDmxChannel > 512) maxDmxChannel = 512; prefs.end(); server.send(200, "OK");
  });

  server.on("/api/patch", []() {
      String json = "["; for(int i=0; i<numFixtures; i++) { json += "{\"a\":" + String(fixtures[i].addr) + ",\"ip\":" + String(fixtures[i].invP?1:0) + ",\"it\":" + String(fixtures[i].invT?1:0) + ",\"ph\":" + String(fixtures[i].phase) + "}"; if(i < numFixtures-1) json += ","; } json += "]";
      server.send(200, "application/json", json);
  });

  server.on("/map_go", []() { mapTargetPan = server.arg("p").toFloat(); mapTargetTilt = server.arg("t").toFloat(); mapIsMoving = true; server.send(200, "OK"); });
  server.on("/save_map", HTTP_POST, []() {
    if (!server.hasArg("plain")) { server.send(400, "text/plain", "missing body"); return; }
    String body = server.arg("plain");
    File f = LittleFS.open("/map.json", "w");
    if (!f) { server.send(500, "text/plain", "storage error"); return; }
    size_t written = f.print(body);
    f.close();
    if (written != body.length()) { LittleFS.remove("/map.json"); server.send(500, "text/plain", "incomplete write"); return; }
    server.send(200, "OK");
  });
  server.on("/load_map", []() { if(LittleFS.exists("/map.json")) { File f = LittleFS.open("/map.json", "r"); if(f) { server.streamFile(f, "application/json"); f.close(); return; } } server.send(200, "application/json", "{}"); });
  server.on("/set_wifi", []() { prefs.begin("sys", false); prefs.putString("ssid", server.arg("s")); prefs.putString("pass", server.arg("p")); prefs.end(); server.send(200, "OK"); delay(500); ESP.restart(); });
  
  server.on("/beat", []() {
    unsigned long now = millis();
    if (globalBPM > 0) {
      unsigned long interval = 60000 / globalBPM;
      lastBeatTime = now - interval; 
    }
    manualTap = true;
    server.send(200, "OK");
  });
  server.on("/sync", []() {
    masterSyncTime = millis();
    if(moveFX.active) moveFX.modPhase = 0.0;
    if(dimFX.active) dimFX.phase = 0.0;
    if(gRotFX.active) gRotFX.phase = 0.0;
    if(pRotFX.active) pRotFX.phase = 0.0;
    server.send(200, "OK");
  });
  server.on("/jog", []() {
    int v = server.arg("v").toInt();
    if(v != 0) jogBend = v; else jogBend = 0;
    server.send(200, "OK");
  });
}
