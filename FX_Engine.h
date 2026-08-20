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

    void process(unsigned long now, float beatsElapsedTotal, const float* syncBeats, float &outVal) {
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
            // beatsElapsedTotal increases smoothly and monotonically with real elapsed beats
            // (see updateEngines()) -- dividing by the cycle length in beats and keeping only
            // the fractional part gives a phase that wraps exactly every `sync` beats,
            // regardless of how often the beat clock itself gets re-anchored (every detected
            // beat, for live audio). Using (now - masterSyncTime) % interval here used to break
            // any divisor > 1 beat, since masterSyncTime is restamped on every single detected
            // beat -- the modulo numerator could then never grow past one beat's worth of ms.
            float cyclePos = beatsElapsedTotal / syncBeats[safeSync];
            phase = cyclePos - floorf(cyclePos);
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
    int spdSt = 10;
    int spdEn = 10;
    int szSt = 10;
    int szEn = 10;
    int modMo = 0;
    int modCu = 0;
    float modSp = 1000.0f;
    int trigger = 0;
    int sync = 3;
    float modPhase = 0.0f;

    float currentSize = 1.0f;
    float currentSpeed = 1.0f;
    float enginePhase = 0.0f;
    
    unsigned long lastUpdate = 0;

    void start() { active = true; lastUpdate = millis(); }
    void stop() { active = false; }

    void process(unsigned long now, float beatsElapsedTotal, const float* syncBeats) {
        if (lastUpdate == 0) lastUpdate = now;
        float dt = (now - lastUpdate) / 1000.0f;
        lastUpdate = now;
        if(dt <= 0 || dt > 1.0f) dt = 0.02f;

        if (trigger == 0 || trigger >= 2) {
            // modSp is presented in the UI identically to Modulator::speed (a 0-10000ms slider,
            // see TriggerBlock's "Modulation speed" / "Manual speed" labels sharing the same
            // min/max/unit) but was being treated as an arbitrary rate multiplier here instead of
            // Modulator::process()'s "period in ms" -- (modSp/100)*dt*2 makes the UI's default
            // 1000ms complete a full Size/Speed LFO cycle in ~50ms (20 cycles/sec) instead of the
            // intended 1 second. Harmless while Size/Speed Start==End (the modulator's output
            // doesn't matter if it doesn't change anything), but with Start!=End the modulator
            // raced 20x faster than the slider claimed. Match Modulator::process() exactly.
            float periodMs = modSp < 1.0f ? 1.0f : modSp;
            modPhase += (dt * 1000.0f) / periodMs;
        } else if (trigger == 1) {
            int safeSync = constrain(sync, 0, 7);
            // See Modulator::process() for why this is beat-count-based rather than
            // (now - masterSyncTime) % interval -- masterSyncTime gets re-anchored on every
            // detected beat, which broke every sync divisor above 1 beat.
            float cyclePos = beatsElapsedTotal / syncBeats[safeSync];
            modPhase = cyclePos - floorf(cyclePos);
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
        float cosR = cosf(rRad), sinR = sinf(rRad);
        float rx = x * cosR - y * sinR;
        float ry = x * sinR + y * cosR;

        if (invP) rx = -rx;
        if (invT) ry = -ry;

        // Tilt fold-avoidance for a physical fixture quirk (confirmed live 2026-08-20, this exact
        // unit -- see history.md): tilt response is non-monotonic below DMX 16-bit 32768 (8-bit
        // tilt ~127/128) -- a clean sine sweep through that point folds back on itself instead of
        // continuing, turning a circle into a self-crossing figure-8. Confirmed NOT a calibration
        // issue: the fixture's own "Tilt Calibration" was -037 (found in its onboard Sensor
        // Monitor menu); zeroing it live and re-testing the raw (uncorrected) output still
        // produced the figure-8, and made the fixture's physical level position visibly wrong
        // (no longer pointed straight up) -- -037 was a real, needed calibration value, not the
        // cause. So this is a genuine hardware/encoder response defect, not fixable from either
        // software or the fixture's own menu. An earlier version of this fix reflected individual
        // output samples off the boundary, which avoided the crossing but folded the whole shape
        // into a flattened dome/halfpipe (same effective distortion, just symmetric instead of a
        // crossing). This is the better fix: shift the pattern's *center* up just enough that its
        // full excursion never needs the broken zone at all, so the shape itself comes out
        // correct -- positioned a bit higher than the raw centerT the caller asked for, instead
        // of half-collapsed. maxTiltExcursion is a conservative bound on how far this frame's
        // rotated shape can swing on the tilt axis; |sin|+|cos| covers any `rot` value, not just
        // the untested rot==0 case. Deliberately only applied here (the Movement-FX path), not to
        // manual/joystick tilt -- no pattern shape to protect there, and auto-shifting a direct
        // drag would just fight the user's own input.
        const int TILT_FOLD_POINT = 32768;
        float maxTiltExcursion = currentSize * 32767.0f * (fabsf(sinR) + fabsf(cosR));
        int adjCenterT = centerT;
        if (adjCenterT - (int)maxTiltExcursion < TILT_FOLD_POINT) adjCenterT = TILT_FOLD_POINT + (int)maxTiltExcursion;

        outP = constrain(centerP + (int)rx, 0, 65535);
        outT = constrain(adjCenterT + (int)ry, 0, 65535);
    }
};
