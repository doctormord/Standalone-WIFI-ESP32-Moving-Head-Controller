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
#define NOISE_FLOOR 100

// Attack/Decay Speeds (bit-shifts for division: 1= /2, 2= /4, 3= /8, 4= /16)
#define ENV_FAST_ATTACK_SHIFT 1
#define ENV_FAST_DECAY_SHIFT 2
#define ENV_MID_ATTACK_SHIFT 2
#define ENV_MID_DECAY_SHIFT 3
#define ENV_SLOW_ATTACK_SHIFT 2
#define ENV_SLOW_DECAY_SHIFT 4 
#define DYN_THRESH_SMOOTH_SHIFT 4

// =========================================================
// --- BEAT DETECTION THRESHOLDS ---
// =========================================================
#define MS_PER_MINUTE 60000
#define MIN_BEAT_INTERVAL_MS 280  
#define MAX_BEAT_INTERVAL_MS 1000 
#define SILENCE_TIMEOUT_MS 2500

#define MID_THRESH_DIV_SHIFT 1
#define MID_DEBOUNCE_MS 150
#define HIGH_THRESH_DIV_SHIFT 2
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

extern int globalBPM;
extern unsigned long lastBeatTime;
extern bool manualTap;
extern unsigned long masterSyncTime;

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
      
      if (s > envFast) envFast += (s - envFast) >> ENV_FAST_ATTACK_SHIFT; 
      else envFast -= (envFast - s) >> ENV_FAST_DECAY_SHIFT;
      
      if (s > envMid)  envMid  += (s - envMid)  >> ENV_MID_ATTACK_SHIFT; 
      else envMid  -= (envMid - s)  >> ENV_MID_DECAY_SHIFT;
      
      if (s > envSlow) envSlow += (s - envSlow) >> ENV_SLOW_ATTACK_SHIFT; 
      else envSlow -= (envSlow - s) >> ENV_SLOW_DECAY_SHIFT;
    }

    int32_t bassEnergy = envSlow;
    int32_t midEnergy  = std::max((int32_t)0, (int32_t)(envMid - envSlow));
    int32_t highEnergy = std::max((int32_t)0, (int32_t)(envFast - envMid));

    dynThreshold += (envSlow - dynThreshold) >> DYN_THRESH_SMOOTH_SHIFT;
    float sens = 2.0f - (hwAudioSensitivity * 0.01f);
    int32_t thBass = (dynThreshold * sens) + NOISE_FLOOR;

    bool beatDetected = (bassEnergy > thBass && (now - lastBassTime) > MIN_BEAT_INTERVAL_MS);

    if (beatDetected) {
      unsigned long diff = now - lastBassTime;
      lastBassTime = now;
      triggerBass = true;
      guiBass = true;

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
      masterSyncTime = now;
      manualTap = true;
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

    if (midEnergy > (thBass >> MID_THRESH_DIV_SHIFT) && (now - lastMidTime) > MID_DEBOUNCE_MS) { 
        lastMidTime = now; triggerMid = true; guiMid = true; 
    }
    if (highEnergy > (thBass >> HIGH_THRESH_DIV_SHIFT) && (now - lastHighTime) > HIGH_DEBOUNCE_MS) { 
        lastHighTime = now; triggerHigh = true; guiHigh = true; 
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
