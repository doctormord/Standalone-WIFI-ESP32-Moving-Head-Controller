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

#define SAMPLING_FREQUENCY 8000
#define SAMPLES 64
#define DMA_BUF_COUNT 2
#define DMA_BUF_LEN 128
#define I2S_READ_TIMEOUT_MS 5
#define AUDIO_POLL_INTERVAL_MS 40
#define BYTES_PER_SAMPLE_32BIT 4

// =========================================================
// --- AUDIO PROCESSING & ENVELOPES ---
// =========================================================
#define SAMPLE_DOWNSCALE_SHIFT 14

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

// Latest band energies + threshold, held here (not just local to pollAudioEngine()) so the AUDIO
// DEBUG tab's /api/audio_debug poll can read the same numbers the beat detector just acted on.
inline int32_t lastBassEnergy = 0, lastMidEnergy = 0, lastHighEnergy = 0, lastThBass = 0;

void pollAudioEngine() {
  triggerBass = false;
  triggerMid  = false;
  triggerHigh = false;

  if (!hwAudioEnabled) return;

  static unsigned long lastAudioPoll = 0;
  unsigned long now = millis();
  
  if (now - lastAudioPoll < AUDIO_POLL_INTERVAL_MS) return; 
  lastAudioPoll = now;

  size_t bytes_read = 0;
  if (i2s_read(I2S_PORT, raw_samples, sizeof(int32_t) * SAMPLES, &bytes_read, pdMS_TO_TICKS(I2S_READ_TIMEOUT_MS)) == ESP_OK && bytes_read > 0) {
    int count = bytes_read / BYTES_PER_SAMPLE_32BIT;
    for (int i = 0; i < count; i++) {
      int32_t s = std::abs(raw_samples[i] >> SAMPLE_DOWNSCALE_SHIFT);
      
      if (s > envFast) envFast += (s - envFast) >> tuneFastAttackShift;
      else envFast -= (envFast - s) >> tuneFastDecayShift;

      if (s > envMid)  envMid  += (s - envMid)  >> tuneMidAttackShift;
      else envMid  -= (envMid - s)  >> tuneMidDecayShift;

      if (s > envSlow) envSlow += (s - envSlow) >> tuneSlowAttackShift;
      else envSlow -= (envSlow - s) >> tuneSlowDecayShift;
    }

    int32_t bassEnergy = envSlow;
    int32_t midEnergy  = std::max((int32_t)0, (int32_t)(envMid - envSlow));
    int32_t highEnergy = std::max((int32_t)0, (int32_t)(envFast - envMid));

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

    if (midEnergy > (thBass >> tuneMidThreshDivShift) && (now - lastMidTime) > MID_DEBOUNCE_MS) {
        lastMidTime = now; triggerMid = true; guiMid = true; dbgMidHit = true;
    }
    if (highEnergy > (thBass >> tuneHighThreshDivShift) && (now - lastHighTime) > HIGH_DEBOUNCE_MS) {
        lastHighTime = now; triggerHigh = true; guiHigh = true; dbgHighHit = true;
    }
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
}
