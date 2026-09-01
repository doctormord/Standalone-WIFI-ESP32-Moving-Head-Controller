#pragma once
#include <Arduino.h>

extern void loadAllChaserScenes();

static File fsUploadFile;

// True if this request's client-known generation (its "g" query arg, the frontend's own
// last-seen stateGen) predates the most recent /recall -- see lastRecallGen's declaration
// comment in the .ino. A request built before the client knew about that recall would, if
// applied, silently re-impose the PREVIOUS preset's FX state over the just-recalled one.
// Missing "g" (a route that doesn't send it) is treated as not stale, so this only guards
// routes that opt in.
bool isStaleWrite() {
  return server.hasArg("g") && (uint32_t)server.arg("g").toInt() < lastRecallGen;
}


// Audio tuning survives a reboot. Everything here was runtime-only until 2026-08-31, so every
// flash or power cycle silently reset the whole audio chain to compile-time defaults -- the same
// trap the live FX state still has (see backlog). Values are read once at boot and written back
// debounced, never per slider step.
#define AUDIO_PREFS_DEBOUNCE_MS 1500

void saveAudioPrefs() {
  prefs.begin("sys", false);
    prefs.putInt("a_nf", tuneNoiseFloor);
    prefs.putInt("a_fa", tuneFastAttackShift);
    prefs.putInt("a_fd", tuneFastDecayShift);
    prefs.putInt("a_ma", tuneMidAttackShift);
    prefs.putInt("a_md", tuneMidDecayShift);
    prefs.putInt("a_sa", tuneSlowAttackShift);
    prefs.putInt("a_sd", tuneSlowDecayShift);
    prefs.putInt("a_dts", tuneDynThreshSmoothShift);
    prefs.putInt("a_mtd", tuneMidThreshDivShift);
    prefs.putInt("a_htd", tuneHighThreshDivShift);
    prefs.putInt("a_fg", tuneFftGainShift);
    prefs.putInt("a_ig", tuneInputGainShift);
    prefs.putInt("a_tw", tempoWindowMs);
    prefs.putInt("a_vmp", sdVarMinPct);
    prefs.putInt("a_mrp", sdMinRangePct);
    prefs.putInt("a_bst", sdBoostMaxQ8);
    prefs.putInt("a_bsh", sdBoostShift);
    prefs.putInt("a_pfp", sdPeakFallPct);
    prefs.putInt("a_pmw", sdPeakMaxWaitMs);
    prefs.putInt("a_agr", tempoAgreeMaxPct);
    prefs.putBool("a_ag", autoGain);
    prefs.putInt("a_agt", agTargetPct);
    prefs.putInt("a_agu", agUpDelayMs);
    prefs.putInt("a_agd", agDownDelayMs);
    prefs.putInt("a_db", tuneDetBass);
    prefs.putInt("a_dm", tuneDetMid);
    prefs.putInt("a_dh", tuneDetHigh);
    prefs.putInt("a_tmul", tempoMulMode);
    prefs.putInt("a_bbl", tuneBinBassLo);
    prefs.putInt("a_bbh", tuneBinBassHi);
    prefs.putInt("a_bml", tuneBinMidLo);
    prefs.putInt("a_bmh", tuneBinMidHi);
    prefs.putInt("a_bhl", tuneBinHighLo);
    prefs.putInt("a_bhh", tuneBinHighHi);
    prefs.putInt("a_sens", hwAudioSensitivity);
    prefs.putBool("a_fft", audioUseFFT);
    prefs.putBool("a_flux", audioUseFlux);
    prefs.putBool("a_trk", audioUseTracker);
    prefs.putBool("a_bsd", sdEnabled);
    prefs.putInt("a_blo", sdKLo);
    prefs.putInt("a_bhi", sdKHi);
    prefs.putInt("a_brl", sdRel);
    prefs.putInt("a_brf", sdRefShift);
    prefs.putInt("a_blk", sdLockoutMs);
    prefs.putBool("a_en", hwAudioEnabled);
  prefs.end();
}

void loadAudioPrefs() {
  prefs.begin("sys", true);   // read-only: never creates the namespace as a side effect
    tuneNoiseFloor = prefs.getInt("a_nf", tuneNoiseFloor);
    tuneFastAttackShift = prefs.getInt("a_fa", tuneFastAttackShift);
    tuneFastDecayShift = prefs.getInt("a_fd", tuneFastDecayShift);
    tuneMidAttackShift = prefs.getInt("a_ma", tuneMidAttackShift);
    tuneMidDecayShift = prefs.getInt("a_md", tuneMidDecayShift);
    tuneSlowAttackShift = prefs.getInt("a_sa", tuneSlowAttackShift);
    tuneSlowDecayShift = prefs.getInt("a_sd", tuneSlowDecayShift);
    tuneDynThreshSmoothShift = prefs.getInt("a_dts", tuneDynThreshSmoothShift);
    tuneMidThreshDivShift = prefs.getInt("a_mtd", tuneMidThreshDivShift);
    tuneHighThreshDivShift = prefs.getInt("a_htd", tuneHighThreshDivShift);
    tuneFftGainShift = prefs.getInt("a_fg", tuneFftGainShift);
    tuneInputGainShift = prefs.getInt("a_ig", tuneInputGainShift);
    tempoWindowMs = prefs.getInt("a_tw", tempoWindowMs);
    sdVarMinPct = prefs.getInt("a_vmp", sdVarMinPct);
    sdMinRangePct = prefs.getInt("a_mrp", sdMinRangePct);
    sdBoostMaxQ8 = prefs.getInt("a_bst", sdBoostMaxQ8);
    sdBoostShift = prefs.getInt("a_bsh", sdBoostShift);
    sdPeakFallPct = prefs.getInt("a_pfp", sdPeakFallPct);
    sdPeakMaxWaitMs = prefs.getInt("a_pmw", sdPeakMaxWaitMs);
    tempoAgreeMaxPct = prefs.getInt("a_agr", tempoAgreeMaxPct);
    autoGain = prefs.getBool("a_ag", autoGain);
    agTargetPct = prefs.getInt("a_agt", agTargetPct);
    agUpDelayMs = prefs.getInt("a_agu", agUpDelayMs);
    agDownDelayMs = prefs.getInt("a_agd", agDownDelayMs);
    tuneDetBass = prefs.getInt("a_db", tuneDetBass);
    tuneDetMid  = prefs.getInt("a_dm", tuneDetMid);
    tuneDetHigh = prefs.getInt("a_dh", tuneDetHigh);
    tempoMulMode = prefs.getInt("a_tmul", tempoMulMode);
    tuneBinBassLo = prefs.getInt("a_bbl", tuneBinBassLo);
    tuneBinBassHi = prefs.getInt("a_bbh", tuneBinBassHi);
    tuneBinMidLo = prefs.getInt("a_bml", tuneBinMidLo);
    tuneBinMidHi = prefs.getInt("a_bmh", tuneBinMidHi);
    tuneBinHighLo = prefs.getInt("a_bhl", tuneBinHighLo);
    tuneBinHighHi = prefs.getInt("a_bhh", tuneBinHighHi);
    hwAudioSensitivity = prefs.getInt("a_sens", hwAudioSensitivity);
    audioUseFFT = prefs.getBool("a_fft", audioUseFFT);
    audioUseFlux = prefs.getBool("a_flux", audioUseFlux);
    audioUseTracker = prefs.getBool("a_trk", audioUseTracker);
    sdEnabled = prefs.getBool("a_bsd", sdEnabled);
    sdKLo = prefs.getInt("a_blo", sdKLo);
    sdKHi = prefs.getInt("a_bhi", sdKHi);
    sdRel = prefs.getInt("a_brl", sdRel);
    sdRefShift = prefs.getInt("a_brf", sdRefShift);
    sdLockoutMs = prefs.getInt("a_blk", sdLockoutMs);
    hwAudioEnabled = prefs.getBool("a_en", hwAudioEnabled);
  prefs.end();
  // Defensive: a corrupt or hand-edited NVS value must not be able to make a band inverted or
  // point past the spectrum, which would read out of bounds in fftBand()/fftFlux().
  const int LAST_BIN = FFT_N / 2 - 1;
  tuneBinBassLo = constrain(tuneBinBassLo, 1, LAST_BIN - 1); tuneBinBassHi = constrain(tuneBinBassHi, tuneBinBassLo, LAST_BIN);
  tuneBinMidLo  = constrain(tuneBinMidLo,  1, LAST_BIN - 1); tuneBinMidHi  = constrain(tuneBinMidHi,  tuneBinMidLo,  LAST_BIN);
  tuneBinHighLo = constrain(tuneBinHighLo, 1, LAST_BIN - 1); tuneBinHighHi = constrain(tuneBinHighHi, tuneBinHighLo, LAST_BIN);
  hwAudioSensitivity = constrain(hwAudioSensitivity, 0, 100);
  // The tap lock is deliberately NOT persisted: it exists because the user tapped a tempo,
  // and that tapped value does not survive a reboot. Restoring the lock without it left the
  // device holding globalBPM at its 120 startup value forever while the tracker, working
  // correctly in the background, was never allowed through. Observed live 2026-09-01.
  tempoTapLock = false;
  tuneDetBass = constrain(tuneDetBass, 0, 1);
  tuneDetMid  = constrain(tuneDetMid, 0, 1);
  tuneDetHigh = constrain(tuneDetHigh, 0, 1);
  tempoMulMode = constrain(tempoMulMode, 0, 2);
}

// Called every loop; writes at most once per settling period.
void flushAudioPrefs() {
  if (!audioPrefsDirty) return;
  if (millis() - audioPrefsDirtyAt < AUDIO_PREFS_DEBOUNCE_MS) return;
  audioPrefsDirty = false;
  saveAudioPrefs();
}

void setupAPI() {
  loadAudioPrefs();

  
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
    // no-store: index.html changes with every UI fix/flash, and this library sends neither
    // ETag nor Last-Modified for it, so a plain browser reload had nothing to revalidate against
    // and could keep serving a stale, already-open tab's in-memory bundle indefinitely -- looked
    // like a live frontend/backend desync bug when it was actually just stale JS. Reported live
    // 2026-08-20 (dimFxRunning shown as active locally while /api/get_dmx's "dA" was already 0).
    server.serveStatic("/", LittleFS, "/index.html", "no-store");
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
    json += "\"cp\":" + String(centerPan16) + ",\"ct\":" + String(centerTilt16) + ",\"bpm\":" + String(globalBPM) + ",\"pr\":" + String(activePresetSlot) + ",\"chA\":" + String(chaserActive ? 1 : 0) + ",\"gen\":" + String(stateGen) + ",\"gsrc\":\"" + String(lastGenSource) + "\",";
    // Fixture 0's actual live Movement FX output -- cp/ct above are only the pattern's *center*,
    // not its animated position. Debug fields, added 2026-08-20 to diagnose a reported "circle
    // looks like a figure-8" issue live instead of guessing at it.
    json += "\"op\":" + String(liveOutPan0) + ",\"ot\":" + String(liveOutTilt0) + ",";
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
    json += "{\"fw\":\"" + String(FW_VERSION) + "\",\"pr\":" + String(activePresetSlot) + ",\"bpm\":" + String(globalBPM) + ",\"chA\":" + String(chaserActive?1:0);
    json += ",\"hwA\":" + String(hwAudioEnabled?1:0) + ",\"fO\":" + String(fadeStateOut?1:0);
    json += ",\"trB\":" + String(guiBass?1:0) + ",\"trM\":" + String(guiMid?1:0) + ",\"trH\":" + String(guiHigh?1:0);
    // Beat-sync/BPM-drift debug fields (2026-08-19): rawBPM/rawMs are the audio engine's
    // pre-smoothing median-detected value and most recent accepted (possibly octave-folded)
    // interval, loopMax is the worst main-loop gap in the last 5s -- pull these live via curl
    // to check the BPM detection and loop-jitter theories against real audio instead of guessing.
    json += ",\"rawBPM\":" + String(lastRawDetectedBPM) + ",\"rawMs\":" + String(lastRawIntervalMs) + ",\"loopMax\":" + String(loopMaxMs)
            + ",\"audUs\":" + String(audioLastUs) + ",\"audMax\":" + String(audioMaxUs)
            + ",\"fftUs\":" + String(fftLastUs)
            + ",\"engUs\":" + String(engineLastUs) + ",\"engMax\":" + String(engineMaxUs)
            // Mic level and clipping ride along on the telemetry poll every client already
            // runs, so the header can warn about a badly set input on any tab without
            // anyone having to open the AUDIO one -- and without a second request.
            + ",\"pk\":" + String((long)micPeak) + ",\"clip\":" + String(micClipCount)
            + ",\"ag\":" + String(autoGain ? 1 : 0) + ",\"rclip\":" + String(micRawClipCount);
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
    server.send(200, "text/plain", String(bumpGen("save")));
  });

  // Re-saves only a slot's pan/tilt center from the current live position,
  // leaving every other already-saved parameter (FX, colors, gobos, speeds)
  // untouched -- lets a pre-programmed slot be re-aimed on site without the
  // full load/edit/save round trip through the Programmer tab.
  server.on("/save_center", []() {
    int s = server.arg("slot").toInt();
    if (s < 1 || s > 10) { server.send(400, "text/plain", "invalid slot"); return; }

    chaserScenes[s - 1].dmx[CH_PAN]       = (byte)(centerPan16 >> 8);
    chaserScenes[s - 1].dmx[CH_PAN_FINE]  = (byte)(centerPan16 & 0xFF);
    chaserScenes[s - 1].dmx[CH_TILT]      = (byte)(centerTilt16 >> 8);
    chaserScenes[s - 1].dmx[CH_TILT_FINE] = (byte)(centerTilt16 & 0xFF);

    prefs.begin(("sc" + String(s)).c_str(), false);
    size_t written = prefs.putBytes("data", &chaserScenes[s - 1], sizeof(SceneData));
    prefs.end();

    if (written != sizeof(SceneData)) { server.send(500, "text/plain", "storage error"); return; }
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

  // Read-back for /joy_cfg. These nine values were persisted to NVS and restored at boot, but no
  // route ever exposed them, so a freshly loaded browser tab held the frontend's hardcoded defaults
  // instead of the device's real config -- and the first state change made its joyKey diff fire and
  // overwrite the saved config with those defaults. Silent data loss on every page reload.
  // Deliberately its own one-shot route rather than extra fields in /api/get_dmx, which is already
  // ~960 bytes built from ~50 sequential String += on a heap-constrained ESP32-C3 and is polled
  // every 2s. Values are emitted in the same units /joy_cfg accepts, so the round-trip is lossless:
  // momentum as a percentage, the pan/tilt limits as the 0-255 bytes the UI works in (they are
  // stored internally as 16-bit, see axisLimits above).
  server.on("/api/joycfg", []() {
    String json = "{\"spd\":" + String(joyMaxSpeed) + ",\"crv\":" + String(joyCurve, 2) +
                  ",\"mom\":" + String(joyMomentum * 100.0f, 0) +
                  ",\"prv\":" + String(joyPanRev ? 1 : 0) + ",\"trv\":" + String(joyTiltRev ? 1 : 0) +
                  ",\"pmin\":" + String(panMinLimit >> 8) + ",\"pmax\":" + String(panMaxLimit >> 8) +
                  ",\"tmin\":" + String(tiltMinLimit >> 8) + ",\"tmax\":" + String(tiltMaxLimit >> 8) + "}";
    server.send(200, "application/json", json);
  });

  server.on("/joy_in", []() {
    joyInputX = server.arg("x").toFloat(); 
    joyInputY = server.arg("y").toFloat();
    server.send(200, "OK");
  });

  server.on("/set_all", []() {
    // Deliberately does NOT touch activePresetSlot (see 2026-08-25 history/backlog entry).
    // It used to clear it whenever a channel value differed from what was already live, on the
    // theory that a real manual edit means the recalled preset is no longer purely in effect.
    // That inference proved unreliable in practice: dmxData is also touched by continuous FX
    // output, joystick smoothing, and multi-field poll catch-up, none of which are a real edit,
    // and each one individually patched (colorOff resync, panFine/tiltFine echo, FX-stop
    // staleness) was followed by another still-live false positive. activePresetSlot now changes
    // only via /recall, /kill_fx, and a genuine /chaser running->stopped transition -- a stable
    // "last recalled slot" indicator instead of a frequently-wrong "and nothing changed since" one.
    for (int i = 1; i <= 18; i++) {
      String arg = "c" + String(i);
      if (server.hasArg(arg)) {
        int v = constrain(server.arg(arg).toInt(), 0, 255);
        dmxData[i] = (byte)v;
        // dimFX.stop() is a side effect none of /set_all's other channels have (it's how the
        // dimmer slider reclaims control from the FX). Gated on !isStaleWrite() specifically --
        // unlike the rest of this route (see the /set_all comment above), a stale echo here could
        // silently disable dimFX again right after a recall had just turned it back on, since
        // this is the one place /set_all can touch FX-active state. dimSmoothTarget is still
        // applied either way -- harmless even when stale, since dimFX overwrites it every tick
        // while genuinely active. Reported live 2026-08-25: Dimmer FX specifically (not the other
        // FX types, which the /fx & /modfx generation guard already fixed) still inconsistent
        // after switching presets.
        if(i==CH_DIMMER) { if (!isStaleWrite()) dimFX.stop(); dimSmoothTarget = v; }
        if(i==CH_PAN) centerPan16 = (v << 8) | (centerPan16 & 0xFF);
        if(i==CH_PAN_FINE) centerPan16 = (centerPan16 & 0xFF00) | v;
        if(i==CH_TILT) centerTilt16 = (v << 8) | (centerTilt16 & 0xFF);
        if(i==CH_TILT_FINE) centerTilt16 = (centerTilt16 & 0xFF00) | v;
      }
    }
    // Replies with the post-write generation, like every other mutating route. It used to send
    // server.send(200, "OK") -- the two-arg overload, which passes "OK" as the *Content-Type* and
    // leaves the body empty, so the frontend had nothing to read even if it had looked. Without a
    // generation to arm pendingGenRef against, every manually edited channel (static gobo slot,
    // colour, focus, zoom, strobe, prism, macro, gobo index) was unprotected against the 2s poll
    // merging a stale value back over a just-made edit. Reported live 2026-08-25: values picked in
    // the Programmer tab reverting within about a second and only sticking on a second attempt.
    server.send(200, "text/plain", String(bumpGen("set_all")));
  });

  server.on("/fx", []() {
      if (isStaleWrite()) { server.send(200, "text/plain", String(stateGen)); return; }
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
      server.send(200, "text/plain", String(bumpGen("fx")));
  });

  server.on("/modfx", []() {
      if (isStaleWrite()) { server.send(200, "text/plain", String(stateGen)); return; }
      String pfx = server.arg("pfx");
      Modulator* m = nullptr;
      int ch = -1; // CH9/CH11 for gr/pr -- dim restores via dimSmoothTarget instead, see below
      if (pfx == "dim") m = &dimFX; else if (pfx == "gr") { m = &gRotFX; ch = 9; } else if (pfx == "pr") { m = &pRotFX; ch = 11; }
      if(m) {
        bool startFresh = !m->active && (server.arg("a") == "1");
        m->active = (server.arg("a") == "1");
        // Clamped like /fx's spd/size and /colfx|/sgobfx|/rgobfx's st/en -- defense-in-depth for
        // values that can also arrive via NVS-persisted presets (see triggerSceneFX), per this
        // project's established pattern. Unclamped, an out-of-range value reaches the (byte) cast
        // in updateEngines() (dmxData[9]/dmxData[11] = (byte)t) as a float outside byte range, which
        // is undefined behavior, not just wraparound.
        m->startVal = constrain(server.arg("st").toInt(), 0, 255); m->endVal = constrain(server.arg("en").toInt(), 0, 255);
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
      server.send(200, "text/plain", String(bumpGen("modfx")));
  });

  server.on("/chaser", []() {
    // Same staleness guard the other FX routes carry (/fx, /modfx, /colfx, /sgobfx, /rgobfx): the
    // chaser group was the one FX group with no protection at all -- it bumped the generation but
    // never checked one, and the frontend sent no "g" either, so an echo still in flight from
    // before a recall could land after it and stomp the freshly recalled chaser config.
    if (isStaleWrite()) { server.send(200, "text/plain", String(stateGen)); return; }
    bool wasActive = chaserActive;
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
    // Only clear the active-preset indicator on a genuine running->stopped
    // transition. The frontend also hits this route routinely just to keep
    // the auto-chaser config in sync (any poll-driven correction re-sends
    // act=0 via syncFx('chaserState', ...)) -- clearing unconditionally on
    // every act!=1 call wiped out a just-recalled preset within a poll cycle
    // even though nothing about which preset is active actually changed.
    else if (wasActive && !chaserActive) { activePresetSlot = 0; }
    server.send(200, "text/plain", String(bumpGen("chaser")));
  });

  server.on("/recall", []() { triggerLoad(1, server.arg("slot").toInt()); lastRecallGen = bumpGen("recall"); server.send(200, "text/plain", String(lastRecallGen)); });
  server.on("/kill_fx", []() { moveFX.stop(); dimFX.stop(); colFX.active = false; sgobFX.active = false; rgobFX.active = false; gRotFX.stop(); pRotFX.stop(); chaserActive = false; activePresetSlot = 0; server.send(200, "text/plain", String(bumpGen("kill_fx"))); });
  server.on("/bump", []() { String t = server.arg("t"); bool s = (server.arg("s") == "1"); if(t=="blinder") bumpBlinder=s; if(t=="strobeF") bumpStrobeF=s; if(t=="strobe50") bumpStrobe50=s; if(t=="blackout") bumpBlackout=s; server.send(200, "OK"); });
  // /masterdim, /smooth and /trans likewise return the post-write generation: the frontend gates
  // master, damping and transMode against it now, so that a poll already in flight cannot merge
  // the pre-edit value back over a just-moved slider.
  server.on("/masterdim", []() { masterBrightness = server.arg("v").toFloat() / 100.0f; prefs.begin("sys", false); prefs.putFloat("mdim", masterBrightness); prefs.end(); server.send(200, "text/plain", String(bumpGen("masterdim"))); });
  server.on("/smooth", []() { dimSmoothVal = constrain(server.arg("v").toInt(), 0, 100); prefs.begin("sys", false); prefs.putInt("ds", dimSmoothVal); prefs.end(); server.send(200, "text/plain", String(bumpGen("smooth"))); });
  // /autofade, /unmute and /hwaudio return the post-write generation (rather than a bare "OK")
  // because the frontend now gates the mute and mic toggles against it -- both are optimistic
  // local writes that an already-in-flight poll could otherwise revert for a cycle.
  server.on("/autofade", []() { fadeDuration = constrain(server.arg("t").toInt(), 1, 3600000); fadeCurve = server.arg("c").toInt(); fadeStateOut = !fadeStateOut; fadeStartTime = millis(); autoFading = true; server.send(200, "text/plain", String(bumpGen("autofade"))); });
  server.on("/unmute", []() { autoFading = false; fadeStateOut = false; fadeMultiplier = 1.0f; server.send(200, "text/plain", String(bumpGen("unmute"))); });
  server.on("/trans", []() { dipToBlack = (server.arg("dip") == "1"); prefs.begin("sys", false); prefs.putBool("dip", dipToBlack); prefs.end(); server.send(200, "text/plain", String(bumpGen("trans"))); });
  server.on("/hwaudio", []() { hwAudioEnabled = (server.arg("en") == "1"); hwAudioSensitivity = constrain(server.arg("sens").toInt(), 0, 100); markAudioPrefsDirty(); server.send(200, "text/plain", String(bumpGen("hwaudio"))); });

  // Live band-energy telemetry for the AUDIO DEBUG tab's scrolling graph. Polled fast (frontend
  // targets ~15Hz) so build the response with one snprintf into a fixed buffer instead of this
  // file's usual sequential String += pattern -- that pattern is fine at the ~0.5-2Hz other
  // endpoints poll at, but repeated String heap churn at 15Hz is wasteful for a value that's just
  // a handful of ints. lo/mi/hi are the three envelope-follower bands (this project's "fake FFT"),
  // th is the live bass detection threshold they're compared against.
  server.on("/api/audio_debug", []() {
    // Sized to hold the band data plus the optional 256-bin spectrum in one response.
    static char buf[4400];
    // thM/thH are the Mid/High bands' own thresholds (FFT mode gives each band an independent one,
    // see Audio_Engine.h); fft/fg report which analysis path is live and its gain, aUs/fUs its cost.
    int n = snprintf(buf, sizeof(buf),
      "{\"lo\":%ld,\"mi\":%ld,\"hi\":%ld,\"th\":%ld,\"thM\":%ld,\"thH\":%ld,"
      "\"xb\":%d,\"xm\":%d,\"xh\":%d,"
      "\"nf\":%d,\"fa\":%d,\"fd\":%d,\"ma\":%d,\"md\":%d,\"sa\":%d,\"sd\":%d,\"mtd\":%d,\"htd\":%d,\"sens\":%d,"
      "\"fft\":%d,\"fg\":%d,\"aUs\":%lu,\"fUs\":%lu,\"flux\":%d,\"dts\":%d,"
      "\"bL\":%ld,\"mL\":%ld,\"hL\":%ld,\"bF\":%ld,\"mF\":%ld,\"hF\":%ld,"
      "\"trk\":%d,\"tap\":%d,\"tBPM\":%d,\"tScore\":%ld,\"tLag\":%ld,\"pB\":%ld,\"pH\":%ld,\"pD\":%ld,\"tmul\":%d,"
      "\"pk\":%ld,\"clip\":%d,\"bbl\":%d,\"bbh\":%d,\"bml\":%d,\"bmh\":%d,\"bhl\":%d,\"bhh\":%d,\"ig\":%d,\"db\":%d,\"dm\":%d,\"dh\":%d,\"nbin\":%d,"
      "\"bsd\":%d,\"blo\":%d,\"bhi\":%d,\"brl\":%d,\"brf\":%d,\"blk\":%d,"
      "\"sdEnv\":%ld,\"sdThr\":%ld,"
      "\"sdFloor\":%ld,\"sdPeak\":%ld,\"sdMad\":%ld,\"sdTrans\":%d,"
      "\"agree\":%d,\"agrMax\":%d,\"pfp\":%d,\"pmw\":%d,\"vmp\":%d,\"drift\":%d,\"ag\":%d,\"agPk\":%ld,\"tw\":%d,\"rclip\":%d",
      (long)lastBassEnergy, (long)lastMidEnergy, (long)lastHighEnergy, (long)lastThBass,
      (long)lastThMid, (long)lastThHigh,
      dbgBassHit ? 1 : 0, dbgMidHit ? 1 : 0, dbgHighHit ? 1 : 0,
      tuneNoiseFloor, tuneFastAttackShift, tuneFastDecayShift, tuneMidAttackShift, tuneMidDecayShift,
      tuneSlowAttackShift, tuneSlowDecayShift, tuneMidThreshDivShift, tuneHighThreshDivShift, hwAudioSensitivity,
      audioUseFFT ? 1 : 0, tuneFftGainShift, (unsigned long)audioLastUs, (unsigned long)fftLastUs,
      audioUseFlux ? 1 : 0, tuneDynThreshSmoothShift,
      (long)lastBassLevel, (long)lastMidLevel, (long)lastHighLevel,
      (long)lastBassFlux, (long)lastMidFlux, (long)lastHighFlux,
      audioUseTracker ? 1 : 0, tempoTapLock ? 1 : 0, trackedBPM, (long)trackedScore,
      (long)dbgLagMilli, (long)dbgPlainBase, (long)dbgPlainHalf, (long)dbgPlainDouble, tempoMulMode,
      (long)micPeak, micClipCount,
      tuneBinBassLo, tuneBinBassHi, tuneBinMidLo, tuneBinMidHi, tuneBinHighLo, tuneBinHighHi, tuneInputGainShift,
      tuneDetBass, tuneDetMid, tuneDetHigh, FFT_N / 2,
      sdEnabled ? 1 : 0, sdKLo, sdKHi, sdRel, sdRefShift, sdLockoutMs,
      (long)sdLastEnv, (long)sdLastThr,
      (long)sdFloor, (long)sdPeakStat, (long)sdVarMad, sdTransient ? 1 : 0,
      tempoAgreePct, tempoAgreeMaxPct, sdPeakFallPct, sdPeakMaxWaitMs, sdVarMinPct, sdClkDriftPpt,
      autoGain ? 1 : 0, (long)agPeakWin, tempoWindowMs, micRawClipCount);
    // The spectrum rides along on request rather than living at its own URL. This server handles
    // requests one at a time from the main loop, so the per-request overhead dominates for small
    // payloads: the AUDIO tab used to poll two endpoints and spent more time on round trips than
    // on data. Callers that only want the band values (a tuning script, say) simply omit spec=1
    // and pay nothing for the 256 bins.
    if (server.hasArg("spec")) {
      n += snprintf(buf + n, sizeof(buf) - n, ",\"n\":%d,\"hz\":%d,\"b\":[",
                    FFT_N / 2, SAMPLING_FREQUENCY / FFT_N);
      for (int i = 0; i < FFT_N / 2 && n < (int)sizeof(buf) - 12; i++) {
        int32_t v = fftMag[i];
        if (v > 99999) v = 99999;
        n += snprintf(buf + n, sizeof(buf) - n, "%s%ld", i ? "," : "", (long)v);
      }
      n += snprintf(buf + n, sizeof(buf) - n, "]");
    }
    snprintf(buf + n, sizeof(buf) - n, "}");
    server.send(200, "application/json", buf);
    // Latch-and-clear (see dbgBassHit's declaration in Audio_Engine.h) -- triggerBass/Mid/High
    // themselves are useless here, they get zeroed by pollAudioEngine() on the very next loop()
    // iteration regardless of whether this handler ever runs.
    dbgBassHit = false; dbgMidHit = false; dbgHighHit = false;
  });

  // Shift amounts are clamped 0-10: >=32 (word width) is undefined behavior for a right-shift, and
  // this project's int32_t band energies stay well inside that range in practice anyway.
  // Raw FFT bins for the AUDIO tab's spectrum display. Its own route rather than more fields on
  // /api/audio_debug: 128 numbers roughly double that response, and the two are polled at
  // different rates. Values are the unscaled magnitudes -- the client decides on log scaling,
  // which keeps the display honest about how much energy is really there.

  server.on("/audio_tune", []() {
    if (server.hasArg("nf"))  tuneNoiseFloor        = constrain(server.arg("nf").toInt(), 0, 2000);
    if (server.hasArg("fa"))  tuneFastAttackShift    = constrain(server.arg("fa").toInt(), 0, 10);
    if (server.hasArg("fd"))  tuneFastDecayShift     = constrain(server.arg("fd").toInt(), 0, 10);
    if (server.hasArg("ma"))  tuneMidAttackShift     = constrain(server.arg("ma").toInt(), 0, 10);
    if (server.hasArg("md"))  tuneMidDecayShift      = constrain(server.arg("md").toInt(), 0, 10);
    if (server.hasArg("sa"))  tuneSlowAttackShift    = constrain(server.arg("sa").toInt(), 0, 10);
    if (server.hasArg("sd"))  tuneSlowDecayShift     = constrain(server.arg("sd").toInt(), 0, 10);
    if (server.hasArg("mtd")) tuneMidThreshDivShift  = constrain(server.arg("mtd").toInt(), 0, 10);
    if (server.hasArg("htd")) tuneHighThreshDivShift = constrain(server.arg("htd").toInt(), 0, 10);
    // fft=0 falls back to the pre-2026-08-27 envelope-follower method without a reflash --
    // the FFT path could not be tested on hardware when it was written. fg is the FFT band
    // gain (left-shift) compensating the transform's 1/N scaling; it depends on the mic's
    // real output level, so it is tunable rather than baked in.
    if (server.hasArg("fft")) audioUseFFT = (server.arg("fft") == "1");
    // flux=0 reverts to level-based detection; dts is how fast the dynamic threshold chases
    // the signal -- the single most relevant knob for sustained-bass material, and it was
    // not reachable from outside at all until now.
    if (server.hasArg("flux")) {
      audioUseFlux = (server.arg("flux") == "1");
      tuneDetBass = tuneDetMid = tuneDetHigh = audioUseFlux ? 1 : 0;   // legacy: all three at once
    }
    // Per-band detector: 0 = band energy with envelope follower, 1 = spectral flux.
    if (server.hasArg("db")) tuneDetBass = constrain(server.arg("db").toInt(), 0, 1);
    if (server.hasArg("dm")) tuneDetMid  = constrain(server.arg("dm").toInt(), 0, 1);
    if (server.hasArg("dh")) tuneDetHigh = constrain(server.arg("dh").toInt(), 0, 1);
    if (server.hasArg("trk")) audioUseTracker = (server.arg("trk") == "1");
    if (server.hasArg("tap")) tempoTapLock = (server.arg("tap") == "1");
    // Sample-rate onset detector (the DJM-style continuous-time chain). blo/bhi are the two
    // one-pole shifts forming the bandpass, brl the envelope release, brf how slowly the
    // comparator reference tracks, blk the pulse window in ms.
    if (server.hasArg("bsd")) sdEnabled = (server.arg("bsd") == "1");
    if (server.hasArg("blo")) sdKLo = constrain(server.arg("blo").toInt(), 2, 10);
    if (server.hasArg("bhi")) sdKHi = constrain(server.arg("bhi").toInt(), 1, 9);
    if (server.hasArg("brl")) sdRel = constrain(server.arg("brl").toInt(), 2, 12);
    if (server.hasArg("brf")) sdRefShift = constrain(server.arg("brf").toInt(), 6, 14);
    if (server.hasArg("blk")) sdLockoutMs = constrain(server.arg("blk").toInt(), 60, 600);
    if (server.hasArg("tmul")) tempoMulMode = constrain(server.arg("tmul").toInt(), 0, 2);
    // Band edges in FFT bins (bin = 31.25Hz). Clamped so a band can never invert or reach
    // past the spectrum; bin 0 is DC and always excluded.
    if (server.hasArg("bbl")) tuneBinBassLo = constrain(server.arg("bbl").toInt(), 1, FFT_N / 2 - 2);
    if (server.hasArg("bbh")) tuneBinBassHi = constrain(server.arg("bbh").toInt(), tuneBinBassLo, FFT_N / 2 - 1);
    if (server.hasArg("bml")) tuneBinMidLo  = constrain(server.arg("bml").toInt(), 1, FFT_N / 2 - 2);
    if (server.hasArg("bmh")) tuneBinMidHi  = constrain(server.arg("bmh").toInt(), tuneBinMidLo, FFT_N / 2 - 1);
    if (server.hasArg("bhl")) tuneBinHighLo = constrain(server.arg("bhl").toInt(), 1, FFT_N / 2 - 2);
    if (server.hasArg("bhh")) tuneBinHighHi = constrain(server.arg("bhh").toInt(), tuneBinHighLo, FFT_N / 2 - 1);
    markAudioPrefsDirty();
    if (server.hasArg("dts")) tuneDynThreshSmoothShift = constrain(server.arg("dts").toInt(), 0, 10);
    if (server.hasArg("fg"))  tuneFftGainShift = constrain(server.arg("fg").toInt(), 0, 10);
    if (server.hasArg("ig")) { tuneInputGainShift = constrain(server.arg("ig").toInt(), 0, 5); autoGain = false; }
    if (server.hasArg("tw"))  tempoWindowMs = constrain(server.arg("tw").toInt(), 1000, 10000);
    if (server.hasArg("vmp")) sdVarMinPct   = constrain(server.arg("vmp").toInt(), 0, 200);
    if (server.hasArg("mrp")) sdMinRangePct = constrain(server.arg("mrp").toInt(), 0, 400);
    if (server.hasArg("bst")) sdBoostMaxQ8  = constrain(server.arg("bst").toInt(), 256, 4096);
    if (server.hasArg("bsh")) sdBoostShift  = constrain(server.arg("bsh").toInt(), 6, 14);
    if (server.hasArg("pfp")) sdPeakFallPct   = constrain(server.arg("pfp").toInt(), 10, 99);
    if (server.hasArg("pmw")) sdPeakMaxWaitMs = constrain(server.arg("pmw").toInt(), 10, 200);
    if (server.hasArg("agr")) tempoAgreeMaxPct = constrain(server.arg("agr").toInt(), 5, 100);
    // Auto range selection. Setting the gain by hand implies wanting it left alone, so an
    // explicit ig= switches auto off rather than fighting the next correction.
    if (server.hasArg("ag"))  autoGain = (server.arg("ag") == "1");
    if (server.hasArg("agt")) agTargetPct = constrain(server.arg("agt").toInt(), 30, 90);
    if (server.hasArg("agu")) agUpDelayMs = constrain(server.arg("agu").toInt(), 2000, 120000);
    if (server.hasArg("agd")) agDownDelayMs = constrain(server.arg("agd").toInt(), 100, 10000);
    server.send(200, "OK");
  });

  server.on("/colfx", []() {
      if (isStaleWrite()) { server.send(200, "text/plain", String(stateGen)); return; }
      colFX.active = (server.arg("a") == "1"); colFX.startVal = constrain(server.arg("st").toInt(), 0, 19); colFX.endVal = constrain(server.arg("en").toInt(), 0, 19);
      if (colFX.startVal > colFX.endVal) { int t = colFX.startVal; colFX.startVal = colFX.endVal; colFX.endVal = t; }
      colFX.holdTime = server.arg("ho").toInt(); colFX.trigger = server.arg("tr").toInt(); colFX.sync = constrain(server.arg("sy").toInt(), 0, 6);
      updateColFXStep();
      if(colFX.active) { colFX.lastStepTime = millis(); colFX.currentIdx = colFX.startVal; }
      // See /sgobfx below: on stop, land on the Programmer tab's manual CH6 value instead of the
      // FX's own last wheel position, and clear colWasActive so runStep() doesn't undo it.
      if (!colFX.active && server.hasArg("mv")) { dmxData[CH_COLOR] = (byte)constrain(server.arg("mv").toInt(), 0, 255); colWasActive = false; }
      server.send(200, "text/plain", String(bumpGen("colfx")));
  });

  server.on("/sgobfx", []() {
      if (isStaleWrite()) { server.send(200, "text/plain", String(stateGen)); return; }
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
      server.send(200, "text/plain", String(bumpGen("sgobfx")));
  });

  server.on("/rgobfx", []() {
      if (isStaleWrite()) { server.send(200, "text/plain", String(stateGen)); return; }
      rgobFX.active = (server.arg("a") == "1"); rgobFX.startVal = constrain(server.arg("st").toInt(), 0, 6); rgobFX.endVal = constrain(server.arg("en").toInt(), 0, 6);
      if (rgobFX.startVal > rgobFX.endVal) { int t = rgobFX.startVal; rgobFX.startVal = rgobFX.endVal; rgobFX.endVal = t; }
      rgobFX.holdTime = server.arg("ho").toInt(); rgobFX.trigger = server.arg("tr").toInt(); rgobFX.sync = constrain(server.arg("sy").toInt(), 0, 6); rgobFX.scratch = (server.arg("sc") == "1");
      if (server.hasArg("spd")) rgobFX.scratchSpeed = constrain(server.arg("spd").toInt(), 1, 5);
      if(rgobFX.active) { rgobFX.lastStepTime = millis(); rgobFX.currentIdx = rgobFX.startVal; }
      // See /sgobfx above: on stop, land on the Programmer tab's manual CH8 value instead of the
      // chaser's own last wheel position, and clear rgWasActive so runStep() doesn't undo it.
      if (!rgobFX.active && server.hasArg("mv")) { dmxData[CH_GOBO_ROT] = (byte)constrain(server.arg("mv").toInt(), 0, 255); rgWasActive = false; }
      server.send(200, "text/plain", String(bumpGen("rgobfx")));
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
    // Previously this only re-anchored lastBeatTime/manualTap (phase alignment) and never touched
    // globalBPM itself -- the frontend's tapped BPM only ever lived in local React state, so it got
    // overwritten by the backend's stale globalBPM on the very next ~500ms /api/state poll. Now the
    // tapped value (already computed client-side by useTapTempo) is sent along and actually applied.
    if (server.hasArg("bpm")) {
      int tappedBPM = server.arg("bpm").toInt();
      if (tappedBPM >= BPM_MIN_LIMIT && tappedBPM <= BPM_MAX_LIMIT) globalBPM = tappedBPM;
    }
    if (globalBPM > 0) {
      unsigned long interval = 60000 / globalBPM;
      lastBeatTime = now - interval;
    }
    manualTap = true;
    // A tap is an explicit statement of the tempo, so it takes over from the tracker until the
    // user hands control back (/audio_tune?tap=0, or the AUDIO tab's Tempo control).
    if (server.hasArg("bpm")) { tempoTapLock = true; markAudioPrefsDirty(); }
    // Returns the post-write generation so the frontend can gate its optimistic tapped BPM against
    // it -- a poll landing right after a tap used to snap the displayed BPM back to the pre-tap value.
    server.send(200, "text/plain", String(bumpGen("beat")));
  });
  server.on("/sync", []() {
    unsigned long now = millis();
    masterSyncTime = now;
    // Trigger==1 (BPM sync) FX recompute phase from the shared beatCount/lastBeatTime clock every
    // tick and ignore their own .phase/.modPhase field (see Modulator::process()/
    // MovementEngine::process()) -- without this reset, Hard Sync was a no-op for exactly the
    // trigger mode it's meant to fix, since the phase=0 writes below got overwritten on the next tick.
    beatCount = 0; lastBeatTime = now;
    if(moveFX.active && moveFX.trigger != 1) moveFX.modPhase = 0.0;
    if(dimFX.active && dimFX.trigger != 1) dimFX.phase = 0.0;
    if(gRotFX.active && gRotFX.trigger != 1) gRotFX.phase = 0.0;
    if(pRotFX.active && pRotFX.trigger != 1) pRotFX.phase = 0.0;
    server.send(200, "OK");
  });
  server.on("/jog", []() {
    int v = server.arg("v").toInt();
    if(v != 0) jogBend = v; else jogBend = 0;
    server.send(200, "OK");
  });
}
