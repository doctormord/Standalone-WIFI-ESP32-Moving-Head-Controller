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

// Shared LFO shaping, used by BOTH Modulator and MovementEngine.
//
// These two used to carry separate copies of this logic and had silently drifted apart:
// MovementEngine implemented only Quadratic and Sine, so Cubic, Gauss and Random -- all
// offered by the very same UI dropdown (MOD_CURVES in data/index.html) -- fell through to
// Linear and did nothing at all. Selecting them changed the movement in no way. One
// definition now, so a curve added here cannot go missing in one of the two engines.
//
// allowRandom: Random re-rolls on every single call. For a dimmer that is a usable
// flicker effect; for a movement pattern it would re-randomise the size every frame and
// shake the fixture. The Movement FX curve dropdown therefore does not offer it, and this
// flag makes movement fall back to Linear if an older saved scene still carries curve 5 --
// which is exactly the behaviour those scenes already had before this fix.
inline float lfoShape(float p, int m, int c, bool allowRandom) {
    // Mode: 0=Forward (Saw), 1=PingPong (Triangle), 2=Reverse (Decay)
    float val = p;
    if (m == 1) val = p < 0.5f ? p * 2.0f : 2.0f - (p * 2.0f);
    else if (m == 2) val = 1.0f - p;

    // Curve: 0=Linear, 1=Quad, 2=Cubic, 3=Sine, 4=Gauss, 5=Random
    if (c == 1) return val * val;
    if (c == 2) return val * val * val;
    if (c == 3) return 0.5f - 0.5f * cosf(val * PI);
    if (c == 4) { float x = (val - 0.5f) * 2.0f; return expf(-(x * x) * 5.0f); }
    if (c == 5) return allowRandom ? (random(0, 1000) / 1000.0f) : val;
    return val; // 0 = Linear, and any unknown value
}

// =========================================================
// --- LFO MODULATOR (Dimmer, Prism & Gobo Rotation) ---
// =========================================================
// Sizes of the two sync-divisor tables declared in the .ino. They live here because this is
// where the indexing happens: a hard-coded clamp inside these classes cannot know how long the
// table it is handed actually is, and the two tables are NOT the same length.
//   syncBeats[]     -- dimmer / gobo-rotation / prism / wheel steppers / chaser, fast divisors
//   moveSyncBeats[] -- MovementEngine only; pan/tilt slew is finite, so a sub-beat divisor
//                      would demand angular velocity the motor cannot reach
#define SYNC_BEATS_COUNT       10
#define MOVE_SYNC_BEATS_COUNT   8

// --- Burst: four numbers, and which one is primary ------------------------------------------
// A beat-locked effect needs four separate numbers, and until 2026-09-03 one divisor answered
// all of them:
//
//   rasterBeats    when the whole figure starts again           (the UI's "Sync")
//   burst          how many pulses there are                    ("Count")
//   lengthBeats    how long ONE pulse lasts                     ("Length")  <- the primary one
//   spacingBeats   how far apart the pulses START, >= length    ("Spacing", defaults to length)
//
// LENGTH is primary and SPACING falls back to it, not the other way round. That ordering was
// wrong at first and it showed immediately: with Count at 1 there is nothing to space, so the
// "Spacing" control was silently setting the pulse length while "Length · fills slot" sat next
// to it doing nothing. Reported live with screenshots -- "spacing und length muessen getauscht
// werden, sonst macht es keinen sinn". Now Length always means what it says, and Spacing only
// adds a gap behind each pulse.
//
// Length == spacing fills the slot (a smooth swell); length much shorter than spacing gives a
// short stab and darkness until the next one. That difference is the whole point of having both.
//
// Outside a pulse the phase is held at 1.0, which is the END of the curve and therefore outputs
// startVal -- dark, for the usual flash/decay setting. Defaults reproduce the old behaviour
// exactly: burst 1, spacing == length, raster == length reduces this to `frac(beats / length)`.
static inline float burstPhase(float beatsIn, float lengthBeats, int burst,
                               float rasterBeats, float spacingBeats) {
    if (lengthBeats <= 0.0f) return 0.0f;
    if (burst < 1) burst = 1;
    if (spacingBeats <= 0.0f || spacingBeats < lengthBeats) spacingBeats = lengthBeats;
    float span = spacingBeats * (float)burst;
    if (rasterBeats < span) rasterBeats = span;   // a raster shorter than the burst would clip it
    float pos = fmodf(beatsIn, rasterBeats);
    if (pos < 0.0f) pos += rasterBeats;
    if (pos >= span) return 1.0f;                 // between figures
    float k = fmodf(pos, spacingBeats);           // position inside this pulse's slot
    if (k >= lengthBeats) return 1.0f;            // pulse over, dark until the next slot
    return k / lengthBeats;                       // the curve, compressed into `length`
}

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
    // Burst controls, see burstPhase() above. burst = passes per raster; rasterSync = index into
    // the same divisor table as `sync`, or -1 for "no pause", which is the old behaviour.
    int burst = 1;
    int rasterSync = -1;
    int spacingSync = -1;     // -1 = same as the length, i.e. pulses run back to back
    float phase = 0.0f;
    // Beat position of the last audio hit, so an audio-triggered LFO can run on the beat clock
    // like the BPM-sync mode does instead of on its own free-running speed. Set via audioHit(),
    // which only raises a flag -- the anchor itself is taken inside process(), where the current
    // beat count is actually available.
    float audioAnchorBeats = 0.0f;
    bool audioHitPending = false;
    bool audioAnchored = false;
    void audioHit() { audioHitPending = true; }
    
    unsigned long lastUpdate = 0; 

    Modulator(int minV, int maxV) { startVal = minV; endVal = maxV; }
    void start() { active = true; lastUpdate = millis(); }
    void stop() { active = false; }

    // Modulators drive dimmer/gobo-rot/prism-rot, where Random is a legitimate effect.
    float getLFO(float p, int m, int c) { return lfoShape(p, m, c, true); }

    void process(unsigned long now, float beatsElapsedTotal, const float* syncBeats, float &outVal) {
        if (lastUpdate == 0) lastUpdate = now;
        float dt = (now - lastUpdate) / 1000.0f;
        lastUpdate = now;
        if(dt <= 0 || dt > 1.0f) dt = 0.02f;

        if (trigger >= 2) {
            // Audio triggers now honour the Sync divisor, which they previously ignored entirely:
            // the LFO ran at `speed` and the detected hit merely reset the phase to 0. Setting
            // "Kickbass + 1 Beat" therefore did nothing that the label promised -- with speed at
            // 2000ms and kicks every 400ms the phase only ever reached 0.2, so the dimmer traversed
            // a fifth of its range and looked like it was not working. Reported live 2026-08-31.
            // Anchoring to the beat count at the last hit makes one cycle last exactly
            // syncBeats[sync] beats and restart on each hit, which is what the two controls read as.
            int safeSync = constrain(sync, 0, SYNC_BEATS_COUNT - 1);
            float span = syncBeats[safeSync];
            if (audioHitPending) {
                audioHitPending = false;
                float elapsed = beatsElapsedTotal - audioAnchorBeats;
                // Re-lock only once the cycle has actually run its course. Re-anchoring on EVERY
                // hit truncates any divisor longer than one beat: with 4 beats and a kick on every
                // beat the phase never got past 0.25, so a sawtooth-shaped effect never left the
                // bottom of its range and looked completely dead. Reported live 2026-08-31, on
                // both the kick and the hi-hat trigger -- same code path.
                if (!audioAnchored || elapsed < 0.0f || elapsed >= span * 0.9f) {
                    audioAnchorBeats = beatsElapsedTotal;
                    audioAnchored = true;
                }
            }
            float since = beatsElapsedTotal - audioAnchorBeats;
            if (since < 0.0f) since = 0.0f;
            phase = burstPhase(since, span, burst,
                               rasterSync < 0 ? span * (float)(burst < 1 ? 1 : burst)
                                              : syncBeats[constrain(rasterSync, 0, SYNC_BEATS_COUNT - 1)],
                               spacingSync < 0 ? span
                                              : syncBeats[constrain(spacingSync, 0, SYNC_BEATS_COUNT - 1)]);
        } else if (trigger == 0) {
            // speed is the full LFO cycle duration in ms (matches the UI slider's ms range).
            float periodMs = speed < 1.0f ? 1.0f : speed;
            phase += (dt * 1000.0f) / periodMs;
        } else if (trigger == 1) {
            int safeSync = constrain(sync, 0, SYNC_BEATS_COUNT - 1);
            // beatsElapsedTotal increases smoothly and monotonically with real elapsed beats
            // (see updateEngines()) -- dividing by the cycle length in beats and keeping only
            // the fractional part gives a phase that wraps exactly every `sync` beats,
            // regardless of how often the beat clock itself gets re-anchored (every detected
            // beat, for live audio). Using (now - masterSyncTime) % interval here used to break
            // any divisor > 1 beat, since masterSyncTime is restamped on every single detected
            // beat -- the modulo numerator could then never grow past one beat's worth of ms.
            float shape = syncBeats[safeSync];
            phase = burstPhase(beatsElapsedTotal, shape, burst,
                               rasterSync < 0 ? shape * (float)(burst < 1 ? 1 : burst)
                                              : syncBeats[constrain(rasterSync, 0, SYNC_BEATS_COUNT - 1)],
                               spacingSync < 0 ? shape
                                              : syncBeats[constrain(spacingSync, 0, SYNC_BEATS_COUNT - 1)]);
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
    // Burst controls, see burstPhase(). In BPM-sync mode enginePhase is derived from modPhase,
    // so a raster genuinely parks the PATTERN -- during the pause modPhase is 1.0, which is a
    // full revolution and therefore the point the shape started from. On an audio trigger the
    // pattern integrates its own speed instead (see the branch below), so there the raster only
    // gates the size/speed modulation, not the movement.
    int burst = 1;
    int rasterSync = -1;
    int spacingSync = -1;
    float modPhase = 0.0f;
    float audioAnchorBeats = 0.0f;
    bool audioHitPending = false;
    bool audioAnchored = false;
    void audioHit() { audioHitPending = true; }

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

        if (trigger >= 2) {
            // Same fix as Modulator above: the Sync divisor applies to audio triggers too, instead
            // of the pattern free-running at modSp while the hit merely reset the phase.
            int safeSync = constrain(sync, 0, MOVE_SYNC_BEATS_COUNT - 1);
            float span = syncBeats[safeSync];
            if (audioHitPending) {
                audioHitPending = false;
                float elapsed = beatsElapsedTotal - audioAnchorBeats;
                if (!audioAnchored || elapsed < 0.0f || elapsed >= span * 0.9f) {
                    audioAnchorBeats = beatsElapsedTotal;
                    audioAnchored = true;
                }
            }
            float since = beatsElapsedTotal - audioAnchorBeats;
            if (since < 0.0f) since = 0.0f;
            modPhase = burstPhase(since, span, burst,
                                  rasterSync < 0 ? span * (float)(burst < 1 ? 1 : burst)
                                                 : syncBeats[constrain(rasterSync, 0, MOVE_SYNC_BEATS_COUNT - 1)],
                                  spacingSync < 0 ? span
                                                 : syncBeats[constrain(spacingSync, 0, MOVE_SYNC_BEATS_COUNT - 1)]);
        } else if (trigger == 0) {
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
            int safeSync = constrain(sync, 0, MOVE_SYNC_BEATS_COUNT - 1);
            // See Modulator::process() for why this is beat-count-based rather than
            // (now - masterSyncTime) % interval -- masterSyncTime gets re-anchored on every
            // detected beat, which broke every sync divisor above 1 beat.
            float shape = syncBeats[safeSync];
            modPhase = burstPhase(beatsElapsedTotal, shape, burst,
                                  rasterSync < 0 ? shape * (float)(burst < 1 ? 1 : burst)
                                                 : syncBeats[constrain(rasterSync, 0, MOVE_SYNC_BEATS_COUNT - 1)],
                                  spacingSync < 0 ? shape
                                                 : syncBeats[constrain(spacingSync, 0, MOVE_SYNC_BEATS_COUNT - 1)]);
        }
        if (modPhase > 1.0f) modPhase -= 1.0f;
        if (modPhase < 0.0f) modPhase += 1.0f;
        
        // Shared with Modulator (see lfoShape) instead of a partial local copy, which had
        // silently reduced Cubic and Gauss to Linear here. allowRandom=false: see lfoShape.
        //
        // Worth knowing when a pattern "judders": modes 0 (Forward/Saw) and 2 (Reverse/Decay)
        // are sawtooths, so whenever szSt != szEn the size snaps back at the end of every
        // modulation cycle and the head physically jumps. Measured live 2026-08-26 on a Clover:
        // 9 discontinuities in 10s, spaced exactly one modSp apart, up to 17728 units of pan in
        // a single frame. That is inherent to a sawtooth, not a defect -- mode 1 (Ping-Pong)
        // is the continuous one and measured zero discontinuities under the same conditions.
        float mVal = lfoShape(modPhase, modMo, modCu, false);

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

        // No software compensation for the tilt-axis defect around DMX tilt ~127 (8-bit) -- see
        // mapping_sheds_160w_3in1_gobo.md / history.md 2026-08-20 for the full diagnosis. A prior
        // attempt auto-shifted a pattern's tilt center away from that point, but a static-position
        // sweep (held, not moving) tracked the fixture's own tilt encoder perfectly monotonically
        // straight through that DMX region -- proving it's not a DMX-to-angle mapping fault. The
        // real defect is a fixed *physical* fault at one absolute tilt angle that only manifests
        // while the mechanism is actually rotating through it (any speed, confirmed live -- not a
        // tracking-rate issue either), which no choice of DMX value can route around when a
        // pattern is deliberately meant to sweep through that angle. Auto-shifting fought the
        // user's own intentional positioning (e.g. a Clover pattern meant to reach over that exact
        // point) worse than it helped, so this is left to the user to route around by choosing
        // pattern centers/sizes that avoid the affected angle when a clean shape matters more than
        // exact positioning.
        outP = constrain(centerP + (int)rx, 0, 65535);
        outT = constrain(centerT + (int)ry, 0, 65535);
    }
};
