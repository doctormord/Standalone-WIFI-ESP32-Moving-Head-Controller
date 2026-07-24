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
    json += "\"sgA\":" + String(sgobFX.active?1:0) + ",\"sgSt\":" + String(sgobFX.startVal) + ",\"sgEn\":" + String(sgobFX.endVal) + ",\"sgHo\":" + String(sgobFX.holdTime) + ",\"sgTr\":" + String(sgobFX.trigger) + ",\"sgSy\":" + String(sgobFX.sync) + ",\"sgSc\":" + String(sgobFX.scratch?1:0) + ",";
    json += "\"rgA\":" + String(rgobFX.active?1:0) + ",\"rgSt\":" + String(rgobFX.startVal) + ",\"rgEn\":" + String(rgobFX.endVal) + ",\"rgHo\":" + String(rgobFX.holdTime) + ",\"rgTr\":" + String(rgobFX.trigger) + ",\"rgSy\":" + String(rgobFX.sync) + ",\"rgSc\":" + String(rgobFX.scratch?1:0) + ",";
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
    String n = server.arg("n"); 
    presetNames[s-1] = n;
    
    dmxData[1] = (byte)dimSmoothTarget;
    dmxData[3] = (byte)(centerPan16 >> 8); dmxData[15] = (byte)(centerPan16 & 0xFF);
    dmxData[4] = (byte)(centerTilt16 >> 8); dmxData[16] = (byte)(centerTilt16 & 0xFF);

    if(colFX.active) dmxData[CH_COLOR] = wheelMap[colFX.currentIdx];
    if(sgobFX.active) dmxData[CH_GOBO] = sGoboMap[sgobFX.currentIdx];
    if(rgobFX.active) dmxData[CH_GOBO_ROT] = rGoboMap[rgobFX.currentIdx];

    // FIX: Das Struct mit 0 initialisieren, um Müll in Padding-Bytes zu vernichten!
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
    prefs.putBytes("data", &sd, sizeof(SceneData)); 
    prefs.end(); 
    
    loadAllChaserScenes();
    server.send(200, "OK");
  });

  server.on("/chaser_cfg", []() {
    if (server.hasArg("st")) chaserStartSlot = server.arg("st").toInt();
    if (server.hasArg("en")) chaserEndSlot = server.arg("en").toInt();
    if (server.hasArg("fade")) fadeTime = server.arg("fade").toInt();
    if (server.hasArg("hold")) holdTime = server.arg("hold").toInt();
    if (server.hasArg("tr")) chaserTrigger = server.arg("tr").toInt();
    if (server.hasArg("sy")) chaserSync = server.arg("sy").toInt();
    if (server.hasArg("o")) chaserOrder = server.arg("o").toInt();
    if (server.hasArg("ftr")) chaserFadeTrigger = server.arg("ftr").toInt();
    if (server.hasArg("fsy")) chaserFadeSync = server.arg("fsy").toInt();
    prefs.begin("sys", false);
    prefs.putInt("c_st", chaserStartSlot); prefs.putInt("c_en", chaserEndSlot);
    prefs.putInt("c_fd", fadeTime); prefs.putInt("c_hd", holdTime);
    prefs.putInt("c_tr", chaserTrigger); prefs.putInt("c_sy", chaserSync);
    prefs.putInt("c_or", chaserOrder); prefs.putInt("c_ftr", chaserFadeTrigger);
    prefs.putInt("c_fsy", chaserFadeSync);
    prefs.end();
    server.send(200, "OK");
  });

  server.on("/joy_in", []() {
    joyInputX = server.arg("x").toFloat(); 
    joyInputY = server.arg("y").toFloat();
    server.send(200, "OK");
  });

  server.on("/joy_cfg", []() {
    joyMaxSpeed = server.arg("spd").toInt(); joyCurve = server.arg("crv").toFloat(); joyMomentum = server.arg("mom").toFloat() / 100.0f;
    joyPanRev = server.arg("pr") == "1"; joyTiltRev = server.arg("tr") == "1";
    panMinLimit = server.arg("pmin").toInt() << 8; panMaxLimit = (server.arg("pmax").toInt() << 8) | 0xFF;
    tiltMinLimit = server.arg("tmin").toInt() << 8; tiltMaxLimit = (server.arg("tmax").toInt() << 8) | 0xFF;
    prefs.begin("sys", false);
    prefs.putInt("j_msp", joyMaxSpeed); prefs.putFloat("j_crv", joyCurve); prefs.putFloat("j_mom", joyMomentum);
    prefs.putBool("j_prv", joyPanRev); prefs.putBool("j_trv", joyTiltRev);
    prefs.putInt("j_pmi", panMinLimit); prefs.putInt("j_pma", panMaxLimit);
    prefs.putInt("j_tmi", tiltMinLimit); prefs.putInt("j_tma", tiltMaxLimit);
    prefs.end();
    server.send(200, "OK");
  });

  server.on("/set_all", []() {
    chaserActive = false; activePresetSlot = 0;
    for (int i = 1; i <= 18; i++) {
      String arg = "c" + String(i);
      if (server.hasArg(arg)) {
        int v = server.arg(arg).toInt();
        dmxData[i] = (byte)v;
        if(i==1) { dimFX.stop(); dimSmoothTarget = v; }
        if(i==3) centerPan16 = (v << 8) | (centerPan16 & 0xFF);
        if(i==15) centerPan16 = (centerPan16 & 0xFF00) | v;
        if(i==4) centerTilt16 = (v << 8) | (centerTilt16 & 0xFF);
        if(i==16) centerTilt16 = (centerTilt16 & 0xFF00) | v;
      }
    }
    server.send(200, "OK");
  });

  server.on("/fx", []() {
      bool startFresh = !moveFX.active && (server.arg("a") == "1");
      moveFX.active = (server.arg("a") == "1");
      moveFX.type = server.arg("t").toInt(); moveFX.rot = server.arg("r").toFloat();
      moveFX.spdSt = server.arg("ss").toInt(); moveFX.spdEn = server.arg("se").toInt();
      moveFX.szSt = server.arg("zs").toInt(); moveFX.szEn = server.arg("ze").toInt();
      moveFX.modMo = server.arg("mm").toInt(); moveFX.modCu = server.arg("mc").toInt();
      moveFX.modSp = server.arg("ms").toFloat(); moveFX.trigger = server.arg("tr").toInt();
      moveFX.sync = server.arg("sy").toInt();
      if(startFresh) moveFX.start(); else if (!moveFX.active) moveFX.stop();
      server.send(200, "OK");
  });

  server.on("/modfx", []() {
      String pfx = server.arg("pfx"); Modulator* m = (pfx=="dim"?&dimFX:(pfx=="gr"?&gRotFX:&pRotFX));
      if(m) {
        bool startFresh = !m->active && (server.arg("a") == "1");
        m->active = (server.arg("a") == "1");
        m->startVal = server.arg("st").toInt(); m->endVal = server.arg("en").toInt();
        m->speed = server.arg("sp").toFloat(); m->mode = server.arg("mo").toInt();
        m->curve = server.arg("cu").toInt(); m->trigger = server.arg("tr").toInt();
        m->sync = server.arg("sy").toInt();
        if(startFresh) m->start(); else if (!m->active) m->stop();
      }
      server.send(200, "OK");
  });

  server.on("/recall", []() { triggerLoad(1, server.arg("slot").toInt()); server.send(200, "OK"); });
  server.on("/kill_fx", []() { moveFX.stop(); dimFX.stop(); colFX.active = false; sgobFX.active = false; rgobFX.active = false; gRotFX.stop(); pRotFX.stop(); chaserActive = false; activePresetSlot = 0; server.send(200, "OK"); });
  server.on("/bump", []() { String t = server.arg("t"); bool s = (server.arg("s") == "1"); if(t=="blinder") bumpBlinder=s; if(t=="strobeF") bumpStrobeF=s; if(t=="strobe50") bumpStrobe50=s; if(t=="blackout") bumpBlackout=s; server.send(200, "OK"); });
  server.on("/masterdim", []() { masterBrightness = server.arg("v").toFloat() / 100.0f; prefs.begin("sys", false); prefs.putFloat("mdim", masterBrightness); prefs.end(); server.send(200, "OK"); });
  server.on("/smooth", []() { dimSmoothVal = server.arg("v").toInt(); prefs.begin("sys", false); prefs.putInt("ds", dimSmoothVal); prefs.end(); server.send(200, "OK"); });
  server.on("/autofade", []() { fadeDuration = server.arg("t").toInt(); fadeCurve = server.arg("c").toInt(); fadeStateOut = !fadeStateOut; fadeStartTime = millis(); autoFading = true; server.send(200, "OK"); });
  server.on("/unmute", []() { autoFading = false; fadeStateOut = false; fadeMultiplier = 1.0f; server.send(200, "OK"); });
  server.on("/trans", []() { dipToBlack = (server.arg("dip") == "1"); prefs.begin("sys", false); prefs.putBool("dip", dipToBlack); prefs.end(); server.send(200, "OK"); });
  server.on("/hwaudio", []() { hwAudioEnabled = (server.arg("en") == "1"); hwAudioSensitivity = server.arg("sens").toInt(); server.send(200, "OK"); });
  
  server.on("/colfx", []() {
      colFX.active = (server.arg("a") == "1"); colFX.startVal = server.arg("st").toInt(); colFX.endVal = server.arg("en").toInt();
      colFX.holdTime = server.arg("ho").toInt(); colFX.trigger = server.arg("tr").toInt(); colFX.sync = server.arg("sy").toInt();
      colFX.step = ((colFX.startVal % 2 == 0 && colFX.endVal % 2 == 0) || (colFX.startVal % 2 != 0 && colFX.endVal % 2 != 0)) ? 2 : 1;
      if(colFX.active) { colFX.lastStepTime = millis(); colFX.currentIdx = colFX.startVal; }
      server.send(200, "OK");
  });

  server.on("/sgobfx", []() {
      sgobFX.active = (server.arg("a") == "1"); sgobFX.startVal = server.arg("st").toInt(); sgobFX.endVal = server.arg("en").toInt();
      sgobFX.holdTime = server.arg("ho").toInt(); sgobFX.trigger = server.arg("tr").toInt(); sgobFX.sync = server.arg("sy").toInt(); sgobFX.scratch = (server.arg("sc") == "1");
      if(sgobFX.active) { sgobFX.lastStepTime = millis(); sgobFX.currentIdx = sgobFX.startVal; }
      server.send(200, "OK");
  });

  server.on("/rgobfx", []() {
      rgobFX.active = (server.arg("a") == "1"); rgobFX.startVal = server.arg("st").toInt(); rgobFX.endVal = server.arg("en").toInt();
      rgobFX.holdTime = server.arg("ho").toInt(); rgobFX.trigger = server.arg("tr").toInt(); rgobFX.sync = server.arg("sy").toInt(); rgobFX.scratch = (server.arg("sc") == "1");
      if(rgobFX.active) { rgobFX.lastStepTime = millis(); rgobFX.currentIdx = rgobFX.startVal; }
      server.send(200, "OK");
  });

  server.on("/save_patch", HTTP_POST, []() {
      int n = server.arg("n").toInt(); prefs.begin("patch", false); prefs.putInt("n", n);
      for(int i=0; i<n; i++) {
        prefs.putInt(("a"+String(i)).c_str(), server.arg("a"+String(i)).toInt()); prefs.putBool(("ip"+String(i)).c_str(), server.arg("ip"+String(i)) == "1");
        prefs.putBool(("it"+String(i)).c_str(), server.arg("it"+String(i)) == "1"); prefs.putInt(("ph"+String(i)).c_str(), server.arg("ph"+String(i)).toInt());
        fixtures[i].addr = server.arg("a"+String(i)).toInt(); fixtures[i].invP = server.arg("ip"+String(i)) == "1"; fixtures[i].invT = server.arg("it"+String(i)) == "1"; fixtures[i].phase = server.arg("ph"+String(i)).toInt();
      }
      numFixtures = n; maxDmxChannel = 0; for(int i=0; i<numFixtures; i++) { int endChan = fixtures[i].addr + 17; if(endChan > maxDmxChannel) maxDmxChannel = endChan; }
      if(maxDmxChannel > 512) maxDmxChannel = 512; prefs.end(); server.send(200, "OK");
  });

  server.on("/api/patch", []() {
      String json = "["; for(int i=0; i<numFixtures; i++) { json += "{\"a\":" + String(fixtures[i].addr) + ",\"ip\":" + String(fixtures[i].invP?1:0) + ",\"it\":" + String(fixtures[i].invT?1:0) + ",\"ph\":" + String(fixtures[i].phase) + "}"; if(i < numFixtures-1) json += ","; } json += "]";
      server.send(200, "application/json", json);
  });

  server.on("/map_go", []() { mapTargetPan = server.arg("p").toFloat(); mapTargetTilt = server.arg("t").toFloat(); mapIsMoving = true; server.send(200, "OK"); });
  server.on("/save_map", HTTP_POST, []() { if(server.hasArg("plain")) { File f = LittleFS.open("/map.json", "w"); if(f) { f.print(server.arg("plain")); f.close(); } } server.send(200, "OK"); });
  server.on("/load_map", []() { if(LittleFS.exists("/map.json")) { File f = LittleFS.open("/map.json", "r"); if(f) { server.streamFile(f, "application/json"); f.close(); return; } } server.send(200, "json", "{}"); });
  server.on("/set_wifi", []() { prefs.begin("sys", false); prefs.putString("ssid", server.arg("s")); prefs.putString("pass", server.arg("p")); prefs.end(); server.send(200, "OK"); delay(500); ESP.restart(); });
}