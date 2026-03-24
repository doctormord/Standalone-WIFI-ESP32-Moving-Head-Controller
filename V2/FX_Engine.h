#pragma once
#include <Arduino.h>

enum TriggerMode { TRIG_MANUAL = 0, TRIG_SYNC = 1 };
enum CurveType { CRV_LINEAR = 0, CRV_QUAD = 1, CRV_CUBIC = 2, CRV_SINE = 3, CRV_GAUSS = 4, CRV_RANDOM = 5 };

// =========================================================
// --- ULTRA-FAST MATH HELPERS (BRANCHLESS) ---
// =========================================================
#define LUT_SIZE 1024
#define RAD_TO_IDX (162.974661726f) // = 1024 / TWO_PI
#define OFFSET_POS (6283.18530718f) // = 1000 * TWO_PI (Verhindert negative Floats vor dem Cast)

static float sinLUT[LUT_SIZE];
static bool lutInit = false;

inline void initFastMath() {
    if (lutInit) return;
    for (int i = 0; i < LUT_SIZE; i++) {
        sinLUT[i] = sinf((float)i * TWO_PI / LUT_SIZE);
    }
    lutInit = true;
}

// 1. Branchless Sinus (Bitmaske übernimmt das Wrapping)
inline float fastSin(float rad) {
    int idx = (int)((rad + OFFSET_POS) * RAD_TO_IDX);
    return sinLUT[idx & (LUT_SIZE - 1)];
}

// 2. Branchless Cosinus (+90 Grad Phasenversatz direkt im Index)
inline float fastCos(float rad) {
    int idx = (int)((rad + OFFSET_POS) * RAD_TO_IDX) + (LUT_SIZE / 4);
    return sinLUT[idx & (LUT_SIZE - 1)];
}

// 3. Ultra-Fast RNG (Bitwise AND + Multiply statt teurem Modulo)
inline float fastRand() {
    return (float)(esp_random() & 0xFFFF) * 0.00001525902f; // Multiplikation mit 1/65535
}

// 4. Zentraler Curve-Renderer (ohne expf)
inline float applyCurve(float p, int curve, float lastRnd, float nextRnd) {
    switch(curve) {
        case CRV_LINEAR: return p;
        case CRV_QUAD:   return p * p;
        case CRV_CUBIC:  return p * p * p;
        case CRV_SINE:   return 0.5f - 0.5f * fastCos(p * PI); 
        case CRV_GAUSS: {
            float d = (p - 0.5f) * 6.0f;
            return 1.0f / (1.0f + d * d); // Algebraische Annäherung (10x schneller als expf)
        }
        case CRV_RANDOM: return lastRnd + (nextRnd - lastRnd) * p;
        default:         return p;
    }
}

// =========================================================
// --- LFO MODULATOR ---
// =========================================================
class Modulator {
  public:
    bool active = false;
    int startVal = 0, endVal = 255;
    int mode = 1, curve = 0, trigger = TRIG_MANUAL, sync = 3;      
    float speed = 1.0f, phase = 0.0f;
    float lastRand = 0.0f, nextRand = 0.0f;
    unsigned long lastUpdate = 0;

    Modulator(int s, int e) { startVal = s; endVal = e; }
    
    void start() { 
        initFastMath(); 
        active = true; phase = 0.0f; lastUpdate = millis(); 
        nextRand = fastRand(); 
    }
    void stop() { active = false; }

    void process(unsigned long now, unsigned long lastSyncTime, int globalBPM, const float* syncBeatsArray, float &outTarget) {
        if (!active) return;
        unsigned long delta = now - lastUpdate;
        if (delta < 1) return; 
        float dt = delta * 0.001f; // Multiplikation statt Division
        lastUpdate = now;

        if (trigger == TRIG_SYNC) {
            int safeSync = sync > 6 ? 6 : (sync < 0 ? 0 : sync); // Inline Constrain
            float beatsPerCycle = syncBeatsArray[safeSync];
            float totalBeatsPassed = ((now - lastSyncTime) * 0.001f) * (globalBPM * 0.01666666f); // /60
            
            float rawPhase = totalBeatsPassed / beatsPerCycle;
            phase = rawPhase - (int)rawPhase; // Branchless fmodf
            if (phase < 0.0f) phase += 1.0f; 
        } else {
            phase += speed * dt; 
            if (mode == 2 && phase >= 1.0f) { 
                phase = 1.0f; active = false; 
            } else { 
                if (phase >= 1.0f) { 
                    phase -= (int)phase; // Branchless Wrap statt while
                    lastRand = nextRand; nextRand = fastRand(); 
                }
            }
        }
        
        float p = phase; 
        if (mode == 1) p = (p < 0.5f) ? (p * 2.0f) : (2.0f - p * 2.0f); 
        float v = applyCurve(p, curve, lastRand, nextRand);
        outTarget = startVal + v * (endVal - startVal);
    }
};

// =========================================================
// --- MOVEMENT ENGINE ---
// =========================================================
class MovementEngine {
  public:
    bool active = false;
    int type = 1, trigger = TRIG_MANUAL, sync = 3;
    float rot = 0.0f, modSp = 0.5f;
    int spdSt = 50, spdEn = 50, szSt = 30, szEn = 30, modMo = 1, modCu = 0;

    float modPhase = 0.0f, theta = 0.0f;
    float lastRand = 0.0f, nextRand = 0.0f;
    unsigned long lastUpdate = 0;
    
    float currentSize = 0.0f;
    float cosRot = 1.0f;
    float sinRot = 0.0f;

    void start() { 
        initFastMath();
        active = true; modPhase = 0.0f; lastUpdate = millis(); 
        nextRand = fastRand();
    }
    void stop() { active = false; }

    void process(unsigned long now, unsigned long lastSyncTime, int globalBPM, const float* syncBeatsArray) {
        if (!active) return;
        unsigned long delta = now - lastUpdate;
        if (delta < 1) return; 
        float dt = delta * 0.001f; 
        lastUpdate = now;

        modPhase += modSp * dt;
        if (modPhase >= 1.0f) { 
            modPhase -= (int)modPhase; 
            lastRand = nextRand; nextRand = fastRand(); 
        }
        
        float p = modPhase; 
        if (modMo == 1) p = (p < 0.5f) ? (p * 2.0f) : (2.0f - p * 2.0f); 
        float v = applyCurve(p, modCu, lastRand, nextRand);
        currentSize = (szSt + v * (szEn - szSt)) * 0.01f; 

        if (trigger == TRIG_SYNC) { 
            int safeSync = sync > 6 ? 6 : (sync < 0 ? 0 : sync);
            float beatsPerCycle = syncBeatsArray[safeSync];
            float totalBeatsPassed = ((now - lastSyncTime) * 0.001f) * (globalBPM * 0.01666666f);
            
            float rawTheta = (totalBeatsPassed / beatsPerCycle) * TWO_PI;
            theta = rawTheta - (int)(rawTheta * 0.15915494f) * TWO_PI; // Branchless Modulo TWO_PI
            if (theta < 0.0f) theta += TWO_PI;
        } else { 
            float currentSpd = (spdSt + v * (spdEn - spdSt)) * 0.05f; 
            theta += currentSpd * dt; 
            // Simple if statt while reicht aus, da dt winzig ist
            if (theta >= TWO_PI) theta -= TWO_PI;
            else if (theta < 0.0f) theta += TWO_PI;
        }

        // Rotationsmatrix pre-calculate
        cosRot = fastCos(rot);
        sinRot = fastSin(rot);
    }

    void getValues(int centerPan, int centerTilt, float phaseOffsetDeg, bool invP, bool invT, int &outP, int &outT) {
        float localTheta = theta + (phaseOffsetDeg * 0.0174532925f);
        if (localTheta >= TWO_PI) localTheta -= TWO_PI;
        else if (localTheta < 0.0f) localTheta += TWO_PI;

        float x = 0.0f, y = 0.0f;
        
        // Basis-Winkel nur 1x laden
        float sinT = fastSin(localTheta);
        float cosT = fastCos(localTheta);

        switch(type) { 
            case 1: x = cosT; y = sinT; break; // Circle
            case 2: {
                // ANGLE DOUBLING: sin(2*T) = 2 * sin(T) * cos(T)
                x = cosT; y = (2.0f * sinT * cosT) * 0.5f; 
                break;
            }
            case 3: { 
                // ANGLE DOUBLING x2: cos(4*T) 
                float cos2T = cosT*cosT - sinT*sinT;
                float sin2T = 2.0f * sinT * cosT;
                float cos4T = cos2T*cos2T - sin2T*sin2T;
                x = cos4T * cosT; y = cos4T * sinT; 
                break; 
            }
            case 4: {
                // Inline Constrain (schneller als Makro)
                x = 1.5f * cosT; if(x > 1.0f) x = 1.0f; else if(x < -1.0f) x = -1.0f;
                y = 1.5f * sinT; if(y > 1.0f) y = 1.0f; else if(y < -1.0f) y = -1.0f;
                break; 
            }
            case 5: x = cosT * cosT * cosT; y = sinT * sinT * sinT; break; 
            case 6: x = cosT; y = fastSin(5.0f*localTheta) * 0.3f; break; // Selten -> LUT OK
            case 7: x = fastCos(3.0f*localTheta); y = fastSin(4.0f*localTheta); break; 
            case 8: x = sinT; y = 0.0f; break; 
            case 9: x = 0.0f; y = sinT; break; 
            case 10:{ 
                // ANGLE DOUBLING: sin(4*T) / cos(4*T)
                float cos2T = cosT*cosT - sinT*sinT;
                float sin2T = 2.0f * sinT * cosT;
                float cos4T = cos2T*cos2T - sin2T*sin2T;
                float sin4T = 2.0f * sin2T * cos2T;
                
                // Halb-Winkel sin(T*0.5) triggern wir noch über LUT
                float r = (fastSin(localTheta*0.5f)+1.0f)*0.5f; 
                x = r*cos4T; y = r*sin4T; 
                break; 
            }
            case 11: x = cosT * fastSin(localTheta*0.7f); y = fastSin(localTheta*1.3f) * fastCos(localTheta*0.4f); break; 
            case 12: {
                // Infinity Loop / Lemniskate
                x = sinT; y = (2.0f * sinT * cosT) * 0.5f; 
                break; 
            }
        }

        // Matrix-Multiplikation
        float rotX = x * cosRot - y * sinRot; 
        float rotY = x * sinRot + y * cosRot;

        // Scale Pre-Calc (verhindert Multiplikation im Cast)
        float scale = currentSize * 32767.0f;
        int pCalc = centerPan + (int)(rotX * scale);
        int tCalc = centerTilt + (int)(rotY * scale);

        // Inline Constrain (Sicherheit gegen Integer-Überläufe)
        if (pCalc > 65535) pCalc = 65535; else if (pCalc < 0) pCalc = 0;
        if (tCalc > 65535) tCalc = 65535; else if (tCalc < 0) tCalc = 0;

        // Invertierungen (Mirroring für Patch) anwenden
        if (invP) pCalc = 65535 - pCalc;
        if (invT) tCalc = 65535 - tCalc;

        outP = pCalc;
        outT = tCalc;
    }
};