// Minimal Arduino surface so Audio_Engine.h compiles natively, unmodified.
// The point of the simulator is to exercise the REAL detector source, not a reimplementation of
// it, so nothing here may change behaviour -- these are only the platform calls the firmware
// happens to make. Time comes from the harness, not the wall clock, so a run is deterministic
// and can be faster or slower than real time without changing a single result.
#pragma once
#include <cstdint>
#include <cstdlib>
#include <cmath>
#include <algorithm>

#ifndef PI
#define PI 3.1415926535897932384626433832795
#endif

// Advanced by the harness. Everything in the firmware that asks the time gets this.
extern uint64_t simMicros;
inline unsigned long micros() { return (unsigned long)simMicros; }
inline unsigned long millis() { return (unsigned long)(simMicros / 1000ULL); }

template <typename T, typename L, typename H>
inline T constrain(T v, L lo, H hi) { return v < (T)lo ? (T)lo : (v > (T)hi ? (T)hi : v); }

using std::min;
using std::max;
