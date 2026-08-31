#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <ArduinoOTA.h>
#include <ArtnetWifi.h> 
#include <ESPmDNS.h> 
#include <math.h> 
#include <Update.h> 
#include <LittleFS.h>
#include "driver/uart.h"
#include "FX_Engine.h" 
#include "Audio_Engine.h"

// Shown by the Settings panel's firmware-version line (see /api/state's "fw" field in WebAPI.h) --
// bump manually when cutting a release.
#define FW_VERSION "1.0.0"

// =========================================================
// --- 1. HARDWARE CONFIGURATION ---
// =========================================================
#define NUM_CHANNELS  18
#define CH_DIMMER     1
#define CH_STROBE     2
#define CH_PAN        3
#define CH_TILT       4
#define CH_COLOR      6
#define CH_GOBO       7
#define CH_GOBO_ROT   8
#define CH_FOCUS      13
#define CH_ZOOM       14
#define CH_PAN_FINE   15
#define CH_TILT_FINE  16

const byte wheelMap[20] = {0, 50, 5, 55, 10, 60, 15, 65, 20, 70, 25, 75, 30, 80, 35, 85, 40, 90, 45, 95};
const byte sGoboMap[10] = {0, 10, 20, 30, 40, 50, 60, 70, 80, 90}; 
const byte rGoboMap[7]  = {0, 10, 20, 30, 40, 50, 60};

const char* ap_ssid = "Moving_Head_Ctrl";   
const char* ap_password = "12345678";  
const int transmitPin = 7; 
const uart_port_t DMX_UART = UART_NUM_1;
volatile uint8_t dmxBuffer[513]; 

// =========================================================
// --- 2. GLOBAL SYSTEM STATE ---
// =========================================================
WebServer server(80);
ArtnetWifi artnet;
Preferences prefs;

byte dmxData[513]; 
String presetNames[10];

int globalBPM = 120;
unsigned long lastBeatTime = 0;
unsigned long masterSyncTime = 0;
bool beatTriggered = false;
bool manualTap = false;
// Counts real elapsed beats (incremented once per internal-metronome tick, see updateEngines()).
// Unlike masterSyncTime/lastBeatTime, which get re-anchored to "now" on every detected beat,
// this only ever increases -- it's the reference multi-beat sync divisors (>1 beat) are computed
// against, so they keep counting correctly across repeated beat-clock re-anchoring instead of
// only ever seeing "time since the last beat".
unsigned long beatCount = 0;

// Main-loop jitter diagnostic: worst gap between consecutive loop() iterations in the current
// 5s window. server.handleClient()/ArduinoOTA/WiFi can occasionally block the single ESP32-C3
// core for tens of ms; exposed via /api/state (loopMaxMs) to check whether that -- rather than
// the beat-sync/BPM logic itself -- is a contributor to observed timing glitches.
unsigned long loopLastMs = 0;
unsigned long loopMaxMs = 0;
unsigned long loopMaxWindowStart = 0;
const float syncBeats[7] = {8.0, 4.0, 2.0, 1.0, 0.5, 0.25, 0.125};
// Movement patterns take real time to trace (pan/tilt slew is finite) — a
// sub-beat divisor demands angular velocity the motor can't reach for a
// full-size shape, so MovementEngine gets its own multi-beat table instead
// of the fast dimmer/gobo-rotation divisors above.
const float moveSyncBeats[8] = {1.0, 2.0, 4.0, 8.0, 16.0, 32.0, 64.0, 128.0};

bool bumpBlackout = false; bool bumpStrobeF = false; bool bumpStrobe50 = false; bool bumpBlinder = false;
int activePresetSlot = 0;
// Bumped by every mutating route whose result the frontend needs to trust over a
// possibly-in-flight-and-now-stale poll response (recall, FX start/stop, chaser
// toggle, preset save) -- each such route returns the post-increment value instead
// of "OK", and the frontend remembers "wait for at least generation G" per field
// instead of a blind wall-clock timeout. Replaces an earlier per-field timer-based
// guard (dirtyUntilRef/isLocalDirty's original design) that had to be wired in by
// hand for every new optimistic UI update and kept missing fields (presetActive
// flickering back to "no slot" was the last instance, 2026-08-25).
uint32_t stateGen = 0;
// Generation stamped at the moment /recall last applied a preset (see WebAPI.h's /recall).
// FX-config routes (/fx, /modfx, /colfx, /sgobfx, /rgobfx) reject a request whose client-known
// generation (its "g" query arg, the frontend's own last-seen stateGen) is older than this: such
// a request was built from state that predates the recall and would silently re-apply the
// PREVIOUS preset's FX active/params over the just-recalled one if allowed through. This closes a
// real race the frontend's own bookkeeping (debounce + "keep the last-sent baseline in lockstep"
// reconciliation, see data/index.html) could not fully close on its own -- confirmed live
// 2026-08-25 via gsrc telemetry showing the identical slot recalled twice landing with different
// Dimmer FX active state each time, even after the frontend-side fixes.
uint32_t lastRecallGen = 0;
// Debug field (2026-08-25, kept permanently like op/ot and rawBPM/rawMs/loopMax below): records
// which route caused the *last* stateGen bump, exposed via /api/get_dmx's "gsrc" field. Live
// telemetry alone couldn't distinguish an HTTP-triggered activePresetSlot reset from dmxData's own
// continuous ~30ms pan/tilt output drift (see updateEngines()), so channel-diff inference between
// polls was unreliable -- this is what actually found the panFine/tiltFine /set_all echo bug (see
// data/index.html's track() comment) and is cheap enough to just leave in for the next one.
const char* lastGenSource = "";
inline uint32_t bumpGen(const char* src) { lastGenSource = src; return ++stateGen; }
int centerPan16 = 32767; int centerTilt16 = 32767;
// Fixture 0's actual per-frame Movement FX output (post-getValues(), what really goes out over
// DMX) -- centerPan16/centerTilt16 alone only show the *center* the pattern orbits, not its live
// animated position, and outDmx[] itself is local to updateEngines()/never exposed via HTTP.
// Debug-only, added 2026-08-20 to actually observe the live pan/tilt trajectory instead of
// guessing at it (see /api/get_dmx's "op"/"ot" fields).
int liveOutPan0 = 32767, liveOutTilt0 = 32767;

float joyInputX = 0.0f, joyInputY = 0.0f;
float joySmoothX = 0.0f, joySmoothY = 0.0f;
int joyMaxSpeed = 2000;
float joyCurve = 1.5f;
float joyMomentum = 0.7f;
bool joyPanRev = false, joyTiltRev = false;
int panMinLimit = 0, panMaxLimit = 65535;
int tiltMinLimit = 0, tiltMaxLimit = 65535;
bool mapIsMoving = false;
float mapTargetPan = 32767.0f, mapTargetTilt = 32767.0f;

struct Fixture { int addr; bool invP; bool invT; int phase; };
Fixture fixtures[8];
int numFixtures = 1;
int maxDmxChannel = 512; 

int dimSmoothVal = 0; 
float dimSmoothTarget = 0.0;
float dimSmoothCurrent = 0.0;
bool autoFading = false;
bool fadeStateOut = false; 
unsigned long fadeStartTime = 0;
unsigned long fadeDuration = 2000;
int fadeCurve = 3;
float fadeMultiplier = 1.0;
float masterBrightness = 1.0f;

MovementEngine moveFX;
Modulator dimFX(0, 255);
Modulator gRotFX(135, 255);
Modulator pRotFX(135, 255);
StepFX colFX, sgobFX, rgobFX;
// Tracks whether each wheel chaser was active last frame, so runStep() (below) can apply a one-shot
// stop-reset on the falling edge. Global (not local to updateEngines()) so WebAPI.h's /sgobfx and
// /rgobfx handlers can clear the relevant flag when they've already applied their own atomic
// stop-restore (the "mv" manual-value param) -- otherwise the very next runStep() call would
// immediately overwrite that restore with its own fallback (the chaser's last wheel position).
bool colWasActive = false, sgWasActive = false, rgWasActive = false;

struct SceneData { byte dmx[19]; bool fA, dA, grA, prA, cA, sgA, rgA; int fT, fTr, fSy, fSS, fSE, fZS, fZE, fMM, fMC; float fR, fMS; int dSt, dEn, dMo, dCu, dTr, dSy; float dSp; int grSt, grEn, grMo, grCu, grTr, grSy; float grSp; int prSt, prEn, prMo, prCu, prTr, prSy; float prSp; int cSt, cEn, cTr, cSy; uint32_t cHo; int sgSt, sgEn, sgTr, sgSy; uint32_t sgHo; bool sgSc; int rgSt, rgEn, rgTr, rgSy; uint32_t rgHo; bool rgSc; };
static SceneData chaserScenes[10]; 

bool chaserActive = false;
int chaserStartSlot = 0, chaserEndSlot = 3, chaserTrigger = 0, chaserSync = 3, chaserOrder = 0;
int chaserFadeTrigger = 0, chaserFadeSync = 3; 
unsigned long fadeTime = 2000, holdTime = 2000, stepStartTime = 0;
int currentSlot = 0, nextSlot = 1;
bool inFade = false;

bool dipToBlack = false;
bool isDipping = false;
int pendingLoadType = 0; 
int pendingLoadParam = 0;
unsigned long dipStartTime = 0;
int jogBend = 0; 

// =========================================================
// --- SCENE EXECUTION HELPERS ---
// =========================================================

void loadAllChaserScenes() {
  for (int i = 0; i < 10; i++) {
    prefs.begin(("sc" + String(i + 1)).c_str(), true);
    presetNames[i] = prefs.getString("n", "");
    
    SceneData sd;
    memset(&sd, 0, sizeof(SceneData));

    if (prefs.getBytes("data", &sd, sizeof(SceneData)) == sizeof(SceneData)) {
        chaserScenes[i] = sd;
    } else {
        for (int c = 1; c <= 18; c++) chaserScenes[i].dmx[c] = prefs.getUChar(String(c).c_str(), 0);
        chaserScenes[i].fA = prefs.getBool("fA", false); chaserScenes[i].fT = prefs.getInt("fT", 1); chaserScenes[i].fR = prefs.getFloat("fR", 0.0);
        chaserScenes[i].fTr = prefs.getInt("fTr", 0); chaserScenes[i].fSy = prefs.getInt("fSy", 3);
        chaserScenes[i].fSS = prefs.getInt("fSS", 50); chaserScenes[i].fSE = prefs.getInt("fSE", 50);
        chaserScenes[i].fZS = prefs.getInt("fZS", 30); chaserScenes[i].fZE = prefs.getInt("fZE", 30);
        chaserScenes[i].fMM = prefs.getInt("fMM", 0); chaserScenes[i].fMC = prefs.getInt("fMC", 0); chaserScenes[i].fMS = prefs.getFloat("fMS", 10.0);
        chaserScenes[i].dA = prefs.getBool("dA", false); chaserScenes[i].dSt = prefs.getInt("dSt", 0); chaserScenes[i].dEn = prefs.getInt("dEn", 255);
        chaserScenes[i].dMo = prefs.getInt("dMo", 0); chaserScenes[i].dCu = prefs.getInt("dCu", 0); chaserScenes[i].dSp = prefs.getFloat("dSp", 30.0);
        chaserScenes[i].dTr = prefs.getInt("dTr", 0); chaserScenes[i].dSy = prefs.getInt("dSy", 3);
        chaserScenes[i].grA = prefs.getBool("grA", false); chaserScenes[i].grSt = prefs.getInt("grSt", 135); chaserScenes[i].grEn = prefs.getInt("grEn", 190);
        chaserScenes[i].grMo = prefs.getInt("grMo", 0); chaserScenes[i].grCu = prefs.getInt("grCu", 0); chaserScenes[i].grSp = prefs.getFloat("grSp", 30.0);
        chaserScenes[i].grTr = prefs.getInt("grTr", 0); chaserScenes[i].grSy = prefs.getInt("grSy", 3);
        chaserScenes[i].prA = prefs.getBool("prA", false); chaserScenes[i].prSt = prefs.getInt("prSt", 193); chaserScenes[i].prEn = prefs.getInt("prEn", 255);
        chaserScenes[i].prMo = prefs.getInt("prMo", 0); chaserScenes[i].prCu = prefs.getInt("prCu", 0); chaserScenes[i].prSp = prefs.getFloat("prSp", 30.0);
        chaserScenes[i].prTr = prefs.getInt("prTr", 0); chaserScenes[i].prSy = prefs.getInt("prSy", 3);
        chaserScenes[i].cA = prefs.getBool("cA", false); chaserScenes[i].cSt = prefs.getInt("cSt", 0); chaserScenes[i].cEn = prefs.getInt("cEn", 0);
        chaserScenes[i].cHo = prefs.getInt("cHo", 1000); chaserScenes[i].cTr = prefs.getInt("cTr", 0); chaserScenes[i].cSy = prefs.getInt("cSy", 3);
        chaserScenes[i].sgA = prefs.getBool("sgA", false); chaserScenes[i].sgSt = prefs.getInt("sgSt", 0); chaserScenes[i].sgEn = prefs.getInt("sgEn", 0);
        chaserScenes[i].sgHo = prefs.getInt("sgHo", 1000); chaserScenes[i].sgTr = prefs.getInt("sgTr", 0); chaserScenes[i].sgSy = prefs.getInt("sgSy", 3);
        chaserScenes[i].sgSc = prefs.getBool("sgSc", false);
        chaserScenes[i].rgA = prefs.getBool("rgA", false); chaserScenes[i].rgSt = prefs.getInt("rgSt", 0); chaserScenes[i].rgEn = prefs.getInt("rgEn", 0);
        chaserScenes[i].rgHo = prefs.getInt("rgHo", 1000); chaserScenes[i].rgTr = prefs.getInt("rgTr", 0); chaserScenes[i].rgSy = prefs.getInt("rgSy", 3);
        chaserScenes[i].rgSc = prefs.getBool("rgSc", false);
    }
    prefs.end();
  }
}

// Shared by triggerSceneFX() and the /colfx HTTP handler so the parity rule
// (same-parity start/end -> step 2, else step 1) can't drift between them.
void updateColFXStep() {
  colFX.step = ((colFX.startVal % 2 == 0 && colFX.endVal % 2 == 0) || (colFX.startVal % 2 != 0 && colFX.endVal % 2 != 0)) ? 2 : 1;
}

void triggerSceneFX(int slot) {
  moveFX.active = chaserScenes[slot].fA; moveFX.type = chaserScenes[slot].fT; moveFX.rot = chaserScenes[slot].fR;
  moveFX.trigger = chaserScenes[slot].fTr; moveFX.sync = chaserScenes[slot].fSy;
  moveFX.spdSt = constrain(chaserScenes[slot].fSS, 1, 100); moveFX.spdEn = constrain(chaserScenes[slot].fSE, 1, 100); moveFX.szSt = constrain(chaserScenes[slot].fZS, 1, 100); moveFX.szEn = constrain(chaserScenes[slot].fZE, 1, 100);
  moveFX.modMo = chaserScenes[slot].fMM; moveFX.modCu = chaserScenes[slot].fMC; moveFX.modSp = chaserScenes[slot].fMS;
  if(moveFX.active) moveFX.start(); else moveFX.stop();

  dimFX.active = chaserScenes[slot].dA; dimFX.startVal = chaserScenes[slot].dSt; dimFX.endVal = chaserScenes[slot].dEn;
  dimFX.mode = chaserScenes[slot].dMo; dimFX.curve = chaserScenes[slot].dCu; dimFX.speed = chaserScenes[slot].dSp;
  dimFX.trigger = chaserScenes[slot].dTr; dimFX.sync = chaserScenes[slot].dSy;
  if(dimFX.active) dimFX.start(); else dimFX.stop();

  gRotFX.active = chaserScenes[slot].grA; gRotFX.startVal = chaserScenes[slot].grSt; gRotFX.endVal = chaserScenes[slot].grEn;
  gRotFX.mode = chaserScenes[slot].grMo; gRotFX.curve = chaserScenes[slot].grCu; gRotFX.speed = chaserScenes[slot].grSp;
  gRotFX.trigger = chaserScenes[slot].grTr; gRotFX.sync = chaserScenes[slot].grSy;
  if(gRotFX.active) gRotFX.start(); else gRotFX.stop();

  pRotFX.active = chaserScenes[slot].prA; pRotFX.startVal = chaserScenes[slot].prSt; pRotFX.endVal = chaserScenes[slot].prEn;
  pRotFX.mode = chaserScenes[slot].prMo; pRotFX.curve = chaserScenes[slot].prCu; pRotFX.speed = chaserScenes[slot].prSp;
  pRotFX.trigger = chaserScenes[slot].prTr; pRotFX.sync = chaserScenes[slot].prSy;
  if(pRotFX.active) pRotFX.start(); else pRotFX.stop();

  colFX.active = chaserScenes[slot].cA; colFX.startVal = constrain(chaserScenes[slot].cSt, 0, 19); colFX.endVal = constrain(chaserScenes[slot].cEn, 0, 19);
  colFX.holdTime = chaserScenes[slot].cHo; colFX.trigger = chaserScenes[slot].cTr; colFX.sync = chaserScenes[slot].cSy;
  updateColFXStep();
  if(colFX.active) { colFX.lastStepTime = millis(); colFX.currentIdx = colFX.startVal; }
  // Sync the *WasActive shadow flag to the state we just set directly (bypassing runStep's own
  // active->inactive transition) -- otherwise a stale wasActive=true from before this call makes
  // runStep's stop-fallback overwrite the dmxData value this function (or its caller) just loaded
  // from the scene snapshot, on the very next updateEngines() tick.
  colWasActive = colFX.active;

  sgobFX.active = chaserScenes[slot].sgA; sgobFX.startVal = constrain(chaserScenes[slot].sgSt, 0, 9); sgobFX.endVal = constrain(chaserScenes[slot].sgEn, 0, 9);
  sgobFX.holdTime = chaserScenes[slot].sgHo; sgobFX.trigger = chaserScenes[slot].sgTr; sgobFX.sync = chaserScenes[slot].sgSy; sgobFX.scratch = chaserScenes[slot].sgSc;
  if(sgobFX.active) { sgobFX.currentIdx = sgobFX.startVal; sgobFX.lastStepTime = millis(); }
  sgWasActive = sgobFX.active;

  rgobFX.active = chaserScenes[slot].rgA; rgobFX.startVal = constrain(chaserScenes[slot].rgSt, 0, 6); rgobFX.endVal = constrain(chaserScenes[slot].rgEn, 0, 6);
  rgobFX.holdTime = chaserScenes[slot].rgHo; rgobFX.trigger = chaserScenes[slot].rgTr; rgobFX.sync = chaserScenes[slot].rgSy; rgobFX.scratch = chaserScenes[slot].rgSc;
  if(rgobFX.active) { rgobFX.currentIdx = rgobFX.startVal; rgobFX.lastStepTime = millis(); }
  rgWasActive = rgobFX.active;
}

void executePreset(int slot) {
    if (slot < 1 || slot > 10) return;
    chaserActive = false; activePresetSlot = slot;
    SceneData sd = chaserScenes[slot - 1];

    for (int i = 1; i <= 18; i++) dmxData[i] = sd.dmx[i];
    
    dimSmoothTarget = dmxData[CH_DIMMER];
    centerPan16 = (dmxData[CH_PAN] << 8) | dmxData[CH_PAN_FINE];
    centerTilt16 = (dmxData[CH_TILT] << 8) | dmxData[CH_TILT_FINE];

    joySmoothX = 0.0f; joySmoothY = 0.0f; mapIsMoving = false;

    // FX-engine state is applied by triggerSceneFX (0-indexed), the same helper
    // executeChaserSlot() and the chaser fade-complete path use — single source
    // of truth instead of a second hand-copied field list here.
    triggerSceneFX(slot - 1);
}

void executeChaserSlot(int slot) {
    if (slot < 0 || slot > 9) return;
    for (int i = 1; i <= 18; i++) { if(i == 1) dimSmoothTarget = chaserScenes[slot].dmx[i]; else dmxData[i] = chaserScenes[slot].dmx[i]; }
    centerPan16 = (dmxData[CH_PAN] << 8) | dmxData[CH_PAN_FINE]; centerTilt16 = (dmxData[CH_TILT] << 8) | dmxData[CH_TILT_FINE];
    joySmoothX = 0.0f; joySmoothY = 0.0f; mapIsMoving = false;
    triggerSceneFX(slot);
}

void triggerLoad(int type, int param) {
    if (type == 1 && (param < 1 || param > 10)) return;
    if (type == 2 && (param < 0 || param > 9)) return;
    if (dipToBlack) {
        pendingLoadType = type; pendingLoadParam = param; isDipping = true; dipStartTime = millis(); autoFading = true; fadeStateOut = true; fadeStartTime = millis();
        unsigned long currentFadeTime = fadeTime; if (chaserFadeTrigger == 1) { int safeSync = constrain(chaserFadeSync, 0, 6); currentFadeTime = (unsigned long)((60000.0f / globalBPM) * syncBeats[safeSync]); }
        fadeDuration = currentFadeTime / 2; 
    } else {
        if (type == 1) executePreset(param); else if (type == 2) executeChaserSlot(param);
    }
}

void setupDMX() {
  uart_config_t config = { .baud_rate = 250000, .data_bits = UART_DATA_8_BITS, .parity = UART_PARITY_DISABLE, .stop_bits = UART_STOP_BITS_2, .flow_ctrl = UART_HW_FLOWCTRL_DISABLE };
  uart_param_config(DMX_UART, &config); uart_set_pin(DMX_UART, transmitPin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
  uart_driver_install(DMX_UART, 1024, 0, 0, NULL, 0); memset((void*)dmxBuffer, 0, 513);
}

void onArtDmx(uint16_t universe, uint16_t length, uint8_t sequence, uint8_t* data) {
  if (universe == 0) {
    bumpGen("artnet"); // debug-only (2026-08-25), see bumpGen's declaration comment
    chaserActive = false; moveFX.stop(); dimFX.stop(); colFX.active = false; sgobFX.active = false; rgobFX.active = false; gRotFX.stop(); pRotFX.stop(); activePresetSlot = 0;
    // Clear the *WasActive shadow flags too -- otherwise runStep()'s stop-fallback overwrites the
    // Art-Net byte just written below with the stopped FX's stale wheel position on this same tick,
    // violating "external DMX always wins over internal effects while active".
    colWasActive = false; sgWasActive = false; rgWasActive = false;
    for (int i = 0; i < length && i < NUM_CHANNELS; i++) dmxData[i + 1] = data[i];
  }
}

void updateEngines(unsigned long now) {
  static unsigned long lastEngUpdate = 0; float dt = (now - lastEngUpdate) / 1000.0f; if (dt <= 0) return; if (dt > 1.0f) dt = 0.02f; lastEngUpdate = now;

  static float exactPan = centerPan16;
  static float exactTilt = centerTilt16;
  if (abs(centerPan16 - (int)exactPan) > 1) exactPan = centerPan16;
  if (abs(centerTilt16 - (int)exactTilt) > 1) exactTilt = centerTilt16;

  if (joyInputX != 0.0f || joyInputY != 0.0f || fabsf(joySmoothX) > 0.001f || fabsf(joySmoothY) > 0.001f) {
      mapIsMoving = false;
      float smoothFactor = 1.0f - joyMomentum; if (smoothFactor < 0.05f) smoothFactor = 0.05f; float blend = 1.0f - powf(1.0f - smoothFactor, dt * 30.0f);
      joySmoothX += (joyInputX - joySmoothX) * blend; joySmoothY += (joyInputY - joySmoothY) * blend;
      if (fabsf(joySmoothX) < 0.001f && joyInputX == 0.0f) joySmoothX = 0.0f; if (fabsf(joySmoothY) < 0.001f && joyInputY == 0.0f) joySmoothY = 0.0f;
      // Curve is the accel ramp's DURATION in seconds (0 = instant full speed, matching a real console's
      // "curve off" expectation), not just its shape -- a fixed-duration ramp whose shape merely got
      // gentler at low curve values still forced every keypress through a multi-second ramp regardless
      // of the curve setting, which is wrong. Decoupled from the momentum blend above (which only
      // governs how fast joySmoothX tracks direction changes, not overall accel time).
      // accelMul freezes at its last value on release instead of snapping to 1.0 -- joySmoothX itself
      // already converges to the held target regardless of accelMul (accelMul only scales the output,
      // not the blend above), so snapping to 1.0 the instant the key is released would multiply that
      // already-converged joySmoothX by a sudden full-speed factor, producing a burst of movement right
      // at release instead of a smooth decay. Freezing keeps release continuous with whatever speed was
      // actually being applied a moment before, and a brief tap (small accelMul the whole time) now
      // stays small through release instead of ending in a full-speed kick.
      static float joyHoldTime = 0.0f;
      static float joyAccelMul = 1.0f;
      bool joyHeld = (fabsf(joyInputX) > 0.001f || fabsf(joyInputY) > 0.001f);
      if (joyHeld) {
        joyHoldTime += dt;
        joyAccelMul = (joyCurve <= 0.05f) ? 1.0f : constrain(joyHoldTime / joyCurve, 0.0f, 1.0f);
      } else joyHoldTime = 0.0f;
      float pD = joySmoothX * joyAccelMul * (joyMaxSpeed * 25.0f) * dt; float tD = joySmoothY * joyAccelMul * (joyMaxSpeed * 25.0f) * dt;
      
      exactPan += (joyPanRev ? pD : -pD); exactTilt += (joyTiltRev ? -tD : tD);
      exactPan = constrain(exactPan, (float)panMinLimit, (float)panMaxLimit); exactTilt = constrain(exactTilt, (float)tiltMinLimit, (float)tiltMaxLimit);
      
      centerPan16 = (int)exactPan; centerTilt16 = (int)exactTilt;
      if (!moveFX.active) { dmxData[CH_PAN] = centerPan16 >> 8; dmxData[CH_PAN_FINE] = centerPan16 & 0xFF; dmxData[CH_TILT] = centerTilt16 >> 8; dmxData[CH_TILT_FINE] = centerTilt16 & 0xFF; }
  } 
  else if (mapIsMoving) {
      float diffP = mapTargetPan - exactPan; float diffT = mapTargetTilt - exactTilt;
      float smoothFactor = 1.0f - joyMomentum; if (smoothFactor < 0.05f) smoothFactor = 0.05f; float blend = 1.0f - powf(1.0f - smoothFactor, dt * 10.0f);
      float stepP = diffP * blend; float stepT = diffT * blend;
      float maxStep = (joyMaxSpeed * 25.0f) * dt; if (fabsf(stepP) > maxStep) stepP = (stepP > 0 ? maxStep : -maxStep); if (fabsf(stepT) > maxStep) stepT = (stepT > 0 ? maxStep : -maxStep);
      
      exactPan += stepP; exactTilt += stepT; 
      exactPan = constrain(exactPan, (float)panMinLimit, (float)panMaxLimit); exactTilt = constrain(exactTilt, (float)tiltMinLimit, (float)tiltMaxLimit);
      centerPan16 = (int)exactPan; centerTilt16 = (int)exactTilt;
      
      if (fabsf(diffP) < 5.0f && fabsf(diffT) < 5.0f) { exactPan = mapTargetPan; exactTilt = mapTargetTilt; centerPan16 = mapTargetPan; centerTilt16 = mapTargetTilt; mapIsMoving = false; }
      if (!moveFX.active) { dmxData[CH_PAN] = centerPan16 >> 8; dmxData[CH_PAN_FINE] = centerPan16 & 0xFF; dmxData[CH_TILT] = centerTilt16 >> 8; dmxData[CH_TILT_FINE] = centerTilt16 & 0xFF; }
  }

  if (globalBPM > 0) { unsigned long beatInterval = 60000 / globalBPM; if (now - lastBeatTime >= beatInterval) { lastBeatTime = now; beatTriggered = true; beatCount++; } }
  // Trigger==1 (BPM sync) FX derive their phase from the shared beatCount/lastBeatTime clock every
  // tick (see Modulator::process()/MovementEngine::process()), not from their own .phase/.modPhase
  // field -- a per-FX phase=0 write here used to be silently discarded the very next updateEngines()
  // call. Resetting the shared clock instead re-aligns every trigger==1 FX to this beat at once.
  if (manualTap) { beatCount = 0; lastBeatTime = now; if (dimFX.trigger != 1) dimFX.phase = 0.0; if (gRotFX.trigger != 1) gRotFX.phase = 0.0; if (pRotFX.trigger != 1) pRotFX.phase = 0.0; if (moveFX.trigger != 1) moveFX.modPhase = 0.0; masterSyncTime = now; manualTap = false; }
  auto checkAudioTrg = [&](int trg) { return (trg == 2 && triggerBass) || (trg == 3 && triggerMid) || (trg == 4 && triggerHigh); };
  // Report the hit instead of zeroing the phase directly: the engines now anchor their cycle
  // to the beat count at that moment, so the Sync divisor applies to audio triggers as well.
  if (checkAudioTrg(dimFX.trigger)) dimFX.audioHit(); if (checkAudioTrg(gRotFX.trigger)) gRotFX.audioHit(); if (checkAudioTrg(pRotFX.trigger)) pRotFX.audioHit(); if (checkAudioTrg(moveFX.trigger)) moveFX.audioHit();

  // Continuous "how many real beats have elapsed" reference for trigger==1 (BPM sync) on any FX
  // with a multi-beat divisor -- see Modulator::process()/MovementEngine::process() for why this
  // replaced (now - masterSyncTime) % interval. beatCount only advances on confirmed whole beats;
  // the fractional term interpolates smoothly within the current beat.
  float beatIntervalMsF = globalBPM > 0 ? 60000.0f / (float)globalBPM : 500.0f;
  float beatsElapsedTotal = (float)beatCount + constrain((float)(now - lastBeatTime) / beatIntervalMsF, 0.0f, 1.0f);

  if (moveFX.active) moveFX.process(now, beatsElapsedTotal, moveSyncBeats);

  // On stop, leave CH9/CH11 as-is instead of forcing 0 -- /modfx's own mv-restore (see WebAPI.h)
  // already writes the Programmer tab's manual value there the moment the stop lands. Previously this
  // unconditionally zeroed the channel, discarding whatever manual value the user had set -- reported
  // live 2026-08-18 as gobo/prism rotation FX "changes not taking" (same symptom class as the sg/rg
  // stop race, but this half of it was a real backend clobber, not just a frontend timing race).
  if (gRotFX.active) { float t; gRotFX.process(now, beatsElapsedTotal, syncBeats, t); dmxData[9] = (byte)t; }
  if (pRotFX.active) { float t; pRotFX.process(now, beatsElapsedTotal, syncBeats, t); dmxData[11] = (byte)t; }

  if (dimFX.active) { dimFX.process(now, beatsElapsedTotal, syncBeats, dimSmoothTarget); dimSmoothCurrent = dimSmoothTarget; }
  else { if (dimSmoothVal > 0) { float sensitivity = (100.0f - dimSmoothVal) * 0.1f; dimSmoothCurrent += (dimSmoothTarget - dimSmoothCurrent) * sensitivity * dt * 10.0f; } else { dimSmoothCurrent = dimSmoothTarget; } }

  if (autoFading) {
    float progress = fadeDuration > 0 ? (float)(now - fadeStartTime) / (float)fadeDuration : 1.0f; if (progress >= 1.0f) { progress = 1.0f; autoFading = false; }
    float v = progress; if (fadeCurve == 1) v = progress * progress; else if (fadeCurve == 3) v = 0.5f - 0.5f * cosf(progress * PI); 
    fadeMultiplier = fadeStateOut ? (1.0f - v) : v; 
  } else { fadeMultiplier = fadeStateOut ? 0.0f : 1.0f; }
  
  float finalDimmer = dimSmoothCurrent * fadeMultiplier * masterBrightness;
  dmxData[CH_DIMMER] = (byte)constrain(finalDimmer, 0.0f, 255.0f);

  if (isDipping) {
      if (fadeMultiplier <= 0.02f || (now - dipStartTime) > (fadeDuration + 200)) {
          if (pendingLoadType == 1) executePreset(pendingLoadParam); else if (pendingLoadType == 2) executeChaserSlot(pendingLoadParam);
          autoFading = true; fadeStateOut = false; fadeStartTime = millis(); isDipping = false; pendingLoadType = 0;
      }
  }

  // shakeBase comes from the fixture's real DMX chart (doc/content/mapping_sheds_160w_3in1_gobo.md):
  // CH8 (rotating gobo) native shake zone starts at 226, 5 DMX units per gobo, covering gobo indices
  // 1..N (index 0 = White/Open has no shake zone). shakeBase=0 disables the native-shake fallback
  // entirely (the color wheel, CH6, has no shake function on this fixture per the same chart).
  auto runStep = [&](StepFX &fx, int channel, const byte* map, int mapLen, int shakeBase, bool &wasActive, bool rotationPulse) {
    if (fx.active) {
      bool doStep = false;
      if (fx.trigger == 0) { if (now - fx.lastStepTime >= fx.holdTime) doStep = true; }
      else if (fx.trigger == 1) { int safeSync = constrain(fx.sync, 0, 6); unsigned long interval = (60000.0 / globalBPM) * syncBeats[safeSync]; if (now - fx.lastStepTime >= interval) doStep = true; }
      else if (checkAudioTrg(fx.trigger)) doStep = true;
      if (doStep) {
        fx.lastStepTime = now; fx.currentIdx += fx.step;
        int safeEnd = constrain(fx.endVal, 0, mapLen - 1);
        int safeStart = constrain(fx.startVal, 0, mapLen - 1);
        if (fx.currentIdx > safeEnd || fx.currentIdx < 0 || fx.currentIdx >= mapLen) fx.currentIdx = safeStart;
      }
      byte val;
      // Post-step settle window: right after landing on a new gobo, hold the plain anchor value for
      // a moment before resuming the shake -- otherwise the rotation pulse (or native shake) starts
      // mid-transition and reads as choppy against the gobo-wheel step. Reported live 2026-08-18:
      // "beim gobo wechsel sollte der shake nicht laufen, sonst sieht das choppy aus."
      const unsigned long SHAKE_SETTLE_MS = 220;
      bool justStepped = (now - fx.lastStepTime) < SHAKE_SETTLE_MS;
      if (fx.scratch && fx.currentIdx > 0 && rotationPulse && !justStepped) {
        // "Rotation pulse" shake (CH7/static gobo only -- see StepFX::scratchSpeed comment in
        // FX_Engine.h for why CH8 can't use this). Confirmed live on hardware 2026-08-17: alternating
        // brief pulses into the fixture's continuous CW (100 slow .. 129 fast) and CCW (135 slow ..
        // 210 fast) rotation zones -- stop is 130 -- makes the wheel pendulum-swing around the
        // selected gobo instead of scrolling to neighbors, as long as the plain index value is
        // re-sent between pulses to re-anchor position and bound drift (the rotation zones are
        // open-loop speed control, not an absolute position seek like the index zone is).
        // Cycle: CW pulse -> re-anchor rest -> CCW pulse -> re-anchor rest.
        // Pulse duration is capped at a FIXED length (not period/4) so travel-per-pulse stays bounded
        // regardless of speed -- previously duration scaled with 1/speedHz, so slow speeds meant much
        // longer pulses and enough angular travel to drift onto the neighboring gobo ("shake ist zu
        // gross fuer langsame speeds, da rollt der gobo raus", reported live 2026-08-18). speedHz now
        // only controls how much idle/rest time separates the pulses (i.e. the shake's rhythm), not
        // how far each pulse travels.
        float speedHz = constrain(fx.scratchSpeed, 0.2f, 10.0f);
        float period = 1.0f / speedHz;
        float halfPeriod = period / 2.0f;
        const float FIXED_PULSE_S = 0.05f; // 50ms, bounds travel-per-pulse at any speed
        float pulseDur = min(FIXED_PULSE_S, halfPeriod * 0.5f);
        // Take the modulo in the integer (ms) domain first, then convert the small remainder to
        // float -- converting the raw `now` timestamp to float directly loses precision past
        // ~4.66h of uptime (float's 24-bit mantissa only represents integers exactly up to
        // 16,777,216), which made this pulse timing silently drift/jitter on long-running shows.
        unsigned long periodMs = (unsigned long)(period * 1000.0f + 0.5f);
        if (periodMs < 1) periodMs = 1;
        float t = (now % periodMs) / 1000.0f;
        int intensity = constrain(fx.scratchRange, 0, 100);
        byte cwVal = (byte)(129 - (intensity * 29) / 100);
        byte ccwVal = (byte)(135 + (intensity * 75) / 100);
        byte anchorVal = map[constrain(fx.currentIdx, 0, mapLen - 1)];
        if (t < pulseDur) val = cwVal;
        else if (t < halfPeriod) val = anchorVal;
        else if (t < halfPeriod + pulseDur) val = ccwVal;
        else val = anchorVal;
      } else if (fx.scratch && fx.currentIdx > 0 && shakeBase > 0 && !justStepped) {
        // Fixture-native shake fallback (CH8/rotating gobo): hold one of the fixture's 5 built-in
        // shake rates steady. Confirmed live on hardware 2026-08-17 that this sub-zone is 5 discrete,
        // ascending speed steps handled entirely by the fixture's own firmware, not a continuous
        // range -- a fine-grained software oscillation across it (an earlier version of this code)
        // just made the fixture rapidly cycle between its 5 built-in speeds, which read as "janky".
        int stage = constrain((int)roundf(fx.scratchSpeed), 1, 5);
        val = (byte)constrain(shakeBase + (fx.currentIdx - 1) * 5 + (stage - 1), 0, 255);
      } else {
        val = map[constrain(fx.currentIdx, 0, mapLen - 1)];
      }
      dmxData[channel] = val;
      wasActive = true;
    } else if (wasActive) {
      // Land on the plain, non-shake value for whatever gobo was last selected -- otherwise a stop
      // caught mid-shake leaves the channel sitting inside the shake/rotation zone, and the fixture
      // keeps shaking/spinning on its own (its own onboard firmware, not ours) even though we
      // consider it stopped.
      dmxData[channel] = map[constrain(fx.currentIdx, 0, mapLen - 1)];
      wasActive = false;
    }
  };
  runStep(colFX, CH_COLOR, wheelMap, sizeof(wheelMap) / sizeof(wheelMap[0]), 0, colWasActive, false);
  runStep(sgobFX, CH_GOBO, sGoboMap, sizeof(sGoboMap) / sizeof(sGoboMap[0]), 0, sgWasActive, true);
  runStep(rgobFX, CH_GOBO_ROT, rGoboMap, sizeof(rGoboMap) / sizeof(rGoboMap[0]), 226, rgWasActive, false);

  if (chaserActive && !isDipping) { 
    if (stepStartTime == 0) stepStartTime = now;
    unsigned long elapsed = now - stepStartTime; unsigned long currentFadeTime = fadeTime;
    if (chaserFadeTrigger == 1) { int safeSync = constrain(chaserFadeSync, 0, 6); currentFadeTime = (unsigned long)((60000.0f / globalBPM) * syncBeats[safeSync]); }
    if (inFade) {
      if (elapsed >= currentFadeTime) { inFade = false; stepStartTime = now; for (int i = 1; i <= 18; i++) { if(i == 1) dimSmoothTarget = chaserScenes[nextSlot].dmx[i]; else dmxData[i] = chaserScenes[nextSlot].dmx[i]; } centerPan16 = (dmxData[CH_PAN] << 8) | dmxData[CH_PAN_FINE]; centerTilt16 = (dmxData[CH_TILT] << 8) | dmxData[CH_TILT_FINE]; joySmoothX = 0.0f; joySmoothY = 0.0f; mapIsMoving = false; triggerSceneFX(nextSlot); } 
      else { float progress = currentFadeTime > 0 ? (float)elapsed / currentFadeTime : 1.0f; for (int i = 1; i <= 18; i++) { if (i==1) dimSmoothTarget = chaserScenes[currentSlot].dmx[i] + (chaserScenes[nextSlot].dmx[i] - chaserScenes[currentSlot].dmx[i]) * progress; else if (i==CH_FOCUS || i==CH_ZOOM) dmxData[i] = chaserScenes[currentSlot].dmx[i] + (chaserScenes[nextSlot].dmx[i] - chaserScenes[currentSlot].dmx[i]) * progress; } long startP = (chaserScenes[currentSlot].dmx[CH_PAN] << 8) | chaserScenes[currentSlot].dmx[CH_PAN_FINE]; long endP = (chaserScenes[nextSlot].dmx[CH_PAN] << 8) | chaserScenes[nextSlot].dmx[CH_PAN_FINE]; centerPan16 = startP + (endP - startP) * progress; long startT = (chaserScenes[currentSlot].dmx[CH_TILT] << 8) | chaserScenes[currentSlot].dmx[CH_TILT_FINE]; long endT = (chaserScenes[nextSlot].dmx[CH_TILT] << 8) | chaserScenes[nextSlot].dmx[CH_TILT_FINE]; centerTilt16 = startT + (endT - startT) * progress; if (!moveFX.active) { dmxData[CH_PAN] = centerPan16 >> 8; dmxData[CH_PAN_FINE] = centerPan16 & 0xFF; dmxData[CH_TILT] = centerTilt16 >> 8; dmxData[CH_TILT_FINE] = centerTilt16 & 0xFF; } }
    } else { 
      bool trg = false; if (chaserTrigger == 0) { if (elapsed >= holdTime) trg = true; } else if (chaserTrigger == 1) { int safeChSync = constrain(chaserSync, 0, 6); unsigned long interval = (60000.0 / globalBPM) * syncBeats[safeChSync]; if (elapsed >= interval) trg = true; } else { if (checkAudioTrg(chaserTrigger)) trg = true; if (elapsed > 3000) trg = true; }
      if (trg) { stepStartTime = now; currentSlot = nextSlot; if (chaserOrder == 1) nextSlot = random(chaserStartSlot, chaserEndSlot + 1); else { nextSlot++; if (nextSlot > chaserEndSlot) nextSlot = chaserStartSlot; } activePresetSlot = currentSlot + 1; if (dipToBlack) triggerLoad(2, nextSlot); else { inFade = true; for (int i = 1; i <= 18; i++) { if (!(i==CH_DIMMER || i==CH_PAN || i==CH_TILT || i==CH_FOCUS || i==CH_ZOOM || i==CH_PAN_FINE || i==CH_TILT_FINE)) dmxData[i] = chaserScenes[nextSlot].dmx[i]; } } }
    }
  }

  byte outDmx[513]; memset(outDmx, 0, 513); 
  for(int f=0; f<numFixtures; f++) {
      int base = fixtures[f].addr - 1; if (base < 0 || base + 18 > 512) continue; 
      for(int c=1; c<=18; c++) outDmx[base + c] = dmxData[c]; 
      int pOut = centerPan16, tOut = centerTilt16;
      if (moveFX.active) moveFX.getValues(centerPan16, centerTilt16, fixtures[f].phase, fixtures[f].invP, fixtures[f].invT, pOut, tOut); else { if (fixtures[f].invP) pOut = 65535 - pOut; if (fixtures[f].invT) tOut = 65535 - tOut; }
      // The tilt fold-avoidance fix now lives inside MovementEngine::getValues() itself (shifts
      // the pattern's center instead of reflecting individual samples -- see FX_Engine.h for why).
      // Not applied here to manual/joystick tilt, which has no pattern shape to protect.
      if (f == 0) { liveOutPan0 = pOut; liveOutTilt0 = tOut; }
      outDmx[base + CH_PAN] = pOut >> 8; outDmx[base + CH_PAN_FINE] = pOut & 0xFF; outDmx[base + CH_TILT] = tOut >> 8; outDmx[base + CH_TILT_FINE] = tOut & 0xFF;
      if (bumpBlackout) outDmx[base + CH_DIMMER] = 0; else if (bumpBlinder) { outDmx[base + CH_DIMMER] = 255; outDmx[base + CH_STROBE] = 255; outDmx[base + CH_COLOR] = 0; } else if (bumpStrobeF) { outDmx[base + CH_DIMMER] = 255; outDmx[base + CH_STROBE] = 247; } else if (bumpStrobe50) { outDmx[base + CH_DIMMER] = 255; outDmx[base + CH_STROBE] = 120; }
  }
  memcpy((void*)dmxBuffer, outDmx, 513);

  static unsigned long lastDmxOut = 0;
  if (now - lastDmxOut >= 30) {
      lastDmxOut = now; uart_set_line_inverse(DMX_UART, UART_SIGNAL_TXD_INV); delayMicroseconds(120); uart_set_line_inverse(DMX_UART, UART_SIGNAL_INV_DISABLE); delayMicroseconds(12); uart_write_bytes(DMX_UART, (const char*)dmxBuffer, maxDmxChannel + 1);
  }
}

#include "WebAPI.h"

void setup() {
  Serial.begin(115200); if(!LittleFS.begin(true)) Serial.println("FS Error");
  prefs.begin("sys", true); String sta_ssid = prefs.getString("ssid", ""), sta_pass = prefs.getString("pass", ""); 
  dimSmoothVal = constrain(prefs.getInt("ds", 0), 0, 100); masterBrightness = prefs.getFloat("mdim", 1.0f); dipToBlack = prefs.getBool("dip", false);
  joyMaxSpeed = prefs.getInt("j_msp", 2000); joyCurve = prefs.getFloat("j_crv", 1.5f); joyMomentum = prefs.getFloat("j_mom", 0.7f); joyPanRev = prefs.getBool("j_prv", false); joyTiltRev = prefs.getBool("j_trv", false); panMinLimit = prefs.getInt("j_pmi", 0); panMaxLimit = prefs.getInt("j_pma", 65535); tiltMinLimit = prefs.getInt("j_tmi", 0); tiltMaxLimit = prefs.getInt("j_tma", 65535);
  chaserStartSlot = prefs.getInt("c_st", 0); chaserEndSlot = prefs.getInt("c_en", 3); fadeTime = prefs.getInt("c_fd", 2000); holdTime = prefs.getInt("c_hd", 2000); chaserTrigger = prefs.getInt("c_tr", 0); chaserSync = prefs.getInt("c_sy", 3); chaserOrder = prefs.getInt("c_or", 0); chaserFadeTrigger = prefs.getInt("c_ftr", 0); chaserFadeSync = prefs.getInt("c_fsy", 3); prefs.end();
  // presetNames[] is populated by loadAllChaserScenes() below — no need to read it separately here first.
  prefs.begin("patch", true); numFixtures = prefs.getInt("n", 1); if (numFixtures < 1 || numFixtures > 8) numFixtures = 1;
  maxDmxChannel = 0; for(int i=0; i<numFixtures; i++) { fixtures[i].addr = prefs.getInt(("a"+String(i)).c_str(), 1 + (i*18)); fixtures[i].invP = prefs.getBool(("ip"+String(i)).c_str(), false); fixtures[i].invT = prefs.getBool(("it"+String(i)).c_str(), false); fixtures[i].phase = prefs.getInt(("ph"+String(i)).c_str(), 0); int endChan = fixtures[i].addr + 17; if (endChan > maxDmxChannel) maxDmxChannel = endChan; }
  if (maxDmxChannel > 512) maxDmxChannel = 512; if (maxDmxChannel < 18) maxDmxChannel = 18; prefs.end();
  
  if (sta_ssid != "") { 
    WiFi.mode(WIFI_STA); 
    WiFi.setTxPower(WIFI_POWER_19_5dBm);
    WiFi.setAutoReconnect(true);         
    WiFi.begin(sta_ssid.c_str(), sta_pass.c_str()); 
    int tries = 0; 
    while (WiFi.status() != WL_CONNECTED && tries < 40) { delay(500); tries++; } 
  }
  
  if (WiFi.status() != WL_CONNECTED) { 
    WiFi.mode(WIFI_AP); 
    WiFi.softAP(ap_ssid, ap_password); 
  }
  
  WiFi.setSleep(false);
  // ArduinoOTA was included and ArduinoOTA.handle() was already being called every loop, but
  // begin() was never called anywhere -- so no OTA listener was ever started and handle() did
  // nothing. OTA has therefore never actually worked, despite README.md advertising it. Starting
  // it here (station mode only; there is no point advertising OTA on the AP fallback) makes the
  // documented behaviour real and means a UI/firmware fix no longer needs physical USB access.
  if (WiFi.status() == WL_CONNECTED) { ArduinoOTA.setHostname("movinghead"); ArduinoOTA.begin(); }
  MDNS.begin("movinghead"); artnet.begin(); artnet.setArtDmxCallback(onArtDmx);
  setupAPI(); server.begin(); setupDMX(); loadAllChaserScenes(); initAudioEngine();
}

void loop() {
  unsigned long loopNow = millis();
  if (loopLastMs > 0) {
    unsigned long delta = loopNow - loopLastMs;
    // Same 5s window also clears the audio/engine cost peaks, so they cannot go stale -- and it
    // lives here rather than in pollAudioEngine() so it keeps running with the mic switched off.
    if (loopNow - loopMaxWindowStart > 5000) { loopMaxWindowStart = loopNow; loopMaxMs = delta; audioMaxUs = 0; engineMaxUs = 0; }
    else if (delta > loopMaxMs) loopMaxMs = delta;
  }
  loopLastMs = loopNow;

  server.handleClient();
  ArduinoOTA.handle();
  flushAudioPrefs();   // debounced NVS write of the audio tuning, see WebAPI.h
  artnet.read();
  pollAudioEngine();
  // Timed alongside pollAudioEngine() (see Audio_Engine.h) so the actual CPU split between the
  // audio path and the Movement soft-float maths is measurable via /api/state, rather than
  // argued about from estimates. This is the expensive one: it rebuilds the output buffer on
  // every loop iteration, ~15x more often than the 30ms DMX cadence needs.
  { uint32_t t0 = micros(); updateEngines(millis()); engineLastUs = micros() - t0;
    if (engineLastUs > engineMaxUs) engineMaxUs = engineLastUs; }
  delay(2);
}
