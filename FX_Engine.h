#pragma once
#include <Arduino.h>
#include <math.h>

// =========================================================
// --- STEP FX (Color Wheels & Static Gobos) ---
// =========================================================
struct StepFX {
    bool active = false;
    int startVal = 0;
    int endVal = 0;
    int step = 1;
    int trigger = 0;
    int sync = 3;
    unsigned long holdTime = 1000;
    unsigned long lastStepTime = 0;
    int currentIdx = 0;
    bool scratch = false;
    // Shake tuning. Meaning depends on which channel/technique runStep() uses for this StepFX
    // (see Moving_Head_Horizon.ino):
    //  - CH7 (static gobo, sgobFX): "rotation pulse" shake -- scratchSpeed is the oscillation rate in
    //    Hz (continuous), scratchRange is 0-100% intensity (how far into the fixture's continuous CW/
    //    CCW rotation zones each pulse reaches). Confirmed live on hardware 2026-08-17: alternating
    //    brief CW/CCW rotation pulses around the currently selected gobo, with the plain index value
    //    re-sent between pulses to re-anchor position, makes the wheel pendulum-swing in place instead
    //    of scrolling through neighboring gobos -- a real, continuously adjustable shake instead of the
    //    fixture's native 5 fixed steps.
    //  - CH8 (rotating gobo, rgobFX): CH8 has no documented counter-rotation zone (only CH7 does), so
    //    the rotation-pulse technique isn't available there without hijacking CH9 (which the separate
    //    Rotation FX already owns). Keeps using the fixture's native 5-stage shake zone instead --
    //    scratchSpeed here is the stage to hold (int 1-5), scratchRange is unused.
    // Deliberately NOT added to SceneData/NVS -- SceneData is a raw sizeof()-checked binary blob (see
    // backlog.md "Tech Debt"), and growing it would reset every currently-saved real preset on this
    // device to defaults on next boot. Live-only for now: takes effect immediately, resets to default
    // on preset/chaser recall or reboot.
    float scratchSpeed = 3.0f;
    int scratchRange = 40;
};

// =========================================================
// --- LFO MODULATOR (Dimmer, Prism & Gobo Rotation) ---
// =========================================================
class Modulator {
public:
    bool active = false;
    int startVal = 0;
    int endVal = 255;
    int mode = 2;
    int curve = 0;
    float speed = 30.0f;
    int trigger = 0;
    int sync = 3;
    float phase = 0.0f;
    
    unsigned long lastUpdate = 0; 

    Modulator(int minV, int maxV) { startVal = minV; endVal = maxV; }
    void start() { active = true; lastUpdate = millis(); }
    void stop() { active = false; }

    float getLFO(float p, int m, int c) {
        float val = 0.0f;
        // Mode: 0=Forward (Saw), 1=PingPong (Triangle), 2=Reverse (Decay)
        if (m == 0) val = p;
        else if (m == 1) val = p < 0.5f ? p * 2.0f : 2.0f - (p * 2.0f);
        else if (m == 2) val = 1.0f - p;

        // Curve: 0=Linear, 1=Quad, 2=Cubic, 3=Sine, 4=Gauss, 5=Random
        if (c == 0) return val;
        if (c == 1) return val * val;
        if (c == 2) return val * val * val;
        if (c == 3) return 0.5f - 0.5f * cosf(val * PI);
        if (c == 4) { float x = (val - 0.5f)*2.0f; return expf(-(x*x)*5.0f); }
        if (c == 5) return random(0, 1000) / 1000.0f; 
        return val;
    }

    void process(unsigned long now, unsigned long masterSyncTime, int globalBPM, const float* syncBeats, float &outVal) {
        if (lastUpdate == 0) lastUpdate = now;
        float dt = (now - lastUpdate) / 1000.0f;
        lastUpdate = now;
        if(dt <= 0 || dt > 1.0f) dt = 0.02f; 

        if (trigger == 0 || trigger >= 2) {
            // speed is the full LFO cycle duration in ms (matches the UI slider's ms range).
            float periodMs = speed < 1.0f ? 1.0f : speed;
            phase += (dt * 1000.0f) / periodMs;
        } else if (trigger == 1) {
            int safeSync = constrain(sync, 0, 6);
            unsigned long interval = (60000.0f / globalBPM) * syncBeats[safeSync];
            phase = (float)((now - masterSyncTime) % interval) / interval;
        }

        if (phase > 1.0f) phase -= 1.0f;
        if (phase < 0.0f) phase += 1.0f;

        float lfo = getLFO(phase, mode, curve);
        outVal = startVal + (endVal - startVal) * lfo;
    }
};

// =========================================================
// --- KINEMATICS ENGINE (Pan & Tilt Movements) ---
// =========================================================
class MovementEngine {
public:
    bool active = false;
    int type = 1;
    float rot = 0.0f;
    int spdSt = 50;
    int spdEn = 50;
    int szSt = 30;
    int szEn = 30;
    int modMo = 0;
    int modCu = 0;
    float modSp = 10.0f;
    int trigger = 0;
    int sync = 3;
    float modPhase = 0.0f;

    float currentSize = 1.0f;
    float currentSpeed = 1.0f;
    float enginePhase = 0.0f;
    
    unsigned long lastUpdate = 0;

    void start() { active = true; lastUpdate = millis(); }
    void stop() { active = false; }

    void process(unsigned long now, unsigned long masterSyncTime, int globalBPM, const float* syncBeats) {
        if (lastUpdate == 0) lastUpdate = now;
        float dt = (now - lastUpdate) / 1000.0f;
        lastUpdate = now;
        if(dt <= 0 || dt > 1.0f) dt = 0.02f;

        if (trigger == 0 || trigger >= 2) {
            modPhase += (modSp / 100.0f) * dt * 2.0f;
        } else if (trigger == 1) {
            int safeSync = constrain(sync, 0, 7);
            unsigned long interval = (60000.0f / globalBPM) * syncBeats[safeSync];
            modPhase = (float)((now - masterSyncTime) % interval) / interval;
        }
        if (modPhase > 1.0f) modPhase -= 1.0f;
        if (modPhase < 0.0f) modPhase += 1.0f;
        
        float mVal = modPhase;
        if (modMo == 1) mVal = modPhase < 0.5f ? modPhase * 2.0f : 2.0f - (modPhase * 2.0f);
        else if (modMo == 2) mVal = 1.0f - modPhase;

        if (modCu == 1) mVal = mVal * mVal;
        else if (modCu == 3) mVal = 0.5f - 0.5f * cosf(mVal * PI);

        currentSize = (szSt + (szEn - szSt) * mVal) / 100.0f;
        currentSpeed = (spdSt + (spdEn - spdSt) * mVal) / 100.0f;

        if (trigger == 1) {
            // Beat-locked: derive the pattern position directly from the
            // beat clock (modPhase is already phase-exact) instead of
            // integrating currentSpeed, so one revolution always starts
            // exactly on a beat and completes exactly at the end of
            // `sync` beats, with no drift and no dependency on frame timing.
            enginePhase = modPhase * PI * 2.0f;
        } else {
            enginePhase += currentSpeed * dt * 5.0f;
            if (enginePhase > PI * 2.0f) enginePhase -= PI * 2.0f;
        }
    }

    void getValues(int centerP, int centerT, int fixturePhase, bool invP, bool invT, int &outP, int &outT) {
        float pOffset = (fixturePhase / 360.0f) * PI * 2.0f;
        float p = enginePhase + pOffset;
        float x = 0, y = 0;

        switch(type) {
            case 1: x = sinf(p); y = cosf(p); break; 
            case 2: x = sinf(p); y = sinf(p*2.0f); break; 
            case 3: { float s2p = sinf(p*2.0f); x = s2p*cosf(p); y = s2p*sinf(p); } break;
            case 4: x = (sinf(p) > 0 ? 1 : -1); y = (cosf(p) > 0 ? 1 : -1); break;
            case 5: { float c2p = cosf(p*2.0f); x = sinf(p)*c2p; y = cosf(p)*c2p; } break;
            case 6: x = sinf(p) * 0.5f + sinf(p*2.5f) * 0.5f; y = cosf(p); break;
            case 7: x = sinf(p*3.0f); y = cosf(p*4.0f); break;
            case 8: x = sinf(p); y = 0; break;
            case 9: x = 0; y = sinf(p); break;
            case 10: { float sizeMod = 0.5f + 0.5f*cosf(p*0.5f); x = sinf(p)*sizeMod; y = cosf(p)*sizeMod; } break;
            case 11: x = sinf(p*1.3f); y = cosf(p*1.7f); break; 
            case 12: x = sinf(p); y = sinf(p) * cosf(p); break; 
            default: x = sinf(p); y = cosf(p); break;
        }

        x *= currentSize * 32767.0f;
        y *= currentSize * 32767.0f;

        float rRad = rot * (PI / 180.0f);
        float rx = x * cosf(rRad) - y * sinf(rRad);
        float ry = x * sinf(rRad) + y * cosf(rRad);

        if (invP) rx = -rx;
        if (invT) ry = -ry;

        outP = constrain(centerP + (int)rx, 0, 65535);
        outT = constrain(centerT + (int)ry, 0, 65535);
    }
};
