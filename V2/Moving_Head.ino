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
const float syncBeats[7] = {8.0, 4.0, 2.0, 1.0, 0.5, 0.25, 0.125};

bool bumpBlackout = false; bool bumpStrobeF = false; bool bumpStrobe50 = false; bool bumpBlinder = false;
int activePresetSlot = 0;
int centerPan16 = 32767; int centerTilt16 = 32767;

// --- PATCH (FIXTURE MATRIX) ---
struct Fixture {
    int addr;
    bool invP;
    bool invT;
    int phase;
};
Fixture fixtures[8];
int numFixtures = 1;

// --- SMOOTHING & FADE STATE ---
int dimSmoothVal = 0; 
float dimSmoothTarget = 0.0;
float dimSmoothCurrent = 0.0;
bool autoFading = false;
bool fadeStateOut = false; 
unsigned long fadeStartTime = 0;
unsigned long fadeDuration = 2000;
int fadeCurve = 3;
float fadeMultiplier = 1.0;

// --- ENGINES ---
MovementEngine moveFX;
Modulator dimFX(0, 255);
Modulator gRotFX(135, 255);
Modulator pRotFX(135, 255);

struct StepFX {
  bool active = false; int startVal = 0; int endVal = 0; int step = 1;
  int trigger = 0; int sync = 3; unsigned long holdTime = 1000;
  unsigned long lastStepTime = 0; int currentIdx = 0; bool scratch = false;
};
StepFX colFX, sgobFX, rgobFX;

// =========================================================================
// --- SCENE DATA STRUCT EXPLANATION ---
// Prefixes: f=Movement, d=Dimmer, gr=GoboRot, pr=PrismRot, c=Color, sg=StaticGobo, rg=RotGobo
// Variables:
//   *A(bool): Active, *T(int): Shape Type, *R(float): Shape Rotation
//   *Tr(int): Trigger(0=Manual,1=Sync), *Sy(int): Sync Division
//   *SS,*SE(int): Speed Start/End, *ZS,*ZE(int): Size Start/End
//   *MM(int): Mod Mode, *MC(int): Mod Curve, *MS(float): Mod Speed
//   *St,*En(int): Start/End Vals, *Sp(float): Speed, *Ho(ulong): Hold ms
// =========================================================================
struct SceneData {
  byte dmx[19];
  bool fA, dA, grA, prA, cA, sgA, rgA;
  int fT, fTr, fSy, fSS, fSE, fZS, fZE, fMM, fMC;
  float fR, fMS;
  int dSt, dEn, dMo, dCu, dTr, dSy;
  float dSp;
  int grSt, grEn, grMo, grCu, grTr, grSy;
  float grSp;
  int prSt, prEn, prMo, prCu, prTr, prSy;
  float prSp;
  int cSt, cEn, cTr, cSy;
  unsigned long cHo;
  int sgSt, sgEn, sgTr, sgSy;
  unsigned long sgHo;
  bool sgSc;
  int rgSt, rgEn, rgTr, rgSy;
  unsigned long rgHo;
  bool rgSc;
};
static SceneData chaserScenes[10]; 

bool chaserActive = false;
int chaserStartSlot = 0, chaserEndSlot = 3, chaserTrigger = 0, chaserSync = 3, chaserOrder = 0;
int chaserFadeTrigger = 0, chaserFadeSync = 3; 
unsigned long fadeTime = 2000, holdTime = 2000, stepStartTime = 0;
int currentSlot = 0, nextSlot = 1;
bool inFade = false;

// =========================================================
// --- 3. DMX ENGINE (FreeRTOS) ---
// =========================================================

void setupDMX() {
  uart_config_t config = { .baud_rate = 250000, .data_bits = UART_DATA_8_BITS, .parity = UART_PARITY_DISABLE, .stop_bits = UART_STOP_BITS_2, .flow_ctrl = UART_HW_FLOWCTRL_DISABLE };
  uart_param_config(DMX_UART, &config);
  uart_set_pin(DMX_UART, transmitPin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
  uart_driver_install(DMX_UART, 1024, 0, 0, NULL, 0);
  memset((void*)dmxBuffer, 0, 513);
}

void dmxTask(void *pvParameters) {
  const TickType_t frameDelay = pdMS_TO_TICKS(23); 
  while (true) {
    uart_set_line_inverse(DMX_UART, UART_SIGNAL_TXD_INV);
    ets_delay_us(120);
    uart_set_line_inverse(DMX_UART, UART_SIGNAL_INV_DISABLE);
    ets_delay_us(12);
    uart_write_bytes(DMX_UART, (const char*)dmxBuffer, 513);
    uart_wait_tx_done(DMX_UART, portMAX_DELAY);
    vTaskDelay(frameDelay);
  }
}

// =========================================================
// --- 4. ENGINE LOGIC ---
// =========================================================

void triggerSceneFX(int slot) {
  moveFX.active = chaserScenes[slot].fA; moveFX.type = chaserScenes[slot].fT; moveFX.rot = chaserScenes[slot].fR;
  moveFX.trigger = chaserScenes[slot].fTr; moveFX.sync = chaserScenes[slot].fSy;
  moveFX.spdSt = chaserScenes[slot].fSS; moveFX.spdEn = chaserScenes[slot].fSE; moveFX.szSt = chaserScenes[slot].fZS; moveFX.szEn = chaserScenes[slot].fZE;
  moveFX.modMo = chaserScenes[slot].fMM; moveFX.modCu = chaserScenes[slot].fMC; moveFX.modSp = chaserScenes[slot].fMS;
  if(moveFX.active) moveFX.start();

  dimFX.active = chaserScenes[slot].dA; dimFX.startVal = chaserScenes[slot].dSt; dimFX.endVal = chaserScenes[slot].dEn;
  dimFX.mode = chaserScenes[slot].dMo; dimFX.curve = chaserScenes[slot].dCu; dimFX.speed = chaserScenes[slot].dSp;
  dimFX.trigger = chaserScenes[slot].dTr; dimFX.sync = chaserScenes[slot].dSy;
  if(dimFX.active) dimFX.start();

  gRotFX.active = chaserScenes[slot].grA; gRotFX.startVal = chaserScenes[slot].grSt; gRotFX.endVal = chaserScenes[slot].grEn;
  gRotFX.mode = chaserScenes[slot].grMo; gRotFX.curve = chaserScenes[slot].grCu; gRotFX.speed = chaserScenes[slot].grSp;
  gRotFX.trigger = chaserScenes[slot].grTr; gRotFX.sync = chaserScenes[slot].grSy;
  if(gRotFX.active) gRotFX.start();

  pRotFX.active = chaserScenes[slot].prA; pRotFX.startVal = chaserScenes[slot].prSt; pRotFX.endVal = chaserScenes[slot].prEn;
  pRotFX.mode = chaserScenes[slot].prMo; pRotFX.curve = chaserScenes[slot].prCu; pRotFX.speed = chaserScenes[slot].prSp;
  pRotFX.trigger = chaserScenes[slot].prTr; pRotFX.sync = chaserScenes[slot].prSy;
  if(pRotFX.active) pRotFX.start();

  colFX.active = chaserScenes[slot].cA; colFX.startVal = chaserScenes[slot].cSt; colFX.endVal = chaserScenes[slot].cEn;
  colFX.holdTime = chaserScenes[slot].cHo; colFX.trigger = chaserScenes[slot].cTr; colFX.sync = chaserScenes[slot].cSy;
  if ((colFX.startVal % 2 == 0 && colFX.endVal % 2 == 0) || (colFX.startVal % 2 != 0 && colFX.endVal % 2 != 0)) colFX.step = 2; else colFX.step = 1;
  if(colFX.active) { colFX.lastStepTime = millis(); colFX.currentIdx = colFX.startVal; }

  sgobFX.active = chaserScenes[slot].sgA; sgobFX.startVal = chaserScenes[slot].sgSt; sgobFX.endVal = chaserScenes[slot].sgEn;
  sgobFX.holdTime = chaserScenes[slot].sgHo; sgobFX.trigger = chaserScenes[slot].sgTr; sgobFX.sync = chaserScenes[slot].sgSy; sgobFX.scratch = chaserScenes[slot].sgSc;
  if(sgobFX.active) { sgobFX.currentIdx = sgobFX.startVal; sgobFX.lastStepTime = millis(); }

  rgobFX.active = chaserScenes[slot].rgA; rgobFX.startVal = chaserScenes[slot].rgSt; rgobFX.endVal = chaserScenes[slot].rgEn;
  rgobFX.holdTime = chaserScenes[slot].rgHo; rgobFX.trigger = chaserScenes[slot].rgTr; rgobFX.sync = chaserScenes[slot].rgSy; rgobFX.scratch = chaserScenes[slot].rgSc;
  if(rgobFX.active) { rgobFX.currentIdx = rgobFX.startVal; rgobFX.lastStepTime = millis(); }
}

void onArtDmx(uint16_t universe, uint16_t length, uint8_t sequence, uint8_t* data) {
  if (universe == 0) {
    chaserActive = false; moveFX.stop(); dimFX.stop(); colFX.active = false; sgobFX.active = false; rgobFX.active = false; gRotFX.stop(); pRotFX.stop();
    activePresetSlot = 0;
    for (int i = 0; i < length && i < NUM_CHANNELS; i++) dmxData[i + 1] = data[i];
  }
}

void updateEngines(unsigned long now) {
  static unsigned long lastEngUpdate = 0;
  float dt = (now - lastEngUpdate) / 1000.0f;
  if (dt <= 0) return;
  lastEngUpdate = now;

  unsigned long beatInterval = 60000 / globalBPM;
  if (now - lastBeatTime >= beatInterval) { lastBeatTime = now; beatTriggered = true; }
  
  if (manualTap) {
      if (dimFX.trigger == 1) dimFX.phase = 0.0;
      if (gRotFX.trigger == 1) gRotFX.phase = 0.0;
      if (pRotFX.trigger == 1) pRotFX.phase = 0.0;
      if (moveFX.trigger == 1) moveFX.modPhase = 0.0;
      
      masterSyncTime = now; 
      manualTap = false;
  }

  // --- LFO Engines ---
  if (moveFX.active) moveFX.process(now, masterSyncTime, globalBPM, syncBeats);
  if (gRotFX.active) { float t; gRotFX.process(now, masterSyncTime, globalBPM, syncBeats, t); dmxData[9] = (byte)t; }
  if (pRotFX.active) { float t; pRotFX.process(now, masterSyncTime, globalBPM, syncBeats, t); dmxData[11] = (byte)t; }

  // --- Smoothing ---
  if (dimFX.active) {
    dimFX.process(now, masterSyncTime, globalBPM, syncBeats, dimSmoothTarget);
    dimSmoothCurrent = dimSmoothTarget; 
  } else {
    if (dimSmoothVal > 0) {
      float sensitivity = (100.0f - dimSmoothVal) * 0.1f; 
      dimSmoothCurrent += (dimSmoothTarget - dimSmoothCurrent) * sensitivity * dt * 10.0f;
    } else {
      dimSmoothCurrent = dimSmoothTarget;
    }
  }

  if (autoFading) {
    float progress = (float)(now - fadeStartTime) / (float)fadeDuration;
    if (progress >= 1.0f) { progress = 1.0f; autoFading = false; }
    float v = progress;
    if (fadeCurve == 1) v = progress * progress; 
    else if (fadeCurve == 3) v = 0.5f - 0.5f * cosf(progress * PI); 
    fadeMultiplier = fadeStateOut ? (1.0f - v) : v; 
  } else {
    fadeMultiplier = fadeStateOut ? 0.0f : 1.0f;
  }
  
  dmxData[CH_DIMMER] = (byte)constrain(dimSmoothCurrent * fadeMultiplier, 0, 255);

  auto runStep = [&](StepFX &fx, int channel, const byte* map) {
    if (fx.active) {
      unsigned long interval = fx.trigger == 1 ? (60000.0 / globalBPM) * syncBeats[fx.sync] : fx.holdTime;
      if (now - fx.lastStepTime >= interval) {
        fx.lastStepTime = now;
        fx.currentIdx += fx.step;
        if (fx.currentIdx > fx.endVal) fx.currentIdx = fx.startVal;
        byte val = map[fx.currentIdx];
        if (fx.scratch) val = constrain(val + 183, 0, 255);
        dmxData[channel] = val;
      }
    }
  };
  runStep(colFX, CH_COLOR, wheelMap);
  runStep(sgobFX, CH_GOBO, sGoboMap);
  runStep(rgobFX, CH_GOBO_ROT, rGoboMap);

  // --- Scene Chaser ---
  if (chaserActive) {
    unsigned long elapsed = now - stepStartTime;
    
    unsigned long currentFadeTime = fadeTime;
    if (chaserFadeTrigger == 1) {
        int safeSync = constrain(chaserFadeSync, 0, 6);
        currentFadeTime = (unsigned long)((60000.0f / globalBPM) * syncBeats[safeSync]);
    }

    if (inFade) {
      if (elapsed >= currentFadeTime) {
        inFade = false; stepStartTime = now;
        for (int i = 1; i <= 18; i++) {
            if(i == 1) dimSmoothTarget = chaserScenes[nextSlot].dmx[i];
            else dmxData[i] = chaserScenes[nextSlot].dmx[i]; 
        }
        centerPan16 = (dmxData[CH_PAN] << 8) | dmxData[CH_PAN_FINE]; centerTilt16 = (dmxData[CH_TILT] << 8) | dmxData[CH_TILT_FINE];
        triggerSceneFX(nextSlot);
      } else {
        float progress = currentFadeTime > 0 ? (float)elapsed / currentFadeTime : 1.0f;
        for (int i = 1; i <= 18; i++) { 
            if (i==1) dimSmoothTarget = chaserScenes[currentSlot].dmx[i] + (chaserScenes[nextSlot].dmx[i] - chaserScenes[currentSlot].dmx[i]) * progress; 
            else if (i==13 || i==14) dmxData[i] = chaserScenes[currentSlot].dmx[i] + (chaserScenes[nextSlot].dmx[i] - chaserScenes[currentSlot].dmx[i]) * progress; 
        }
        long startP = (chaserScenes[currentSlot].dmx[CH_PAN] << 8) | chaserScenes[currentSlot].dmx[CH_PAN_FINE]; long endP = (chaserScenes[nextSlot].dmx[CH_PAN] << 8) | chaserScenes[nextSlot].dmx[CH_PAN_FINE];
        centerPan16 = startP + (endP - startP) * progress;
        long startT = (chaserScenes[currentSlot].dmx[CH_TILT] << 8) | chaserScenes[currentSlot].dmx[CH_TILT_FINE]; long endT = (chaserScenes[nextSlot].dmx[CH_TILT] << 8) | chaserScenes[nextSlot].dmx[CH_TILT_FINE];
        centerTilt16 = startT + (endT - startT) * progress;
        if (!moveFX.active) { dmxData[CH_PAN] = centerPan16 >> 8; dmxData[CH_PAN_FINE] = centerPan16 & 0xFF; dmxData[CH_TILT] = centerTilt16 >> 8; dmxData[CH_TILT_FINE] = centerTilt16 & 0xFF; }
      }
    } else { 
      bool trg = false; 
      if (chaserTrigger == 1) {
          unsigned long interval = (60000.0 / globalBPM) * syncBeats[chaserSync];
          if (elapsed >= interval) trg = true;
      } else {
          if (elapsed >= holdTime) trg = true;
      }
      if (trg) {
        inFade = true; stepStartTime = now; currentSlot = nextSlot; 
        if (chaserOrder == 1) { nextSlot = random(chaserStartSlot, chaserEndSlot + 1); } 
        else { nextSlot++; if (nextSlot > chaserEndSlot) nextSlot = chaserStartSlot; }
        
        activePresetSlot = currentSlot + 1; 
        for (int i = 1; i <= 18; i++) { if (!(i==1 || i==3 || i==4 || i==13 || i==14 || i==15 || i==16)) dmxData[i] = chaserScenes[nextSlot].dmx[i]; }
      }
    }
  }

  // =======================================================
  // --- MATRIX ENGINE: VERTEILUNG AUF GEPATCHTE LAMPEN ---
  // =======================================================
  byte outDmx[513];
  memset(outDmx, 0, 513);

  for(int f=0; f<numFixtures; f++) {
      int base = fixtures[f].addr - 1;
      if (base < 0 || base + 18 > 512) continue; 

      for(int c=1; c<=18; c++) {
          outDmx[base + c] = dmxData[c]; 
      }

      int pOut = centerPan16;
      int tOut = centerTilt16;
      
      if (moveFX.active) {
          moveFX.getValues(centerPan16, centerTilt16, fixtures[f].phase, fixtures[f].invP, fixtures[f].invT, pOut, tOut);
      } else {
          if (fixtures[f].invP) pOut = 65535 - pOut;
          if (fixtures[f].invT) tOut = 65535 - tOut;
      }

      outDmx[base + CH_PAN] = pOut >> 8;
      outDmx[base + CH_PAN_FINE] = pOut & 0xFF;
      outDmx[base + CH_TILT] = tOut >> 8;
      outDmx[base + CH_TILT_FINE] = tOut & 0xFF;

      if (bumpBlackout) { outDmx[base + CH_DIMMER] = 0; }
      else if (bumpBlinder) { outDmx[base + CH_DIMMER] = 255; outDmx[base + CH_STROBE] = 255; outDmx[base + CH_COLOR] = 0; outDmx[base + CH_GOBO] = 0; outDmx[base + CH_GOBO_ROT] = 0; }
      else if (bumpStrobeF) { outDmx[base + CH_DIMMER] = 255; outDmx[base + CH_STROBE] = 247; }
      else if (bumpStrobe50){ outDmx[base + CH_DIMMER] = 255; outDmx[base + CH_STROBE] = 120; }
  }

  memcpy((void*)dmxBuffer, outDmx, 513);
}

// =========================================================
// --- 5. API INCLUDE) ---
// =========================================================
#include "WebAPI.h"


// =========================================================
// --- 6. CORE ---
// =========================================================

void setup() {
  Serial.begin(115200);
  if(!LittleFS.begin(true)) Serial.println("FS Error");
  
  prefs.begin("sys", true);
  String sta_ssid = prefs.getString("ssid", ""); 
  String sta_pass = prefs.getString("pass", ""); 
  dimSmoothVal = prefs.getInt("ds", 0);
  prefs.end();

  for(int i=0; i<10; i++) {
    prefs.begin(("sc" + String(i+1)).c_str(), true);
    presetNames[i] = prefs.getString("n", "");
    prefs.end();
  }

  prefs.begin("patch", true);
  numFixtures = prefs.getInt("n", 1);
  if (numFixtures < 1 || numFixtures > 8) numFixtures = 1;
  for(int i=0; i<numFixtures; i++) {
      fixtures[i].addr = prefs.getInt(("a"+String(i)).c_str(), 1 + (i*18));
      fixtures[i].invP = prefs.getBool(("ip"+String(i)).c_str(), false);
      fixtures[i].invT = prefs.getBool(("it"+String(i)).c_str(), false);
      fixtures[i].phase = prefs.getInt(("ph"+String(i)).c_str(), 0);
  }
  prefs.end();

  if (sta_ssid != "") { 
    WiFi.mode(WIFI_STA); 
    WiFi.begin(sta_ssid.c_str(), sta_pass.c_str()); 
    int tries = 0; 
    while (WiFi.status() != WL_CONNECTED && tries < 20) { delay(500); tries++; } 
  }
  if (WiFi.status() != WL_CONNECTED) { 
    WiFi.mode(WIFI_AP); 
    WiFi.softAP(ap_ssid, ap_password); 
  }
  WiFi.setSleep(false); 

  MDNS.begin("movinghead"); 
  artnet.begin(); 
  artnet.setArtDmxCallback(onArtDmx);
  
  setupAPI(); 
  server.serveStatic("/", LittleFS, "/index.html");
  server.begin();
  
  setupDMX();
  xTaskCreate(dmxTask, "DMX", 4096, NULL, 3, NULL);
}

void loop() {
  server.handleClient();
  ArduinoOTA.handle();
  artnet.read();
  updateEngines(millis());
}
