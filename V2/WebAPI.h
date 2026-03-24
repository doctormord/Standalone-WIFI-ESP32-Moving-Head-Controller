#pragma once
#include <Arduino.h>

void setupAPI() {
  server.on("/api/get_dmx", []() {
    String json; json.reserve(2500);
    json += "{";
    json += "\"1\":" + String((int)dimSmoothTarget) + ",";
    for (int i = 2; i <= 18; i++) json += "\"" + String(i) + "\":" + String(dmxData[i]) + ",";
    json += "\"cp\":" + String(centerPan16) + ",\"ct\":" + String(centerTilt16) + ",\"bpm\":" + String(globalBPM) + ",\"pr\":" + String(activePresetSlot) + ",\"chA\":" + String(chaserActive ? 1 : 0) + ",";
    json += "\"pn\":["; for(int i=0; i<10; i++) { json += "\"" + presetNames[i] + "\"" + (i<9?",":""); } json += "],";
    json += "\"dSm\":" + String(dimSmoothVal) + ",\"fO\":" + String(fadeStateOut ? 1 : 0) + ",";
    json += "\"fA\":" + String(moveFX.active?1:0) + ",\"fT\":" + String(moveFX.type) + ",\"fR\":" + String((int)(moveFX.rot*180/PI)) + ",\"fTr\":" + String(moveFX.trigger) + ",\"fSy\":" + String(moveFX.sync) + ",\"fSS\":" + String(moveFX.spdSt) + ",\"fSE\":" + String(moveFX.spdEn) + ",\"fZS\":" + String(moveFX.szSt) + ",\"fZE\":" + String(moveFX.szEn) + ",\"fMM\":" + String(moveFX.modMo) + ",\"fMC\":" + String(moveFX.modCu) + ",\"fMS\":" + String((int)(moveFX.modSp/4*100)) + ",";
    json += "\"dA\":" + String(dimFX.active?1:0) + ",\"dSt\":" + String(dimFX.startVal) + ",\"dEn\":" + String(dimFX.endVal) + ",\"dMo\":" + String(dimFX.mode) + ",\"dCu\":" + String(dimFX.curve) + ",\"dSp\":" + String((int)(dimFX.speed/4*100)) + ",\"dTr\":" + String(dimFX.trigger) + ",\"dSy\":" + String(dimFX.sync) + ",";
    json += "\"grA\":" + String(gRotFX.active?1:0) + ",\"grSt\":" + String(gRotFX.startVal) + ",\"grEn\":" + String(gRotFX.endVal) + ",\"grMo\":" + String(gRotFX.mode) + ",\"grCu\":" + String(gRotFX.curve) + ",\"grSp\":" + String((int)(gRotFX.speed/4*100)) + ",\"grTr\":" + String(gRotFX.trigger) + ",\"grSy\":" + String(gRotFX.sync) + ",";
    json += "\"prA\":" + String(pRotFX.active?1:0) + ",\"prSt\":" + String(pRotFX.startVal) + ",\"prEn\":" + String(pRotFX.endVal) + ",\"prMo\":" + String(pRotFX.mode) + ",\"prCu\":" + String(pRotFX.curve) + ",\"prSp\":" + String((int)(pRotFX.speed/4*100)) + ",\"prTr\":" + String(pRotFX.trigger) + ",\"prSy\":" + String(pRotFX.sync) + ",";
    json += "\"cA\":" + String(colFX.active?1:0) + ",\"cSt\":" + String(colFX.startVal) + ",\"cEn\":" + String(colFX.endVal) + ",\"cHo\":" + String(colFX.holdTime) + ",\"cTr\":" + String(colFX.trigger) + ",\"cSy\":" + String(colFX.sync) + ",";
    json += "\"sgA\":" + String(sgobFX.active?1:0) + ",\"sgSt\":" + String(sgobFX.startVal) + ",\"sgEn\":" + String(sgobFX.endVal) + ",\"sgHo\":" + String(sgobFX.holdTime) + ",\"sgTr\":" + String(sgobFX.trigger) + ",\"sgSy\":" + String(sgobFX.sync) + ",\"sgSc\":" + String(sgobFX.scratch?1:0) + ",";
    json += "\"rgA\":" + String(rgobFX.active?1:0) + ",\"rgSt\":" + String(rgobFX.startVal) + ",\"rgEn\":" + String(rgobFX.endVal) + ",\"rgHo\":" + String(rgobFX.holdTime) + ",\"rgTr\":" + String(rgobFX.trigger) + ",\"rgSy\":" + String(rgobFX.sync) + ",\"rgSc\":" + String(rgobFX.scratch?1:0) + ",";
    
    json += "\"chSS\":" + String(chaserStartSlot) + ",\"chES\":" + String(chaserEndSlot) + ",\"chF\":" + String(fadeTime) + ",\"chH\":" + String(holdTime) + ",\"chTr\":" + String(chaserTrigger) + ",\"chSy\":" + String(chaserSync) + ",\"chOrd\":" + String(chaserOrder) + ",\"chFTr\":" + String(chaserFadeTrigger) + ",\"chFSy\":" + String(chaserFadeSync);
    json += "}"; server.send(200, "application/json", json);
  });

  server.on("/api/state", []() {
    String json; json.reserve(600);
    json += "{\"pr\":" + String(activePresetSlot) + ",\"bpm\":" + String(globalBPM) + ",\"chA\":" + String(chaserActive?1:0) + ",\"dA\":" + String(dimFX.active?1:0) + ",\"fO\":" + String(fadeStateOut?1:0) + ",\"pn\":[";
    for(int i=0; i<10; i++) { json += "\"" + presetNames[i] + "\"" + (i<9?",":""); }
    json += "]}"; server.send(200, "application/json", json);
  });

  server.on("/api/patch", []() {
    String json = "[";
    for(int i=0; i<numFixtures; i++) {
        json += "{\"a\":" + String(fixtures[i].addr) + ",\"ip\":" + String(fixtures[i].invP?1:0) + ",\"it\":" + String(fixtures[i].invT?1:0) + ",\"ph\":" + String(fixtures[i].phase) + "}";
        if(i < numFixtures-1) json += ",";
    }
    json += "]";
    server.send(200, "application/json", json);
  });

  server.on("/save_patch", []() {
    int n = server.arg("n").toInt();
    if (n < 1) n = 1; if (n > 8) n = 8;
    numFixtures = n;
    
    prefs.begin("patch", false);
    prefs.putInt("n", numFixtures);
    for (int i=0; i<numFixtures; i++) {
        fixtures[i].addr = server.arg("a"+String(i)).toInt();
        fixtures[i].invP = server.arg("ip"+String(i)).toInt() == 1;
        fixtures[i].invT = server.arg("it"+String(i)).toInt() == 1;
        fixtures[i].phase = server.arg("ph"+String(i)).toInt();
        
        prefs.putInt(("a"+String(i)).c_str(), fixtures[i].addr);
        prefs.putBool(("ip"+String(i)).c_str(), fixtures[i].invP);
        prefs.putBool(("it"+String(i)).c_str(), fixtures[i].invT);
        prefs.putInt(("ph"+String(i)).c_str(), fixtures[i].phase);
    }
    prefs.end();
    server.send(200, "OK");
  });

  server.on("/smooth", []() { dimSmoothVal = server.arg("v").toInt(); prefs.begin("sys", false); prefs.putInt("ds", dimSmoothVal); prefs.end(); server.send(200, "OK"); });
  
  server.on("/autofade", []() { 
    fadeDuration = server.arg("t").toInt(); fadeCurve = server.arg("c").toInt(); 
    fadeStateOut = !fadeStateOut; fadeStartTime = millis(); autoFading = true; 
    server.send(200, "OK"); 
  });

  server.on("/unmute", []() {
    autoFading = false; fadeStateOut = false; fadeMultiplier = 1.0f;
    server.send(200, "OK");
  });
  
  server.on("/set", []() {
    int ch = server.arg("ch").toInt(); int val = server.arg("val").toInt();
    if (ch >= 1 && ch <= 18) {
      if(ch == 1) { dimFX.stop(); dimSmoothTarget = val; }
      else dmxData[ch] = (byte)val;
      if(ch == 3) centerPan16 = (val << 8) | (centerPan16 & 0xFF);
      if(ch == 4) centerTilt16 = (val << 8) | (centerTilt16 & 0xFF);
    }
    server.send(200, "OK");
  });

  server.on("/set_all", []() {
    chaserActive = false; activePresetSlot = 0;
    for (int i = 1; i <= 18; i++) {
      String argName = "c" + String(i);
      if (server.hasArg(argName)) {
        int val = server.arg(argName).toInt();
        if(i == 3) centerPan16 = (val << 8) | (centerPan16 & 0xFF);
        if(i == 15) centerPan16 = (centerPan16 & 0xFF00) | val;
        if(i == 4) centerTilt16 = (val << 8) | (centerTilt16 & 0xFF);
        if(i == 16) centerTilt16 = (centerTilt16 & 0xFF00) | val;
        if (i == 1) { dimFX.stop(); dimSmoothTarget = val; }
        if (i == 6) colFX.active = false; 
        if (i == 7) sgobFX.active = false;
        if (i == 8) rgobFX.active = false;
        if (i == 9) gRotFX.stop();
        if (i == 11) pRotFX.stop();
        if (!moveFX.active || (i != 3 && i != 4 && i != 15 && i != 16)) { 
          if(i != 1 || dimSmoothVal == 0) dmxData[i] = (byte)val; 
        }
      }
    }
    server.send(200, "OK");
  });

  server.on("/bump", []() {
    String t = server.arg("t"); bool s = (server.arg("s") == "1");
    if(t == "blinder") bumpBlinder = s; if(t == "strobeF") bumpStrobeF = s; if(t == "strobe50") bumpStrobe50 = s; if(t == "blackout") bumpBlackout = s;
    server.send(200, "OK");
  });

  server.on("/kill_fx", []() {
    moveFX.stop(); dimFX.stop(); colFX.active = false; sgobFX.active = false; rgobFX.active = false; gRotFX.stop(); pRotFX.stop(); chaserActive = false; activePresetSlot = 0;
    server.send(200, "OK");
  });

  server.on("/bpm", []() { globalBPM = server.arg("v").toInt(); lastBeatTime = millis(); beatTriggered = true; manualTap = true; server.send(200); });
  server.on("/beat", []() { lastBeatTime = millis(); beatTriggered = true; manualTap = true; server.send(200); });
  
  server.on("/sync", []() { 
    if (moveFX.active) moveFX.start();
    if (dimFX.active) dimFX.start(); 
    if (gRotFX.active) gRotFX.start(); 
    if (pRotFX.active) pRotFX.start(); 
    colFX.lastStepTime = millis(); sgobFX.lastStepTime = millis(); rgobFX.lastStepTime = millis(); stepStartTime = millis();
    lastBeatTime = millis(); beatTriggered = true; manualTap = true;
    server.send(200, "OK"); 
  });

  server.on("/fx", []() {
    moveFX.active = (server.arg("a") == "1"); moveFX.type = server.arg("t").toInt(); 
    moveFX.rot = server.arg("r").toFloat() * (PI / 180.0); moveFX.spdSt = server.arg("ss").toInt(); 
    moveFX.spdEn = server.arg("se").toInt(); moveFX.szSt = server.arg("zs").toInt(); moveFX.szEn = server.arg("ze").toInt(); 
    moveFX.modMo = server.arg("mm").toInt(); moveFX.modCu = server.arg("mc").toInt(); moveFX.modSp = (server.arg("ms").toFloat() / 100.0) * 4.0; 
    moveFX.trigger = server.arg("tr").toInt(); moveFX.sync = server.arg("sy").toInt();
    if(moveFX.active) { chaserActive = false; moveFX.start(); } activePresetSlot = 0; server.send(200, "OK");
  });

  server.on("/modfx", []() {
    String pfx = server.arg("pfx"); bool act = (server.arg("a") == "1"); int st = server.arg("st").toInt(); int en = server.arg("en").toInt(); float sp = (server.arg("sp").toFloat() / 100.0) * 4.0; int mo = server.arg("mo").toInt(); int cu = server.arg("cu").toInt(); int tr = server.arg("tr").toInt(); int sy = server.arg("sy").toInt();
    if (pfx == "dim") { dimFX.active = act; dimFX.startVal = st; dimFX.endVal = en; dimFX.speed = sp; dimFX.mode = mo; dimFX.curve = cu; dimFX.trigger = tr; dimFX.sync = sy; if(act) { chaserActive=false; dimFX.start(); } }
    else if (pfx == "gr") { gRotFX.active = act; gRotFX.startVal = st; gRotFX.endVal = en; gRotFX.speed = sp; gRotFX.mode = mo; gRotFX.curve = cu; gRotFX.trigger = tr; gRotFX.sync = sy; if(act) { chaserActive=false; gRotFX.start(); } }
    else if (pfx == "pr") { pRotFX.active = act; pRotFX.startVal = st; pRotFX.endVal = en; pRotFX.speed = sp; pRotFX.mode = mo; pRotFX.curve = cu; pRotFX.trigger = tr; pRotFX.sync = sy; if(act) { chaserActive=false; pRotFX.start(); } }
    activePresetSlot = 0; server.send(200, "OK");
  });

  server.on("/colfx", []() {
    colFX.active = (server.arg("a") == "1"); colFX.startVal = min(server.arg("st").toInt(), server.arg("en").toInt()); colFX.endVal = max(server.arg("st").toInt(), server.arg("en").toInt()); colFX.holdTime = server.arg("ho").toInt(); colFX.trigger = server.arg("tr").toInt(); colFX.sync = server.arg("sy").toInt();
    if ((colFX.startVal % 2 == 0 && colFX.endVal % 2 == 0) || (colFX.startVal % 2 != 0 && colFX.endVal % 2 != 0)) colFX.step = 2; else colFX.step = 1;
    if(colFX.active) { chaserActive = false; colFX.lastStepTime = millis(); colFX.currentIdx = colFX.startVal; dmxData[6] = wheelMap[colFX.currentIdx]; }
    activePresetSlot = 0; server.send(200, "OK");
  });

  server.on("/sgobfx", []() {
    sgobFX.active = (server.arg("a") == "1"); sgobFX.startVal = min(server.arg("st").toInt(), server.arg("en").toInt()); sgobFX.endVal = max(server.arg("st").toInt(), server.arg("en").toInt()); sgobFX.holdTime = server.arg("ho").toInt(); sgobFX.trigger = server.arg("tr").toInt(); sgobFX.sync = server.arg("sy").toInt(); sgobFX.scratch = (server.arg("sc") == "1");
    if(sgobFX.active) { chaserActive = false; sgobFX.lastStepTime = millis(); sgobFX.currentIdx = sgobFX.startVal; dmxData[7] = sGoboMap[sgobFX.currentIdx]; }
    activePresetSlot = 0; server.send(200, "OK");
  });

  server.on("/rgobfx", []() {
    rgobFX.active = (server.arg("a") == "1"); rgobFX.startVal = min(server.arg("st").toInt(), server.arg("en").toInt()); rgobFX.endVal = max(server.arg("st").toInt(), server.arg("en").toInt()); rgobFX.holdTime = server.arg("ho").toInt(); rgobFX.trigger = server.arg("tr").toInt(); rgobFX.sync = server.arg("sy").toInt(); rgobFX.scratch = (server.arg("sc") == "1");
    if(rgobFX.active) { chaserActive = false; rgobFX.lastStepTime = millis(); rgobFX.currentIdx = rgobFX.startVal; dmxData[8] = rGoboMap[rgobFX.currentIdx]; }
    activePresetSlot = 0; server.send(200, "OK");
  });

  server.on("/chaser", []() {
    chaserActive = (server.arg("active") == "1"); 
    chaserStartSlot = min(server.arg("st").toInt(), server.arg("en").toInt());
    chaserEndSlot = max(server.arg("st").toInt(), server.arg("en").toInt());
    fadeTime = server.arg("fade").toInt(); holdTime = server.arg("hold").toInt(); chaserTrigger = server.arg("tr").toInt(); chaserSync = server.arg("sy").toInt(); chaserOrder = server.arg("o").toInt();
    
    chaserFadeTrigger = server.arg("ftr").toInt();
    chaserFadeSync = server.arg("fsy").toInt();

    if(chaserActive) {
      activePresetSlot = 0;
      for(int s=0; s<10; s++) {
        prefs.begin(("sc" + String(s+1)).c_str(), true);
        for (int i = 1; i <= 18; i++) chaserScenes[s].dmx[i] = prefs.getUChar(String(i).c_str(), 0);
        chaserScenes[s].fA = prefs.getBool("fA", false); chaserScenes[s].fT = prefs.getInt("fT", 1); chaserScenes[s].fR = prefs.getFloat("fR", 0.0); chaserScenes[s].fTr = prefs.getInt("fTr", 0); chaserScenes[s].fSy = prefs.getInt("fSy", 2); chaserScenes[s].fSS = prefs.getInt("fSS", 50); chaserScenes[s].fSE = prefs.getInt("fSE", 50); chaserScenes[s].fZS = prefs.getInt("fZS", 30); chaserScenes[s].fZE = prefs.getInt("fZE", 30); chaserScenes[s].fMM = prefs.getInt("fMM", 1); chaserScenes[s].fMC = prefs.getInt("fMC", 0); chaserScenes[s].fMS = prefs.getFloat("fMS", 0.5);
        chaserScenes[s].dA = prefs.getBool("dA", false); chaserScenes[s].dSt = prefs.getInt("dSt", 0); chaserScenes[s].dEn = prefs.getInt("dEn", 255); chaserScenes[s].dMo = prefs.getInt("dMo", 1); chaserScenes[s].dCu = prefs.getInt("dCu", 0); chaserScenes[s].dSp = prefs.getFloat("dSp", 1.0); chaserScenes[s].dTr = prefs.getInt("dTr", 0); chaserScenes[s].dSy = prefs.getInt("dSy", 2);
        chaserScenes[s].grA = prefs.getBool("grA", false); chaserScenes[s].grSt = prefs.getInt("grSt", 0); chaserScenes[s].grEn = prefs.getInt("grEn", 255); chaserScenes[s].grMo = prefs.getInt("grMo", 1); chaserScenes[s].grCu = prefs.getInt("grCu", 0); chaserScenes[s].grSp = prefs.getFloat("grSp", 1.0); chaserScenes[s].grTr = prefs.getInt("grTr", 0); chaserScenes[s].grSy = prefs.getInt("grSy", 2);
        chaserScenes[s].prA = prefs.getBool("prA", false); chaserScenes[s].prSt = prefs.getInt("prSt", 0); chaserScenes[s].prEn = prefs.getInt("prEn", 255); chaserScenes[s].prMo = prefs.getInt("prMo", 1); chaserScenes[s].prCu = prefs.getInt("prCu", 0); chaserScenes[s].prSp = prefs.getFloat("prSp", 1.0); chaserScenes[s].prTr = prefs.getInt("prTr", 0); chaserScenes[s].prSy = prefs.getInt("prSy", 2);
        chaserScenes[s].cA = prefs.getBool("cA", false); chaserScenes[s].cSt = prefs.getInt("cSt", 0); chaserScenes[s].cEn = prefs.getInt("cEn", 19); chaserScenes[s].cHo = prefs.getULong("cHo", 1000); chaserScenes[s].cTr = prefs.getInt("cTr", 0); chaserScenes[s].cSy = prefs.getInt("cSy", 2);
        chaserScenes[s].sgA = prefs.getBool("sgA", false); chaserScenes[s].sgSt = prefs.getInt("sgSt", 0); chaserScenes[s].sgEn = prefs.getInt("sgEn", 9); chaserScenes[s].sgHo = prefs.getULong("sgHo", 1000); chaserScenes[s].sgTr = prefs.getInt("sgTr", 0); chaserScenes[s].sgSy = prefs.getInt("sgSy", 2); chaserScenes[s].sgSc = prefs.getBool("sgSc", false);
        chaserScenes[s].rgA = prefs.getBool("rgA", false); chaserScenes[s].rgSt = prefs.getInt("rgSt", 0); chaserScenes[s].rgEn = prefs.getInt("rgEn", 6); chaserScenes[s].rgHo = prefs.getULong("rgHo", 1000); chaserScenes[s].rgTr = prefs.getInt("rgTr", 0); chaserScenes[s].rgSy = prefs.getInt("rgSy", 2); chaserScenes[s].rgSc = prefs.getBool("rgSc", false);
        prefs.end();
      }
      currentSlot = chaserStartSlot;
      if (chaserOrder == 1) { nextSlot = random(chaserStartSlot, chaserEndSlot + 1); } 
      else { nextSlot = currentSlot + 1; if(nextSlot > chaserEndSlot) nextSlot = chaserStartSlot; }
      inFade = true; stepStartTime = millis(); triggerSceneFX(currentSlot); 
      for (int i = 1; i <= 18; i++) { if (!(i==1 || i==3 || i==4 || i==13 || i==14 || i==15 || i==16)) dmxData[i] = chaserScenes[nextSlot].dmx[i]; }
    }
    server.send(200, "OK");
  });

  server.on("/save_map", HTTP_POST, []() {
    if (server.hasArg("plain")) {
      File f = LittleFS.open("/map.json", "w");
      if (f) { f.print(server.arg("plain")); f.close(); server.send(200, "text/plain", "OK"); } 
      else { server.send(500, "text/plain", "FS Error"); }
    } else { server.send(400, "text/plain", "No Data"); }
  });

  server.on("/load_map", HTTP_GET, []() {
    if (LittleFS.exists("/map.json")) {
      File f = LittleFS.open("/map.json", "r");
      server.streamFile(f, "application/json");
      f.close();
    } else { server.send(404, "application/json", "{}"); }
  });

  server.on("/recall", []() {
    chaserActive = false; int slot = server.arg("slot").toInt(); activePresetSlot = slot;
    prefs.begin(("sc" + String(slot)).c_str(), false);
    for (int i = 1; i <= 18; i++) {
      dmxData[i] = prefs.getUChar(String(i).c_str(), 0);
      if(i == 3) centerPan16 = (dmxData[i] << 8) | (centerPan16 & 0xFF); if(i == 15) centerPan16 = (centerPan16 & 0xFF00) | dmxData[i];
      if(i == 4) centerTilt16 = (dmxData[i] << 8) | (centerTilt16 & 0xFF); if(i == 16) centerTilt16 = (centerTilt16 & 0xFF00) | dmxData[i];
    }
    dimSmoothTarget = dmxData[1]; 
    moveFX.active = prefs.getBool("fA", false); moveFX.type = prefs.getInt("fT", 1); moveFX.rot = prefs.getFloat("fR", 0.0); moveFX.trigger = prefs.getInt("fTr", 0); moveFX.sync = prefs.getInt("fSy", 2); moveFX.spdSt = prefs.getInt("fSS", 50); moveFX.spdEn = prefs.getInt("fSE", 50); moveFX.szSt = prefs.getInt("fZS", 30); moveFX.szEn = prefs.getInt("fZE", 30); moveFX.modMo = prefs.getInt("fMM", 1); moveFX.modCu = prefs.getInt("fMC", 0); moveFX.modSp = prefs.getFloat("fMS", 0.5);
    dimFX.active = prefs.getBool("dA", false); dimFX.startVal = prefs.getInt("dSt", 0); dimFX.endVal = prefs.getInt("dEn", 255); dimFX.mode = prefs.getInt("dMo", 1); dimFX.curve = prefs.getInt("dCu", 0); dimFX.speed = prefs.getFloat("dSp", 1.0); dimFX.trigger = prefs.getInt("dTr", 0); dimFX.sync = prefs.getInt("dSy", 2);
    gRotFX.active = prefs.getBool("grA", false); gRotFX.startVal = prefs.getInt("grSt", 135); gRotFX.endVal = prefs.getInt("grEn", 255); gRotFX.mode = prefs.getInt("grMo", 1); gRotFX.curve = prefs.getInt("grCu", 0); gRotFX.speed = prefs.getFloat("grSp", 1.0); gRotFX.trigger = prefs.getInt("grTr", 0); gRotFX.sync = prefs.getInt("grSy", 2);
    pRotFX.active = prefs.getBool("prA", false); pRotFX.startVal = prefs.getInt("prSt", 135); pRotFX.endVal = prefs.getInt("prEn", 255); pRotFX.mode = prefs.getInt("prMo", 1); pRotFX.curve = prefs.getInt("prCu", 0); pRotFX.speed = prefs.getFloat("prSp", 1.0); pRotFX.trigger = prefs.getInt("prTr", 0); pRotFX.sync = prefs.getInt("prSy", 2);
    colFX.active = prefs.getBool("cA", false); colFX.startVal = prefs.getInt("cSt", 0); colFX.endVal = prefs.getInt("cEn", 19); colFX.holdTime = prefs.getULong("cHo", 1000); colFX.trigger = prefs.getInt("cTr", 0); colFX.sync = prefs.getInt("cSy", 2);
    sgobFX.active = prefs.getBool("sgA", false); sgobFX.startVal = prefs.getInt("sgSt", 0); sgobFX.endVal = prefs.getInt("sgEn", 9); sgobFX.holdTime = prefs.getULong("sgHo", 1000); sgobFX.trigger = prefs.getInt("sgTr", 0); sgobFX.sync = prefs.getInt("sgSy", 2); sgobFX.scratch = prefs.getBool("sgSc", false);
    rgobFX.active = prefs.getBool("rgA", false); rgobFX.startVal = prefs.getInt("rgSt", 0); rgobFX.endVal = prefs.getInt("rgEn", 6); rgobFX.holdTime = prefs.getULong("rgHo", 1000); rgobFX.trigger = prefs.getInt("rgTr", 0); rgobFX.sync = prefs.getInt("rgSy", 2); rgobFX.scratch = prefs.getBool("rgSc", false);
    prefs.end(); 
    
    if(moveFX.active) moveFX.start(); if(dimFX.active) dimFX.start(); if(gRotFX.active) gRotFX.start(); if(pRotFX.active) pRotFX.start();
    if ((colFX.startVal % 2 == 0 && colFX.endVal % 2 == 0) || (colFX.startVal % 2 != 0 && colFX.endVal % 2 != 0)) colFX.step = 2; else colFX.step = 1;
    colFX.currentIdx = colFX.startVal; sgobFX.currentIdx = sgobFX.startVal; rgobFX.currentIdx = rgobFX.startVal;
    server.send(200, "OK");
  });

  server.on("/save", []() {
    int s = server.arg("slot").toInt(); String n = server.arg("n"); n.replace("\"", "'"); n.replace("\\", "/"); presetNames[s-1] = n;
    prefs.begin(("sc" + String(s)).c_str(), false);
    prefs.putString("n", n);
    prefs.putUChar("1", (byte)dimSmoothTarget); 
    for (int i = 2; i <= 18; i++) prefs.putUChar(String(i).c_str(), dmxData[i]);
    
    prefs.putBool("fA", moveFX.active); prefs.putInt("fT", moveFX.type); prefs.putFloat("fR", moveFX.rot); prefs.putInt("fTr", moveFX.trigger); prefs.putInt("fSy", moveFX.sync); prefs.putInt("fSS", moveFX.spdSt); prefs.putInt("fSE", moveFX.spdEn); prefs.putInt("fZS", moveFX.szSt); prefs.putInt("fZE", moveFX.szEn); prefs.putInt("fMM", moveFX.modMo); prefs.putInt("fMC", moveFX.modCu); prefs.putFloat("fMS", moveFX.modSp);
    prefs.putBool("dA", dimFX.active); prefs.putInt("dSt", dimFX.startVal); prefs.putInt("dEn", dimFX.endVal); prefs.putInt("dMo", dimFX.mode); prefs.putInt("dCu", dimFX.curve); prefs.putFloat("dSp", dimFX.speed); prefs.putInt("dTr", dimFX.trigger); prefs.putInt("dSy", dimFX.sync);
    prefs.putBool("grA", gRotFX.active); prefs.putInt("grSt", gRotFX.startVal); prefs.putInt("grEn", gRotFX.endVal); prefs.putInt("grMo", gRotFX.mode); prefs.putInt("grCu", gRotFX.curve); prefs.putFloat("grSp", gRotFX.speed); prefs.putInt("grTr", gRotFX.trigger); prefs.putInt("grSy", gRotFX.sync);
    prefs.putBool("prA", pRotFX.active); prefs.putInt("prSt", pRotFX.startVal); prefs.putInt("prEn", pRotFX.endVal); prefs.putInt("prMo", pRotFX.mode); prefs.putInt("prCu", pRotFX.curve); prefs.putFloat("prSp", pRotFX.speed); prefs.putInt("prTr", pRotFX.trigger); prefs.putInt("prSy", pRotFX.sync);
    prefs.putBool("cA", colFX.active); prefs.putInt("cSt", colFX.startVal); prefs.putInt("cEn", colFX.endVal); prefs.putULong("cHo", colFX.holdTime); prefs.putInt("cTr", colFX.trigger); prefs.putInt("cSy", colFX.sync);
    prefs.putBool("sgA", sgobFX.active); prefs.putInt("sgSt", sgobFX.startVal); prefs.putInt("sgEn", sgobFX.endVal); prefs.putULong("sgHo", sgobFX.holdTime); prefs.putInt("sgTr", sgobFX.trigger); prefs.putInt("sgSy", sgobFX.sync); prefs.putBool("sgSc", sgobFX.scratch);
    prefs.putBool("rgA", rgobFX.active); prefs.putInt("rgSt", rgobFX.startVal); prefs.putInt("rgEn", rgobFX.endVal); prefs.putULong("rgHo", rgobFX.holdTime); prefs.putInt("rgTr", rgobFX.trigger); prefs.putInt("rgSy", rgobFX.sync); prefs.putBool("rgSc", rgobFX.scratch);
    prefs.end(); server.send(200, "OK");
  });

  server.on("/update", HTTP_POST, []() {
    server.sendHeader("Connection", "close"); server.send(200, "text/plain", (Update.hasError()) ? "Update failed!" : "Update successful. ESP restarting..."); delay(1000); ESP.restart();
  }, []() {
    HTTPUpload& upload = server.upload();
    if (upload.status == UPLOAD_FILE_START) { if (!Update.begin(UPDATE_SIZE_UNKNOWN)) Update.printError(Serial); } else if (upload.status == UPLOAD_FILE_WRITE) { if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) Update.printError(Serial); } else if (upload.status == UPLOAD_FILE_END) { Update.end(true); }
  });

  server.on("/set_wifi", []() { prefs.begin("sys", false); prefs.putString("ssid", server.arg("s")); prefs.putString("pass", server.arg("p")); prefs.end(); server.send(200, "text/plain", "OK"); delay(500); ESP.restart(); });
}