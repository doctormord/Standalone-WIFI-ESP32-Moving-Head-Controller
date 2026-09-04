#pragma once
#include <Arduino.h>
#include <driver/i2s.h>
#include <cmath>
#include <algorithm>

// =========================================================
// --- AUDIO HARDWARE & I2S CONFIG ---
// =========================================================
#define I2S_WS   4
#define I2S_SCK  5
#define I2S_SD   6
#define I2S_PORT I2S_NUM_0

// 16kHz, raised from 8000 on 2026-09-01. (Lowering it to ~2kHz was considered earlier and
// rejected for the opposite reason: it would have deleted the High band entirely.) Nyquist moves from 4kHz to 8kHz, which is where hi-hat
// and cymbal energy actually lives -- at 8kHz sampling the High band was clipped at its most
// useful point. N doubles with it, so the bin width stays 31.25Hz and every configured band edge
// keeps its meaning; only the highest usable bin moves from 127 to 255. Also puts the mic nearer
// its specified clock range. Costs about 900us per frame instead of 400us, still under 3% CPU.
#define SAMPLING_FREQUENCY 16000
// One FFT frame. 512 @ 16kHz = 32ms of audio and 31.25Hz per bin -- fine enough to separate kick
// from snare from hi-hat, short enough that a frame still fits inside one poll interval.
#define FFT_N 512
#define FFT_LOG2N 9
#define SAMPLES FFT_N
// Poll cadence deliberately equals the frame duration (512/16000 = 32ms), so samples are consumed
// at exactly the rate the I2S peripheral produces them. At the old 40ms the DMA ring would slowly
// fill and then drop blocks, which shows up as sporadically missed beats rather than as an error.
#define AUDIO_POLL_INTERVAL_MS 32
// 4 x 512 samples = 2048 samples (~128ms) of slack, so an occasional long loop iteration cannot
// cost us a frame. The original 2 x 128 held exactly one frame, with nothing to spare.
#define DMA_BUF_COUNT 4
#define DMA_BUF_LEN 512
// Zero, deliberately: i2s_read must NEVER block the main loop. A blocking read here would stall
// DMX output and movement -- the exact failure mode an earlier FFT attempt produced. If a full
// frame is not buffered yet we simply skip this poll and pick it up 32ms later; the DMA ring
// holds ~128ms, so nothing is lost.
#define I2S_READ_TIMEOUT_MS 0
#define BYTES_PER_SAMPLE_32BIT 4

// Band edges as FFT bin indices (bin width = 16000/512 = 31.25Hz). Bin 0 (DC) is always skipped.
// Bin 1 (31Hz) is rumble rather than kick, and the 1995 DJM-500 -- whose beat detector is a
// bandpass, envelope follower and adaptive comparator, i.e. exactly this shape in analogue --
// used roughly 50-150Hz. Starting at bin 2 matches that and drops the sub-rumble.
#define BIN_BASS_LO 2     //   62 Hz
#define BIN_BASS_HI 5     //  156 Hz  -- kick drum fundamental
#define BIN_MID_LO  6     //  187 Hz
#define BIN_MID_HI  38    // 1187 Hz  -- snare body, vocals, most instruments
#define BIN_HIGH_LO 80    // 2500 Hz
#define BIN_HIGH_HI 200   // 6250 Hz  -- hi-hat / cymbal, now that 16kHz sampling reaches it

// =========================================================
// --- AUDIO PROCESSING & ENVELOPES ---
// =========================================================
#define SAMPLE_DOWNSCALE_SHIFT 14
// The FFT needs its input to fit int16 exactly, not just to be "small". These mics deliver 24-bit
// data left-aligned in a 32-bit word, so full scale is 2^31; >>16 maps that onto +/-32768, using
// the whole int16 range without ever clipping. The legacy path's >>14 would overflow int16 on loud
// passages -- harmless there because it only ever takes abs() of it, fatal for an FFT.
#define SAMPLE_DOWNSCALE_SHIFT_FFT 16

// Band edges as runtime values (defaults from the BIN_* defines above). Tunable because the
// right split depends on the material: many kick drums carry usable energy up to ~200Hz,
// which the default bass edge at 156Hz leaves in the Mid band. Changing them by ear beats
// guessing at compile time, and needs no reflash.
inline int tuneBinBassLo = BIN_BASS_LO, tuneBinBassHi = BIN_BASS_HI;
inline int tuneBinMidLo  = BIN_MID_LO,  tuneBinMidHi  = BIN_MID_HI;
inline int tuneBinHighLo = BIN_HIGH_LO, tuneBinHighHi = BIN_HIGH_HI;

// Raw input telemetry, so the mic level (and whether it clips) is visible instead of guessed.
// Without this the rolling graph's auto-scaling hides both silence and overload -- they look
// identical once everything is normalised.
// 0.98 of full scale for the 32-bit I2S word, i.e. (int32_t)(0.98 * 2147483647). Deliberately
// short of full scale: the converter's top codes are already compressed, so counting the last
// 2% as clipped catches saturation before the waveform is visibly flat-topped.
#define MIC_RAW_CLIP_LEVEL  2104533975

inline int32_t micPeak = 0;       // peak magnitude of the last frame, pre-clamp -- may exceed 32767 when overdriven
inline int micClipCount = 0;      // samples at/near full scale after our gain, in the last frame
inline int micRawClipCount = 0;   // samples already saturated at the microphone -- gain cannot fix these

// Audio tuning is persisted to NVS, but NOT on every change: dragging a slider fires a
// request per step, and writing flash on each one would burn erase cycles for no reason.
// Changes only set this flag; flushAudioPrefs() (WebAPI.h, called from loop) writes once
// the values have been quiet for a moment.
inline bool audioPrefsDirty = false;
inline unsigned long audioPrefsDirtyAt = 0;
inline void markAudioPrefsDirty() { audioPrefsDirty = true; audioPrefsDirtyAt = millis(); }

// Digital input gain, applied to the FFT input only. Measured 2026-08-31 with the mic 10cm
// from the speaker: peak was 1231 median / 3136 max out of 32767, i.e. under 10% of the
// available range, which throws away more than three bits before the transform even starts.
// Physical level could not be raised further, so the resolution is recovered here instead.
// The clip counter above is the guard rail: turn this up until CLIPPING shows, then back off.
inline int tuneInputGainShift = 3;   // 0..5 -> x1 .. x32
// 3 measured on the device 2026-09-01: 83% of full scale on peaks with zero clipping.
// 4 already runs 168% and clips, 5 runs 293% -- which is where this sat all along, so every
// spectrum and band level was being read off a hard-clipped signal.

// Attack/Decay speeds as bit-shifts (1 = /2, 2 = /4 ...), the mid/high threshold trims and the
// noise floor. Runtime-tunable rather than #define so the AUDIO tab can adjust them live via
// /audio_tune without a reflash. These envelope followers were once the whole detector -- the
// project's "fake FFT" -- and now shape only the bands set to energy detection.
inline int tuneNoiseFloor = 100;
inline int tuneFastAttackShift = 1;
inline int tuneFastDecayShift = 2;
inline int tuneMidAttackShift = 2;
inline int tuneMidDecayShift = 3;
// Was 2 (same as tuneMidAttackShift) -- since midEnergy = max(0, envMid - envSlow), an equal
// attack speed meant envMid and envSlow rose in perfect lockstep on every attack (identical
// coefficient), and envMid's faster decay only ever pulled it BELOW envSlow afterward -- so
// envMid could never exceed envSlow and midEnergy was structurally ~0 by construction, no matter
// what the mid threshold or sensitivity were set to. Same issue would apply to high vs mid once
// fixed unless kept distinct; fast/mid already differ (shift 1 vs 2). Slowing only the slow band's
// attack restores a strict fast<mid<slow attack-speed ordering that mirrors the decay ordering
// already in place, giving mid (and by extension high) a real signal to work with. Confirmed via
// the AUDIO DEBUG tab live 2026-08-20: mid/high read ~0 the entire session with active, audible
// music playing, while low tracked the beat clearly.
inline int tuneSlowAttackShift = 3;
inline int tuneSlowDecayShift = 4;
inline int tuneDynThreshSmoothShift = 4;
inline int tuneMidThreshDivShift = 1;
inline int tuneHighThreshDivShift = 2;

// =========================================================
// --- BEAT DETECTION THRESHOLDS ---
// =========================================================
#define MS_PER_MINUTE 60000
#define MIN_BEAT_INTERVAL_MS 280
#define MAX_BEAT_INTERVAL_MS 1000
#define SILENCE_TIMEOUT_MS 2500

#define MID_DEBOUNCE_MS 150
#define HIGH_DEBOUNCE_MS 80

// =========================================================
// --- BPM CALCULATION & SMOOTHING ---
// =========================================================
#define BPM_HISTORY_SIZE 16
#define BPM_MIN_VALID_SAMPLES 6
#define BPM_DEFAULT_FALLBACK 120
#define BPM_MIN_LIMIT 60
// Raised from 180: drum & bass sits at 172-180, so the old ceiling left a genuine 178 BPM
// track pinned against the limit with no room for the median filter to settle above it.
#define BPM_MAX_LIMIT 200

#define BPM_DEVIATION_TOLERANCE_DIVISOR 5 
// Above this percentage difference between the measured median and the current globalBPM,
// snap straight to the measurement instead of smoothing towards it -- see the re-lock
// comment in pollAudioEngine().
#define BPM_RELOCK_PERCENT 20
#define BPM_SMOOTHING_WEIGHT_OLD 19
#define BPM_SMOOTHING_WEIGHT_TOTAL 20

// =========================================================
// --- SYSTEM STATE VARIABLES ---
// =========================================================
inline bool hwAudioEnabled = false;
inline int hwAudioSensitivity = 50;

inline bool triggerBass = false;
inline bool triggerMid  = false;
inline bool triggerHigh = false;

inline bool guiBass = false;
inline bool guiMid  = false;
inline bool guiHigh = false;

// Latched-until-read hit flags for the AUDIO DEBUG tab's fast (~66ms) /api/audio_debug poll.
// Deliberately separate from guiBass/guiMid/guiHigh (which /api/state already latches-and-clears
// on its own independent ~500ms poll) -- two pollers clearing the same flag would race each other
// for whichever request happens to land first, dropping hits for the other. triggerBass/Mid/High
// themselves are NOT pollable at all: pollAudioEngine() unconditionally zeroes them at the top of
// every call, and loop() calls it far more often than the 40ms internal audio-processing throttle,
// so a "true" set here survives only until the very next loop() iteration -- microseconds, not
// something an HTTP request arriving from outside can ever realistically catch. Reported live
// 2026-08-20 as "graph zeigt so gut wie keine beat detects an" despite audible, obvious beats.
inline bool dbgBassHit = false, dbgMidHit = false, dbgHighHit = false;

extern int globalBPM;
extern unsigned long lastBeatTime;
extern bool manualTap;
extern unsigned long masterSyncTime;
extern unsigned long beatCount;

inline int32_t raw_samples[SAMPLES];

// =========================================================
// --- FIXED-POINT FFT (no float anywhere on this path) ---
// =========================================================
// The ESP32-C3 is RV32IMC and has NO hardware FPU, so every float op is software-emulated. A
// float FFT here would compete directly with MovementEngine's sinf/cosf load and can starve the
// main loop -- which is exactly how an earlier float-based FFT attempt made the fixture stutter
// and stall. Everything below is int16/int32 only: Q15 twiddles, per-stage /2 scaling to keep the
// butterflies from overflowing int16, and a square-root-free magnitude estimate.
//
// Measured cost of this shape at N=256: +718 bytes flash, +2KB RAM, well under 1% CPU.
inline int16_t fftRe[FFT_N];
inline int16_t fftIm[FFT_N];
inline int16_t fftTwCos[FFT_N / 2];
inline int16_t fftTwSin[FFT_N / 2];
inline int16_t fftWindow[FFT_N];
inline int32_t fftMag[FFT_N / 2];

// Built once at boot. cosf/sinf are float, but this runs exactly once -- never on the audio path.
inline void fftInitTables() {
  for (int i = 0; i < FFT_N / 2; i++) {
    fftTwCos[i] = (int16_t)(cosf(2.0f * PI * i / FFT_N) * 32767.0f);
    fftTwSin[i] = (int16_t)(-sinf(2.0f * PI * i / FFT_N) * 32767.0f);
  }
  // Hann window: without it a tone between two bins smears across the whole spectrum
  // (spectral leakage) and the band sums stop meaning anything.
  for (int i = 0; i < FFT_N; i++) {
    fftWindow[i] = (int16_t)((0.5f - 0.5f * cosf(2.0f * PI * i / (FFT_N - 1))) * 32767.0f);
  }
}

// In-place radix-2 decimation-in-time FFT on fftRe/fftIm.
inline void fftRun() {
  for (int i = 1, j = 0; i < FFT_N; i++) {          // bit-reversal permutation
    int bit = FFT_N >> 1;
    for (; j & bit; bit >>= 1) j ^= bit;
    j ^= bit;
    if (i < j) {
      int16_t t = fftRe[i]; fftRe[i] = fftRe[j]; fftRe[j] = t;
      t = fftIm[i]; fftIm[i] = fftIm[j]; fftIm[j] = t;
    }
  }
  for (int len = 2; len <= FFT_N; len <<= 1) {
    int step = FFT_N / len;
    for (int i = 0; i < FFT_N; i += len) {
      for (int k = 0; k < len / 2; k++) {
        int t = k * step;
        int32_t wr = fftTwCos[t], wi = fftTwSin[t];
        int a = i + k, b = a + len / 2;
        int32_t xr = ((int32_t)fftRe[b] * wr - (int32_t)fftIm[b] * wi) >> 15;
        int32_t xi = ((int32_t)fftRe[b] * wi + (int32_t)fftIm[b] * wr) >> 15;
        // The >>1 on every output is what keeps this from overflowing int16: each stage can at
        // most double the magnitude, so halving per stage bounds it. Total scaling is 1/N, which
        // the band gain below compensates for.
        fftRe[b] = (int16_t)((fftRe[a] - xr) >> 1);
        fftIm[b] = (int16_t)((fftIm[a] - xi) >> 1);
        fftRe[a] = (int16_t)((fftRe[a] + xr) >> 1);
        fftIm[a] = (int16_t)((fftIm[a] + xi) >> 1);
      }
    }
  }
  // Magnitude without sqrt: alpha-max-plus-beta-min, max + max/8 + min/2 style, ~1% error.
  // A real sqrtf() per bin would be 128 emulated square roots per frame -- on its own more
  // expensive than the entire FFT above, and the single easiest way to undo the point of this.
  for (int i = 0; i < FFT_N / 2; i++) {
    int32_t a = fftRe[i] < 0 ? -fftRe[i] : fftRe[i];
    int32_t b = fftIm[i] < 0 ? -fftIm[i] : fftIm[i];
    int32_t mx = a > b ? a : b, mn = a > b ? b : a;
    fftMag[i] = mx - (mx >> 3) + (mn >> 1);
  }
}

// Average magnitude across an inclusive bin range.
inline int32_t fftBand(int lo, int hi) {
  int32_t sum = 0;
  for (int i = lo; i <= hi; i++) sum += fftMag[i];
  return sum / (hi - lo + 1);
}

// Previous frame's magnitudes, for spectral flux below.
inline int32_t fftMagPrev[FFT_N / 2];

// =========================================================
// --- SAMPLE-RATE ONSET DETECTOR (the DJM-500 topology) ---
// =========================================================
// Everything above detects on FFT frames, which means an onset can only ever be timestamped on a
// frame boundary -- once every 32ms. A kick's attack lasts 5-20ms, so it fits inside a single
// frame and gets smeared by that frame's average, and the resulting timestamp carries +/-32ms of
// quantisation. At a 462ms beat that is +/-7% of jitter before any tempo estimator even starts,
// and five different estimators were built on top of those timestamps without fixing it.
//
// The 1995 DJM-500 does not have this problem because it works in continuous time: analogue
// bandpass around the kick, envelope rectifier, comparator against a slowly-tracking reference.
// This is that chain, run per sample at 16kHz instead of per frame -- roughly thirty times the
// timing resolution, for a few integer operations per sample.
//
// Timestamps come from a SAMPLE COUNTER rather than millis(): derived from the audio clock
// itself, it carries none of the polling loop's jitter.
// Mid and High cost two more filter chains per sample on a chip with no FPU. Measured audio cost
// was 4-7% of the loop with one band; expect roughly three times that. If the DMX frame ever
// starts slipping (watch loopMax on /api/state), turning this off drops back to bass-only
// detection and mid/high revert to the frame-based band comparison.
inline bool  sdAllBands = true;

// The bass onset the engine last acted on, as a millisecond timestamp. A latched flag (xb) can
// only ever say "at least one since you asked"; a timestamp lets the INTERVALS be histogrammed,
// and that is what distinguishes a detector finding a grid from one spraying onsets. Every
// diagnosis on 2026-09-03 rested on it.
inline uint32_t bassOnsetUsedMs = 0;

// The FFT is no longer part of detection -- all three bands are found by the sample-rate chain,
// which works on raw samples and never touches a spectrum. What still needs it is the AUDIO
// tab's spectrum and band display, and the fallback path when the sample detector is switched
// off. So run it when something is actually going to read it, and not otherwise.
//
// This is the single largest cost in the audio path: measured on the device, fftUs was
// 1127-1207us of an audUs of 1212-2438us per 32ms frame -- around 3.6% of the loop, spent on a
// spectrum nobody was looking at. Skipping it pays for the two extra detector bands.
//
// Any request to /api/audio_debug renews the lease, so the spectrum is live for as long as a
// browser is on that tab and stops within two seconds of it being closed.
// Which bands anything is actually listening to. Set from the .ino, which is where the FX
// objects live and therefore the only place that knows what each one is triggered from. A band
// nobody has routed anything to costs a filter chain and a comparator per sample for a result
// that is thrown away, so it simply does not run -- and the AUDIO tab still sees all three,
// because a browser watching it renews the same lease the FFT uses.
inline bool sdMidWanted = false, sdHighWanted = false;
// Where the per-block threshold sits inside a band's [floor .. peak] range, in Q8 (256 would
// place it exactly at the running peak). Sensitivity 0 puts it at 230/256 = 0.90 of the range,
// sensitivity 100 at 77/256 = 0.30 -- so the slider moves the threshold through the range
// rather than scaling an absolute level, which is why one setting works across loud and quiet
// material. Raise the MIN to make a fully-open sensitivity less twitchy.
#define SD_THR_FRAC_Q8_MAX   230   // at sensitivity 0
#define SD_THR_FRAC_Q8_MIN    77   // at sensitivity 100
// Added on top so the threshold cannot coincide with the floor when the range collapses to
// nothing (a passage with no beat in it), which would turn the detector loose on the noise.
#define SD_THR_FLOOR_MARGIN    8

inline bool sdRunMid = false, sdRunHigh = false;

inline uint32_t fftWantedUntil = 0;
inline bool fftIsNeeded(unsigned long now) {
  if (!sdAllBands) return true;    // the mid/high fallback reads FFT band energies
  return (int32_t)(fftWantedUntil - (uint32_t)now) > 0;
}

// One band of the detector. Three of these run over the same samples so that Mid and High get
// the identical chain the kick does, instead of the frame-based comparison they used to share
// with the old envelope followers -- that path timestamps only on 32ms frame boundaries and
// measures against a smoothed mean, both of which this session established as the reason nothing
// downstream could be trusted. Measured in the simulator, the High band is in fact the cleanest
// of the three (1% interval spread against 3% for the kick), so there is no reason to leave it
// on the worse detector.
#define SD_STAT_HIST 24            // ~24 blocks * 32ms = 768ms of history
struct SdBand {
  SdBand(int lo, int hi, int rel_, int lock_, int bmax_, int bsh_, int sadd_)
    : kLo(lo), kHi(hi), rel(rel_), sensAdd(sadd_), lockoutMs(lock_), boostMaxQ8(bmax_), boostShift(bsh_) {}
  // Filter edges, as one-pole shifts: fc = 16000 / (2*pi * 2^k), so 6 is 40Hz, 4 is 159Hz,
  // 2 is 637Hz, 1 is 1273Hz, and 0 on the upper edge is a pass-through, which turns the pair
  // into a plain highpass.
  int kLo, kHi;
  int rel;                  // envelope release, 2^k samples / 16000
  // Added to the global sensitivity for this band, in the same 0..100 units. The threshold is a
  // position inside the band's own dynamic range, and in a band the kick dominates -- which is
  // every band, since a kick is broadband -- the kick sets the top of that range. For Bass that
  // is exactly right: the thing setting the ceiling is also the thing being detected. For High it
  // is backwards, because there the point is to find what is quieter than the kick. Measured on
  // the device 2026-09-02: raising the global sensitivity from 60 to 80 left Bass at 2.6/s but
  // took High from 2.73 to 3.55/s, so the band was being held back by a threshold meant for the
  // kick and not by an absence of hats.
  int sensAdd = 0;
  int lockoutMs;            // hard floor only; the soft boost does the real work
  int boostMaxQ8;           // how far an onset lifts the threshold, Q8
  int boostShift;           // and how fast that decays back
  int att = 2;              // envelope attack, ~0.25ms -- follows the leading edge
  int refShift = 14;        // ~1s rolling reference, used for re-arming and for display
  int varMinPct = 10;       // MAD must be at least this % of the mean for an event to count
  int minRangePct = 25;     // the window must contain this much range, as a % of its floor
  int peakFallPct = 70;     // commit the onset once the envelope drops to this % of its peak
  int peakMaxWaitMs = 60;   // ...or after this long, whichever comes first

  int32_t lp1 = 0, lp2 = 0, env = 0, ref = 0, refAcc = 0;
  int32_t statHist[SD_STAT_HIST] = {0};
  int     statIdx = 0;
  bool    transient = false, hasDynamics = false;
  int32_t varMad = 0, varMean = 0, floorV = 0, peakStat = 0, thrBlock = 0;
  int32_t boost = 65536;    // Q16, 65536 = 1.0
  bool    peaking = false;
  int32_t peakVal = 0;
  uint32_t peakClock = 0;
  bool    armed = true;
  uint32_t lastOnsetMs = 0;
  int32_t lastEnv = 0, lastThr = 0;
  int     onsetsThisFrame = 0;
  uint32_t onsetMs = 0;     // timestamp found in this block, 0 if none
};

// Bass is the kick band the DJM uses. Mid covers snare and vocal body. High is a plain highpass
// above 1273Hz, which is where hats and the snare's crack live.
//
// Each band gets its own recovery, because they are being asked for different things. A kick
// arrives once a beat and its neighbours should be suppressed, so the bass band lifts the
// threshold 4x and takes ~128ms to come back. Hats run at eighths or sixteenths -- 207ms and
// 103ms at 145 BPM -- so the high band has to be ready again long before that, or a dimmer effect
// set to "high" simply follows the beat instead of the hats, which is the whole point of routing
// it there. Its release is shorter too: a hat is a few milliseconds, a kick rings for a hundred.
//                    lo hi  rel lock boost  bshift  sens+
inline SdBand sdBass ( 6, 4,   8,  60, 1024,     11,     0);  // kick:  4.0x, ~128ms recovery
inline SdBand sdMid  ( 4, 2,   7,  50,  768,     10,    20);  // snare: 3.0x, ~64ms
inline SdBand sdHigh ( 1, 0,   6,  40,  512,      9,    40);  // hats:  2.0x, ~32ms, most permissive

// The existing names stay as references to the bass band, so the API, the debug JSON and the
// simulator keep addressing "the detector" exactly as before without a rename sweep.
inline int&      sdKLo = sdBass.kLo;
inline int&      sdKHi = sdBass.kHi;
inline int&      sdAtt = sdBass.att;
inline int&      sdRel = sdBass.rel;
inline int&      sdRefShift = sdBass.refShift;
inline int&      sdLockoutMs = sdBass.lockoutMs;
inline int&      sdVarMinPct = sdBass.varMinPct;
inline int&      sdMinRangePct = sdBass.minRangePct;
inline int&      sdPeakFallPct = sdBass.peakFallPct;
inline int&      sdPeakMaxWaitMs = sdBass.peakMaxWaitMs;
inline int&      sdBoostMaxQ8 = sdBass.boostMaxQ8;
inline int&      sdBoostShift = sdBass.boostShift;
inline int32_t&  sdLp1 = sdBass.lp1;
inline int32_t&  sdLp2 = sdBass.lp2;
inline int32_t&  sdEnv = sdBass.env;
inline int32_t&  sdRef = sdBass.ref;
inline int32_t&  sdRefAcc = sdBass.refAcc;
inline int32_t (&sdStatHist)[SD_STAT_HIST] = sdBass.statHist;
inline int&      sdStatIdx = sdBass.statIdx;
inline bool&     sdTransient = sdBass.transient;
inline bool&     sdHasDynamics = sdBass.hasDynamics;
inline int32_t&  sdVarMad = sdBass.varMad;
inline int32_t&  sdVarMean = sdBass.varMean;
inline int32_t&  sdFloor = sdBass.floorV;
inline int32_t&  sdPeakStat = sdBass.peakStat;
inline int32_t&  sdThrBlock = sdBass.thrBlock;
inline int32_t&  sdBoost = sdBass.boost;
inline bool&     sdPeaking = sdBass.peaking;
inline int32_t&  sdPeakVal = sdBass.peakVal;
inline uint32_t& sdPeakClock = sdBass.peakClock;
inline bool&     sdArmed = sdBass.armed;
inline uint32_t& sdLastOnsetMs = sdBass.lastOnsetMs;
inline int32_t&  sdLastEnv = sdBass.lastEnv;
inline int32_t&  sdLastThr = sdBass.lastThr;
inline int&      sdOnsetsThisFrame = sdBass.onsetsThisFrame;

// Shared: one audio stream, one clock.
inline uint32_t sdSampleClock = 0;
// Does the sample clock keep up with real time? It only advances for audio we actually processed,
// so any sample the DMA ring drops makes it run slow -- and since beat intervals are measured on
// it, a slow clock reports every tempo too high. Positive means behind, in parts per thousand.
inline uint32_t sdClkStartWall = 0, sdClkStartSample = 0;
inline int      sdClkDriftPpt = 0;

// Rescale every level-domain state when the input gain shift changes, so a gain step causes no
// transient. Without this the envelopes, their references and the whole window history would jump
// by a factor of two and the detector would fire on its own gain change.
inline void sdScaleBand(SdBand& b, int d) {
  #define SD_SC(x) ((x) = (d > 0) ? ((x) << d) : ((x) >> (-d)))
  SD_SC(b.lp1); SD_SC(b.lp2); SD_SC(b.env); SD_SC(b.ref); SD_SC(b.refAcc);
  SD_SC(b.peakVal); SD_SC(b.floorV); SD_SC(b.peakStat); SD_SC(b.thrBlock);
  for (int i = 0; i < SD_STAT_HIST; i++) SD_SC(b.statHist[i]);
  #undef SD_SC
}
inline void sdScaleState(int d) {
  if (d == 0) return;
  sdScaleBand(sdBass, d); sdScaleBand(sdMid, d); sdScaleBand(sdHigh, d);
}

// --- Automatic input range selection ------------------------------------------------------
// tuneInputGainShift is a SHIFT, so every step is a factor of two. This is not a continuous AGC,
// it is range selection, like an auto-ranging meter -- and that is what makes it stable: the
// acceptable window (25%..92% of full scale, a factor of 3.7) is wider than one step (a factor
// of 2), so a correction always lands inside the window and cannot oscillate between two steps.
//
// The two directions deliberately have different time constants. Clipping destroys the signal
// and we now know it silently ruins detection, so coming down is fast. A quiet passage is
// ordinary music and lasts tens of seconds, and raising the gain during a breakdown guarantees
// clipping when the drop lands, so going up is slow and one step at a time.
//
// Because the level is measured before the clamp, an overdriven input says by how much, so the
// downward correction can go straight to the right range instead of feeling its way.
//
// Not persisted: it re-converges within seconds of any restart, and writing NVS on every gain
// change would be pointless flash wear. Only the on/off switch is stored.
inline bool     autoGain      = true;
inline int32_t  agPeakWin     = 0;      // peak over ~2s, decaying
inline uint32_t agHighSince   = 0, agLowSince = 0, agLastChange = 0;
inline int      agTargetPct   = 70;     // where a correction aims to put the peak
inline int      agHighPct     = 92, agLowPct = 25;
inline int      agDownDelayMs = 1000, agUpDelayMs = 20000, agHoldMs = 3000;
// Winding the gain UP is not allowed just because it is quiet. Left ungated it opens all the way
// through a pad or a breakdown and then reports room noise as onsets -- observed live 2026-09-02
// as tempo readings of 100/170/180/140 in a section with no drums in it at all. Turning it DOWN
// stays completely ungated: a passage that suddenly gets loud, or one that clips, must be able to
// bring the gain back whether or not anything is being detected.
//
// In AUTO tempo mode the gain may climb while kicks are actually arriving. In MANUAL mode it may
// not: there the user has taken over the tempo, so the only thing that should move the gain is
// the user saying so. Either way a tap permits it, and that is not a convenience -- the beat gate
// can deadlock on its own (too little gain to detect anything, therefore no reason to add gain),
// and the tap is the way out, because it asserts that there is a beat to find.
// Auto vs. manual tempo. Declared here rather than with the rest of the tempo state further
// down, because updateAutoGain() below needs it and this file is read top to bottom.
inline bool tempoAuto = true;
inline uint32_t agLastBeatMs = 0;      // wall clock of the last detected kick
inline uint32_t agTapArmUntil = 0;     // a tap permits winding up until this time
#define AG_BEAT_RECENT_MS 2000         // covers 30 BPM, so it never lapses between two kicks
#define AG_TAP_ARM_MS    15000
// Below this gain the beat gate is not applied. It exists to stop the gain winding up during a
// pad or a break, and that rule assumes beats CAN be detected -- an assumption that fails at the
// bottom of the range, where too little gain means no onsets and no onsets means the gate never
// opens. A loud passage pushing the gain to 0 therefore used to strand it there permanently.
// Bounded on purpose: from this shift upward the beat gate applies again, so the worst a silent
// room can do is lift the gain two steps, and "no beats, no tempo" still holds the readout.
#define AG_STARVE_SHIFT      1         // long enough for several agUpDelayMs steps

inline void updateAutoGain(uint32_t now) {
  // Track the recent peak whether or not auto is on, so the UI always has something honest.
  if (micPeak > agPeakWin) agPeakWin = micPeak;
  else agPeakWin -= agPeakWin >> 6;      // ~64 frames at 31fps, about 2s
  if (!autoGain) { agHighSince = agLowSince = 0; return; }
  if ((uint32_t)(now - agLastChange) < (uint32_t)agHoldMs) return;   // let the window resettle

  const int32_t full = 32767;
  int32_t hi = (full * agHighPct) / 100, lo = (full * agLowPct) / 100;
  int delta = 0;

  // Turning the gain down cannot undo saturation that happened at the converter, so do not keep
  // stepping down chasing it -- that would only make a signal that is already flat-topped quieter.
  if (micRawClipCount > 0) { agHighSince = agLowSince = 0; return; }
  if (agPeakWin > hi) {
    if (agHighSince == 0) agHighSince = now;
    else if ((uint32_t)(now - agHighSince) > (uint32_t)agDownDelayMs) {
      int32_t target = (full * agTargetPct) / 100, p = agPeakWin;
      while (p > target && delta > -5) { p >>= 1; delta--; }   // straight to the right range
    }
    agLowSince = 0;
  } else if (agPeakWin < lo && (agPeakWin << (5 - tuneInputGainShift)) > (full / 32)
             && (((int32_t)(agTapArmUntil - now) > 0)
                 || tuneInputGainShift <= AG_STARVE_SHIFT
                 || (tempoAuto && (uint32_t)(now - agLastBeatMs) < AG_BEAT_RECENT_MS))) {
    // The level floor stops it winding all the way up in silence and then clipping the moment
    // the music starts. It has to be judged at FULL gain, not at the current setting: measured
    // as a plain comparison it latched up. Once a loud passage had pushed the gain down to 0,
    // ordinary music read below the floor -- because the gain was low, which is the very thing
    // being corrected -- so it looked like silence and never came back. Observed on the fixture
    // with the input at 2% and ig stuck at 0, which starved the detector down to 0.7 onsets/s
    // and turned the tempo readout into noise. Shifting by the remaining headroom asks the right
    // question: would this be a real signal if the gain were turned up?
    if (agLowSince == 0) agLowSince = now;
    else if ((uint32_t)(now - agLowSince) > (uint32_t)agUpDelayMs) delta = 1;
    agHighSince = 0;
  } else {
    agHighSince = agLowSince = 0;
  }

  if (delta == 0) return;
  int ns = constrain(tuneInputGainShift + delta, 0, 5);
  delta = ns - tuneInputGainShift;
  if (delta == 0) { agHighSince = agLowSince = 0; return; }
  sdScaleState(delta);
  agPeakWin = (delta > 0) ? (agPeakWin << delta) : (agPeakWin >> (-delta));
  tuneInputGainShift = ns;
  agLastChange = now;
  agHighSince = agLowSince = 0;
}

// Once per block: the window's median, maximum and spread, and from them this block's threshold.
//
// The threshold is placed IN THE DYNAMIC RANGE rather than as a multiple of an average:
//   threshold = floor + fraction * (peak - floor)
// which is the rule in gibbedy/BeatDetector. That makes the one remaining knob dimensionless --
// a position between the quiet level and the loud level, not a gain -- so it does not have to be
// recalibrated when the material changes level.
//
// The floor is the MEDIAN, not the mean, as foo_bpm does for its peak picking: a mean is dragged
// upward by the very peaks it is supposed to be measured against, a median is not.
inline void sdUpdateStatsBand(SdBand& b) {
  b.statHist[b.statIdx] = b.env;
  b.statIdx = (b.statIdx + 1) % SD_STAT_HIST;

  int32_t v[SD_STAT_HIST], sum = 0;
  for (int i = 0; i < SD_STAT_HIST; i++) { v[i] = b.statHist[i]; sum += v[i]; }
  b.varMean = sum / SD_STAT_HIST;
  for (int i = 1; i < SD_STAT_HIST; i++) {          // insertion sort; 24 values once per block
    int32_t k = v[i]; int j = i - 1;
    while (j >= 0 && v[j] > k) { v[j+1] = v[j]; j--; }
    v[j+1] = k;
  }
  b.floorV   = v[SD_STAT_HIST / 2];
  b.peakStat = v[SD_STAT_HIST - 1];

  // Mean absolute deviation rather than a true variance: same decision, no squaring.
  int32_t mad = 0;
  for (int i = 0; i < SD_STAT_HIST; i++) {
    int32_t d = b.statHist[i] - b.varMean;
    mad += d < 0 ? -d : d;
  }
  b.varMad = mad / SD_STAT_HIST;
  // A beat needs VARIANCE, not level: a held note sits above any threshold indefinitely.
  b.transient = (b.varMean > 0) && ((b.varMad * 100) >= (b.varMean * b.varMinPct));

  int32_t bandSens = hwAudioSensitivity + b.sensAdd;
  if (bandSens < 0) bandSens = 0; else if (bandSens > 100) bandSens = 100;
  int32_t fracQ8 = SD_THR_FRAC_Q8_MAX - (bandSens * (SD_THR_FRAC_Q8_MAX - SD_THR_FRAC_Q8_MIN)) / 100;
  int32_t range  = b.peakStat - b.floorV;
  if (range < 0) range = 0;
  b.thrBlock = b.floorV + ((range * fracQ8) >> 8) + SD_THR_FLOOR_MARGIN;
  // A passage with no beat in it has almost no range, and a threshold placed WITHIN the range
  // would then sit just above the floor and start picking apart the noise.
  b.hasDynamics = (range * 100) >= (b.floorV * b.minRangePct);
}
inline void sdUpdateStats() {
  sdUpdateStatsBand(sdBass);
  if (sdRunMid)  sdUpdateStatsBand(sdMid);
  if (sdRunHigh) sdUpdateStatsBand(sdHigh);
}

// One sample through one band.
inline void sdStep(SdBand& b, int32_t x) {
  // Bandpass as the difference of two one-pole lowpasses: cheap, stable, and the two shifts map
  // directly onto the two edges the DJM's filter array sets in hardware.
  b.lp1 += (x - b.lp1) >> b.kHi;
  b.lp2 += (x - b.lp2) >> b.kLo;
  int32_t bp = b.lp1 - b.lp2;
  int32_t mag = bp < 0 ? -bp : bp;

  if (mag > b.env) b.env += (mag - b.env) >> b.att;
  else             b.env -= (b.env - mag) >> b.rel;

  // Carried with 8 extra bits: written as a plain shift the decrement underflows to zero and the
  // reference freezes. Kept as the re-arm level and for display; the decision threshold is
  // thrBlock, computed once per block from the window statistics.
  b.refAcc += (((int32_t)b.env << 8) - b.refAcc) >> b.refShift;
  b.ref = b.refAcc >> 8;

  // Soft refractory: an onset lifts the threshold and it decays back, so a candidate arriving
  // early is made harder rather than impossible. A hard lockout, by contrast, becomes the thing
  // that sets the rate. Carried in Q16 for the same underflow reason as the reference.
  if (b.boost > 65536) b.boost -= (b.boost - 65536) >> b.boostShift;
  int32_t thr = (b.thrBlock * (b.boost >> 8)) >> 8;

  if (b.armed && b.transient && b.hasDynamics && b.env > thr && !b.peaking) {
    b.peaking   = true;
    b.peakVal   = b.env;
    b.peakClock = sdSampleClock;
  }

  if (b.peaking) {
    // Take the onset at the envelope's PEAK, not where it crossed: a crossing moves with the
    // signal level, a peak does not, so intervals measured peak-to-peak are far more repeatable.
    if (b.env > b.peakVal) {
      b.peakVal   = b.env;
      b.peakClock = sdSampleClock;
    } else if (b.env * 100 < b.peakVal * b.peakFallPct ||
               (uint32_t)(sdSampleClock - b.peakClock) >
                 (uint32_t)b.peakMaxWaitMs * (SAMPLING_FREQUENCY / 1000)) {
      uint32_t tms = (uint32_t)(((uint64_t)b.peakClock * 1000ULL) / SAMPLING_FREQUENCY);
      if ((uint32_t)(tms - b.lastOnsetMs) > (uint32_t)b.lockoutMs) {
        b.lastOnsetMs = tms;
        b.boost = b.boostMaxQ8 << 8;      // the API keeps this knob in Q8
        b.onsetMs = tms;
        b.onsetsThisFrame++;
      }
      b.peaking = false;
      b.armed   = false;
    }
  }
  // Re-arm only once the envelope has fallen back, so one hit produces one edge.
  if (!b.peaking && b.env < b.floorV) b.armed = true;
  b.lastEnv = b.env; b.lastThr = thr;
}

// All three bands over the same block. The gain and clamp are computed once per sample and shared,
// so the extra cost of two more bands is only their filters and comparator, not the input stage.
inline uint32_t sdProcessBlock(const int32_t* raw, int count, int gainShift) {
  sdBass.onsetMs = sdMid.onsetMs = sdHigh.onsetMs = 0;
  sdBass.onsetsThisFrame = sdMid.onsetsThisFrame = sdHigh.onsetsThisFrame = 0;
  for (int i = 0; i < count; i++) {
    int32_t x = (raw[i] >> SAMPLE_DOWNSCALE_SHIFT_FFT) << gainShift;
    if (x > 32767) x = 32767; else if (x < -32768) x = -32768;
    sdStep(sdBass, x);
    if (sdRunMid)  sdStep(sdMid,  x);
    if (sdRunHigh) sdStep(sdHigh, x);
    sdSampleClock++;
  }
  return sdBass.onsetMs;
}

// =========================================================
// --- TEMPO TRACKER (median gap between detected onsets) ---
// =========================================================
// The estimator works on onset TIMES, not on a flux signal: it collects the gaps between
// detected kicks over a window and takes their median. An autocorrelation over a flux ring
// used to live here too; it was measured against the median in sim/ (--mode compare) and
// dropped, because its only advantage was running without a tap, and without a tap it fails
// on real material for the same reason the median does. See doc/content/history.md 2026-09-02.
#define TEMPO_EVAL_MS  1000   // re-evaluate once a second; the onset window changes slowly

// --- Beat-clock phase lock ------------------------------------------------
// Acceptance window of the phase detector, as a percentage of one beat interval: an onset
// further from the grid than this is not treated as a beat at all. See the measurement note at
// the comparison itself for why this is 15 and not 30.
#define BEAT_PHASE_WINDOW_PCT      15
// Loop gain. Each accepted onset moves the grid by 1/N of the measured phase error, so the grid
// converges over a few beats and no single onset can yank it. Smaller N = faster and twitchier.
#define BEAT_PHASE_GAIN_DIV         4
// Consecutive onsets landing outside the window before the downbeat is re-established outright.
// At a couple of onsets a second this is on the order of twenty seconds.
#define BEAT_GRID_MISS_LIMIT       40

// --- Tap anchor -----------------------------------------------------------
// How close the tracker/anchor ratio must be to a rung (1/2, 2/3, 3/4, 1, 4/3, 3/2, 2) to be
// folded onto it. Kept equal to TEMPO_ANCHOR_BAND_PCT below -- see the note there.
#define TAP_FOLD_TOLERANCE      0.08f
// Consecutive EVALUATIONS (not audio blocks -- see tempoEvalSeq) with no foldable relation
// before the anchor is dropped. At one evaluation a second this is twenty seconds of disagreement.
#define TAP_ANCHOR_MISS_LIMIT      20
// The anchor's own raw-tempo reference follows the tracker by 1/N per evaluation. This is what
// notices a genuine track change while an anchor stands, independently of the folding.
#define TAP_ANCHOR_RAW_GAIN_DIV     8

// --- Reported-tempo band --------------------------------------------------
// While a tap anchor stands, the reported tempo may sit at most this far from it, in percent.
// Deliberately equal to TAP_FOLD_TOLERANCE (8%): a wider band here would admit a reading that
// the fold itself rejects, which is exactly how a tapped tempo used to get overwritten a
// second after tapping.
#define TEMPO_ANCHOR_BAND_PCT       8
// Unanchored, the slow reference drifts toward each accepted reading by 1/N. It drifts, it does
// not chase -- a larger N makes the reported tempo steadier and slower to follow a real change.
#define TEMPO_REF_GAIN_DIV         16

inline unsigned long tempoLastEval = 0;

// How far back the median looks. Long enough to hold several beats so one bad gap cannot
// decide the answer, short enough that a tempo change shows up rather than being averaged away.
inline int tempoWindowMs = 3000;

inline int trackedBPM = 0;          // 0 = no confident estimate yet
inline int32_t trackedScore = 0;    // how many gaps the answer rests on, for the AUDIO tab
inline int32_t dbgLagMilli = 0;     // the winning median gap in ms, for the AUDIO tab
// A tapped tempo outranks the tracker. Until this was added the tracker reassigned globalBPM on
// every audio frame (~31 times a second), so a tapped value survived about 32ms and tapping was
// simply ineffective whenever the mic was on -- which reads as "tapping is jumpy" rather than as
// the override it actually was. Tapping now holds the tempo and the audio keeps supplying phase
// through the beat-clock correction, which is how a lighting desk is normally driven.
// Tempo mode. Auto is the resting state and survives a restart; a tap does NOT leave it.
//
// This used to be a latch: one tap shut the tracker out until the user
// found /audio_tune?tap=0, which only the AUDIO tab exposes. Tapping is something you do mid-set,
// on the light, and it silently cost you automatic tracking for the rest of the night.
//
// A tap is better understood as ground truth about the OCTAVE than as a tempo to obey. The
// tracker's own failure mode is landing on a real periodicity at the wrong rung of the ladder --
// measured on hip-hop at 98 BPM it locked to 132, which is 4/3 of the beat. It had found
// something real and only misjudged which subdivision it was. So a tap becomes an anchor: the
// tracker keeps measuring, and its answer is folded to the nearest simple ratio of the tapped
// value. Drift too far from every ratio and the tempo genuinely changed, so the anchor is dropped.
// (tempoAuto itself is declared further up, next to the auto-gain state, because updateAutoGain()
// consults it: in manual mode only a tap may raise the gain.)
// The band the reported tempo may move within, and how long a reading outside it must persist
// before it is believed. Evaluations run once a second, so 30 is thirty seconds.
inline int  tempoSlewPct = 15;      // the band, in percent, around the established tempo
inline int  tempoJumpConfirm = 30;  // evaluations of agreement before adopting outside it
inline int  tapAnchorMiss = 0;   // consecutive evaluations fitting no rung
// The raw, unfolded measurement that was current when the anchor was tapped, and how long the
// current one has disagreed with it. This is the anchor's expiry criterion, and it deliberately
// does NOT depend on the fold: a stale anchor can keep folding successfully forever. Measured
// live 2026-09-02 -- a 120 BPM track under an anchor of 174 reads 125 raw, and 125/174 = 0.72 is
// within 4% of the 3/4 rung, so it was folded up to 183 and published, every single evaluation,
// with tapAnchorMiss never once incrementing. The music had changed and nothing could notice.
inline int  tapAnchorRaw = 0;      // 0 = capture it on the next evaluation
inline int  tapAnchorRawMiss = 0;
// Which tempo evaluation the anchor has already been reconciled against. The fold below lives in
// pollAudioEngine(), which runs once per audio block -- about 31 times a second -- while
// trackedBPM is only recomputed once a second by tempoEvalMedian(). Without this sequence check
// the SAME measurement was tested against the rungs ~31 times and each failure counted as its own
// miss, so "20 consecutive evaluations" was really 0.64 seconds and a single measurement that
// happened not to fit killed the anchor before the next one existed. Measured on hardware
// 2026-09-02: six taps on a hip-hop track, six anchors, lifetimes 0,0,0,11,0,0 seconds, and the
// reported tempo sat at 85 instead of 100 for 92% of three minutes.
inline int tempoRef = 0;               // slow reference the reported tempo is allowed to sit around
inline int beatGridMiss = 0;           // consecutive onsets landing nowhere near the beat grid
inline uint32_t tempoEvalSeq = 0;      // bumped by tempoEvalMedian on every new measurement
inline uint32_t tapAnchorSeq = 0;      // the last one the anchor logic has seen
inline int  tapAnchorBPM = 0;      // last tapped tempo, 0 = none. Deliberately not persisted:
                                   // it is a statement about the music playing now.
// Manual octave override for the tracker's result: 0 = as measured, 1 = double, 2 = halve.
// Deliberately a user decision, not a heuristic. The tracker reports the pulse the bass
// actually has, and for drum & bass that is genuinely the half-time grid -- measured on a
// 176 BPM track, the autocorrelation at 341ms was NEGATIVE while 671ms scored ~1400. Any
// automatic rule that turned 89 into 178 would equally turn 90 BPM hip-hop into 180, which
// is the wrong guess in the other direction. So the octave is offered, not inferred.
inline int tempoMulMode = 0;

// =========================================================
// --- TEMPO: the median gap between kicks, and nothing else ---
// =========================================================
// Five estimators were stacked here over time -- a smoothed median history, autocorrelation with
// harmonic summing, an interval histogram, an onset-phase DFT, and hysteresis on top. Each was
// added to patch the previous one's failure, two of them wrote globalBPM independently, and in
// the end no single one of them owned the answer. All of them were also working on onsets that
// the reference-deadband bug above had made meaningless, so none of the tuning meant anything.
//
// What is left is the whole thing: time the gaps between detected kicks, throw away any gap that
// is not a plausible beat, and take the median of what remains over the last few seconds. The
// median rather than the mean because one missed beat doubles a gap, and a mean would carry that
// straight into the answer while a median ignores it.
#define TEMPO_IVL_RING 16
inline uint32_t tempoIvl[TEMPO_IVL_RING];    // the gap
inline uint32_t tempoIvlAt[TEMPO_IVL_RING];  // when it was measured, so the window can expire it
inline int      tempoIvlIdx = 0;
inline uint32_t tempoPrevOnset = 0;
inline int      tempoAgreePct = 0;      // spread of the gaps around their median, in %
inline int      tempoAgreeMaxPct = 20;  // above this they do not agree on a tempo

// A gap outside 60..200 BPM is not a tempo -- it is a double trigger or a missed beat. Rejecting
// it here is what removes the need for any octave or folding logic downstream.
#define TEMPO_IVL_MIN (60000 / BPM_MAX_LIMIT)   // 300ms
#define TEMPO_IVL_MAX (60000 / BPM_MIN_LIMIT)   // 1000ms

// Two clocks, on purpose. The GAP is measured on the sample clock, which is what makes it
// precise. WHEN it was measured is recorded on millis(), because that is the clock the window is
// later checked against -- the sample clock only advances while blocks are being processed, so it
// runs behind wall time, and comparing the two made every stored interval look already expired.
inline void tempoPushOnsetTime(uint32_t preciseMs, uint32_t wallMs) {
  if (tempoPrevOnset != 0) {
    uint32_t d = preciseMs - tempoPrevOnset;
    if (d >= TEMPO_IVL_MIN && d <= TEMPO_IVL_MAX) {
      tempoIvl[tempoIvlIdx] = d;
      tempoIvlAt[tempoIvlIdx] = wallMs;
      tempoIvlIdx = (tempoIvlIdx + 1) % TEMPO_IVL_RING;
    }
  }
  tempoPrevOnset = preciseMs;
}

inline void tempoEvalMedian(uint32_t nowMs) {
  uint32_t v[TEMPO_IVL_RING]; int n = 0;
  for (int i = 0; i < TEMPO_IVL_RING; i++) {
    if (tempoIvl[i] && (uint32_t)(nowMs - tempoIvlAt[i]) <= (uint32_t)tempoWindowMs) v[n++] = tempoIvl[i];
  }
  // Below three gaps there is nothing honest to report, so the previous answer stands.
  if (n < 3) return;

  // No beats, no tempo -- hold, do not drift.
  //
  // Intervals expire out of the window on their own, so during a breakdown only a handful of
  // stray gaps survive: a pad transient here, a sweep there. A handful of stray gaps is not
  // evidence of a tempo, however tightly they happen to agree with one another -- and the
  // agreement gate below cannot tell the difference, because they do agree. Requiring the
  // surviving intervals to cover at least half the window states "beats are actually arriving"
  // without inventing a beat-count threshold: it scales with the window and with the tempo.
  //
  // Reported live 2026-09-02: during a section with no drums the readout drifted UPWARD. For a
  // light show that is worse than a wrong number -- it speeds up exactly where the music emptied
  // out. When there is nothing to hear, the last thing heard is the right answer, indefinitely.
  uint32_t covered = 0;
  for (int i = 0; i < n; i++) covered += v[i];
  if (covered * 2 < (uint32_t)tempoWindowMs) return;
  for (int i = 1; i < n; i++) {
    uint32_t k = v[i]; int j = i - 1;
    while (j >= 0 && v[j] > k) { v[j+1] = v[j]; j--; }
    v[j+1] = k;
  }

  // Fold integer multiples back onto the base period before taking the median.
  //
  // A missed kick does not produce a wrong interval, it produces exactly two beats' worth stuck
  // together. Left alone those doubles form a second, internally consistent population, and the
  // agreement gate below then PREFERS them: a mixed window scatters and gets rejected, while a
  // short clean run of doubles is tight and gets accepted. So a small minority beats a large
  // majority whenever it happens to arrive in a run. Measured on a 120 BPM track 2026-09-02:
  // 96% of kicks detected, 80% of gaps at ~488ms and only 9% at ~900ms -- and a reported tempo
  // bouncing between 122 and 61.
  //
  // Clustering inter-onset intervals and treating integer multiples of one another as votes for
  // the same period is the established idea (Dixon's IOI clustering in BeatRoot; the harmonic
  // summing used in autocorrelation-based tempo induction is the same thought, and this project's
  // own simulator measured it working -- harmonic summing was what let autocorrelation see
  // through syncopation at all). What follows is a simplification of that, not a citation.
  //
  // The base period is taken from the window's own lower quartile rather than from the previous
  // estimate. Folding onto the previous estimate would be self-reinforcing: once the tracker sat
  // on the wrong octave, every interval would be folded to agree with it and it could never get
  // out. The quartile carries no such memory, and it is robust to the ~8% of gaps that are too
  // short (double triggers) because those sit below it.
  uint32_t base = v[n / 4];
  if (base >= (uint32_t)TEMPO_IVL_MIN) {
    for (int i = 0; i < n; i++) {
      uint32_t k = (v[i] + base / 2) / base;        // nearest integer multiple of the base
      if (k < 2) continue;                          // already the base period, or shorter
      uint32_t folded = v[i] / k;
      uint32_t off = folded > base ? folded - base : base - folded;
      // Only when it genuinely lands back on the base period. Otherwise this is a different
      // interval, not a run of missed beats, and forcing it would invent an agreement that is
      // not there -- which is exactly the failure being fixed, in the other direction.
      if (folded >= (uint32_t)TEMPO_IVL_MIN && off * 4 <= base) v[i] = folded;
    }
    for (int i = 1; i < n; i++) {
      uint32_t k = v[i]; int j = i - 1;
      while (j >= 0 && v[j] > k) { v[j+1] = v[j]; j--; }
      v[j+1] = k;
    }
  }

  uint32_t med = v[n / 2];
  if (med == 0) return;

  // Only report a tempo when the gaps actually agree on one. gibbedy/BeatDetector requires every
  // recent interval to match the newest within a tolerance before it will update its BPM, and
  // holds the previous value otherwise; the same idea, but expressed as spread around the median
  // so that a single outlier does not veto an otherwise clean reading. Without this a median is
  // always willing to produce a number, including from gaps that mean nothing.
  uint32_t dev = 0;
  for (int i = 0; i < n; i++) dev += (v[i] > med) ? (v[i] - med) : (med - v[i]);
  dev /= (uint32_t)n;
  tempoAgreePct = (int)((dev * 100) / med);
  if (tempoAgreePct > tempoAgreeMaxPct) return;   // gaps disagree: keep what we had

  trackedBPM = constrain((int)(60000UL / med), BPM_MIN_LIMIT, BPM_MAX_LIMIT);
  trackedScore = n;          // how many gaps the answer rests on, for the AUDIO tab
  dbgLagMilli = (int32_t)med;
  tempoEvalSeq++;            // a genuinely new measurement, which is what the anchor counts
}

// Drives the once-a-second re-evaluation. Called from the audio path on every block; the
// estimator itself reads the onset window, so nothing needs to be handed in.
inline void tempoTrackerTick(unsigned long now) {
  if (now - tempoLastEval >= TEMPO_EVAL_MS) { tempoLastEval = now; tempoEvalMedian((uint32_t)now); }
}

// Spectral flux: the sum of the POSITIVE changes across a bin range, i.e. how much energy
// newly appeared since the last frame. Negative changes are discarded on purpose -- an onset
// is energy arriving, not leaving.
//
// This is what makes drum & bass work. The level-based detector compares a band's absolute
// value against its own running average, so a continuously rolling sub-bass keeps that average
// permanently high and the kick barely stands out -- measured live 2026-08-28 with a 178 BPM
// track: the threshold sat at 9222 while the band read 6709, and detected beat intervals
// scattered with a standard deviation of 200-594ms around a ~550ms median, locking the BPM
// readout onto ~118 instead of 178 (not even an octave error, so the existing x2 correction
// could never recover it). Flux ignores sustained energy by construction: a held sub-bass has
// near-zero flux, a kick attack is a spike.
inline int32_t fftFlux(int lo, int hi) {
  int32_t sum = 0;
  for (int i = lo; i <= hi; i++) {
    int32_t d = fftMag[i] - fftMagPrev[i];
    if (d > 0) sum += d;
  }
  return sum / (hi - lo + 1);
}
inline unsigned long lastBassTime = 0;
inline unsigned long lastMidTime  = 0;
inline unsigned long lastHighTime = 0;

// Debug/verification signals for the octave-error correction above -- rawDetectedBPM is the
// median-derived BPM BEFORE the 19:1 smoothing into globalBPM, lastRawIntervalMs is the most
// recent accepted (possibly octave-folded) sample. Exposed via /api/state so live drift/lock-up
// behaviour can actually be observed instead of guessed at.
inline int lastRawDetectedBPM = 0;
inline unsigned long lastRawIntervalMs = 0;

inline unsigned long beatIntervals[BPM_HISTORY_SIZE];
inline uint8_t beatIdx = 0;

inline int32_t envFast = 0;
inline int32_t envMid  = 0;
inline int32_t envSlow = 0;
inline int32_t dynThreshold = 1000;
// Own running averages for the Mid/High bands (FFT mode only) -- see the trigger block below.
inline int32_t dynThresholdMid = 100;
inline int32_t dynThresholdHigh = 100;
inline int32_t lastThMid = 0, lastThHigh = 0;

// Latest band energies + threshold, held here (not just local to pollAudioEngine()) so the AUDIO
// DEBUG tab's /api/audio_debug poll can read the same numbers the beat detector just acted on.
inline int32_t lastBassEnergy = 0, lastMidEnergy = 0, lastHighEnergy = 0, lastThBass = 0;

// Runtime switch back to the old envelope-follower method (/audio_tune?fft=0). The FFT path could
// not be tested on hardware when it was written, and a bad audio path is the kind of thing that is
// noticed mid-show -- so the previous, known-working behaviour stays one HTTP call away instead of
// requiring a reflash. Reported state is in /api/audio_debug ("fft").
// Spectral-flux onset detection instead of absolute level (/audio_tune?flux=0 to compare).
// Default on: with sustained-bass material (drum & bass) the level detector measurably
// cannot find the pulse -- see fftFlux() for the numbers.
// Detector per band: 0 = band energy with envelope follower, 1 = spectral flux.
// They are genuinely different tools and the right choice differs per band:
//   Bass  -- energy. The kick dominates the band, so its level IS the event, and an envelope
//            follower on it is what classic hardware beat detectors do.
//   Mid   -- flux. Vocals, pads and bass harmonics sit in this band CONTINUOUSLY, so a level
//            detector always sees "a lot"; only the snare's rise distinguishes it.
//   High  -- either. Cymbals sustain (favours flux), closed hats are short (energy is fine).
inline int tuneDetBass = 0;
inline int tuneDetMid  = 1;
inline int tuneDetHigh = 1;

// Mean absolute deviation per band, for a variance-adaptive threshold. A fixed multiple of the
// mean cannot work across material: in steady music the mean sits close to the peaks and nothing
// crosses, in dynamic music everything does -- which is why the best sensitivity came out at 30
// for techno and 70 for drum & bass. Scaling the threshold by how much the band actually
// fluctuates removes that dependency, and is what Patin's classic energy beat detector does with
// the variance. MAD is used rather than variance because it needs no squaring and no rescaling.
inline int32_t dynMadBass = 0, dynMadMid = 0, dynMadHigh = 0;
// Timestamp of the onset the sample-rate detector found in this frame, 0 if none.
inline uint32_t sdOnsetMs = 0;
// Display-only copies so the AUDIO tab can show level and flux side by side.
inline int32_t lastBassLevel = 0, lastMidLevel = 0, lastHighLevel = 0;
inline int32_t lastBassFlux = 0, lastMidFlux = 0, lastHighFlux = 0;
// Compensates the FFT's 1/N output scaling. Left-shift, runtime-tunable (/audio_tune?fg=), because
// the right value depends on the microphone's actual output level and could not be calibrated
// without the device. If the bands read near zero with music playing, raise it; if they peg, lower it.
inline int tuneFftGainShift = 4;

// Cost instrumentation (see /api/state). Written every poll, worst case held per 5s window, so the
// question "how much headroom is actually left" has a measured answer instead of an estimate.
// Timing telemetry. Read the definitions before drawing a conclusion from them -- both of
// these used to measure something other than their name, and the first CPU measurement taken
// with them (2026-09-03) was unusable as a result:
//   audioLastUs  cost of processing ONE full 512-sample block. It is deliberately NOT updated
//                on the calls that find no data, which are the majority: pollAudioEngine() runs
//                every loop iteration (~170/s) but a block only completes ~31 times a second,
//                and letting the empty calls write here left the field showing a no-op.
//   fftLastUs    cost of fftRun() ALONE. It used to be measured from the top of the block, so
//                it silently included the sample scaling loop, auto-gain and the entire
//                sample-rate detector -- work that runs whether or not the FFT does. That made
//                the FFT look several times more expensive than it is.
inline uint32_t audioLastUs = 0, audioMaxUs = 0, fftLastUs = 0;
inline uint32_t engineLastUs = 0, engineMaxUs = 0;
inline unsigned long perfWindowStart = 0;

void pollAudioEngine() {
  triggerBass = false;
  triggerMid  = false;
  triggerHigh = false;

  if (!hwAudioEnabled) return;

  unsigned long now = millis();
  uint32_t pollT0 = micros();
  bool processedBlock = false;

  // Drain the I2S ring on EVERY call and accumulate until a full frame is assembled, rather than
  // asking for 512 samples once every 32ms.
  //
  // 512 samples at 16kHz is exactly 32.000ms, and the old gate was `>= 32ms`, so with millis()
  // granularity and loop jitter the average collection interval was necessarily a little SLOWER
  // than the microphone produces. The DMA ring (4 x 512 = 128ms) therefore backed up until the
  // driver began dropping new samples. Worse, i2s_read has already removed a partial frame from
  // the ring by the time we see it is short, so the old early-return threw those samples away.
  //
  // Both losses corrupt time itself here: sdSampleClock counts only samples we processed, so
  // missing audio makes the clock run slow, which makes every beat interval measure SHORT and
  // every tempo read HIGH. Measured across this session, wall-clock onsets sat at 452ms while
  // the device reported 427ms -- a ratio of 1.059, i.e. about 6% of the audio never arrived.
  // That is the whole of the systematic error we kept blaming on the estimator.
  static int rawFill = 0;
  size_t bytes_read = 0;
  if (i2s_read(I2S_PORT, raw_samples + rawFill, sizeof(int32_t) * (SAMPLES - rawFill),
               &bytes_read, pdMS_TO_TICKS(I2S_READ_TIMEOUT_MS)) == ESP_OK && bytes_read > 0) {
    rawFill += bytes_read / BYTES_PER_SAMPLE_32BIT;
    if (rawFill < SAMPLES) return;   // keep what we have; the rest arrives on a later call
    rawFill = 0;
    processedBlock = true;
    int count = SAMPLES;

    // Frequency separation via FFT. This used to be one of two paths, the other being three
    // envelope followers over the wideband sample magnitude. Those never separated by
    // FREQUENCY at all -- they separated by how fast the overall level rose, so a hi-hat, a
    // snare and a click were indistinguishable and "bass" was merely the smoothed total
    // level. It was removed on 2026-09-02; see doc/content/proposal.md if it ever needs to
    // come back.
    // --- Real frequency separation ---------------------------------------------------
    // The three envelope followers in the else-branch below never separated by FREQUENCY at
    // all: they separated by how fast the overall level rose, so a hi-hat, a snare and a click
    // were indistinguishable and "bass" was just the smoothed total level. That is why a
    // dedicated High trigger (e.g. strobe on hi-hats) could not work before.
    uint32_t t0 = micros();
    const bool runFft = fftIsNeeded(now);
    int32_t peak = 0; int clips = 0; int rawClips = 0;
    for (int i = 0; i < FFT_N; i++) {
      // Saturation at the microphone is a different failure from our own gain being too high,
      // and only one of them is fixable from here. `s` below is an int32 computation that does
      // not saturate, so however far past full scale it lands is measurable and the input range
      // can be corrected by exactly that much. But if the acoustic level exceeded the converter
      // itself, raw_samples arrives already flat-topped: the overshoot is unknowable, and
      // lowering the gain cannot undo it because the damage is upstream. Count that separately
      // so it can be reported as what it is -- move the mic, do not turn anything down.
      if (raw_samples[i] > MIC_RAW_CLIP_LEVEL || raw_samples[i] < -MIC_RAW_CLIP_LEVEL) rawClips++;
      int32_t s = (raw_samples[i] >> SAMPLE_DOWNSCALE_SHIFT_FFT) << tuneInputGainShift;
      // Measure the level BEFORE clamping. Taking it afterwards pins the meter to full scale
      // as soon as one sample in the frame saturates, and it then cannot move again however
      // far the gain is turned down -- so it shows neither the real level nor any headroom.
      // Letting peak run past 32767 is the point: that overshoot is what says "turn it down".
      int32_t a = s < 0 ? -s : s;
      if (a > peak) peak = a;
      if (a >= 32000) clips++;              // count each saturated sample once
      if (runFft) {
        if (s > 32767) s = 32767; else if (s < -32768) s = -32768;
        fftRe[i] = (int16_t)(((int32_t)s * fftWindow[i]) >> 15);
        fftIm[i] = 0;
      }
    }
    micPeak = peak; micClipCount = clips; micRawClipCount = rawClips;
    // Continuous-time onset detection on the same block the FFT just consumed.
    // Clock health, measured over a long window so it reflects steady-state loss, not jitter.
    if (sdClkStartWall == 0 || (uint32_t)(now - sdClkStartWall) > 60000UL) {
      sdClkStartWall = (uint32_t)now; sdClkStartSample = sdSampleClock;
    } else {
      uint32_t wallEl = (uint32_t)now - sdClkStartWall;
      if (wallEl > 3000UL) {
        uint32_t sampEl = (uint32_t)(((uint64_t)(sdSampleClock - sdClkStartSample) * 1000ULL) / SAMPLING_FREQUENCY);
        sdClkDriftPpt = (int)(((int64_t)wallEl - (int64_t)sampEl) * 1000LL / (int64_t)wallEl);
      }
    }
    updateAutoGain((uint32_t)now);
    // Decided once per block, not per sample. A band runs if something is routed to it, or if
    // a browser is on the AUDIO tab and would otherwise see it as permanently silent.
    sdRunMid  = sdAllBands && (sdMidWanted  || runFft);
    sdRunHigh = sdAllBands && (sdHighWanted || runFft);
    sdUpdateStats();   // block-level: window median, peak, spread -> this block's threshold
    sdOnsetMs = sdProcessBlock(raw_samples, FFT_N, tuneInputGainShift);
    // Everything above is detection and runs every frame. Everything below is spectrum work.
    if (runFft) {
    uint32_t fftT0 = micros();
    fftRun();
    fftLastUs = micros() - fftT0;

    // Per-band averages, then the same attack/decay smoothing the tuning UI already exposes --
    // a single 32ms frame is too noisy to threshold directly. The tune* names keep their
    // meaning: fast = High band, mid = Mid band, slow = Bass band.
    // Clamped before shifting: an unclamped average (max ~32767) shifted by a large gain would
    // overflow int32 and wrap to a negative energy -- which reads as "silence" and would look
    // like the detector randomly dying on loud passages.
    const int32_t BAND_MAX = 200000;
    int32_t bRaw = std::min(fftBand(tuneBinBassLo, tuneBinBassHi), BAND_MAX) << tuneFftGainShift;
    int32_t mRaw = std::min(fftBand(tuneBinMidLo,  tuneBinMidHi),  BAND_MAX) << tuneFftGainShift;
    int32_t hRaw = std::min(fftBand(tuneBinHighLo, tuneBinHighHi), BAND_MAX) << tuneFftGainShift;

    // Spectral flux of the same three ranges, computed before fftMagPrev is overwritten.
    // Kept as display values regardless of which detector is active, so the AUDIO tab can
    // show level and flux side by side.
    lastBassFlux = std::min(fftFlux(tuneBinBassLo, tuneBinBassHi), BAND_MAX) << tuneFftGainShift;
    lastMidFlux  = std::min(fftFlux(tuneBinMidLo,  tuneBinMidHi),  BAND_MAX) << tuneFftGainShift;
    lastHighFlux = std::min(fftFlux(tuneBinHighLo, tuneBinHighHi), BAND_MAX) << tuneFftGainShift;
    for (int i = 0; i < FFT_N / 2; i++) fftMagPrev[i] = fftMag[i];

    // Per band: flux goes in unsmoothed (an onset IS a single-frame spike, and the envelope
    // would smear exactly the peak we are looking for), while energy runs through the
    // attack/decay follower -- which is what makes those sliders meaningful again for any band
    // set to energy.
    if (tuneDetBass) envSlow = lastBassFlux;
    else {
      if (bRaw > envSlow) envSlow += (bRaw - envSlow) >> tuneSlowAttackShift;
      else envSlow -= (envSlow - bRaw) >> tuneSlowDecayShift;
    }
    if (tuneDetMid) envMid = lastMidFlux;
    else {
      if (mRaw > envMid) envMid += (mRaw - envMid) >> tuneMidAttackShift;
      else envMid -= (envMid - mRaw) >> tuneMidDecayShift;
    }
    if (tuneDetHigh) envFast = lastHighFlux;
    else {
      if (hRaw > envFast) envFast += (hRaw - envFast) >> tuneFastAttackShift;
      else envFast -= (envFast - hRaw) >> tuneFastDecayShift;
    }
    lastBassLevel = bRaw; lastMidLevel = mRaw; lastHighLevel = hRaw;
    } else {
      fftLastUs = 0;          // reported as zero so the saving is visible on the AUDIO tab
    }
    // Outside the spectrum work: the tempo estimator runs off onset timestamps, not the FFT.
    tempoTrackerTick(now);

    // Bands are now independent, so mid/high are read directly instead of being derived as
    // differences between envelope speeds (that construction is what once made midEnergy
    // structurally ~0 -- see the tuneMidAttackShift comment above).
    // Read directly, not derived as differences between envelope speeds: the bands are genuinely
    // independent now. That old construction is what once made midEnergy structurally ~0.
    int32_t bassEnergy = envSlow, midEnergy = envMid, highEnergy = envFast;

    // Threshold = running mean + k x mean-absolute-deviation. Sensitivity now sets k rather than
    // a multiple of the mean, so the same setting works whether the music is steady or dynamic.
    int32_t kSens = ((100 - hwAudioSensitivity) * 32) / 100 + 4;   // sens 0 -> 36, sens 100 -> 4
    dynThreshold += (envSlow - dynThreshold) >> tuneDynThreshSmoothShift;
    { int32_t dv = envSlow - dynThreshold; if (dv < 0) dv = -dv;
      dynMadBass += (dv - dynMadBass) >> tuneDynThreshSmoothShift; }
    int32_t thBass = dynThreshold + (dynMadBass * kSens) / 8 + tuneNoiseFloor;
    lastBassEnergy = bassEnergy; lastMidEnergy = midEnergy; lastHighEnergy = highEnergy; lastThBass = thBass;

    // With the sample-rate detector active the bass trigger comes from it, timestamped from the
    // audio clock; the frame-based comparison stays as the fallback (/audio_tune?bsd=0).
    // Bass always comes from the sample-rate detector: it timestamps on the audio clock rather
    // than on a 32ms frame boundary, which is what the tempo estimator needs. The frame-based
    // comparison survives only for Mid/High under sab=0 -- see below.
    if (sdOnsetMs) bassOnsetUsedMs = sdOnsetMs;
    bool beatDetected = (sdOnsetMs != 0);

    if (beatDetected) {
      unsigned long diff = now - lastBassTime;
      // Precise timestamp where it matters: the tempo estimator works on onset positions, so it
      // gets the sample-clock time rather than the frame's millis(). Everything else stays on
      // millis() so the beat clock is not fed from two different timebases.
      uint32_t onsetAt = sdOnsetMs ? sdOnsetMs : (uint32_t)now;
      tempoPushOnsetTime(onsetAt, (uint32_t)now);
      lastBassTime = now;
      agLastBeatMs = (uint32_t)now;   // auto-gain may only climb while these keep coming
      triggerBass = true;
      guiBass = true;
      dbgBassHit = true;

      if (diff < MAX_BEAT_INTERVAL_MS) {
        unsigned long currentInterval = MS_PER_MINUTE / globalBPM;
        long error = std::abs((long)diff - (long)currentInterval);

        // Octave-error correction: a plain energy-threshold onset detector misses quiet kicks
        // sometimes, and once globalBPM has locked onto the wrong octave (e.g. half the real
        // tempo after a run of missed hits), every genuinely-correct, faster interval differs
        // from currentInterval by ~50% and gets rejected by the tolerance gate below forever --
        // a permanent "too slow" lock with no way back. Also fold the reverse case (a single
        // missed beat: diff is ~2x a real beat) down to its true single-beat length instead of
        // feeding a falsely-slow raw interval into the history/median.
        long errorAsDouble = std::abs((long)(diff * 2) - (long)currentInterval); // diff looks like half of currentInterval
        long errorAsHalf   = std::abs((long)(diff / 2) - (long)currentInterval); // diff looks like a missed beat (2x currentInterval)
        unsigned long candidate = diff;
        long bestError = error;
        if (errorAsDouble < bestError) { bestError = errorAsDouble; } // keep raw (faster) diff -- it's the correct one
        if (errorAsHalf < bestError && (diff / 2) >= MIN_BEAT_INTERVAL_MS) { candidate = diff / 2; bestError = errorAsHalf; }

        // Every plausible interval is recorded now, instead of only those that already agree
        // with the current globalBPM. The old gate (bestError < currentInterval/5) was
        // self-reinforcing: once globalBPM sat on a wrong value more than ~20% away from the
        // truth, every CORRECT interval was rejected for disagreeing with it, and only an exact
        // reading of BPM_DEFAULT_FALLBACK could ever break out. Measured live 2026-08-28 on a
        // 178 BPM track: flux detection produced a clean 349ms median interval (true beat 337ms)
        // while globalBPM stayed pinned at 131, because 349 vs the assumed 458 exceeded the
        // tolerance on every single sample. The 16-sample median below is what rejects outliers;
        // it does not need a gate in front of it that presumes the answer.
        bool sampleWritten = false;
        if (candidate >= MIN_BEAT_INTERVAL_MS && candidate <= MAX_BEAT_INTERVAL_MS) {
            beatIntervals[beatIdx] = candidate;
            beatIdx = (beatIdx + 1) % BPM_HISTORY_SIZE;
            sampleWritten = true;
            lastRawIntervalMs = candidate;
        }

        if (sampleWritten) {
          unsigned long sorted[BPM_HISTORY_SIZE];
          int validCount = 0;
          for(int i=0; i<BPM_HISTORY_SIZE; i++) {
            if(beatIntervals[i] > 0) sorted[validCount++] = beatIntervals[i];
          }

          if (validCount >= BPM_MIN_VALID_SAMPLES) {
            for(int i = 1; i < validCount; i++) {
              unsigned long key = sorted[i];
              int j = i - 1;
              while(j >= 0 && sorted[j] > key) { sorted[j+1] = sorted[j]; j--; }
              sorted[j+1] = key;
            }
            unsigned long medianInterval = sorted[validCount / 2];
            int detectedBPM = MS_PER_MINUTE / medianInterval;
            lastRawDetectedBPM = detectedBPM;

            // Snap rather than crawl when the median disagrees sharply with the current value.
            // The 19:1 smoothing moves globalBPM by ~5% per sample, so recovering from a wrong
            // lock would take dozens of accepted beats even once they are no longer rejected --
            // long enough to look like it never recovers. A median over 16 intervals is already
            // robust enough to trust outright when it is this far away; the smoothing stays for
            // fine tracking, where it belongs.
            // The tempo tracker wins when it has an estimate: it answers "which period
            // explains all the onsets", whereas this median only averages the gaps between
            // consecutive ones -- which syncopated material defeats by construction. The
            // median path stays as the fallback for when the tracker has nothing yet
            // (startup, silence) and remains visible as rawBPM for comparison.
            // Display only. This path used to write globalBPM as well, so two estimators wrote
            // it from the same function and whichever ran last won; it stays visible as rawBPM
            // purely as a second opinion to compare against.
          }
        }
      }
      // Phase-locked correction instead of snapping the clock to every onset.
      //
      // beatsElapsedTotal (see the .ino) is beatCount plus (now - lastBeatTime)/interval, so
      // whatever moves lastBeatTime moves the phase of every BPM-synced effect. Setting it to
      // `now` on each detected onset handed the detector's jitter straight to that clock: with
      // onsets scattering ~180ms around a 400ms beat, the phase jumped on every hit and even the
      // "Global BPM" trigger fired irregularly. Reported live 2026-08-31 as "global bpm feuert
      // auch wahllos".
      //
      // The tempo tracker already supplies a stable period, so an onset only has to correct the
      // PHASE, and only when it lands near where a beat was expected. Absorbing a quarter of the
      // error per beat locks on within a few beats while ignoring individual stray hits.
      {
        long interval = (globalBPM > 0) ? (long)(MS_PER_MINUTE / globalBPM) : 500L;
        // The phase error against the running grid, folded into +/- half a beat so that it is
        // SIGNED and symmetric: positive means the onset came after the grid beat (the grid is
        // running fast and must be held back), negative means it came before the next one (the
        // grid is slow and must be pulled forward).
        //
        // The previous form, sinceLast - interval, could only ever be negative in practice. The
        // metronome consumes each beat the instant the grid reaches it, so an onset arriving after
        // that point measured as almost a whole interval early and fell outside the window; the
        // grid was correctable in one direction only. Reported live 2026-09-02: after a hard sync
        // the effect sat right and then walked away from the music.
        long sinceLast = (long)(now - lastBeatTime);
        long err = sinceLast;
        if (err > interval / 2) err -= interval;

        // +/-15%, not +/-30%. A phase detector may only be fed onsets that plausibly ARE the beat.
        // At 30% the window swallowed the off-beat hits of syncopated material -- a boom-bap kick
        // sits a quarter of a beat off the grid -- and each of those dragged the grid a sixteenth
        // of a beat in whichever direction it happened to lie. Measured at the DMX output: 8%
        // cycle jitter at 30%, against 3% with the audio engine switched off entirely.
        if (labs(err) <= (long)(interval * BEAT_PHASE_WINDOW_PCT / 100)) {
          // PHASE ONLY. The beat itself is counted by the metronome in updateEngines(), which runs
          // at globalBPM; here the grid is merely pulled a quarter of the error toward the onset.
          //
          // Counting the beat here was the jitter. Because that metronome consumes every beat the
          // moment the grid reaches it, the only onsets that can still arrive in this branch are
          // EARLY ones -- a late onset finds sinceLast just reset and falls outside the window. So
          // every correction incremented beatCount ahead of the grid and jumped beatsElapsedTotal
          // forward by up to the full 30% the window admits. Measured at the DMX output
          // 2026-09-02: a one-beat dimmer cycle nominally 488ms ran as short as 336ms (0.69 --
          // precisely that window edge), against 455..513ms with the audio engine switched off.
          // Nudging instead of counting keeps the phase continuous: the grid drifts toward the
          // music over a few beats and the effect never skips.
          lastBeatTime = (unsigned long)((long)lastBeatTime + err / BEAT_PHASE_GAIN_DIV);
          masterSyncTime = lastBeatTime;
          beatGridMiss = 0;
        } else if (++beatGridMiss > BEAT_GRID_MISS_LIMIT) {
          // Nothing has landed near the grid for a long while -- at a couple of onsets a second
          // that is on the order of twenty seconds. Playback restarted, or the track changed;
          // re-establish the downbeat outright. The threshold has to be generous now that the
          // window is narrow, or syncopated material would force a resync it does not need. beatCount is deliberately NOT touched --
          // it is advanced by the metronome alone, and that single ownership is what keeps the
          // effect phase continuous instead of jumping whenever an onset arrives early.
          lastBeatTime = now;
          masterSyncTime = now;
          beatGridMiss = 0;
        }
        // Otherwise: an off-grid onset. It still counts as an FX trigger above -- that is what an
        // audio trigger is for -- but it must not be allowed to drag the tempo grid with it.
      }
      // Deliberately NOT setting manualTap here (that used to happen on every real beat, not just
      // an actual tap-tempo button press). manualTap's handler in the .ino unconditionally does
      // `beatCount = 0`, meant for a literal user tap re-establishing the downbeat -- firing it on
      // every ongoing audio-detected beat zeroed beatCount back out on the very same loop()
      // iteration it was just incremented above (pollAudioEngine() runs immediately before
      // updateEngines() in loop()), permanently pinning multi-beat sync (Movement FX "Global BPM
      // Sync" with e.g. 32 beats/rev) inside a single beat -- the pattern only ever traced 1/32 of
      // a revolution before snapping back, looking like a left-right twitch with no real sweep.
      // Reported live 2026-08-20. The beatCount++/lastBeatTime/masterSyncTime updates above are
      // the correct incremental resync for an ongoing beat stream; a full phase-zeroing resync is
      // still exactly what /beat (the actual tap-tempo endpoint) sets manualTap=true for.
    }

    if (now - lastBassTime > SILENCE_TIMEOUT_MS) {
        // Hold the tempo through quiet passages instead of discarding it.
        //
        // This used to force globalBPM to a fixed 120 after 2.5s without a kick, straight past
        // the band that everything else goes through, and wipe the interval history with it. A
        // breakdown routinely runs longer than 2.5s, so the tempo was being reset mid-track --
        // which is what "it drops during the quiet part" actually was: not a mismeasurement but
        // a reset. And the fixed 120 was arbitrary; it happened to be invisible on a 120 BPM
        // track and wrong on every other one.
        //
        // When there is nothing to hear, the last thing heard is the best information available,
        // and for lighting it is also what you want -- the pulse should carry on through the
        // breakdown at the tempo of the track, ready for the drop. The virtual beat below keeps
        // running on exactly that.
        for(int i=0; i<BPM_HISTORY_SIZE; i++) beatIntervals[i] = 0;
        beatIdx = 0;

        unsigned long virtualInterval = MS_PER_MINUTE / globalBPM;
        if (now - masterSyncTime >= virtualInterval) {
            masterSyncTime = now;
        }
    }

    // Mid/High get their OWN dynamic thresholds in FFT mode. Measuring them against the bass
    // threshold (as the legacy path does) only made sense while all three came from the same
    // broadband level. With real bands, a hi-hat carries far less energy than a kick, so a
    // bass-derived threshold would sit permanently above the entire High band and the trigger
    // would never fire -- which is precisely the "strobe on hi-hat" case this is for. Each band
    // now fires when it rises above its OWN recent average, i.e. a real per-band onset detector.
    // Per-band thresholds, each from its own running mean and deviation. The removed legacy path
    // derived both by shifting the BASS threshold down, which is why a bass-heavy track could
    // hold the High threshold above everything the High band ever produced.
    int32_t thMid, thHigh;
    {
      dynThresholdMid  += (envMid  - dynThresholdMid)  >> tuneDynThreshSmoothShift;
      dynThresholdHigh += (envFast - dynThresholdHigh) >> tuneDynThreshSmoothShift;
      { int32_t dv = envMid - dynThresholdMid; if (dv < 0) dv = -dv;
        dynMadMid += (dv - dynMadMid) >> tuneDynThreshSmoothShift; }
      { int32_t dv = envFast - dynThresholdHigh; if (dv < 0) dv = -dv;
        dynMadHigh += (dv - dynMadHigh) >> tuneDynThreshSmoothShift; }
      thMid  = dynThresholdMid  + (dynMadMid  * kSens) / 8 + tuneNoiseFloor;
      thHigh = dynThresholdHigh + (dynMadHigh * kSens) / 8 + tuneNoiseFloor;
      // The divisor tunables stay usable as a per-band trim on top of that.
      thMid  >>= (tuneMidThreshDivShift  > 0 ? tuneMidThreshDivShift  - 1 : 0);
      thHigh >>= (tuneHighThreshDivShift > 0 ? tuneHighThreshDivShift - 1 : 0);
    }
    lastThMid = thMid; lastThHigh = thHigh;

    // Mid and High now come from their own instances of the same chain the kick uses, rather
    // than from a frame-based comparison against a smoothed mean. That old path could only
    // timestamp on 32ms frame boundaries and had no peak picking, which is why a dimmer effect
    // set to "high" never sat on the hi-hats properly. The band comparison stays as the fallback
    // for when the sample detector is off (/audio_tune?bsd=0) or the FFT path is not running.
    bool midHit, highHit;
    if (sdAllBands) {
      midHit  = sdRunMid  && (sdMid.onsetMs  != 0);
      highHit = sdRunHigh && (sdHigh.onsetMs != 0);
    } else {
      // Only reachable via /audio_tune?sab=0, the escape hatch for measuring what the three
      // detector bands actually cost. Frame resolution and a smoothed mean -- good enough for a
      // trigger, not for tempo, which is why bass never uses it.
      midHit  = (midEnergy  > thMid);
      highHit = (highEnergy > thHigh);
    }
    if (midHit && (now - lastMidTime) > MID_DEBOUNCE_MS) {
        lastMidTime = now; triggerMid = true; guiMid = true; dbgMidHit = true;
    }
    if (highHit && (now - lastHighTime) > HIGH_DEBOUNCE_MS) {
        lastHighTime = now; triggerHigh = true; guiHigh = true; dbgHighHit = true;
    }
  }

  // Held per 5s window so a single outlier cannot hide behind an average.
  // Applied outside the beat-detected block on purpose: the tracker does not need an onset
  // to have just fired, it works off the rolling flux history.
  if (trackedBPM > 0 && hwAudioEnabled && tempoAuto) {
    // Fold the measurement onto the rung the tap identified, when there is one and it fits.
    // Once per new measurement, not once per audio block -- see tempoEvalSeq. The fold rewrites
    // trackedBPM in place, so re-running it on an unchanged value would achieve nothing anyway.
    if (tapAnchorBPM >= BPM_MIN_LIMIT && tempoEvalSeq != tapAnchorSeq) {
      tapAnchorSeq = tempoEvalSeq;
      // Expiry, judged on the raw measurement before any folding. An anchor describes the track
      // that was playing when it was tapped; when the underlying measurement has settled
      // somewhere else for half a minute, that track is over. Small differences still move the
      // reference along, so a track that merely drifts does not trip this -- and a measurement
      // that only jumps away now and then (drum & bass swings between the half-time pulse and the
      // full one) keeps resetting the counter, which is why the anchor survives there.
      int rawTracked = trackedBPM;
      if (tapAnchorRaw <= 0) tapAnchorRaw = rawTracked;
      int rawGap = abs(rawTracked - tapAnchorRaw) * 100 / tapAnchorRaw;
      if (rawGap <= tempoSlewPct) {
        tapAnchorRawMiss = 0;
        tapAnchorRaw += (rawTracked - tapAnchorRaw) / TAP_ANCHOR_RAW_GAIN_DIV;
      } else if (++tapAnchorRawMiss > tempoJumpConfirm) {
        tapAnchorBPM = 0; tapAnchorRaw = 0; tapAnchorRawMiss = 0; tapAnchorMiss = 0;
        tempoRef = globalBPM;   // see the note at the other drop site below
      }
      // Nested, not a second top-level `if`: the fold must run once per new measurement, never
      // once per audio block. Guarded again because the expiry above may just have dropped it.
      if (tapAnchorBPM >= BPM_MIN_LIMIT) {
      static const float kRatios[] = { 0.5f, 2.0f/3.0f, 0.75f, 1.0f, 4.0f/3.0f, 1.5f, 2.0f };
      float r = (float)trackedBPM / (float)tapAnchorBPM;
      float best = 0; float bestErr = 1e9f;
      for (float k : kRatios) {
        float e = fabsf(r - k) / k;
        if (e < bestErr) { bestErr = e; best = k; }
      }
      // 8% is wide enough to absorb the tracker's own scatter, narrow enough that a real tempo
      // change lands between rungs and is passed through untouched instead of being forced.
      if (bestErr < TAP_FOLD_TOLERANCE) { trackedBPM = (int)((float)trackedBPM / best + 0.5f); tapAnchorMiss = 0; }
      // An anchor describes the track that was playing when it was tapped. Once the measurement
      // has fitted none of the rungs for a while the music has moved on, and holding on would
      // force the new tempo onto the old track's grid. Drop it and go back to plain tracking.
      // Hand the band back a usable reference. tempoRef is only advanced while unanchored, so
      // without this it still holds whatever was current before the tap -- tap 174 over a
      // reference of 90 and, when the anchor eventually expires, every correct reading near 174
      // is >15% out and the readout stays stuck for another tempoJumpConfirm evaluations.
      else if (++tapAnchorMiss > TAP_ANCHOR_MISS_LIMIT) {
        tapAnchorBPM = 0; tapAnchorMiss = 0; tapAnchorRaw = 0; tapAnchorRawMiss = 0;
        tempoRef = globalBPM;
      }
      }
    }
    int shown = trackedBPM;
    // The override is ignored rather than clamped when it would leave the valid range --
    // clamping would silently show a tempo that is neither the measurement nor its octave.
    if (tempoMulMode == 1 && trackedBPM * 2 <= BPM_MAX_LIMIT) shown = trackedBPM * 2;
    else if (tempoMulMode == 2 && trackedBPM / 2 >= BPM_MIN_LIMIT) shown = trackedBPM / 2;
    shown = constrain(shown, BPM_MIN_LIMIT, BPM_MAX_LIMIT);

    // A tempo does not change in seconds. Music can drift, and a DJ can mix into something
    // faster, but neither happens between one evaluation and the next -- so a large jump is
    // far more likely to be the estimator losing its footing than the music changing.
    //
    // Observed on a 120 BPM house track: it read 120 correctly, dropped to 83 through a quiet
    // passage (fewer kicks, gaps stretch, the median slips a rung -- 83/120 is close to 2/3)
    // and then overshot to 136 when the track came back. Every one of those was published
    // immediately, which makes the readout useless precisely when a breakdown means you are
    // relying on it.
    //
    // So the reported tempo lives in a band around the value already established: inside it the
    // tracker works freely, outside it the reading is simply not published. 120 to 83 is -31%
    // and never gets through at all.
    //
    // A hard band alone would strand the readout on the old tempo after a genuine track change,
    // with nothing to say so. A reading outside the band is therefore still adopted -- but only
    // after it has said the same thing for thirty consecutive evaluations, which is thirty
    // seconds. That is nowhere near "within seconds" and it is not a dead end either. Tapping
    // remains the immediate way to set a new base, which is what a tap is for.
    // The band has to be measured against something that does NOT move with every accepted
    // reading. Gating against globalBPM itself made this a rate limiter, not a band: each
    // evaluation was free to move 15%, so four of them in four seconds carried the tempo from
    // 122 to 66 and nothing in the code objected -- every single step was "small". Measured live
    // 2026-09-02: 66..128 over two minutes with a tap anchor standing at 122 the whole time, and
    // 68..163 with no anchor at all. The dimmer effect faithfully followed all of it, which is
    // what the reports "it drifts apart" and "jumps to 72 in the quiet passages" actually were.
    //
    // A tap is the best reference there is, because it is the one number the user asserted. With
    // no tap, a slowly-moving average of what has been accepted stands in: it still follows a
    // genuine drift, but sixteen times slower than the readings themselves, so a run of bad
    // evaluations in a quiet passage cannot walk the tempo away.
    static int pendCand = 0, pendCount = 0;
    if (tempoRef <= 0) tempoRef = shown;
    bool anchored = (tapAnchorBPM >= BPM_MIN_LIMIT);
    int ref = anchored ? tapAnchorBPM : tempoRef;
    // With an anchor the band must match the rung tolerance the fold above uses. At 15% it did
    // not: a tap of 125 against a tracker reading 112 is 10.4% off -- too far for the fold to
    // recognise it as the same rung, so the raw 112 passed through unfolded, yet close enough for
    // the band to publish it. The tap was overwritten by the next evaluation, one second later.
    // Reported live 2026-09-02 (translated): "I tap in 125 and it drops straight to 112".
    // Not a regression from the reference-band change -- the old code compared against globalBPM,
    // which equals the tapped value right after a tap, so the arithmetic was identical.
    int band = anchored ? TEMPO_ANCHOR_BAND_PCT : tempoSlewPct;
    int diffPct = (ref > 0) ? (abs(shown - ref) * 100 / ref) : 100;
    if (diffPct <= band) {
      globalBPM = shown; pendCount = 0; pendCand = 0;
      if (!anchored) tempoRef += (shown - tempoRef) / TEMPO_REF_GAIN_DIV;   // the reference drifts, it does not chase
    } else {
      if (pendCand > 0 && (abs(shown - pendCand) * 100 / pendCand) <= tempoSlewPct) pendCount++;
      else { pendCand = shown; pendCount = 1; }
      // Thirty evaluations saying the same new thing: the music really did change. Adopt it and
      // move the reference with it, otherwise the band would keep pulling back to the old tempo.
      //
      // NOT while a tap anchor stands. A tap is the user naming which pulse is the beat, and a
      // measurement that insists on a different one is exactly the case the anchor exists for --
      // adopting it would let persistence beat instruction. Drum & bass makes this concrete:
      // measured on a ~168 BPM track, the interval median sits on ~860ms, which is 2.45 beats and
      // therefore fits no rung, so the fold cannot rescue it; the reading held near 70 for long
      // stretches and this branch published it thirty seconds later, against a standing anchor of
      // 171. Reported live 2026-09-02 (translated) as "drum & bass at 177 BPM is hopeless" and
      // "back at 70 again".
      // The escape from a genuinely stale anchor stays tapAnchorMiss, which drops the anchor after
      // twenty evaluations that fit no rung at all; adoption then resumes normally.
      if (!anchored && pendCount >= tempoJumpConfirm) { globalBPM = shown; tempoRef = shown; pendCount = 0; pendCand = 0; }
    }
  }
  // Only a call that actually processed a block says anything about cost -- see the note at
  // the declaration.
  if (processedBlock) {
    audioLastUs = micros() - pollT0;
    if (audioLastUs > audioMaxUs) audioMaxUs = audioLastUs;
  }
}

void initAudioEngine() {
  i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate = SAMPLING_FREQUENCY,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = DMA_BUF_COUNT,
    .dma_buf_len = DMA_BUF_LEN,
    .use_apll = false,
    .tx_desc_auto_clear = true,
    .fixed_mclk = 0
  };
  i2s_pin_config_t pin_config = { .bck_io_num = I2S_SCK, .ws_io_num = I2S_WS, .data_out_num = I2S_PIN_NO_CHANGE, .data_in_num = I2S_SD };
  i2s_driver_install(I2S_PORT, &i2s_config, 0, NULL);
  i2s_set_pin(I2S_PORT, &pin_config);
  for(int i=0; i<BPM_HISTORY_SIZE; i++) beatIntervals[i] = 0;
  fftInitTables();
}
