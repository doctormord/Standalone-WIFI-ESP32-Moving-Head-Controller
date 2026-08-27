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

// 8kHz stays: it puts the Nyquist limit at 4kHz, which still carries plenty of hi-hat/cymbal
// energy for the High trigger. Dropping to ~2kHz (as considered once) would resolve bass a little
// better but would delete the High band entirely, and these I2S mics are already being clocked
// below their usual spec range at 8kHz.
#define SAMPLING_FREQUENCY 8000
// One FFT frame. 256 @ 8kHz = 32ms of audio and 31.25Hz per bin -- fine enough to separate kick
// from snare from hi-hat, short enough that a frame still fits inside one poll interval.
#define FFT_N 256
#define FFT_LOG2N 8
#define SAMPLES FFT_N
// Poll cadence deliberately equals the frame duration (256/8000 = 32ms), so samples are consumed
// at exactly the rate the I2S peripheral produces them. At the old 40ms the DMA ring would slowly
// fill and then drop blocks, which shows up as sporadically missed beats rather than as an error.
#define AUDIO_POLL_INTERVAL_MS 32
// 4 x 256 samples = 1024 samples (~128ms) of slack, so an occasional long loop iteration cannot
// cost us a frame. The old 2 x 128 held exactly 256 samples -- one frame, with nothing to spare.
#define DMA_BUF_COUNT 4
#define DMA_BUF_LEN 256
// Zero, deliberately: i2s_read must NEVER block the main loop. A blocking read here would stall
// DMX output and movement -- the exact failure mode an earlier FFT attempt produced. If a full
// frame is not buffered yet we simply skip this poll and pick it up 32ms later; the DMA ring
// holds ~128ms, so nothing is lost.
#define I2S_READ_TIMEOUT_MS 0
#define BYTES_PER_SAMPLE_32BIT 4

// Band edges as FFT bin indices (bin width = 8000/256 = 31.25Hz). Bin 0 (DC) is always skipped.
#define BIN_BASS_LO 1     //   31 Hz
#define BIN_BASS_HI 5     //  156 Hz  -- kick drum fundamental
#define BIN_MID_LO  6     //  187 Hz
#define BIN_MID_HI  38    // 1187 Hz  -- snare body, vocals, most instruments
#define BIN_HIGH_LO 80    // 2500 Hz
#define BIN_HIGH_HI 127   // 3968 Hz  -- hi-hat / cymbal

// =========================================================
// --- AUDIO PROCESSING & ENVELOPES ---
// =========================================================
#define SAMPLE_DOWNSCALE_SHIFT 14
// The FFT needs its input to fit int16 exactly, not just to be "small". These mics deliver 24-bit
// data left-aligned in a 32-bit word, so full scale is 2^31; >>16 maps that onto +/-32768, using
// the whole int16 range without ever clipping. The legacy path's >>14 would overflow int16 on loud
// passages -- harmless there because it only ever takes abs() of it, fatal for an FFT.
#define SAMPLE_DOWNSCALE_SHIFT_FFT 16

// Attack/Decay speeds (bit-shifts: 1=/2, 2=/4, 3=/8, 4=/16), the mid/high threshold divisors, and
// the noise floor together are this project's "fake FFT" -- three leaky-integrator envelope
// followers at different attack/decay speeds standing in for a real frequency-domain split (see
// bassEnergy/midEnergy/highEnergy below). Runtime-tunable (not #define) so the new AUDIO DEBUG tab
// can adjust them live via /audio_tune without a reflash -- added 2026-08-20 after live movement
// beat-sync debugging showed there was no way to see or tune this pipeline except by guessing.
// Defaults match the previous #define values exactly.
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
#define BPM_MAX_LIMIT 180

#define BPM_DEVIATION_TOLERANCE_DIVISOR 5 
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

  static unsigned long lastAudioPoll = 0;
  unsigned long now = millis();
  
  if (now - lastAudioPoll < AUDIO_POLL_INTERVAL_MS) return; 
  lastAudioPoll = now;
  uint32_t pollT0 = micros();

  size_t bytes_read = 0;
  if (i2s_read(I2S_PORT, raw_samples, sizeof(int32_t) * SAMPLES, &bytes_read, pdMS_TO_TICKS(I2S_READ_TIMEOUT_MS)) == ESP_OK && bytes_read > 0) {
    int count = bytes_read / BYTES_PER_SAMPLE_32BIT;
    // A partial frame is skipped outright rather than fed through the legacy maths, which would
      // emit a wrongly-scaled energy for that frame and show up as a spurious trigger.
    if (audioUseFFT && count < FFT_N) return;

    if (audioUseFFT) {
      // --- Real frequency separation ---------------------------------------------------
      // The three envelope followers in the else-branch below never separated by FREQUENCY at
      // all: they separated by how fast the overall level rose, so a hi-hat, a snare and a click
      // were indistinguishable and "bass" was just the smoothed total level. That is why a
      // dedicated High trigger (e.g. strobe on hi-hats) could not work before.
      uint32_t t0 = micros();
      for (int i = 0; i < FFT_N; i++) {
        int32_t s = raw_samples[i] >> SAMPLE_DOWNSCALE_SHIFT_FFT;
        if (s > 32767) s = 32767; else if (s < -32768) s = -32768;   // clip, never wrap
        fftRe[i] = (int16_t)(((int32_t)s * fftWindow[i]) >> 15);
        fftIm[i] = 0;
      }
      fftRun();
      fftLastUs = micros() - t0;

      // Per-band averages, then the same attack/decay smoothing the tuning UI already exposes --
      // a single 32ms frame is too noisy to threshold directly. The tune* names keep their
      // meaning: fast = High band, mid = Mid band, slow = Bass band.
      // Clamped before shifting: an unclamped average (max ~32767) shifted by a large gain would
      // overflow int32 and wrap to a negative energy -- which reads as "silence" and would look
      // like the detector randomly dying on loud passages.
      const int32_t BAND_MAX = 200000;
      int32_t bRaw = std::min(fftBand(BIN_BASS_LO, BIN_BASS_HI), BAND_MAX) << tuneFftGainShift;
      int32_t mRaw = std::min(fftBand(BIN_MID_LO,  BIN_MID_HI),  BAND_MAX) << tuneFftGainShift;
      int32_t hRaw = std::min(fftBand(BIN_HIGH_LO, BIN_HIGH_HI), BAND_MAX) << tuneFftGainShift;

      if (bRaw > envSlow) envSlow += (bRaw - envSlow) >> tuneSlowAttackShift;
      else envSlow -= (envSlow - bRaw) >> tuneSlowDecayShift;
      if (mRaw > envMid)  envMid  += (mRaw - envMid)  >> tuneMidAttackShift;
      else envMid  -= (envMid - mRaw)  >> tuneMidDecayShift;
      if (hRaw > envFast) envFast += (hRaw - envFast) >> tuneFastAttackShift;
      else envFast -= (envFast - hRaw) >> tuneFastDecayShift;
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
    int32_t bassEnergy, midEnergy, highEnergy;
    if (audioUseFFT) {
      bassEnergy = envSlow;
      midEnergy  = envMid;
      highEnergy = envFast;
    } else {
      bassEnergy = envSlow;
      midEnergy  = std::max((int32_t)0, (int32_t)(envMid - envSlow));
      highEnergy = std::max((int32_t)0, (int32_t)(envFast - envMid));
    }

    dynThreshold += (envSlow - dynThreshold) >> tuneDynThreshSmoothShift;
    float sens = 2.0f - (hwAudioSensitivity * 0.01f);
    int32_t thBass = (dynThreshold * sens) + tuneNoiseFloor;
    lastBassEnergy = bassEnergy; lastMidEnergy = midEnergy; lastHighEnergy = highEnergy; lastThBass = thBass;

    bool beatDetected = (bassEnergy > thBass && (now - lastBassTime) > MIN_BEAT_INTERVAL_MS);

    if (beatDetected) {
      unsigned long diff = now - lastBassTime;
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

        bool sampleWritten = false;
        if (bestError < (currentInterval / BPM_DEVIATION_TOLERANCE_DIVISOR) || globalBPM == BPM_DEFAULT_FALLBACK) {
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

            globalBPM = ((globalBPM * BPM_SMOOTHING_WEIGHT_OLD) + detectedBPM) / BPM_SMOOTHING_WEIGHT_TOTAL;
            globalBPM = constrain(globalBPM, BPM_MIN_LIMIT, BPM_MAX_LIMIT);
          }
        }
      }
      lastBeatTime = now;
      // Pair every real-beat lastBeatTime reset with a beatCount increment (see .ino's internal
      // metronome, which does the same for virtual ticks). Without this, a detected beat resets
      // lastBeatTime but not beatCount, and since audio detection keeps winning the race against
      // the metronome's own tick, beatCount nearly freezes while beatsElapsedTotal's fractional
      // part keeps snapping back toward 0 every beat -- multi-beat sync stayed stuck cycling
      // within a single beat and visibly jerked backward on every detection ("juggling").
      beatCount++;
      masterSyncTime = now;
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
      thMid  = (int32_t)(dynThresholdMid  * sens) + tuneNoiseFloor;
      thHigh = (int32_t)(dynThresholdHigh * sens) + tuneNoiseFloor;
      // The existing divisor tunables stay usable as a per-band sensitivity trim.
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
