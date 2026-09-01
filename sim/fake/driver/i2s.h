// A fake I2S driver whose ring fills at the real sample rate against the harness clock.
//
// This is deliberately not a stub that always hands over a full frame: the collection path is
// where a 6% audio loss was found (the poll cadence sat exactly on the production rate and the
// DMA ring backed up until the driver dropped samples), so the simulator has to be able to
// reproduce that. It models the ring depth, fills it from the generator on demand, and counts
// what it had to discard -- simI2sDropped is the number the fix has to keep at zero.
#pragma once
#include <cstdint>
#include <cstddef>
#include <cstring>

typedef int esp_err_t;
#define ESP_OK 0
#define ESP_INTR_FLAG_LEVEL1 (1 << 1)
#define I2S_PIN_NO_CHANGE (-1)

typedef enum { I2S_NUM_0 = 0 } i2s_port_t;
typedef enum { I2S_MODE_MASTER = 1, I2S_MODE_RX = 4 } i2s_mode_t;
typedef enum { I2S_BITS_PER_SAMPLE_32BIT = 32 } i2s_bits_per_sample_t;
typedef enum { I2S_CHANNEL_FMT_ONLY_LEFT = 3 } i2s_channel_fmt_t;
typedef enum { I2S_COMM_FORMAT_STAND_I2S = 1 } i2s_comm_format_t;

typedef struct {
  i2s_mode_t mode; int sample_rate; i2s_bits_per_sample_t bits_per_sample;
  i2s_channel_fmt_t channel_format; i2s_comm_format_t communication_format;
  int intr_alloc_flags; int dma_buf_count; int dma_buf_len;
  bool use_apll; bool tx_desc_auto_clear; int fixed_mclk;
} i2s_config_t;

typedef struct { int bck_io_num, ws_io_num, data_out_num, data_in_num; } i2s_pin_config_t;

#define pdMS_TO_TICKS(x) (x)

// Supplied by the harness: produces the next sample of the synthetic signal.
int32_t simNextSample();

extern uint64_t simMicros;
inline int      simRingCap   = 0;    // dma_buf_count * dma_buf_len, as configured
inline int      simRingFill  = 0;
inline uint64_t simLastFillUs = 0;
inline long     simI2sDropped = 0;   // samples the ring had no room for
inline int      simSampleRate = 16000;
inline int32_t  simRing[16384];
inline int      simRingHead = 0;

inline esp_err_t i2s_driver_install(i2s_port_t, const i2s_config_t* c, int, void*) {
  simRingCap = c->dma_buf_count * c->dma_buf_len;
  simSampleRate = c->sample_rate;
  simRingFill = 0; simRingHead = 0; simI2sDropped = 0; simLastFillUs = simMicros;
  return ESP_OK;
}
inline esp_err_t i2s_set_pin(i2s_port_t, const i2s_pin_config_t*) { return ESP_OK; }

// Bring the ring up to date with the clock, then hand over what was asked for, or less.
inline esp_err_t i2s_read(i2s_port_t, void* dest, size_t bytes, size_t* got, int) {
  uint64_t due = ((simMicros - simLastFillUs) * (uint64_t)simSampleRate) / 1000000ULL;
  if (due > 0) {
    simLastFillUs += (due * 1000000ULL) / (uint64_t)simSampleRate;
    for (uint64_t i = 0; i < due; i++) {
      int32_t s = simNextSample();
      if (simRingFill < simRingCap) {
        simRing[(simRingHead + simRingFill) % simRingCap] = s;
        simRingFill++;
      } else {
        simI2sDropped++;      // the ring is full: this sample never reaches the firmware
      }
    }
  }
  int want = (int)(bytes / sizeof(int32_t));
  int n = want < simRingFill ? want : simRingFill;
  int32_t* out = (int32_t*)dest;
  for (int i = 0; i < n; i++) out[i] = simRing[(simRingHead + i) % simRingCap];
  simRingHead = (simRingHead + n) % (simRingCap ? simRingCap : 1);
  simRingFill -= n;
  *got = (size_t)n * sizeof(int32_t);
  return ESP_OK;
}
