#pragma once
#include <Arduino.h>

extern void loadAllChaserScenes();
extern bool scenesSaveFile();
extern bool scenesLoadFile();
extern const char* SCENES_PATH;

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
    // Only a MANUALLY set gain is stored. Persisting the auto-ranged value would make whatever
    // a loud passage happened to force into the shift the next boot's starting point -- and a
    // gain latched at zero is precisely the state auto-ranging then struggles to climb out of.
    if (!autoGain) prefs.putInt("a_ig", tuneInputGainShift);
    prefs.putInt("a_tw", tempoWindowMs);
    prefs.putInt("a_vmp", sdVarMinPct);
    prefs.putInt("a_mrp", sdMinRangePct);
    prefs.putInt("a_sam", sdMid.sensAdd);
    prefs.putInt("a_sah", sdHigh.sensAdd);
    prefs.putInt("a_bst", sdBoostMaxQ8);
    prefs.putInt("a_bsh", sdBoostShift);
    prefs.putInt("a_pfp", sdPeakFallPct);
    prefs.putInt("a_pmw", sdPeakMaxWaitMs);
    prefs.putInt("a_agr", tempoAgreeMaxPct);
    prefs.putInt("a_slew", tempoSlewPct);
    prefs.putInt("a_jcf", tempoJumpConfirm);
    prefs.putBool("a_ag", autoGain);
    prefs.putBool("a_auto", tempoAuto);
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
    prefs.putBool("a_sab", sdAllBands);
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
    tuneFastAttackShift = constrain(prefs.getInt("a_fa", tuneFastAttackShift), 0, 10);
    tuneFastDecayShift = constrain(prefs.getInt("a_fd", tuneFastDecayShift), 0, 10);
    // Clamped on the way in, mirroring /audio_tune's ranges. These are SHIFT amounts: a stored
    // value out of range (older firmware, a hand-edited NVS, a partial write) is undefined
    // behavior at the shift site, not merely a bad-sounding setting.
    tuneMidAttackShift = constrain(prefs.getInt("a_ma", tuneMidAttackShift), 0, 10);
    tuneMidDecayShift = constrain(prefs.getInt("a_md", tuneMidDecayShift), 0, 10);
    tuneSlowAttackShift = constrain(prefs.getInt("a_sa", tuneSlowAttackShift), 0, 10);
    tuneSlowDecayShift = constrain(prefs.getInt("a_sd", tuneSlowDecayShift), 0, 10);
    tuneDynThreshSmoothShift = constrain(prefs.getInt("a_dts", tuneDynThreshSmoothShift), 0, 10);
    tuneMidThreshDivShift = constrain(prefs.getInt("a_mtd", tuneMidThreshDivShift), 0, 10);
    tuneHighThreshDivShift = constrain(prefs.getInt("a_htd", tuneHighThreshDivShift), 0, 10);
    tuneFftGainShift = constrain(prefs.getInt("a_fg", tuneFftGainShift), 0, 10);
    tempoWindowMs = prefs.getInt("a_tw", tempoWindowMs);
    sdVarMinPct = prefs.getInt("a_vmp", sdVarMinPct);
    sdMinRangePct = prefs.getInt("a_mrp", sdMinRangePct);
    sdMid.sensAdd = prefs.getInt("a_sam", sdMid.sensAdd);
    sdHigh.sensAdd = prefs.getInt("a_sah", sdHigh.sensAdd);
    sdBoostMaxQ8 = prefs.getInt("a_bst", sdBoostMaxQ8);
    sdBoostShift = constrain(prefs.getInt("a_bsh", sdBoostShift), 6, 14);
    sdPeakFallPct = prefs.getInt("a_pfp", sdPeakFallPct);
    sdPeakMaxWaitMs = prefs.getInt("a_pmw", sdPeakMaxWaitMs);
    tempoAgreeMaxPct = prefs.getInt("a_agr", tempoAgreeMaxPct);
    tempoSlewPct = prefs.getInt("a_slew", tempoSlewPct);
    tempoJumpConfirm = prefs.getInt("a_jcf", tempoJumpConfirm);
    autoGain = prefs.getBool("a_ag", autoGain);
    // The stored input gain is restored only when the gain is MANUAL -- and this has to come
    // after autoGain is known, which is why it is here and not up with the other tune* loads.
    // With auto-gain on, a_ig holds whatever a past loud passage happened to force, and a
    // stored 0 is a trap with no way out: too little gain, so no onsets, so the climb (which is
    // gated on incoming beats) never unlocks. Observed on the fixture 2026-09-03 -- ig=0, ag=1,
    // input at 2.6%, zero onsets, and it only recovered because a tap armed the gain for 15s.
    // Booting from the default is what the comment at `autoGain` has always claimed happens.
    if (!autoGain) tuneInputGainShift = constrain(prefs.getInt("a_ig", tuneInputGainShift), 0, 5);
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
    sdAllBands = prefs.getBool("a_sab", sdAllBands);
    sdKLo = prefs.getInt("a_blo", sdKLo);
    sdKHi = prefs.getInt("a_bhi", sdKHi);
    sdRel = prefs.getInt("a_brl", sdRel);
    sdRefShift = prefs.getInt("a_brf", sdRefShift);
    sdLockoutMs = prefs.getInt("a_blk", sdLockoutMs);
    hwAudioEnabled = prefs.getBool("a_en", hwAudioEnabled);
    // Must be read while the handle is still open. It previously sat below prefs.end(),
    // where a read silently returns the default instead of failing -- so the tempo mode
    // looked stored but came back as "auto" after every restart.
    tempoAuto = prefs.getBool("a_auto", true);
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
  // Auto is the resting state and must survive a restart. The old code forced the latch off at
  // boot because the tapped value it existed for did not survive one; now the MODE persists and
  // the anchor does not, which is the right way round.
  tapAnchorBPM = 0;
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

  
  if (!LittleFS.exists("/index.html.gz")) {
    server.on("/", HTTP_GET, []() {
      String html = "<!DOCTYPE html><html><head><title>Setup Mode</title></head><body style='background:#121212;color:white;text-align:center;'><h2>SYSTEM SETUP</h2><p>No UI installed. Upload data/index.html.gz (the gzipped build, not the raw HTML).</p><form method='POST' action='/upload_gui' enctype='multipart/form-data'><input type='file' name='update'><input type='submit' value='INSTALL'></form></body></html>";
      server.send(200, "text/html", html);
    });

    server.on("/upload_gui", HTTP_POST, []() {
      server.send(200, "text/plain", "Successful! Rebooting...");
      delay(1000); ESP.restart();
    }, []() {
      HTTPUpload& upload = server.upload();
      if (upload.status == UPLOAD_FILE_START) { if (LittleFS.exists("/index.html.gz")) LittleFS.remove("/index.html.gz"); fsUploadFile = LittleFS.open("/index.html.gz", "w"); }
      else if (upload.status == UPLOAD_FILE_WRITE) { if (fsUploadFile) fsUploadFile.write(upload.buf, upload.currentSize); }
      else if (upload.status == UPLOAD_FILE_END) { if (fsUploadFile) fsUploadFile.close(); }
    });
  } else {
    // no-store: index.html changes with every UI fix/flash, and this library sends neither
    // ETag nor Last-Modified for it, so a plain browser reload had nothing to revalidate against
    // and could keep serving a stale, already-open tab's in-memory bundle indefinitely -- looked
    // like a live frontend/backend desync bug when it was actually just stale JS. Reported live
    // 2026-08-20 (dimFxRunning shown as active locally while /api/get_dmx's "dA" was already 0).
    // Served gzipped: the filesystem partition is the tight budget on this device, and the
    // uncompressed UI was ~216KB of 896KB. streamFile() sees the ".gz" name and sets
    // Content-Encoding itself -- adding it by hand duplicates the header and breaks the page
    // (see the vendor routes below for the same note). Cache-Control has to be set explicitly
    // though, and it matters: this library sends neither ETag nor Last-Modified, so without
    // no-store a browser can keep serving an already-open tab's stale bundle indefinitely,
    // which reads as a frontend/backend desync bug. Reported live 2026-08-20.
    server.on("/", HTTP_GET, []() {
      File f = LittleFS.open("/index.html.gz", "r");
      if (!f) { server.send(404, "text/plain", "no UI installed"); return; }
      server.sendHeader("Cache-Control", "no-store");
      server.streamFile(f, "text/html");
      f.close();
    });
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

    json += "\"fA\":" + String(moveFX.active?1:0) + ",\"fT\":" + String(moveFX.type) + ",\"fR\":" + String(moveFX.rot) + ",\"fTr\":" + String(moveFX.trigger) + ",\"fSy\":" + String(moveFX.sync) + ",\"fSS\":" + String(moveFX.spdSt) + ",\"fSE\":" + String(moveFX.spdEn) + ",\"fZS\":" + String(moveFX.szSt) + ",\"fZE\":" + String(moveFX.szEn) + ",\"fMM\":" + String(moveFX.modMo) + ",\"fMC\":" + String(moveFX.modCu) + ",\"fMS\":" + String(moveFX.modSp) + "," + "\"fBn\":" + String(moveFX.burst) + ",\"fRp\":" + String(moveFX.rasterSync) + ",\"fSg\":" + String(moveFX.spacingSync) + ",";
    json += "\"dA\":" + String(dimFX.active?1:0) + ",\"dSt\":" + String(dimFX.startVal) + ",\"dEn\":" + String(dimFX.endVal) + ",\"dMo\":" + String(dimFX.mode) + ",\"dCu\":" + String(dimFX.curve) + ",\"dSp\":" + String(dimFX.speed) + ",\"dTr\":" + String(dimFX.trigger) + ",\"dSy\":" + String(dimFX.sync) + "," + "\"dBn\":" + String(dimFX.burst) + ",\"dRp\":" + String(dimFX.rasterSync) + ",\"dSg\":" + String(dimFX.spacingSync) + ",";
    json += "\"grA\":" + String(gRotFX.active?1:0) + ",\"grSt\":" + String(gRotFX.startVal) + ",\"grEn\":" + String(gRotFX.endVal) + ",\"grMo\":" + String(gRotFX.mode) + ",\"grCu\":" + String(gRotFX.curve) + ",\"grSp\":" + String(gRotFX.speed) + ",\"grTr\":" + String(gRotFX.trigger) + ",\"grSy\":" + String(gRotFX.sync) + "," + "\"grBn\":" + String(gRotFX.burst) + ",\"grRp\":" + String(gRotFX.rasterSync) + ",\"grSg\":" + String(gRotFX.spacingSync) + ",";
    json += "\"prA\":" + String(pRotFX.active?1:0) + ",\"prSt\":" + String(pRotFX.startVal) + ",\"prEn\":" + String(pRotFX.endVal) + ",\"prMo\":" + String(pRotFX.mode) + ",\"prCu\":" + String(pRotFX.curve) + ",\"prSp\":" + String(pRotFX.speed) + ",\"prTr\":" + String(pRotFX.trigger) + ",\"prSy\":" + String(pRotFX.sync) + "," + "\"prBn\":" + String(pRotFX.burst) + ",\"prRp\":" + String(pRotFX.rasterSync) + ",\"prSg\":" + String(pRotFX.spacingSync) + ",";
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
    json += ",\"rawBPM\":" + String(lastRawDetectedBPM) + ",\"rawMs\":" + String(lastRawIntervalMs) + ",\"loopMax\":" + String(loopMaxMs) + ",\"lps\":" + String(loopsPerSec) + ",\"asmEvery\":" + String(dmxAssembleEveryLoop ? 1 : 0) + ",\"joyWd\":" + String(joyWatchdogTrips) + ",\"tAuto\":" + String(tempoAuto ? 1 : 0) + ",\"tAnchor\":" + String(tapAnchorBPM) + ",\"bld\":\"" + String(__DATE__ " " __TIME__) + "\""
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

    // Default-constructed, NOT memset to zero. Every field carries its proper default in the
    // struct now, so a field this handler forgets to assign keeps a sensible value instead of
    // silently becoming 0 -- which for things like dSy (sync divisor) or fT (pattern type) is
    // not a neutral value but a different effect.
    SceneData sd;

    for(int i=1; i<=18; i++) sd.dmx[i] = dmxData[i];
    
    sd.fA = moveFX.active; sd.fT = moveFX.type; sd.fR = moveFX.rot; 
    sd.fSS = moveFX.spdSt; sd.fSE = moveFX.spdEn; sd.fZS = moveFX.szSt; sd.fZE = moveFX.szEn; 
    sd.fMM = moveFX.modMo; sd.fMC = moveFX.modCu; sd.fMS = moveFX.modSp; 
    sd.fTr = moveFX.trigger; sd.fSy = moveFX.sync;

    sd.dA = dimFX.active; sd.dSt = dimFX.startVal; sd.dEn = dimFX.endVal; 
    sd.dMo = dimFX.mode; sd.dCu = dimFX.curve; sd.dSp = dimFX.speed; 
    sd.dTr = dimFX.trigger; sd.dSy = dimFX.sync;
    sd.dBn = dimFX.burst;  sd.dRp = dimFX.rasterSync;
    sd.grBn = gRotFX.burst; sd.grRp = gRotFX.rasterSync;
    sd.prBn = pRotFX.burst; sd.prRp = pRotFX.rasterSync;
    sd.fBn = moveFX.burst;  sd.fRp = moveFX.rasterSync;
    sd.dSg = dimFX.spacingSync; sd.grSg = gRotFX.spacingSync;
    sd.prSg = pRotFX.spacingSync; sd.fSg = moveFX.spacingSync;

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

    // In-memory first, then one write of the whole file. Scenes are keyed JSON on LittleFS now,
    // not a struct blob per NVS namespace -- see the note above loadAllChaserScenes().
    chaserScenes[s - 1] = sd;
    if (!scenesSaveFile()) { server.send(500, "text/plain", "storage error"); return; }
    server.send(200, "text/plain", String(bumpGen("save")));
  });

  // Re-saves only a slot's pan/tilt center from the current live position,
  // leaving every other already-saved parameter (FX, colors, gobos, speeds)
  // untouched -- lets a pre-programmed slot be re-aimed on site without the
  // full load/edit/save round trip through the Programmer tab.
  // Download the scenes as a file, and put one back. This is what the keyed-JSON format buys
  // beyond safety: the programmed show is a thing you can keep, diff and restore, instead of
  // living only in an opaque flash partition.
  server.on("/api/scenes", HTTP_GET, []() {
    File f = LittleFS.open(SCENES_PATH, "r");
    if (!f) { server.send(404, "text/plain", "no scenes file"); return; }
    server.sendHeader("Content-Disposition", "attachment; filename=\"scenes.json\"");
    server.streamFile(f, "application/json");
    f.close();
  });

  // Restore. The upload is written to a temporary file and only swapped in once it has been
  // PARSED successfully -- a truncated or hand-mangled file must not be able to replace a
  // working show. On success the slots are reloaded so the restore takes effect immediately,
  // without a reboot.
  server.on("/api/scenes", HTTP_POST, []() {
    if (!LittleFS.exists("/scenes.up")) { server.send(400, "text/plain", "no upload"); return; }
    File chk = LittleFS.open("/scenes.up", "r");
    JsonDocument probe;
    DeserializationError err = deserializeJson(probe, chk);
    bool ok = !err && !probe["s"].isNull();
    chk.close();
    if (!ok) {
      LittleFS.remove("/scenes.up");
      server.send(400, "text/plain", String("not a valid scenes file: ") + (err ? err.c_str() : "missing \"s\" array"));
      return;
    }
    LittleFS.remove(SCENES_PATH);
    if (!LittleFS.rename("/scenes.up", SCENES_PATH)) {
      LittleFS.remove("/scenes.up");
      server.send(500, "text/plain", "could not replace scenes file"); return;
    }
    scenesLoadFile();
    server.send(200, "text/plain", String(bumpGen("scenes_restore")));
  }, []() {
    HTTPUpload& up = server.upload();
    if (up.status == UPLOAD_FILE_START) {
      LittleFS.remove("/scenes.up");
      fsUploadFile = LittleFS.open("/scenes.up", "w");
    } else if (up.status == UPLOAD_FILE_WRITE) {
      if (fsUploadFile) fsUploadFile.write(up.buf, up.currentSize);
    } else if (up.status == UPLOAD_FILE_END) {
      if (fsUploadFile) fsUploadFile.close();
    }
  });

  server.on("/save_center", []() {
    int s = server.arg("slot").toInt();
    if (s < 1 || s > 10) { server.send(400, "text/plain", "invalid slot"); return; }

    chaserScenes[s - 1].dmx[CH_PAN]       = (byte)(centerPan16 >> 8);
    chaserScenes[s - 1].dmx[CH_PAN_FINE]  = (byte)(centerPan16 & 0xFF);
    chaserScenes[s - 1].dmx[CH_TILT]      = (byte)(centerTilt16 >> 8);
    chaserScenes[s - 1].dmx[CH_TILT_FINE] = (byte)(centerTilt16 & 0xFF);

    if (!scenesSaveFile()) { server.send(500, "text/plain", "storage error"); return; }
    server.send(200, "OK");
  });

  server.on("/joy_cfg", []() {
    joyMaxSpeed = constrain(server.arg("spd").toInt(), 1, 20000);
    if (server.hasArg("ratep")) ptMaxRatePan  = constrain(server.arg("ratep").toInt(), 2000, 200000);
    if (server.hasArg("ratet")) ptMaxRateTilt = constrain(server.arg("ratet").toInt(), 2000, 200000);
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
    prefs.putInt("j_msp", joyMaxSpeed); prefs.putInt("j_rtp", ptMaxRatePan); prefs.putInt("j_rtt", ptMaxRateTilt); prefs.putFloat("j_crv", joyCurve); prefs.putFloat("j_mom", joyMomentum);
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
    String json = "{\"ratep\":" + String(ptMaxRatePan) + ",\"ratet\":" + String(ptMaxRateTilt) + ",\"spd\":" + String(joyMaxSpeed) + ",\"crv\":" + String(joyCurve, 2) +
                  ",\"mom\":" + String(joyMomentum * 100.0f, 0) +
                  ",\"prv\":" + String(joyPanRev ? 1 : 0) + ",\"trv\":" + String(joyTiltRev ? 1 : 0) +
                  ",\"pmin\":" + String(panMinLimit >> 8) + ",\"pmax\":" + String(panMaxLimit >> 8) +
                  ",\"tmin\":" + String(tiltMinLimit >> 8) + ",\"tmax\":" + String(tiltMaxLimit >> 8) + "}";
    server.send(200, "application/json", json);
  });

  server.on("/joy_in", []() {
    lastJoyInMs = millis();          // feeds the dead-man switch in updateEngines()
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
      if (server.hasArg("bn")) moveFX.burst = constrain(server.arg("bn").toInt(), 1, 8);
      if (server.hasArg("rp")) moveFX.rasterSync = constrain(server.arg("rp").toInt(), -1, MOVE_SYNC_BEATS_COUNT - 1);
      if (server.hasArg("sg")) moveFX.spacingSync = constrain(server.arg("sg").toInt(), -1, MOVE_SYNC_BEATS_COUNT - 1);
      // Clamped to 1 (not 0): a 0% size collapses the movement pattern's amplitude to a single point
      // (fixture sits static at dead center) with the FX still reporting "running" -- looks identical
      // to a stuck/broken FX. Frontend sliders already enforce min=1, this is defense-in-depth for
      // values that can also arrive via NVS-persisted presets, per this project's established pattern.
      moveFX.spdSt = constrain(server.arg("ss").toInt(), 1, 100); moveFX.spdEn = constrain(server.arg("se").toInt(), 1, 100);
      moveFX.szSt = constrain(server.arg("zs").toInt(), 1, 100); moveFX.szEn = constrain(server.arg("ze").toInt(), 1, 100);
      moveFX.modMo = server.arg("mm").toInt(); moveFX.modCu = server.arg("mc").toInt();
      moveFX.modSp = server.arg("ms").toFloat(); moveFX.trigger = server.arg("tr").toInt();
      moveFX.sync = constrain(server.arg("sy").toInt(), 0, MOVE_SYNC_BEATS_COUNT - 1);
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
        // Burst count and raster divisor. Optional: a client that does not send them leaves
        // the effect exactly as it was, which is what keeps older UI builds working.
        if (server.hasArg("bn")) m->burst = constrain(server.arg("bn").toInt(), 1, 8);
        if (server.hasArg("rp")) m->rasterSync = constrain(server.arg("rp").toInt(), -1, SYNC_BEATS_COUNT - 1);
        if (server.hasArg("sg")) m->spacingSync = constrain(server.arg("sg").toInt(), -1, SYNC_BEATS_COUNT - 1);
        m->startVal = constrain(server.arg("st").toInt(), 0, 255); m->endVal = constrain(server.arg("en").toInt(), 0, 255);
        m->speed = server.arg("sp").toFloat(); m->mode = server.arg("mo").toInt();
        m->curve = server.arg("cu").toInt(); m->trigger = server.arg("tr").toInt();
        m->sync = constrain(server.arg("sy").toInt(), 0, SYNC_BEATS_COUNT - 1);
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
    if (server.hasArg("sync")) chaserSync = constrain(server.arg("sync").toInt(), 0, SYNC_BEATS_COUNT - 1);
    if (server.hasArg("ord")) chaserOrder = server.arg("ord").toInt();
    if (server.hasArg("f_trg")) chaserFadeTrigger = server.arg("f_trg").toInt();
    if (server.hasArg("f_sync")) chaserFadeSync = constrain(server.arg("f_sync").toInt(), 0, SYNC_BEATS_COUNT - 1);

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
  // A/B switch for the output-assembly cadence, so the change can be measured on the device
  // rather than argued about. ?every=1 restores the old every-loop behaviour.
  server.on("/api/asmmode", []() {
    if (server.hasArg("every")) dmxAssembleEveryLoop = (server.arg("every") == "1");
    server.send(200, "text/plain", String(dmxAssembleEveryLoop ? 1 : 0));
  });

  server.on("/hwaudio", []() { hwAudioEnabled = (server.arg("en") == "1"); if (server.hasArg("sens")) hwAudioSensitivity = constrain(server.arg("sens").toInt(), 0, 100); markAudioPrefsDirty(); server.send(200, "text/plain", String(bumpGen("hwaudio"))); });

  // Live band-energy telemetry for the AUDIO DEBUG tab's scrolling graph. Polled fast (frontend
  // targets ~15Hz) so build the response with one snprintf into a fixed buffer instead of this
  // file's usual sequential String += pattern -- that pattern is fine at the ~0.5-2Hz other
  // endpoints poll at, but repeated String heap churn at 15Hz is wasteful for a value that's just
  // a handful of ints. lo/mi/hi are the three envelope-follower bands (this project's "fake FFT"),
  // th is the live bass detection threshold they're compared against.
  server.on("/api/audio_debug", []() {
    // Renew the FFT's lease: detection does not need it, only this display does, so it runs
    // while somebody is on the AUDIO tab and stops a couple of seconds after they leave.
    fftWantedUntil = millis() + 2000;
    // Sized to hold the band data plus the optional 256-bin spectrum in one response.
    static char buf[4400];
    // thM/thH are the Mid/High bands' own thresholds (FFT mode gives each band an independent one,
    // see Audio_Engine.h); fft/fg report which analysis path is live and its gain, aUs/fUs its cost.
    int n = snprintf(buf, sizeof(buf),
      "{\"lo\":%ld,\"mi\":%ld,\"hi\":%ld,\"th\":%ld,\"thM\":%ld,\"thH\":%ld,"
      "\"xb\":%d,\"xm\":%d,\"xh\":%d,"
      "\"nf\":%d,\"fa\":%d,\"fd\":%d,\"ma\":%d,\"md\":%d,\"sa\":%d,\"sd\":%d,\"mtd\":%d,\"htd\":%d,\"sens\":%d,"
      "\"fg\":%d,\"aUs\":%lu,\"fUs\":%lu,\"dts\":%d,"
      "\"bL\":%ld,\"mL\":%ld,\"hL\":%ld,\"bF\":%ld,\"mF\":%ld,\"hF\":%ld,"
      "\"tap\":%d,\"tBPM\":%d,\"tScore\":%ld,\"tLag\":%ld,\"tmul\":%d,"
      "\"pk\":%ld,\"clip\":%d,\"bbl\":%d,\"bbh\":%d,\"bml\":%d,\"bmh\":%d,\"bhl\":%d,\"bhh\":%d,\"ig\":%d,\"db\":%d,\"dm\":%d,\"dh\":%d,\"nbin\":%d,"
      "\"blo\":%d,\"bhi\":%d,\"brl\":%d,\"brf\":%d,\"blk\":%d,"
      "\"sdEnv\":%ld,\"sdThr\":%ld,\"bOn\":%ld,"
      "\"sdFloor\":%ld,\"sdPeak\":%ld,\"sdMad\":%ld,\"sdTrans\":%d,"
      "\"agree\":%d,\"agrMax\":%d,\"pfp\":%d,\"pmw\":%d,\"vmp\":%d,\"mrp\":%d,\"sam\":%d,\"sah\":%d,\"drift\":%d,\"ag\":%d,\"agPk\":%ld,\"tw\":%d,\"rclip\":%d,\"sab\":%d,\"mOn\":%ld,\"hOn\":%ld",
      (long)lastBassEnergy, (long)lastMidEnergy, (long)lastHighEnergy, (long)lastThBass,
      (long)lastThMid, (long)lastThHigh,
      dbgBassHit ? 1 : 0, dbgMidHit ? 1 : 0, dbgHighHit ? 1 : 0,
      tuneNoiseFloor, tuneFastAttackShift, tuneFastDecayShift, tuneMidAttackShift, tuneMidDecayShift,
      tuneSlowAttackShift, tuneSlowDecayShift, tuneMidThreshDivShift, tuneHighThreshDivShift, hwAudioSensitivity,
      tuneFftGainShift, (unsigned long)audioLastUs, (unsigned long)fftLastUs,
      tuneDynThreshSmoothShift,
      (long)lastBassLevel, (long)lastMidLevel, (long)lastHighLevel,
      (long)lastBassFlux, (long)lastMidFlux, (long)lastHighFlux,
      tempoAuto ? 0 : 1, trackedBPM, (long)trackedScore,
      (long)dbgLagMilli, tempoMulMode,
      (long)micPeak, micClipCount,
      tuneBinBassLo, tuneBinBassHi, tuneBinMidLo, tuneBinMidHi, tuneBinHighLo, tuneBinHighHi, tuneInputGainShift,
      tuneDetBass, tuneDetMid, tuneDetHigh, FFT_N / 2,
      sdKLo, sdKHi, sdRel, sdRefShift, sdLockoutMs,
      (long)sdLastEnv, (long)sdLastThr,
      // The bass onset the engine last acted on, whichever detector produced it. A latched
      // flag (xb) can only ever say "at least one since you asked"; a timestamp lets the
      // INTERVALS be histogrammed, which is what distinguishes finding a grid from spraying.
      (long)bassOnsetUsedMs,
      (long)sdFloor, (long)sdPeakStat, (long)sdVarMad, sdTransient ? 1 : 0,
      tempoAgreePct, tempoAgreeMaxPct, sdPeakFallPct, sdPeakMaxWaitMs, sdVarMinPct, sdMinRangePct, sdMid.sensAdd, sdHigh.sensAdd, sdClkDriftPpt,
      autoGain ? 1 : 0, (long)agPeakWin, tempoWindowMs, micRawClipCount, sdAllBands ? 1 : 0,
      (long)sdMid.lastOnsetMs, (long)sdHigh.lastOnsetMs);
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

  // Every tunable the AUDIO tab can reach. Shift amounts are clamped 0-10 (or tighter where the
  // value is also used as `N - shift`): >= the word width is undefined behavior for a shift, and
  // this project's int32_t band energies stay well inside that range in practice anyway. The same
  // ranges are applied again when these are read back from NVS -- see loadAudioPrefs().
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
    // Per-band detector: 0 = band energy with envelope follower, 1 = spectral flux. This
    // replaced a single `flux` switch that set all three bands at once; per-band is what the
    // detector actually wants, since bass reads better on level and mid/high on flux.
    if (server.hasArg("db")) tuneDetBass = constrain(server.arg("db").toInt(), 0, 1);
    if (server.hasArg("dm")) tuneDetMid  = constrain(server.arg("dm").toInt(), 0, 1);
    if (server.hasArg("dh")) tuneDetHigh = constrain(server.arg("dh").toInt(), 0, 1);
    // tap=1 means "hold what was tapped", i.e. the inverse of auto. Both spellings are kept
    // because the AUDIO tab sends tap and the TAP button sends auto; there is only one flag.
    if (server.hasArg("tap")) { tempoAuto = (server.arg("tap") != "1"); markAudioPrefsDirty(); }
    if (server.hasArg("auto")) { tempoAuto = (server.arg("auto") == "1");
                                 if (tempoAuto) tapAnchorBPM = 0; markAudioPrefsDirty(); }
    // Sample-rate onset detector (the DJM-style continuous-time chain). blo/bhi are the two
    // one-pole shifts forming the bandpass, brl the envelope release, brf how slowly the
    // comparator reference tracks, blk the pulse window in ms.
    if (server.hasArg("sab")) sdAllBands = (server.arg("sab") == "1");
    if (server.hasArg("blo")) sdKLo = constrain(server.arg("blo").toInt(), 2, 10);
    if (server.hasArg("bhi")) sdKHi = constrain(server.arg("bhi").toInt(), 1, 9);
    // Cross-check, not just two independent clamps: lp1 runs at kHi and lp2 at kLo, so the
    // band only exists while kHi < kLo. blo=2&bhi=9 passed both clamps and inverted it, which
    // leaves the detector running on noise with nothing in the UI to say so.
    if (sdKHi >= sdKLo) sdKHi = sdKLo - 1;
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
    if (server.hasArg("sam")) sdMid.sensAdd  = constrain(server.arg("sam").toInt(), -50, 100);
    if (server.hasArg("sah")) sdHigh.sensAdd = constrain(server.arg("sah").toInt(), -50, 100);
    if (server.hasArg("bst")) sdBoostMaxQ8  = constrain(server.arg("bst").toInt(), 256, 4096);
    if (server.hasArg("bsh")) sdBoostShift  = constrain(server.arg("bsh").toInt(), 6, 14);
    if (server.hasArg("pfp")) sdPeakFallPct   = constrain(server.arg("pfp").toInt(), 10, 99);
    if (server.hasArg("pmw")) sdPeakMaxWaitMs = constrain(server.arg("pmw").toInt(), 10, 200);
    if (server.hasArg("slew")) tempoSlewPct = constrain(server.arg("slew").toInt(), 1, 50);
    if (server.hasArg("jcf"))  tempoJumpConfirm = constrain(server.arg("jcf").toInt(), 1, 30);
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
      colFX.holdTime = server.arg("ho").toInt(); colFX.trigger = server.arg("tr").toInt(); colFX.sync = constrain(server.arg("sy").toInt(), 0, SYNC_BEATS_COUNT - 1);
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
      sgobFX.holdTime = server.arg("ho").toInt(); sgobFX.trigger = server.arg("tr").toInt(); sgobFX.sync = constrain(server.arg("sy").toInt(), 0, SYNC_BEATS_COUNT - 1); sgobFX.scratch = (server.arg("sc") == "1");
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
      rgobFX.holdTime = server.arg("ho").toInt(); rgobFX.trigger = server.arg("tr").toInt(); rgobFX.sync = constrain(server.arg("sy").toInt(), 0, SYNC_BEATS_COUNT - 1); rgobFX.scratch = (server.arg("sc") == "1");
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
    // A tap anchors the tracker rather than shutting it off. In auto mode the tracker keeps
    // measuring and its answer is folded onto the rung this tap identified; in manual mode the
    // tapped value simply stands. Either way the mode is unchanged -- tapping is not a mode
    // switch, and treating it as one cost the user automatic tracking for a whole set.
    if (server.hasArg("bpm")) {
      int t = server.arg("bpm").toInt();
      // Re-capture the raw measurement this anchor is being set against, so its expiry is
      // judged from here rather than from whatever the previous anchor was measuring.
      if (t >= BPM_MIN_LIMIT && t <= BPM_MAX_LIMIT) { tapAnchorBPM = t; tapAnchorRaw = 0; tapAnchorRawMiss = 0; tapAnchorMiss = 0; }
    }
    // A tap also permits auto-gain to climb again. The gain may otherwise only rise while
    // kicks are being detected, which can deadlock: too little gain to detect anything, and
    // therefore no reason to add gain. Tapping asserts that there is a beat to find.
    agTapArmUntil = millis() + AG_TAP_ARM_MS;
    {
    }
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
