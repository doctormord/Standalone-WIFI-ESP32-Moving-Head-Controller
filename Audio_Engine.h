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
inline int32_t micPeak = 0;       // peak magnitude of the last frame, pre-clamp -- may exceed 32767 when overdriven
inline int micClipCount = 0;      // samples at/near full scale in the last frame

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
inline bool  sdEnabled  = true;
inline int   sdKLo      = 6;    // lower edge ~40Hz   (fc = 16000 / (2*pi * 2^k))
inline int   sdKHi      = 4;    // upper edge ~160Hz -- the kick's band, as on the DJM.
// 3 (~320Hz) was wide enough to let the bass line and the low mids through, and every one
// of those that crossed the threshold became a spurious beat.
inline int   sdAtt      = 2;    // envelope attack, ~0.25ms -- follows the leading edge
inline int   sdRel      = 11;   // envelope release, ~128ms (2^k samples / 16000)
// 7 was a mistake: 2^7 samples at 16kHz is 8ms, not the ~50ms the comment claimed. At an 8ms
// release nothing is being enveloped -- the value just follows the rectified bandpass, which
// swings at 40-160Hz and therefore crosses any threshold continuously. The only thing holding
// the rate down was the refractory period, which is exactly the failure this chain kept showing.
// A kick needs the release long enough that its decay reads as one hump.
inline int   sdRefShift = 14;   // ~1s: the rolling average the comparator measures against
// The refractory period only suppresses one kick re-triggering on its own decay. It must
// stay far below the shortest beat in range (60000/200 = 300ms), because a lockout longer
// than that stops being a guard and becomes the thing that sets the rate: measured on the
// device, the onset median came out as the lockout plus ~40ms at every setting tried.
inline int   sdLockoutMs = 60;  // hard floor only; the soft boost above does the real work

inline int32_t  sdLp1 = 0, sdLp2 = 0, sdEnv = 0, sdRef = 0, sdRefAcc = 0;

// --- Two ideas taken from Steppschuh/Micro-Beat-Detection ---------------------------------
// (github.com/Steppschuh/Micro-Beat-Detection, an AVR/FHT sketch). Its FHT front-end is well
// below what we already have and its magnitude curve is hand-fitted magic numbers, but two of
// its tests are scale-free -- they are ratios, so they need no per-track calibration, which is
// exactly what our threshold factor kept demanding.
//
// 1. A beat needs VARIANCE, not level. Our comparator fires while the envelope sits above the
//    threshold, so a held bass note keeps re-triggering; that is what put extra onsets between
//    the kicks and dragged the interval median down (427ms measured against a true 451ms).
//    Requiring the recent envelope history to be uneven admits transients, rejects sustain.
// 2. A SOFT refractory rather than a hard one. The sketch weights a candidate by how long it
//    has been since the last beat instead of excluding it outright. That matters here
//    specifically: a hard lockout is what turned this detector into a free-running oscillator
//    whose onset median was always the lockout plus ~40ms. A threshold that decays back to
//    normal cannot form a grid of its own.
// One window of recent envelope samples, one per block (~32ms), and three statistics from it.
//
// The threshold is placed IN THE DYNAMIC RANGE rather than as a multiple of an average:
//   threshold = floor + fraction * (peak - floor)
// which is the rule in gibbedy/BeatDetector. That makes the one remaining knob dimensionless --
// a position between the quiet level and the loud level, not a gain -- so it does not have to be
// recalibrated when the material changes level, which is what every previous tuning attempt here
// foundered on.
//
// The floor is the MEDIAN, not the mean: foo_bpm (stengerh/foo_bpm) picks peaks against a median
// of a sliding window, and the reason matters -- a mean is dragged upward by the very peaks it is
// supposed to be measuring against, a median is not. Working around that with an ever slower
// average is what produced the frozen-reference bug earlier.
#define SD_STAT_HIST 24            // ~24 * 32ms = 768ms of history
inline int32_t sdStatHist[SD_STAT_HIST];
inline int     sdStatIdx = 0;
inline bool    sdTransient = false;          // does the recent envelope look like an event?
inline int32_t sdVarMad = 0, sdVarMean = 0;  // exposed for the AUDIO tab
inline int32_t sdFloor = 0, sdPeakStat = 0;  // median and maximum of the window
inline int32_t sdThrBlock = 0, sdMinLevel = 0;
inline int     sdVarMinPct = 25;             // MAD must be at least this % of the mean
// Fire at the envelope's PEAK, not at the moment it crosses the threshold. This is condition 1
// of the peak-picker in Dixon, "Onset Detection Revisited" (DAFx-06), which requires an onset to
// be a local maximum of the detection function. It matters because a threshold crossing moves
// with the signal level -- louder passage, earlier crossing -- while the peak does not, so
// intervals measured peak-to-peak are far more repeatable than crossing-to-crossing. That
// repeatability is the entire problem here.
//
// Done causally: once above threshold, follow the maximum, and commit when the envelope has
// fallen back from it. The onset is timestamped with the PEAK's time, so nothing is added to the
// reported onset time -- only to how late we learn of it (bounded by sdPeakMaxWaitMs).
// The same paper measured its condition 3 (a decaying threshold, which is what sdBoost does) as
// only a marginal improvement, so sdBoost stays modest rather than being asked to do the work.
inline bool     sdPeaking       = false;
inline int32_t  sdPeakVal       = 0;
inline uint32_t sdPeakClock     = 0;
inline int      sdPeakFallPct   = 70;   // commit once the envelope drops to this % of the peak
inline int      sdPeakMaxWaitMs = 60;   // ...or after this long, whichever comes first

inline int32_t sdBoost = 256;                // Q8 threshold multiplier, 256 = 1.0
inline int     sdBoostMaxQ8 = 1024;          // 4.0x immediately after a beat
inline int     sdBoostShift = 11;            // decays back, ~128ms time constant

// Rescale every level-domain filter state when the input gain shift changes, so a gain step
// causes no transient at all. Without this the envelope, its reference and the whole window
// history would jump by a factor of two and the detector would fire on its own gain change --
// or go deaf for a window -- every time the level was adjusted.
inline void sdScaleState(int d) {
  if (d == 0) return;
  #define SD_SC(x) ((x) = (d > 0) ? ((x) << d) : ((x) >> (-d)))
  SD_SC(sdLp1); SD_SC(sdLp2); SD_SC(sdEnv); SD_SC(sdRef); SD_SC(sdRefAcc);
  SD_SC(sdPeakVal); SD_SC(sdFloor); SD_SC(sdPeakStat); SD_SC(sdThrBlock); SD_SC(sdMinLevel);
  for (int i = 0; i < SD_STAT_HIST; i++) SD_SC(sdStatHist[i]);
  #undef SD_SC
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

inline void updateAutoGain(uint32_t now) {
  // Track the recent peak whether or not auto is on, so the UI always has something honest.
  if (micPeak > agPeakWin) agPeakWin = micPeak;
  else agPeakWin -= agPeakWin >> 6;      // ~64 frames at 31fps, about 2s
  if (!autoGain) { agHighSince = agLowSince = 0; return; }
  if ((uint32_t)(now - agLastChange) < (uint32_t)agHoldMs) return;   // let the window resettle

  const int32_t full = 32767;
  int32_t hi = (full * agHighPct) / 100, lo = (full * agLowPct) / 100;
  int delta = 0;

  if (agPeakWin > hi) {
    if (agHighSince == 0) agHighSince = now;
    else if ((uint32_t)(now - agHighSince) > (uint32_t)agDownDelayMs) {
      int32_t target = (full * agTargetPct) / 100, p = agPeakWin;
      while (p > target && delta > -5) { p >>= 1; delta--; }   // straight to the right range
    }
    agLowSince = 0;
  } else if (agPeakWin < lo && agPeakWin > (full / 32)) {
    // The level floor matters: in silence the peak is near zero and without it the gain would
    // wind all the way up, then clip the moment the music starts.
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

// Called once per block. Mean absolute deviation rather than a true variance: same decision,
// no squaring, and it matches how the frame-based threshold already measures spread.
inline void sdUpdateStats() {
  sdStatHist[sdStatIdx] = sdEnv;
  sdStatIdx = (sdStatIdx + 1) % SD_STAT_HIST;

  int32_t v[SD_STAT_HIST], sum = 0;
  for (int i = 0; i < SD_STAT_HIST; i++) { v[i] = sdStatHist[i]; sum += v[i]; }
  sdVarMean = sum / SD_STAT_HIST;
  for (int i = 1; i < SD_STAT_HIST; i++) {          // insertion sort; 24 values once per block
    int32_t k = v[i]; int j = i - 1;
    while (j >= 0 && v[j] > k) { v[j+1] = v[j]; j--; }
    v[j+1] = k;
  }
  sdFloor    = v[SD_STAT_HIST / 2];
  sdPeakStat = v[SD_STAT_HIST - 1];

  // Mean absolute deviation rather than a true variance: same decision, no squaring, and it
  // matches how the frame-based threshold already measures spread.
  int32_t mad = 0;
  for (int i = 0; i < SD_STAT_HIST; i++) {
    int32_t d = sdStatHist[i] - sdVarMean;
    mad += d < 0 ? -d : d;
  }
  sdVarMad = mad / SD_STAT_HIST;
  sdTransient = (sdVarMean > 0) && ((sdVarMad * 100) >= (sdVarMean * sdVarMinPct));

  // Sensitivity now positions the threshold inside the range: 100 puts it 30% of the way from
  // the floor to the peak, 0 puts it at 90%.
  int32_t fracQ8 = 230 - ((int32_t)hwAudioSensitivity * 153) / 100;
  int32_t range  = sdPeakStat - sdFloor;
  if (range < 0) range = 0;
  sdThrBlock = sdFloor + ((range * fracQ8) >> 8) + 8;
  // A beat also has to be well clear of the typical level, so that a passage with no beat in it
  // does not have its own noise picked apart -- the "2 x average" test in gibbedy/BeatDetector.
  sdMinLevel = sdFloor * 2;
}
inline uint32_t sdSampleClock = 0;
// Does the sample clock keep up with real time? sdSampleClock only advances for audio we actually
// processed, so any sample the DMA ring drops makes it run slow -- and since beat intervals are
// measured on it, a slow clock reports every tempo too high. This is the number that says whether
// the collection path is losing audio: positive means the sample clock is behind the wall clock,
// in parts per thousand. It should sit at essentially zero.
inline uint32_t sdClkStartWall = 0, sdClkStartSample = 0;
inline int      sdClkDriftPpt = 0;
inline uint32_t sdLastOnsetMs = 0;
inline bool     sdArmed = true;
inline int32_t  sdLastEnv = 0, sdLastThr = 0;   // for the AUDIO tab
inline int      sdOnsetsThisFrame = 0;

// Returns the timestamp of the last onset found in this block, or 0 if there was none.
// `gainShift` matches the FFT path so both see the same signal level.
inline uint32_t sdProcessBlock(const int32_t* raw, int count, int gainShift) {
  uint32_t fired = 0;
  sdOnsetsThisFrame = 0;
  // Sensitivity sets the comparator's margin over the tracked reference: 1.0x at 100, 3.0x at 0.
  const uint32_t peakMaxWaitSamples = (uint32_t)sdPeakMaxWaitMs * (SAMPLING_FREQUENCY / 1000);
  for (int i = 0; i < count; i++) {
    int32_t x = (raw[i] >> SAMPLE_DOWNSCALE_SHIFT_FFT) << gainShift;
    if (x > 32767) x = 32767; else if (x < -32768) x = -32768;

    // Bandpass as the difference of two one-pole lowpasses: cheap, stable, and the two shifts
    // map directly onto the two edges the DJM's filter array sets in hardware.
    sdLp1 += (x - sdLp1) >> sdKHi;
    sdLp2 += (x - sdLp2) >> sdKLo;
    int32_t bp = sdLp1 - sdLp2;
    int32_t mag = bp < 0 ? -bp : bp;

    if (mag > sdEnv) sdEnv += (mag - sdEnv) >> sdAtt;
    else             sdEnv -= (sdEnv - mag) >> sdRel;

    // Kept only as the re-arm level and for display; the decision threshold is sdThrBlock,
    // computed once per block from the window statistics above.
    sdRefAcc += (((int32_t)sdEnv << 8) - sdRefAcc) >> sdRefShift;
    sdRef = sdRefAcc >> 8;

    // Soft refractory: the threshold is lifted 4x by a beat and decays back over ~130ms, so a
    // candidate arriving early is made harder rather than impossible. Shift only, no divide.
    if (sdBoost > 256) sdBoost -= (sdBoost - 256) >> sdBoostShift;
    int32_t thr = (sdThrBlock * sdBoost) >> 8;

    // sdTransient is the variance gate: level alone is not a beat, the envelope has to be moving.
    if (sdArmed && sdTransient && sdEnv > thr && sdEnv > sdMinLevel && !sdPeaking) {
      sdPeaking   = true;
      sdPeakVal   = sdEnv;
      sdPeakClock = sdSampleClock;
    }

    if (sdPeaking) {
      if (sdEnv > sdPeakVal) {              // still rising: this is the peak so far
        sdPeakVal   = sdEnv;
        sdPeakClock = sdSampleClock;
      } else if (sdEnv * 100 < sdPeakVal * sdPeakFallPct ||
                 (uint32_t)(sdSampleClock - sdPeakClock) > peakMaxWaitSamples) {
        // Fallen back from the peak, or waited long enough: commit it, timestamped at the peak.
        uint32_t tms = (uint32_t)(((uint64_t)sdPeakClock * 1000ULL) / SAMPLING_FREQUENCY);
        if ((uint32_t)(tms - sdLastOnsetMs) > (uint32_t)sdLockoutMs) {
          sdLastOnsetMs = tms;
          sdBoost = sdBoostMaxQ8;
          fired = tms;
          sdOnsetsThisFrame++;
        }
        sdPeaking = false;
        sdArmed   = false;
      }
    }
    // Re-arm only once the envelope has fallen back to the reference, so one kick produces one
    // edge rather than a burst while it decays.
    if (!sdPeaking && sdEnv < sdFloor) sdArmed = true;

    sdSampleClock++;
    sdLastEnv = sdEnv; sdLastThr = thr;
  }
  return fired;
}

// =========================================================
// --- TEMPO TRACKER (autocorrelation over the flux history) ---
// =========================================================
// Why this exists: taking the median of the gaps between detected onsets cannot recover the
// tempo of syncopated material. Measured live 2026-08-28 on a 176 BPM drum & bass track, the
// onset gaps clustered at 1.17-1.38x the beat with only ~80ms spread -- a wrong grid, not
// noise -- because the kicks simply do not sit on the quarter pulse. Autocorrelating the flux
// signal instead asks "which period best explains ALL the onsets", which is a different and
// answerable question.
#define TEMPO_RING      192   // ~6.1s of flux at one frame per 32ms
#define TEMPO_LAG_MIN     9   // ~208 BPM
#define TEMPO_LAG_MAX    40   // ~47 BPM
#define TEMPO_EVAL_MS  1000   // re-evaluate once a second; the ring changes slowly
#define TEMPO_P_MIN   300     // 200 BPM
#define TEMPO_P_MAX  1000     //  60 BPM
#define TEMPO_P_STEP    4
#define TEMPO_CAND_N ((TEMPO_P_MAX - TEMPO_P_MIN) / TEMPO_P_STEP + 1)

inline int16_t tempoRing[TEMPO_RING];
inline int tempoRingIdx = 0;
inline int32_t tempoFluxMean = 0;
inline unsigned long tempoLastEval = 0;
// Smoothed score curves rather than a smoothed decision. Each evaluation is noisy enough on
// its own that picking a winner per second made the output jump between 179, 89 and 72 on
// steady material (measured live 2026-08-28). Averaging the per-lag scores over successive
// evaluations accumulates evidence for ~10s and then takes ONE peak, which is both more
// stable and cheaper than any post-hoc median of the winners.
inline int32_t tempoHarmAvg[TEMPO_LAG_MAX + 2];
inline int32_t tempoPlainAvg[TEMPO_LAG_MAX * 2 + 4];
inline bool tempoAvgSeeded = false;
// Onset TIMES, not intervals: the estimator below asks how well all onsets line up in phase
// for a candidate period, which needs their absolute positions.
#define IOI_RING 64

// How far back the median looks. Long enough to hold several beats so one bad gap cannot
// decide the answer, short enough that a tempo change shows up rather than being averaged away.
inline int tempoWindowMs = 3000;
inline uint32_t onsetRing[IOI_RING];
// How far above its threshold each onset was, 16 == exactly at threshold. Weighting the phase
// test by this is what removes the need to tune sensitivity per track: a weak off-beat hit still
// counts, but it can no longer outvote the kicks. Measured on techno, the detector fired 2.67
// times a second against a beat rate of 2.37, and that surplus formed a competing ~170 BPM grid
// that the unweighted test kept locking onto.
inline uint16_t onsetW[IOI_RING];
inline int ioiIdx = 0, ioiCount = 0;
inline void pushOnset(unsigned long ms, int32_t energy, int32_t threshold) {
  onsetRing[ioiIdx] = (uint32_t)ms;
  int32_t w = (threshold > 0) ? (energy * 16) / threshold : 16;
  if (w < 1) w = 1; else if (w > 255) w = 255;
  onsetW[ioiIdx] = (uint16_t)w;
  ioiIdx = (ioiIdx + 1) % IOI_RING;
  if (ioiCount < IOI_RING) ioiCount++;
}

// Quarter-wave sine in Q7, 256 steps. Integer only: 200+ candidate periods times 60 onsets is far
// too many sinf() calls for a chip without an FPU.
inline int8_t tempoSinTab[256];
inline bool tempoSinReady = false;
inline void tempoInitSin() {
  for (int i = 0; i < 256; i++) tempoSinTab[i] = (int8_t)(sinf(2.0f * PI * i / 256.0f) * 127.0f);
  tempoSinReady = true;
}

inline int trackedBPM = 0;          // 0 = no confident estimate yet
inline int32_t trackedScore = 0;    // winning harmonic score, for the AUDIO tab
// Octave-decision telemetry: which lag won and what the plain scores of the competing
// octaves were, so the choice can be inspected instead of inferred.
inline int32_t dbgLagMilli = 0, dbgPlainBase = 0, dbgPlainHalf = 0, dbgPlainDouble = 0;
inline int tempoCandidate = 0, tempoAgree = 0;
// A tapped tempo outranks the tracker. Until this was added the tracker reassigned globalBPM on
// every audio frame (~31 times a second), so a tapped value survived about 32ms and tapping was
// simply ineffective whenever the mic was on -- which reads as "tapping is jumpy" rather than as
// the override it actually was. Tapping now holds the tempo and the audio keeps supplying phase
// through the beat-clock correction, which is how a lighting desk is normally driven.
inline bool tempoTapLock = false;
inline bool audioUseTracker = true;
// Manual octave override for the tracker's result: 0 = as measured, 1 = double, 2 = halve.
// Deliberately a user decision, not a heuristic. The tracker reports the pulse the bass
// actually has, and for drum & bass that is genuinely the half-time grid -- measured on a
// 176 BPM track, the autocorrelation at 341ms was NEGATIVE while 671ms scored ~1400. Any
// automatic rule that turned 89 into 178 would equally turn 90 BPM hip-hop into 180, which
// is the wrong guess in the other direction. So the octave is offered, not inferred.
inline int tempoMulMode = 0;

inline int64_t tempoAutocorr(int lag) {
  if (lag < 2 || lag > TEMPO_RING / 3) return 0;
  int64_t sum = 0;
  int n = TEMPO_RING - lag;
  for (int i = 0; i < n; i++) {
    int a = (tempoRingIdx + i) % TEMPO_RING;
    int b = (tempoRingIdx + i + lag) % TEMPO_RING;
    sum += (int32_t)tempoRing[a] * (int32_t)tempoRing[b];
  }
  return sum / n;
}

inline void tempoTrackerEval() {
  // Tempo from the ONSET TRAIN, by asking for each candidate period how tightly the onsets cluster
  // in phase. This is a Fourier component evaluated over onset times rather than over audio, and
  // it is phase-invariant, so it needs no assumption about where the downbeat sits.
  //
  // Two earlier attempts here failed, and both failures were informative:
  //   * autocorrelation with harmonic summing locked onto the dotted quarter -- it read 98 for a
  //     track tapped at 143, and every second multiple of a 1.5x period does line up, so the
  //     relative collects genuine support;
  //   * sweeping periods and scoring integer multiples of the intervals collapsed onto the
  //     shortest period in the range, because a short period explains nearly any interval as some
  //     multiple; the winning and runner-up scores came out 602 against 601.
  //   * taking the median interval failed too: the detector also fires between beats, so the most
  //     common gap (330ms) simply is not the beat (420ms).
  // What the measurements did show is that the onset RATE is already right -- 2.39 per second
  // against 2.38 expected -- so the information is in how the onsets distribute, which is exactly
  // what a phase test reads.
  if (ioiCount < 10) return;
  if (!tempoSinReady) tempoInitSin();

  uint32_t newest = onsetRing[(ioiIdx + IOI_RING - 1) % IOI_RING];
  int32_t bestMag = 0; int bestP = 0;
  int32_t magOf[TEMPO_CAND_N];

  for (int ci = 0; ci < TEMPO_CAND_N; ci++) {
    int P = TEMPO_P_MIN + ci * TEMPO_P_STEP;
    int32_t re = 0, im = 0, wsum = 0, used = 0;
    for (int i = 0; i < ioiCount; i++) {
      uint32_t t = onsetRing[i];
      uint32_t age = newest - t;
      if (age > (uint32_t)tempoWindowMs) continue;
      uint32_t ph = ((age % (uint32_t)P) * 256u) / (uint32_t)P;
      int32_t w = (int32_t)onsetW[i];
      re += (int32_t)tempoSinTab[(ph + 64) & 255] * w;
      im += (int32_t)tempoSinTab[ph & 255] * w;
      wsum += w;
      used++;
    }
    if (used < 8 || wsum <= 0) { magOf[ci] = 0; continue; }
    // Normalised by the total weight, so neither a busier window nor a louder passage can
    // outscore a quieter one -- only how well the onsets line up matters.
    int32_t mag = (re / wsum) * (re / wsum) + (im / wsum) * (im / wsum);
    magOf[ci] = mag;
    if (mag > bestMag) { bestMag = mag; bestP = P; }
  }
  if (bestP == 0) return;

  // An onset train at period P also produces peaks at P/2, P/3 ... (the harmonics of 1/P), so the
  // strongest peak can be a subdivision. Fold up to the longest period that still holds most of
  // the strength -- that is the beat rather than its subdivisions.
  for (int m = 2; m <= 3; m++) {
    int cand = bestP * m;
    if (cand > TEMPO_P_MAX) break;
    int ci = (cand - TEMPO_P_MIN) / TEMPO_P_STEP;
    if (ci < 0 || ci >= TEMPO_CAND_N) continue;
    if (magOf[ci] * 100 >= bestMag * 75) { bestP = cand; bestMag = magOf[ci]; }
  }

  int bpm = 60000 / bestP;
  if (bpm < BPM_MIN_LIMIT || bpm > BPM_MAX_LIMIT) return;

  // Hysteresis. A tempo that flips changes the beat length underneath every synced effect and
  // makes the phase jump, so switching has to be deliberately hard. Measured live 2026-09-01 on a
  // track the user confirmed as 147 BPM: the correct value was already the most common reading by
  // far, but the tracker still spent a third of the time at 192-197 (147 x 4/3). Two conditions
  // now have to hold before the reported tempo moves:
  //
  //   1. the challenger has to win several evaluations in a row, not just two, and
  //   2. it has to be clearly better than the tempo currently held -- the incumbent's own score is
  //      looked up and the challenger must beat it by a margin.
  //
  // Without the second test a marginally noisier window was enough to switch, which is exactly
  // how a value that is right most of the time still ends up unusable.
  if (trackedBPM > 0 && (bpm > trackedBPM + 2 || bpm < trackedBPM - 2)) {
    int curP = 60000 / trackedBPM;
    int ci = (curP - TEMPO_P_MIN) / TEMPO_P_STEP;
    if (ci >= 0 && ci < TEMPO_CAND_N) {
      int32_t curMag = magOf[ci];
      // Not clearly better than what we already have: keep the incumbent.
      if (curMag > 0 && (int64_t)bestMag * 100 < (int64_t)curMag * 130) { tempoAgree = 0; return; }
    }
    if (tempoCandidate > bpm + 2 || tempoCandidate < bpm - 2) { tempoCandidate = bpm; tempoAgree = 1; return; }
    if (++tempoAgree < 4) return;
  }
  tempoCandidate = bpm;
  tempoAgree = 0;
  trackedBPM = bpm;
  trackedScore = bestMag;
  dbgLagMilli = bestP;
  dbgPlainBase = bestMag;
  dbgPlainHalf = ioiCount;
}

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
  for (int i = 1; i < n; i++) {
    uint32_t k = v[i]; int j = i - 1;
    while (j >= 0 && v[j] > k) { v[j+1] = v[j]; j--; }
    v[j+1] = k;
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
}

inline void tempoTrackerPush(int32_t flux, unsigned long now) {
  (void)flux;
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
inline bool audioUseFFT = true;
// Spectral-flux onset detection instead of absolute level (/audio_tune?flux=0 to compare).
// Default on: with sustained-bass material (drum & bass) the level detector measurably
// cannot find the pulse -- see fftFlux() for the numbers.
inline bool audioUseFlux = true;   // legacy global (?flux=) -- sets all three bands at once
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
    int count = SAMPLES;

    if (audioUseFFT) {
      // --- Real frequency separation ---------------------------------------------------
      // The three envelope followers in the else-branch below never separated by FREQUENCY at
      // all: they separated by how fast the overall level rose, so a hi-hat, a snare and a click
      // were indistinguishable and "bass" was just the smoothed total level. That is why a
      // dedicated High trigger (e.g. strobe on hi-hats) could not work before.
      uint32_t t0 = micros();
      int32_t peak = 0; int clips = 0;
      for (int i = 0; i < FFT_N; i++) {
        int32_t s = (raw_samples[i] >> SAMPLE_DOWNSCALE_SHIFT_FFT) << tuneInputGainShift;
        // Measure the level BEFORE clamping. Taking it afterwards pins the meter to full scale
        // as soon as one sample in the frame saturates, and it then cannot move again however
        // far the gain is turned down -- so it shows neither the real level nor any headroom.
        // Letting peak run past 32767 is the point: that overshoot is what says "turn it down".
        int32_t a = s < 0 ? -s : s;
        if (a > peak) peak = a;
        if (a >= 32000) clips++;              // count each saturated sample once
        if (s > 32767) s = 32767; else if (s < -32768) s = -32768;
        fftRe[i] = (int16_t)(((int32_t)s * fftWindow[i]) >> 15);
        fftIm[i] = 0;
      }
      micPeak = peak; micClipCount = clips;
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
      sdUpdateStats();   // block-level: window median, peak, spread -> this block's threshold
      sdOnsetMs = sdEnabled ? sdProcessBlock(raw_samples, FFT_N, tuneInputGainShift) : 0;
      fftRun();
      fftLastUs = micros() - t0;

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
      tempoTrackerPush(lastBassFlux, now);

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
      // --- Legacy envelope-follower path (/audio_tune?fft=0) ---------------------------
      for (int i = 0; i < count; i++) {
        int32_t s = std::abs(raw_samples[i] >> SAMPLE_DOWNSCALE_SHIFT);

        if (s > envFast) envFast += (s - envFast) >> tuneFastAttackShift;
        else envFast -= (envFast - s) >> tuneFastDecayShift;

        if (s > envMid)  envMid  += (s - envMid)  >> tuneMidAttackShift;
        else envMid  -= (envMid - s)  >> tuneMidDecayShift;

        if (s > envSlow) envSlow += (s - envSlow) >> tuneSlowAttackShift;
        else envSlow -= (envSlow - s) >> tuneSlowDecayShift;
      }
      fftLastUs = 0;
    }

    // Bands are now independent, so mid/high are read directly instead of being derived as
    // differences between envelope speeds (that construction is what once made midEnergy
    // structurally ~0 -- see the tuneMidAttackShift comment above).
    int32_t bassEnergy = envSlow, midEnergy = envMid, highEnergy = envFast;
    if (!audioUseFFT) {
      // Legacy broadband path: the three envelopes are all fed from the same wideband sample
      // magnitude, so mid and high only mean anything as differences between them.
      midEnergy  = std::max((int32_t)0, (int32_t)(envMid - envSlow));
      highEnergy = std::max((int32_t)0, (int32_t)(envFast - envMid));
    }

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
    bool beatDetected;
    if (sdEnabled && audioUseFFT) beatDetected = (sdOnsetMs != 0);
    else beatDetected = (bassEnergy > thBass && (now - lastBassTime) > MIN_BEAT_INTERVAL_MS);

    if (beatDetected) {
      unsigned long diff = now - lastBassTime;
      // Precise timestamp where it matters: the tempo estimator works on onset positions, so it
      // gets the sample-clock time rather than the frame's millis(). Everything else stays on
      // millis() so the beat clock is not fed from two different timebases.
      uint32_t onsetAt = sdEnabled && sdOnsetMs ? sdOnsetMs : (uint32_t)now;
      pushOnset(onsetAt, bassEnergy, thBass);
      tempoPushOnsetTime(onsetAt, (uint32_t)now);
      lastBassTime = now;
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
        unsigned long interval = (globalBPM > 0) ? (unsigned long)(MS_PER_MINUTE / globalBPM) : 500UL;
        long sinceLast = (long)(now - lastBeatTime);
        long err = sinceLast - (long)interval;

        if (sinceLast > (long)(interval * 7 / 4)) {
          // Grid lost -- playback just started, or beats were missed for a while. Re-establish it
          // outright rather than crawling back a quarter of a huge error at a time.
          lastBeatTime = now;
          beatCount++;
          masterSyncTime = now;
        } else if (labs(err) <= (long)(interval * 3 / 10)) {
          // Close to the prediction: advance exactly one beat and take a quarter of the error.
          long adj = (long)interval + err / 4;
          if (adj < 0) adj = 0;
          unsigned long newLast = lastBeatTime + (unsigned long)adj;
          // Never move the clock into the future: (now - lastBeatTime) is unsigned in the .ino,
          // so a lastBeatTime past `now` would underflow to a huge value and fire the internal
          // metronome immediately, every loop.
          if (newLast > now) newLast = now;
          lastBeatTime = newLast;
          beatCount++;
          masterSyncTime = lastBeatTime;
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
        globalBPM = BPM_DEFAULT_FALLBACK;
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
    int32_t thMid, thHigh;
    if (audioUseFFT) {
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
    } else {
      thMid  = thBass >> tuneMidThreshDivShift;
      thHigh = thBass >> tuneHighThreshDivShift;
    }
    lastThMid = thMid; lastThHigh = thHigh;

    if (midEnergy > thMid && (now - lastMidTime) > MID_DEBOUNCE_MS) {
        lastMidTime = now; triggerMid = true; guiMid = true; dbgMidHit = true;
    }
    if (highEnergy > thHigh && (now - lastHighTime) > HIGH_DEBOUNCE_MS) {
        lastHighTime = now; triggerHigh = true; guiHigh = true; dbgHighHit = true;
    }
  }

  // Held per 5s window so a single outlier cannot hide behind an average.
  // Applied outside the beat-detected block on purpose: the tracker does not need an onset
  // to have just fired, it works off the rolling flux history.
  if (audioUseTracker && trackedBPM > 0 && hwAudioEnabled && !tempoTapLock) {
    int shown = trackedBPM;
    // The override is ignored rather than clamped when it would leave the valid range --
    // clamping would silently show a tempo that is neither the measurement nor its octave.
    if (tempoMulMode == 1 && trackedBPM * 2 <= BPM_MAX_LIMIT) shown = trackedBPM * 2;
    else if (tempoMulMode == 2 && trackedBPM / 2 >= BPM_MIN_LIMIT) shown = trackedBPM / 2;
    globalBPM = constrain(shown, BPM_MIN_LIMIT, BPM_MAX_LIMIT);
  }
  audioLastUs = micros() - pollT0;
  if (audioLastUs > audioMaxUs) audioMaxUs = audioLastUs;
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
