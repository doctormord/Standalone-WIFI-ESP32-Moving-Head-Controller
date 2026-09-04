// Host simulator for the beat detector.
//
// The detector is integer DSP over a sample stream, so it does not need the microcontroller to be
// exercised -- only the platform calls around it, which sim/fake supplies. This compiles the REAL
// Audio_Engine.h unmodified, drives it from a synthesised track whose beat positions are known
// exactly, and models the device's timing: the DMA ring depth, the sample rate, and a main loop
// whose interval jitters the way the measured one does.
//
// It exists because three separate tuning runs on hardware were invalidated by the music changing
// underneath them, and because the "true" tempo was never better known than by tapping along. Here
// the ground truth is exact, a run is deterministic, and a parameter sweep takes seconds.
//
// What it cannot tell us: how a real microphone in a real room behaves -- reverb, the PA's own
// compression, crowd noise, a DJ riding the gain. Anything that passes here still has to be
// confirmed on hardware. What it can do is stop us shipping something that fails on a signal we
// already know the answer to.
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <cmath>
#include <string>
#include <vector>
#include <random>
#include <algorithm>

uint64_t simMicros = 0;

// Globals the engine reaches for that normally live in the .ino.
int globalBPM = 120;
unsigned long lastBeatTime = 0;
unsigned long beatCount = 0;
unsigned long masterSyncTime = 0;

int32_t simNextSample();
#include "Audio_Engine.h"

// ---------------------------------------------------------------------------
// A synthetic track. Deliberately not a clean click: the parts that broke the
// detector on real music are the ones worth reproducing -- a sustained bass line
// between the kicks (which kept the envelope above any fixed threshold), hats
// bleeding down into the bass band, and a snare sharing the low mids.
// ---------------------------------------------------------------------------
struct Track {
  double bpm = 130.0;
  int    sr  = SAMPLING_FREQUENCY;
  long   n   = 0;
  std::mt19937 rng{1234};
  std::normal_distribution<double> gauss{0.0, 1.0};

  double kickAmp = 1.00, bassAmp = 0.55, hatAmp = 0.25, snareAmp = 0.45, noiseAmp = 0.01;
  double masterAmp = 2.0e8;      // into the range a 32-bit I2S mic actually delivers
  double swingPct = 0.0;         // shifts offbeats, to check nothing locks to a rigid grid
  double dropoutEvery = 0.0;     // seconds of silence every N seconds, 0 = never

  double kickT = 1e9, snareT = 1e9, hatT = 1e9;
  double kickPhase = 0, bassPhase = 0, snarePhase = 0;
  long   beatIdx = -1, eighthIdx = -1;
  std::vector<double> trueBeats;   // seconds, exact

  // Where the kicks sit inside a four-beat bar, in beats. Four-on-the-floor is the case the
  // interval median was built for and the only one it can do: every gap equals the beat, so the
  // median of the gaps IS the beat. Everything else in this list breaks that assumption on
  // purpose -- the gaps are then fractions and multiples of the beat, and no single gap is the
  // answer. That is the failure seen on hardware: hip-hop at 98 BPM locked onto 454ms, which is
  // 3/4 of the 612ms beat to within 1%.
  std::vector<double> kickPos { 0.0, 1.0, 2.0, 3.0 };
  long   kickSlot = -1;
  std::vector<double> trueKicks;   // seconds, exact -- where a kick really was

  static double env(double t, double tau) { return t < 0 ? 0.0 : exp(-t / tau); }

  double next() {
    double t  = (double)n / sr;
    double bp = 60.0 / bpm;

    long bi = (long)floor(t / bp);
    if (bi != beatIdx) {                       // a beat starts here
      beatIdx = bi;
      trueBeats.push_back(bi * bp);
      if (bi % 4 == 1 || bi % 4 == 3) { snareT = 0; snarePhase = 0; }
    }
    // Kicks follow the pattern, not the beat grid. The beat grid stays the ground truth: the
    // question a tempo estimator has to answer is "what is the beat", and on syncopated material
    // that is a period the kicks do not all sit on.
    {
      double bar = 4 * bp;
      long barIdx = (long)floor(t / bar);
      double inBar = t - barIdx * bar;
      long slot = -1;
      for (size_t k = 0; k < kickPos.size(); k++) if (inBar >= kickPos[k] * bp) slot = (long)k;
      long id = barIdx * 64 + slot;
      if (slot >= 0 && id != kickSlot) {
        kickSlot = id;
        kickT = 0; kickPhase = 0;
        trueKicks.push_back(barIdx * bar + kickPos[slot] * bp);
      }
    }
    long ei = (long)floor(t / (bp / 2));
    if (ei != eighthIdx) { eighthIdx = ei; hatT = 0; }

    bool silent = dropoutEvery > 0 && fmod(t, dropoutEvery) > dropoutEvery - 2.0;
    double v = 0;

    // Kick: pitch falls 120Hz -> 45Hz in about 30ms, amplitude decays over ~180ms.
    if (kickT < 1.0) {
      double f = 45.0 + 75.0 * exp(-kickT / 0.03);
      kickPhase += 2 * M_PI * f / sr;
      v += kickAmp * env(kickT, 0.18) * sin(kickPhase);
      kickT += 1.0 / sr;
    }
    // Bass line: sustained, sits between the kicks, changes note each bar. This is the part
    // that makes "energy above a threshold" fire continuously.
    {
      double f = 55.0 * pow(2.0, ((beatIdx / 4) % 3) / 12.0 * 5.0);
      bassPhase += 2 * M_PI * f / sr;
      double gate = (fmod(t, bp) > bp * 0.45) ? 1.0 : 0.25;   // louder off the beat
      v += bassAmp * gate * sin(bassPhase);
    }
    // Snare: noise plus a 190Hz body, so it reaches into the low mids.
    if (snareT < 0.5) {
      snarePhase += 2 * M_PI * 190.0 / sr;
      v += snareAmp * env(snareT, 0.09) * (0.6 * gauss(rng) + 0.4 * sin(snarePhase));
      snareT += 1.0 / sr;
    }
    // Hat: short, bright. Included because broadband transients used to leak into the bass band.
    if (hatT < 0.2) {
      v += hatAmp * env(hatT, 0.012) * gauss(rng);
      hatT += 1.0 / sr;
    }
    v += noiseAmp * gauss(rng);
    if (silent) v *= 0.02;

    n++;
    double s = v * masterAmp;
    if (s >  2.0e9) s =  2.0e9;
    if (s < -2.0e9) s = -2.0e9;
    return s;
  }
};

Track track;

// ---------------------------------------------------------------------------
// Kick patterns. Only the first is what the interval median assumes.
// ---------------------------------------------------------------------------
struct Pattern { const char* name; std::vector<double> pos; const char* note; };
static const std::vector<Pattern> kPatterns = {
  { "four",   { 0.0, 1.0, 2.0, 3.0 },   "Four-on-the-floor: every gap is exactly one beat" },
  { "hiphop", { 0.0, 0.75, 1.25, 2.5 }, "Boom-bap: gaps of 3/4, 1/2, 1 1/4, 1 1/2 beats" },
  { "broken", { 0.0, 1.5, 2.0, 3.25 },  "Broken: gaps of 1 1/2, 1/2, 1 1/4, 3/4 beats" },
  { "dnb",    { 0.0, 2.5 },             "Two-step: gaps of 2 1/2 and 1 1/2 beats" },
  { "half",   { 0.0, 2.0 },             "Halftime: every gap is two beats" },
};
static const Pattern* findPattern(const std::string& n) {
  for (auto& p : kPatterns) if (n == p.name) return &p;
  return nullptr;
}

// ---------------------------------------------------------------------------
// Candidate tempo estimators, all host-side, all fed the SAME onset train the
// firmware saw. The point is to compare methods, not implementations: whatever
// wins here still has to be written in integer arithmetic for the C3.
//
// The interval median is reimplemented here rather than read out of the engine so
// that every method sees an identical window. (It is checked against the engine's
// own trackedBPM in the compare output -- if the two disagree, this harness is
// wrong, not the method.)
// ---------------------------------------------------------------------------
static const int TB_MS      = 10;                            // bin width, ms
static const int TB_LAG_MIN = 60000 / BPM_MAX_LIMIT / TB_MS;  //  30 bins = 300ms = 200 BPM
static const int TB_LAG_MAX = 60000 / BPM_MIN_LIMIT / TB_MS;  // 100 bins = 1000ms = 60 BPM

// Onsets -> a coarse onset-strength envelope. Placement is fractional so a 3ms timing difference
// is not thrown away by the binning, and one round of [1 2 1] smoothing widens each impulse to
// about +/-15ms, which is what lets autocorrelation count a near-miss as a partial hit. A hard
// impulse train would make the ACF brittle in exactly the way real music is not.
static std::vector<double> binOnsets(const std::vector<double>& on, double t0, double t1) {
  int n = (int)((t1 - t0) / TB_MS) + 2;
  if (n < 4) return {};
  std::vector<double> x(n, 0.0);
  for (double o : on) {
    if (o < t0 || o > t1) continue;
    double b = (o - t0) / TB_MS;
    int i = (int)b; double fr = b - i;
    if (i >= 0 && i < n) x[i] += 1.0 - fr;
    if (i + 1 < n)       x[i + 1] += fr;
  }
  std::vector<double> y(n, 0.0);
  for (int i = 0; i < n; i++) {
    double a = i > 0 ? x[i-1] : 0.0, c = i + 1 < n ? x[i+1] : 0.0;
    y[i] = 0.25 * a + 0.5 * x[i] + 0.25 * c;
  }
  return y;
}

static double acf(const std::vector<double>& x, int lag) {
  int n = (int)x.size() - lag;
  if (lag <= 0 || n < 8) return 0.0;
  double s = 0;
  for (int i = 0; i < n; i++) s += x[i] * x[i + lag];
  return s / n;          // per-sample, or long lags lose purely for having fewer terms
}

// Harmonic summing: a period is credited with what lands on its multiples too. This is the part
// that can see through syncopation -- the bar is a multiple of the beat, so a strong bar-length
// periodicity votes for the beat even when no pair of kicks is one beat apart.
static double acfHarm(const std::vector<double>& x, int lag) {
  static const double w[4] = { 1.0, 0.5, 0.34, 0.25 };
  double s = 0;
  for (int h = 1; h <= 4; h++) s += w[h-1] * acf(x, lag * h);
  return s;
}

// Ellis's log-Gaussian tempo prior, centred where dance music actually lives. It does not decide
// anything on its own; it breaks the octave tie that autocorrelation cannot break, because a
// period and half that period are both genuinely present in the signal.
inline double gPriorCentre = 120.0, gPriorSigma = 0.9;
static double prior(double bpm) {
  double d = log2(bpm / gPriorCentre) / gPriorSigma;
  return exp(-0.5 * d * d);
}

// The tap anchor as the prior's centre. The firmware already treats a tap as a statement about
// the OCTAVE rather than a tempo to obey, and folds the tracker's answer to the nearest ratio
// rung of it. Centring the prior on the anchor expresses the same idea in one mechanism instead
// of two: autocorrelation says which periods are present, the anchor says which one the user
// means. A tap is modelled here as the true tempo off by a few percent, which is about what
// four taps on a phone actually achieve.
inline double gAnchorBPM = 0.0, gAnchorSigma = 0.40;

enum EstKind { EST_MEDIAN, EST_ACF, EST_ACFH, EST_ACFHP, EST_ACFHA };

static int estimate(EstKind kind, const std::vector<double>& on, double t0, double t1) {
  if (kind == EST_MEDIAN) {
    std::vector<double> g;
    for (size_t i = 1; i < on.size(); i++) {
      if (on[i] < t0 || on[i] > t1) continue;
      double d = on[i] - on[i-1];
      if (d >= TEMPO_IVL_MIN && d <= TEMPO_IVL_MAX) g.push_back(d);
    }
    if (g.size() < 3) return 0;
    if (g.size() > TEMPO_IVL_RING) g.erase(g.begin(), g.end() - TEMPO_IVL_RING);
    std::sort(g.begin(), g.end());
    // Interval folding, mirroring tempoEvalMedian() in the firmware: a gap that is close to an
    // integer multiple of the window's lower quartile is a MISSED kick, not a slower tempo, so
    // it votes for the same period rather than for the multiple. Without this the sim scores a
    // weaker estimator than the device actually runs, and every comparison drawn here would
    // flatter whatever it is compared against.
    {
      double base = g[g.size() / 4];
      if (base >= TEMPO_IVL_MIN) {
        for (double& v : g) {
          long k = (long)(v / base + 0.5);
          if (k < 2) continue;
          double folded = v / k;
          if (folded >= TEMPO_IVL_MIN && fabs(folded - base) * 4 <= base) v = folded;
        }
        std::sort(g.begin(), g.end());
      }
    }
    double med = g[g.size()/2];
    double dev = 0;
    for (double v : g) dev += fabs(v - med);
    dev /= g.size();
    if (dev * 100.0 / med > tempoAgreeMaxPct) return 0;   // same agreement gate as the firmware
    return (int)(60000.0 / med + 0.5);
  }

  std::vector<double> x = binOnsets(on, t0, t1);
  if (x.empty()) return 0;
  int bestLag = 0; double bestS = 0;
  for (int lag = TB_LAG_MIN; lag <= TB_LAG_MAX; lag++) {
    double sc = (kind == EST_ACF) ? acf(x, lag) : acfHarm(x, lag);
    double bpmHere = 60000.0 / (lag * TB_MS);
    if (kind == EST_ACFHP) sc *= prior(bpmHere);
    if (kind == EST_ACFHA) {
      double d = log2(bpmHere / (gAnchorBPM > 0 ? gAnchorBPM : gPriorCentre))
                 / (gAnchorBPM > 0 ? gAnchorSigma : gPriorSigma);
      sc *= exp(-0.5 * d * d);
    }
    if (sc > bestS) { bestS = sc; bestLag = lag; }
  }
  if (!bestLag || bestS <= 0) return 0;

  // Parabolic interpolation around the winning bin: the true period rarely lands on a 10ms
  // boundary, and without this the answer is quantised to ~2 BPM at 120 and ~5 BPM at 200.
  double ym1 = (kind == EST_ACF) ? acf(x, bestLag-1) : acfHarm(x, bestLag-1);
  double yp1 = (kind == EST_ACF) ? acf(x, bestLag+1) : acfHarm(x, bestLag+1);
  if (kind == EST_ACFHP) {
    ym1 *= prior(60000.0 / ((bestLag-1) * TB_MS));
    yp1 *= prior(60000.0 / ((bestLag+1) * TB_MS));
  }
  // The anchor prior is deliberately NOT applied to the interpolation: it would bend the
  // sub-bin estimate toward the tapped value and make the result partly an echo of the tap.

  double den = ym1 - 2*bestS + yp1;
  double adj = (den != 0) ? 0.5 * (ym1 - yp1) / den : 0.0;
  if (adj < -1 || adj > 1) adj = 0;
  double periodMs = (bestLag + adj) * TB_MS;
  return (int)(60000.0 / periodMs + 0.5);
}

// How a reading relates to the truth. "ok" and everything else are different failures: an octave
// error still gives a usable light show, a 3/4 error does not.
static const char* relation(int got, double truth) {
  if (!got) return "--";
  static const struct { double r; const char* n; } rel[] = {
    { 1.0, "ok" }, { 2.0, "x2" }, { 0.5, "/2" }, { 1.5, "x3/2" }, { 2.0/3, "x2/3" },
    { 4.0/3, "x4/3" }, { 0.75, "x3/4" }, { 3.0, "x3" }, { 1.0/3, "/3" }, { 4.0, "x4" }, { 0.25, "/4" },
  };
  double q = got / truth;
  for (auto& r : rel) if (fabs(q - r.r) / r.r < 0.04) return r.n;
  return "wrong";
}


// ---------------------------------------------------------------------------
// Real audio, when a file is given instead of the synthesiser. Mono-summed and
// resampled to the device's 16kHz, with a lowpass first: 44.1k -> 16k without one
// would fold everything above 8kHz straight down into the bands we detect on.
// ---------------------------------------------------------------------------
// Ground truth for a real recording. Without this the F-measure can only be computed against
// the synthetic track, whose beat positions are known by construction -- which is why every
// conclusion drawn here about real material used to rest on the reported BPM alone. Format is
// one beat time in SECONDS per line; a second tab/space-separated column is ignored, so an
// Audacity label export ("start<TAB>end<TAB>label") can be used unchanged. Blank lines and
// lines starting with '#' are skipped.
// Held outside Track because resetEngine() replaces the whole Track for every run; without
// this the annotation was silently wiped between being parsed and being scored against.
static std::vector<double> gAnnotatedBeats;

static bool loadBeatAnnotations(const char* path, std::vector<double>& out) {
  FILE* f = fopen(path, "r");
  if (!f) return false;
  char line[512];
  while (fgets(line, sizeof line, f)) {
    char* p = line;
    while (*p == ' ' || *p == '\t') p++;
    if (*p == '#' || *p == '\n' || *p == '\r' || *p == 0) continue;
    char* end = nullptr;
    double t = strtod(p, &end);
    if (end == p) continue;             // not a number: skip rather than abort
    out.push_back(t);
  }
  fclose(f);
  std::sort(out.begin(), out.end());
  return !out.empty();
}

// Median inter-beat interval of an annotation, as BPM. This is the truth the reported tempo
// gets compared against on a real file -- do not take it from --bpm, which describes the
// synthetic generator and means nothing here.
static double annotatedBPM(const std::vector<double>& beats) {
  if (beats.size() < 2) return 0;
  std::vector<double> ibi;
  for (size_t i = 1; i < beats.size(); i++) ibi.push_back(beats[i] - beats[i - 1]);
  std::sort(ibi.begin(), ibi.end());
  double m = ibi[ibi.size() / 2];
  return m > 0 ? 60.0 / m : 0;
}

struct Wav {
  std::vector<float> mono;      // at 16kHz
  bool loaded = false;
  double srcRate = 0;
  size_t pos = 0;
  double gain = 1.0;

  bool load(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) return false;
    unsigned char hdr[12];
    if (fread(hdr, 1, 12, f) != 12 || memcmp(hdr, "RIFF", 4) || memcmp(hdr + 8, "WAVE", 4)) { fclose(f); return false; }
    int channels = 0, bits = 0; long dataLen = 0;
    std::vector<unsigned char> data;
    while (!feof(f)) {
      unsigned char ch[8];
      if (fread(ch, 1, 8, f) != 8) break;
      uint32_t sz; memcpy(&sz, ch + 4, 4);
      if (!memcmp(ch, "fmt ", 4)) {
        std::vector<unsigned char> fmt(sz);
        if (fread(fmt.data(), 1, sz, f) != sz) break;
        uint16_t c, b; uint32_t sr;
        memcpy(&c, fmt.data() + 2, 2); memcpy(&sr, fmt.data() + 4, 4); memcpy(&b, fmt.data() + 14, 2);
        channels = c; srcRate = sr; bits = b;
      } else if (!memcmp(ch, "data", 4)) {
        data.resize(sz); dataLen = fread(data.data(), 1, sz, f); break;
      } else {
        fseek(f, sz + (sz & 1), SEEK_CUR);
      }
    }
    fclose(f);
    if (bits != 16 || channels < 1 || dataLen <= 0) return false;

    // Mono sum at the source rate.
    size_t frames = (size_t)dataLen / (2 * channels);
    std::vector<double> src(frames);
    for (size_t i = 0; i < frames; i++) {
      double acc = 0;
      for (int c = 0; c < channels; c++) {
        int16_t v; memcpy(&v, &data[(i * channels + c) * 2], 2);
        acc += v;
      }
      src[i] = acc / channels;
    }
    // Two cascaded one-pole lowpasses near 7kHz as the anti-alias filter.
    double a = 1.0 - exp(-2.0 * M_PI * 7000.0 / srcRate), l1 = 0, l2 = 0;
    for (size_t i = 0; i < frames; i++) {
      l1 += a * (src[i] - l1);
      l2 += a * (l1 - l2);
      src[i] = l2;
    }
    // Linear resample to the device rate.
    double step = srcRate / (double)SAMPLING_FREQUENCY;
    size_t out = (size_t)(frames / step);
    mono.resize(out);
    for (size_t i = 0; i < out; i++) {
      double x = i * step; size_t i0 = (size_t)x; double fr = x - i0;
      double v0 = src[i0], v1 = (i0 + 1 < frames) ? src[i0 + 1] : v0;
      mono[i] = (float)(v0 + fr * (v1 - v0));
    }
    loaded = true;
    return true;
  }

  double lengthSec() const { return mono.empty() ? 0 : (double)mono.size() / SAMPLING_FREQUENCY; }

  // Loops, because the estimator wants more than one pass of a short clip. The seam is one bad
  // interval per loop and the median absorbs it.
  int32_t next() {
    if (mono.empty()) return 0;
    float v = mono[pos % mono.size()];
    pos++;
    double s = (double)v * 65536.0 * gain;      // int16 -> the 32-bit range an I2S mic delivers
    if (s >  2.0e9) s =  2.0e9;
    if (s < -2.0e9) s = -2.0e9;
    return (int32_t)s;
  }
};

Wav wav;
int32_t simNextSample() { return wav.loaded ? wav.next() : (int32_t)track.next(); }

// ---------------------------------------------------------------------------
struct Result {
  std::vector<double> onsets;      // sample-clock ms, as the firmware timestamps them (bass)
  // The same run's mid and high onsets, and the three merged. The tempo estimator has only ever
  // been fed the bass, which is the whole reason syncopation defeats it: a boom-bap kick pattern
  // does not state the beat, and no amount of processing can recover what is not there. The
  // snare on 2 and 4 and the hats on the eighths state it plainly, and both are already detected.
  std::vector<double> onsetsMid, onsetsHigh, onsetsAll;
  std::vector<int>    bpmReadings;
  long   drops = 0;
  int    drift = 0;
};


// =========================================================
// --- PSYCHOACOUSTIC ONSET DETECTION FUNCTION (prototype) ---
// =========================================================
// Not firmware. This is the classical-DSP half of the pipeline described in Bello et al. [1]
// and refined by Boeck & Widmer (SuperFlux, DAFx-13), built here so it can be MEASURED against
// the detector actually running on the device before a single byte of it is ported.
//
// Deliberately out of scope: refs [2][3][5][6] are neural networks -- BLSTM, TCN, Transformer.
// They are the state of the art and they are not reachable on an ESP32-C3 with no FPU and
// ~78KB of flash left. Ref [4] adds a dynamic Bayesian network on top of an RNN, same problem.
// What IS reachable is everything those papers use as their INPUT representation, and that is
// where the perceptual modelling actually lives.
//
// Three ideas, each switchable so the contribution of each can be measured separately:
//
//  1. CRITICAL BANDS. Hearing does not resolve frequency linearly; it integrates energy within
//     roughly constant-Q bands. Summing FFT bins into Bark bands means a kick competes with the
//     other energy in ITS band, not with the whole spectrum -- which is the entire reason a
//     kick under a loud pad is still audible to a person.
//
//  2. LOGARITHMIC COMPRESSION. Loudness is roughly logarithmic in amplitude. Differencing raw
//     magnitudes makes an onset in a loud passage numerically enormous and the identical onset
//     in a quiet passage numerically invisible; differencing log magnitudes makes them equal,
//     which is what a listener reports. This is the single step the SuperFlux authors credit
//     most, and it is the reason a fixed sensitivity can work across a whole set.
//
//  3. MAXIMUM FILTER (SuperFlux). Vibrato, portamento and any drifting partial move energy
//     between neighbouring bands, producing a positive difference in the band it moves INTO --
//     a false onset. Comparing against the maximum of the previous frame's neighbouring bands
//     lets a partial wander without registering. Boeck & Widmer report up to 60% fewer false
//     positives on vibrato-heavy material, with no additional misses.
//
// The FFT is the firmware's own (fftRun/fftMag), so a port would not need a new one.

// Integer log2 with a linearly interpolated mantissa, Q8. No powf/logf: the C3 has no FPU, and
// this is ~6 instructions. Worst-case error is about 0.086 in log2 units (~2.6% in magnitude),
// which is far below the differences this function is asked to resolve.
static inline int32_t log2Q8(uint32_t v) {
  if (v == 0) return 0;
  int msb = 31 - __builtin_clz(v);
  uint32_t frac = (msb >= 8) ? ((v >> (msb - 8)) & 0xFF) : ((v << (8 - msb)) & 0xFF);
  return (int32_t)(msb << 8) | (int32_t)frac;
}

// Bark-scale critical band edges in Hz (Zwicker), truncated at the 8kHz Nyquist of our 16kHz
// sampling. 22 bands. The lowest ones span only two or three of our 31.25Hz bins, which is
// coarse -- but the kick lives there and it is where a finer split would buy the least.
static const int kBarkEdgesHz[] = {
  20, 100, 200, 300, 400, 510, 630, 770, 920, 1080, 1270, 1480,
  1720, 2000, 2320, 2700, 3150, 3700, 4400, 5300, 6400, 7700, 8000
};
static const int kNumBark = (int)(sizeof(kBarkEdgesHz) / sizeof(kBarkEdgesHz[0])) - 1;
#define PSY_MAX_BANDS 64

struct PsyCfg {
  bool bark     = true;   // 1. critical bands instead of raw FFT bins
  bool logComp  = true;   // 2. logarithmic magnitude compression
  bool maxFilt  = true;   // 3. SuperFlux maximum filter over +/-1 band
  int  mu       = 1;      // frames of lag for the difference
  int  hop      = FFT_N;  // samples between frames; FFT_N = no overlap
  int  deltaQ8  = 384;    // peak threshold = mean + delta*MAD, Q8 (256 == 1.0)
  int  meanFr   = 16;     // frames in the moving mean/MAD window
  int  refracMs = 60;     // refractory period
  int  locMaxFr = 3;      // frames each side that the peak must dominate
  int  bandLo   = 0;      // first band summed into the flux
  int  bandHi   = 99;     // last band (clamped to the band count)
  const char* name = "psy";
};

// Runs the ODF over the same source the firmware run() sees, and returns onset times in ms.
// Offline and self-contained on purpose: it must not be able to disturb the engine it is being
// compared against.
static std::vector<double> psyRun(double seconds, const PsyCfg& cfg,
                                  std::vector<double>* odfOut = nullptr) {
  std::vector<double> onsets;
  // Rewind the SOURCE without discarding what it was configured to play. `track = Track()` here
  // silently reset tempo, level and kick pattern to the defaults, so every variant and every
  // test case heard the identical default track -- which showed up as identical onset counts
  // down every column. Same trap as resetEngine() wiping the beat annotations.
  if (wav.loaded) {
    wav.pos = 0;
  } else {
    Track cfg = track;
    track = Track();
    track.bpm = cfg.bpm; track.bassAmp = cfg.bassAmp; track.masterAmp = cfg.masterAmp;
    track.kickPos = cfg.kickPos;
  }

  fftInitTables();
  static int binOf[PSY_MAX_BANDS + 1];
  int nBands;
  if (cfg.bark) {
    nBands = kNumBark;
    for (int i = 0; i <= nBands; i++) {
      int b = (int)((double)kBarkEdgesHz[i] * FFT_N / SAMPLING_FREQUENCY + 0.5);
      binOf[i] = std::min(b, FFT_N / 2 - 1);
    }
  } else {
    // Raw bins, decimated to the same count so the comparison is about the SCALE, not about
    // how many numbers the flux is summed over.
    nBands = kNumBark;
    for (int i = 0; i <= nBands; i++) binOf[i] = (i * (FFT_N / 2 - 1)) / nBands;
  }

  std::vector<int32_t> ring(FFT_N, 0);
  size_t ringFill = 0;
  const int lagMax = std::max(1, cfg.mu);
  std::vector<std::vector<int32_t>> hist(lagMax + 1, std::vector<int32_t>(nBands, 0));
  int histAt = 0, framesSeen = 0;

  std::vector<double> odf;            // one value per frame
  std::vector<double> odfTimeMs;

  uint64_t sampleIdx = 0;
  const uint64_t total = (uint64_t)(seconds * SAMPLING_FREQUENCY);
  while (sampleIdx < total) {
    // Fill/slide the analysis window.
    int need = (ringFill < (size_t)FFT_N) ? (FFT_N - (int)ringFill) : cfg.hop;
    if (ringFill == (size_t)FFT_N) {
      std::rotate(ring.begin(), ring.begin() + cfg.hop, ring.end());
      for (int i = 0; i < cfg.hop; i++) { ring[FFT_N - cfg.hop + i] = simNextSample(); sampleIdx++; }
    } else {
      for (int i = 0; i < need; i++) { ring[ringFill++] = simNextSample(); sampleIdx++; }
      continue;
    }

    for (int i = 0; i < FFT_N; i++) {
      int32_t s = (ring[i] >> SAMPLE_DOWNSCALE_SHIFT_FFT) << tuneInputGainShift;
      if (s > 32767) s = 32767; else if (s < -32767) s = -32767;
      fftRe[i] = (int16_t)(((int32_t)s * fftWindow[i]) >> 15);
      fftIm[i] = 0;
    }
    fftRun();

    std::vector<int32_t>& cur = hist[histAt];
    for (int b = 0; b < nBands; b++) {
      int lo = binOf[b], hi = std::max(binOf[b + 1] - 1, binOf[b]);
      int32_t sum = 0;
      for (int i = lo; i <= hi; i++) sum += fftMag[i];
      cur[b] = cfg.logComp ? log2Q8((uint32_t)sum + 1) : sum;
    }
    framesSeen++;

    if (framesSeen > lagMax) {
      const std::vector<int32_t>& prev = hist[(histAt - cfg.mu + lagMax + 1) % (lagMax + 1)];
      int64_t flux = 0;
      const int bLo = std::max(0, cfg.bandLo), bHi = std::min(nBands - 1, cfg.bandHi);
      for (int b = bLo; b <= bHi; b++) {
        int32_t ref = prev[b];
        if (cfg.maxFilt) {                       // SuperFlux: let a partial wander one band
          if (b > 0)          ref = std::max(ref, prev[b - 1]);
          if (b < nBands - 1) ref = std::max(ref, prev[b + 1]);
        }
        int32_t d = cur[b] - ref;
        if (d > 0) flux += d;                    // half-wave rectified: onsets, not offsets
      }
      odf.push_back((double)flux);
      // Timestamp at the END of the window: that is when the evidence is complete, and it is
      // what the firmware's own detector reports too, so the two are comparable.
      odfTimeMs.push_back((double)sampleIdx * 1000.0 / SAMPLING_FREQUENCY);
    }
    histAt = (histAt + 1) % (lagMax + 1);
  }

  // --- peak picking -------------------------------------------------------
  // The standard three-condition picker (Bello et al. [1], as parameterised by Boeck):
  //   1. the frame is the maximum over a local neighbourhood,
  //   2. it exceeds mean + delta * MAD over a longer trailing window,
  //   3. enough time has passed since the last onset.
  //
  // Condition 1 with a neighbourhood of +/-1 frame is not enough and produced a detector that
  // fired at a near-constant ~3 onsets/s whatever the music was doing -- which then looked like
  // a 100% score at any tempo near 180 BPM and 0% elsewhere. A wider window is what makes the
  // picker select events rather than sample the bed.
  const int w1 = cfg.locMaxFr;      // frames each side for the local maximum
  const int w3 = cfg.meanFr;        // trailing frames for mean/MAD
  double lastMs = -1e9;
  for (size_t n = (size_t)w1; n + w1 < odf.size(); n++) {
    bool isMax = true;
    for (int k = -w1; k <= w1 && isMax; k++) if (odf[n + k] > odf[n]) isMax = false;
    if (!isMax) continue;
    size_t from = (n >= (size_t)w3) ? n - w3 : 0;
    if (n - from < 4) continue;
    double mean = 0; size_t cnt = 0;
    for (size_t k = from; k < n; k++) { mean += odf[k]; cnt++; }
    mean /= cnt;
    double mad = 0;
    for (size_t k = from; k < n; k++) mad += fabs(odf[k] - mean);
    mad /= cnt;
    if (odf[n] < mean + (cfg.deltaQ8 / 256.0) * mad) continue;
    if (odfTimeMs[n] - lastMs < cfg.refracMs) continue;
    lastMs = odfTimeMs[n];
    onsets.push_back(odfTimeMs[n]);
  }
  if (odfOut) *odfOut = odf;
  return onsets;
}

Result run(double seconds) {
  Result r;
  std::mt19937 loopRng{99};
  uint32_t lastOnset = 0, lastMid = 0, lastHigh = 0;
  double nextSampleAt = 0.5;   // let the detector settle before recording anything

  while (simMicros < (uint64_t)(seconds * 1e6)) {
    // The measured device loop: about a millisecond, with occasional longer spikes when the DMX
    // frame and the web server land in the same iteration (loopMax read 16-20ms on hardware).
    uint64_t step = 900 + (loopRng() % 400);
    if (loopRng() % 250 == 0) step += 15000;
    simMicros += step;

    pollAudioEngine();

    // bassOnsetUsedMs, not sdBass.lastOnsetMs: the latter always holds the sample-rate
    // detector's result, so with --psy this harness would have scored the detector that was
    // NOT driving anything and reported the two as identical. It did, until this was fixed.
    if (bassOnsetUsedMs != lastOnset) {
      lastOnset = bassOnsetUsedMs;
      if (simMicros > nextSampleAt * 1e6) r.onsets.push_back((double)bassOnsetUsedMs);
    }
    if (sdMid.lastOnsetMs != lastMid) {
      lastMid = sdMid.lastOnsetMs;
      if (simMicros > nextSampleAt * 1e6) r.onsetsMid.push_back((double)lastMid);
    }
    if (sdHigh.lastOnsetMs != lastHigh) {
      lastHigh = sdHigh.lastOnsetMs;
      if (simMicros > nextSampleAt * 1e6) r.onsetsHigh.push_back((double)lastHigh);
    }
    static uint64_t lastBpmAt = 0;
    if (simMicros - lastBpmAt > 200000) { lastBpmAt = simMicros; r.bpmReadings.push_back(globalBPM); }
  }
  r.drops = simI2sDropped;
  r.drift = sdClkDriftPpt;
  // Merged, in time order. Onsets within 25ms of one another are one event heard in two bands
  // (a kick has high-frequency click, a snare has body) -- counting it twice would just weight
  // that instant more heavily, which is in fact what a broadband detection function does, so
  // they are kept. What is collapsed is only an exact duplicate timestamp.
  r.onsetsAll = r.onsets;
  r.onsetsAll.insert(r.onsetsAll.end(), r.onsetsMid.begin(), r.onsetsMid.end());
  r.onsetsAll.insert(r.onsetsAll.end(), r.onsetsHigh.begin(), r.onsetsHigh.end());
  std::sort(r.onsetsAll.begin(), r.onsetsAll.end());
  r.onsetsAll.erase(std::unique(r.onsetsAll.begin(), r.onsetsAll.end()), r.onsetsAll.end());
  return r;
}

// F-measure against the known beat grid, MIREX-style: a detection counts if it falls within 50ms
// of a true beat. A constant lag is removed first -- the peak picker deliberately reports the
// envelope peak, which sits a few ms after the transient, and that offset is not an error.
struct Score { double p = 0, rec = 0, f = 0, err = 0, offset = 0; int matched = 0; };

Score score(const std::vector<double>& onsets, const std::vector<double>& beatsSec, double window = 50.0) {
  Score s;
  if (onsets.empty() || beatsSec.empty()) return s;
  std::vector<double> beats;
  for (double b : beatsSec) beats.push_back(b * 1000.0);

  std::vector<double> diffs;
  for (double o : onsets) {
    double best = 1e18;
    for (double b : beats) { double d = o - b; if (fabs(d) < fabs(best)) best = d; }
    diffs.push_back(best);
  }
  std::vector<double> sorted = diffs;
  std::sort(sorted.begin(), sorted.end());
  s.offset = sorted[sorted.size() / 2];

  std::vector<bool> used(beats.size(), false);
  double errSum = 0;
  for (double o : onsets) {
    int bi = -1; double best = 1e18;
    for (size_t i = 0; i < beats.size(); i++) {
      if (used[i]) continue;
      double d = fabs((o - s.offset) - beats[i]);
      if (d < best) { best = d; bi = (int)i; }
    }
    if (bi >= 0 && best <= window) { used[bi] = true; s.matched++; errSum += best; }
  }
  s.p   = (double)s.matched / onsets.size();
  s.rec = (double)s.matched / beats.size();
  s.f   = (s.p + s.rec) > 0 ? 2 * s.p * s.rec / (s.p + s.rec) : 0;
  s.err = s.matched ? errSum / s.matched : 0;
  return s;
}

int medianBPM(std::vector<int> v) {
  if (v.empty()) return 0;
  std::sort(v.begin(), v.end());
  return v[v.size() / 2];
}

// Overrides applied after initAudioEngine(), so a sweep can vary one parameter at a time
// without touching the firmware defaults. -1 means "leave at the firmware default".
int ovRel = -1, ovRefShift = -1, ovKLo = -1, ovKHi = -1, ovLockout = -1, ovVarMin = -1;
int ovMinRange = -1, ovBoostMax = -1, ovBoostSh = -1, ovPeakFall = -1, ovPeakWait = -1, ovWindow = -1;
const Pattern* gPattern = nullptr;   // kick pattern, applied by resetEngine()

void resetEngine(int sens, bool autogain) {
  // Fresh state for every run, so one case cannot colour the next.
  simMicros = 0; track = Track();
  // Real ground truth outlives the reset -- see gAnnotatedBeats.
  if (!gAnnotatedBeats.empty()) track.trueBeats = gAnnotatedBeats;
  for (SdBand* b : { &sdBass, &sdMid, &sdHigh }) {
    b->lp1 = b->lp2 = b->env = b->ref = b->refAcc = 0;
    b->lastOnsetMs = 0; b->armed = true; b->peaking = false; b->boost = 65536;
    b->statIdx = 0; memset(b->statHist, 0, sizeof(b->statHist));
    b->onsetMs = 0; b->transient = false; b->hasDynamics = false;
  }
  sdSampleClock = 0;
  sdClkStartWall = sdClkStartSample = 0; sdClkDriftPpt = 0;
  tempoPrevOnset = 0; tempoIvlIdx = 0;
  memset(tempoIvl, 0, sizeof(tempoIvl)); memset(tempoIvlAt, 0, sizeof(tempoIvlAt));
  trackedBPM = 0; globalBPM = 120;
  hwAudioEnabled = true; hwAudioSensitivity = sens; autoGain = autogain;
  // On the device these are set from the FX routing; here we ask for all three so a band test
  // measures the bands rather than the routing.
  sdMidWanted = sdHighWanted = true;
  initAudioEngine();
  if (ovRel      >= 0) sdRel           = ovRel;
  if (ovRefShift >= 0) sdRefShift      = ovRefShift;
  if (ovKLo      >= 0) sdKLo           = ovKLo;
  if (ovKHi      >= 0) sdKHi           = ovKHi;
  if (ovLockout  >= 0) sdLockoutMs     = ovLockout;
  if (ovVarMin   >= 0) sdVarMinPct     = ovVarMin;
  if (ovMinRange >= 0) sdMinRangePct   = ovMinRange;
  if (ovBoostMax >= 0) sdBoostMaxQ8    = ovBoostMax;
  if (ovBoostSh  >= 0) sdBoostShift    = ovBoostSh;
  if (ovPeakFall >= 0) sdPeakFallPct   = ovPeakFall;
  if (ovPeakWait >= 0) sdPeakMaxWaitMs = ovPeakWait;
  if (ovWindow   >= 0) tempoWindowMs   = ovWindow;
  if (gPattern) track.kickPos = gPattern->pos;
}

int main(int argc, char** argv) {
  double bpm = 130, secs = 60, wavGain = 1.0;
  const char* wavPath = nullptr;
  const char* beatsPath = nullptr;
  int sens = 60; bool autogain = false; std::string mode = "single";
  std::string patName; double winMs = 8000; bool bpmGiven = false;
  double anchorRatio = 1.03;   // how wrong the simulated tap is
  double bassAmp = 0.55, amp = 2.0e8;
  for (int i = 1; i < argc; i++) {
    std::string a = argv[i];
    auto val = [&]() { return atof(argv[++i]); };
    if      (a == "--bpm")  { bpm = val(); bpmGiven = true; }
    else if (a == "--secs") secs = val();
    else if (a == "--sens") sens = (int)val();
    else if (a == "--bass") bassAmp = val();
    else if (a == "--amp")  amp = val();
    else if (a == "--auto") autogain = true;
    else if (a == "--mode") mode = argv[++i];
    // Every detector parameter reachable from the command line, so a sweep is a shell loop.
    else if (a == "--brl") ovRel      = (int)val();
    else if (a == "--brf") ovRefShift = (int)val();
    else if (a == "--blo") ovKLo      = (int)val();
    else if (a == "--bhi") ovKHi      = (int)val();
    else if (a == "--blk") ovLockout  = (int)val();
    else if (a == "--vmp") ovVarMin   = (int)val();
    else if (a == "--mrp") ovMinRange = (int)val();
    else if (a == "--bst") ovBoostMax = (int)val();
    else if (a == "--bsh") ovBoostSh  = (int)val();
    else if (a == "--pfp") ovPeakFall = (int)val();
    else if (a == "--pmw") ovPeakWait = (int)val();
    else if (a == "--tw")  ovWindow   = (int)val();
    else if (a == "--file") wavPath = argv[++i];
    else if (a == "--beats") beatsPath = argv[++i];
    else if (a == "--wavgain") wavGain = val();
    else if (a == "--pattern") patName = argv[++i];
    else if (a == "--win") winMs = val();
    else if (a == "--pc") gPriorCentre = val();
    else if (a == "--ps") gPriorSigma = val();
    else if (a == "--ar") anchorRatio = val();   // anchor = truth * this (1.03 = a good tap)
    else if (a == "--as") gAnchorSigma = val();
  }

  if (wavPath) {
    if (!wav.load(wavPath)) { fprintf(stderr, "could not read WAV: %s\n", wavPath); return 1; }
  }
  if (beatsPath) {
    std::vector<double> ann;
    if (!loadBeatAnnotations(beatsPath, ann)) {
      fprintf(stderr, "could not read beat annotations: %s\n", beatsPath); return 1;
    }
    // Replaces the synthetic grid outright. Scoring, the timing error and the BPM comparison all
    // read track.trueBeats, so this is the single point where real ground truth enters.
    gAnnotatedBeats = ann;
    track.trueBeats = ann;
    bpm = annotatedBPM(ann);
    if (bpm <= 0) { fprintf(stderr, "beat annotations do not yield a tempo: %s\n", beatsPath); return 1; }
    fprintf(stderr, "annotations: %zu beats, %.1f s ... %.1f s, median %.1f BPM\n",
            ann.size(), ann.front(), ann.back(), bpm);
    if (!wavPath) fprintf(stderr, "warning: --beats without --file scores the SYNTHETIC track\n");
    wav.gain = wavGain;
  }

  if (!patName.empty() && mode != "compare") {
    gPattern = findPattern(patName);
    if (!gPattern) { fprintf(stderr, "unknown pattern: %s\n", patName.c_str()); return 1; }
  }

  if (mode == "compare") {
    // The question this mode exists to answer: on material where the kicks do NOT sit on the beat
    // grid, which estimator still finds the beat? Every method below is handed the identical onset
    // train from one run, evaluated over identical rolling windows, so the only difference between
    // the columns is the method.
    std::vector<const Pattern*> pats;
    if (!patName.empty()) {
      const Pattern* pp = findPattern(patName);
      if (!pp) { fprintf(stderr, "unknown pattern: %s\n", patName.c_str()); return 1; }
      pats.push_back(pp);
    } else for (auto& q : kPatterns) pats.push_back(&q);

    std::vector<double> bpms;
    if (bpmGiven) bpms.push_back(bpm); else bpms = { 90, 98, 120, 128, 140, 174 };

    printf("Window %.0f ms, %.0f s per run, sens=%d%s\n\n", winMs, secs, sens,
           autogain ? ", AUTO-GAIN" : "");
    for (auto& q : kPatterns) printf("  %-7s %s\n", q.name, q.note);
    printf("\n  Columns: reported BPM, ratio to ground truth, share of correct windows\n");
    printf("  Firmware = trackedBPM straight out of the engine; Median = the same method rebuilt\n");
    printf("  here, as a cross-check that this harness itself measures cleanly.\n\n");

    // Five methods. The last two differ from the middle two ONLY in which onsets they are given,
    // which is the comparison that matters: method versus material.
    const int NM = 5;
    struct Tot { int windows = 0, ok = 0; } tot[NM];
    const char* names[NM] = { "Median B", "ACF+H B", "+Prior B", "+Prior A", "+Anchor A" };
    EstKind kinds[NM]     = { EST_MEDIAN, EST_ACFH, EST_ACFHP, EST_ACFHP, EST_ACFHA };
    bool    useAll[NM]    = { false, false, false, true, true };

    printf("  B = bass only (what the firmware uses today), A = bass + mid + high combined\n\n");
    printf("  Pattern  true  Onsets  Firmware   ");
    for (int k = 0; k < NM; k++) printf(" %-12s", names[k]);
    printf("\n");
    for (const Pattern* pp : pats) {
      for (double b : bpms) {
        gPattern = pp;
        // A tap 3% off the truth -- deliberately imperfect, or the column would be measuring
        // the ground truth being handed to the estimator rather than the anchor mechanism.
        gAnchorBPM = b * anchorRatio;
        resetEngine(sens, autogain);
        track.bpm = b; track.bassAmp = bassAmp; track.masterAmp = amp;
        Result r = run(secs);
        int fw = trackedBPM;
        printf("  %-7s %4.0f  %6zu  %3d %-7s", pp->name, b, r.onsets.size(), fw, relation(fw, b));
        if (r.onsets.size() < 8 || r.onsetsAll.size() < 8) { printf(" too few onsets\n"); continue; }
        double s0 = r.onsetsAll.front(), s1 = r.onsetsAll.back();
        for (int k = 0; k < NM; k++) {
          const std::vector<double>& src = useAll[k] ? r.onsetsAll : r.onsets;
          std::vector<int> reads; int ok = 0;
          for (double t = s0 + winMs; t <= s1; t += 500.0) {
            int v = estimate(kinds[k], src, t - winMs, t);
            if (!v) continue;
            reads.push_back(v);
            if (fabs(v - b) / b < 0.04) ok++;
          }
          tot[k].windows += (int)reads.size(); tot[k].ok += ok;
          if (reads.empty()) { printf(" %-12s", "--"); continue; }
          std::sort(reads.begin(), reads.end());
          int med = reads[reads.size()/2];
          char cell[40];
          snprintf(cell, sizeof cell, "%3d %-4s%3.0f%%", med, relation(med, b),
                   100.0 * ok / reads.size());
          printf(" %-12s", cell);
        }
        printf("\n");
      }
      printf("\n");
    }
    printf("  Overall (share of correct windows across all cases):\n");
    for (int k = 0; k < NM; k++)
      printf("    %-10s %3.0f%%  (%d of %d windows)\n", names[k],
             tot[k].windows ? 100.0 * tot[k].ok / tot[k].windows : 0.0, tot[k].ok, tot[k].windows);
    return 0;
  }

  if (mode == "bands") {
    // ONE run with all three detectors live, which is how the device actually runs them --
    // not three separate runs, which would not show them competing for the same samples.
    resetEngine(sens, autogain);
    if (wavPath) { wav.pos = 0; wav.gain = wavGain; }
    track.bpm = bpm; track.bassAmp = bassAmp; track.masterAmp = amp;

    struct Obs { const char* name; SdBand* b; const char* range; uint32_t last; std::vector<double> on; };
    Obs obs[] = {
      { "Bass", &sdBass, "40-159 Hz",     0, {} },
      { "Mid",  &sdMid,  "159-637 Hz",    0, {} },
      { "High", &sdHigh, "above 1273 Hz", 0, {} },
    };
    std::mt19937 lr{99};
    while (simMicros < (uint64_t)(secs * 1e6)) {
      simMicros += 900 + (lr() % 400);
      if (lr() % 250 == 0) simMicros += 15000;
      pollAudioEngine();
      for (auto& o : obs)
        if (o.b->lastOnsetMs != o.last) {
          o.last = o.b->lastOnsetMs;
          if (simMicros > 500000) o.on.push_back((double)o.last);
        }
    }

    printf("File: %s\n", wavPath ? wavPath : "(synthetic)");
    if (wav.loaded) printf("  %.1f s Material, auf %d Hz gewandelt, %.0fx geschleift auf %.0fs\n",
                           wav.lengthSec(), SAMPLING_FREQUENCY, secs / wav.lengthSec(), secs);
    printf("  ig=%d, dropped samples %ld, clock drift %d per mille\n\n",
           tuneInputGainShift, simI2sDropped, sdClkDriftPpt);
    printf("  Band   Range            Onsets  per s   Gap       gives    Spread\n");
    for (auto& o : obs) {
      std::vector<double> gaps;
      for (size_t i = 1; i < o.on.size(); i++) {
        double d = o.on[i] - o.on[i-1];
        if (d > 40 && d < 2000) gaps.push_back(d);
      }
      if (gaps.size() < 4) { printf("  %-5s  %-15s  %6zu   zu wenige\n", o.name, o.range, o.on.size()); continue; }
      std::sort(gaps.begin(), gaps.end());
      double med = gaps[gaps.size()/2], dev = 0;
      for (double g : gaps) dev += fabs(g - med);
      dev /= gaps.size();
      printf("  %-5s  %-15s  %6zu  %5.2f   %5.0f ms  %6.1f    %5.0f%%\n",
             o.name, o.range, o.on.size(), o.on.size()/secs, med, 60000.0/med, dev/med*100);
    }
    // Where do Mid and High actually land relative to the kick? "On the offbeat" and "on every
    // eighth" produce the same interval but very different lighting, so measure the phase rather
    // than infer it: for each onset, how far after the previous bass onset it sits, as a
    // fraction of the bass interval.
    {
      std::vector<double>& bass = obs[0].on;
      std::vector<double> bg;
      for (size_t i = 1; i < bass.size(); i++) {
        double d = bass[i] - bass[i-1];
        if (d > 40 && d < 2000) bg.push_back(d);
      }
      if (bg.size() >= 4 && bass.size() >= 4) {
        std::sort(bg.begin(), bg.end());
        double beat = bg[bg.size()/2];
        printf("\n  Phasenlage zum Kick (Beat = %.0f ms), in Achteln des Beats:\n", beat);
        printf("  %-6s", "Band");
        for (int k = 0; k < 8; k++) printf("%6.2f", k / 8.0);
        printf("\n");
        for (int bi = 1; bi < 3; bi++) {
          int bins[8] = {0};
          for (double o : obs[bi].on) {
            // nearest preceding bass onset
            double prev = -1;
            for (double b : bass) { if (b <= o + 20) prev = b; else break; }
            if (prev < 0) continue;
            double ph = (o - prev) / beat;
            ph -= floor(ph);
            bins[(int)(ph * 8) % 8]++;
          }
          int tot = 0; for (int k = 0; k < 8; k++) tot += bins[k];
          printf("  %-6s", obs[bi].name);
          for (int k = 0; k < 8; k++) printf("%5.0f%%", tot ? 100.0 * bins[k] / tot : 0.0);
          printf("\n");
        }
      }
    }
    printf("\n  globalBPM %d\n", globalBPM);
    return 0;
  }


  if (mode == "psy") {
    // Ablation of the psychoacoustic front end. Every variant is fed through the SAME tempo
    // estimator the firmware runs (median of the gaps, with the interval folding), so the only
    // thing that varies between columns is the onset detection function. That is the comparison
    // worth making: method against material, not method against a different estimator.
    std::vector<const Pattern*> pats;
    if (gPattern) pats.push_back(gPattern); else for (auto& q : kPatterns) pats.push_back(&q);
    std::vector<double> bpms = bpmGiven ? std::vector<double>{ bpm }
                                        : std::vector<double>{ 90, 98, 120, 128, 140, 174 };
    PsyCfg variants[4];
    // Bands 0..4 on the Bark scale are 20..510 Hz -- the kick's range. The full-band variants
    // are kept alongside because the difference between them is the finding, not a detail.
    variants[0] = PsyCfg{ true,  false, false, 1, FFT_N, 384, 24, 60, 3, 0, 99, "bark full" };
    variants[1] = PsyCfg{ true,  true,  true,  1, FFT_N, 384, 24, 60, 3, 0, 99, "SFlux full" };
    variants[2] = PsyCfg{ true,  true,  false, 1, FFT_N, 384, 24, 60, 3, 0,  4, "log bass"   };
    variants[3] = PsyCfg{ true,  true,  true,  1, FFT_N, 384, 24, 60, 3, 0,  4, "SFlux bass" };
    const int NV = 4;

    printf("Psychoacoustic front end, ablated. %.0f s per run, sens=%d, window %.0f ms\n\n",
           secs, sens, winMs);
    printf("  Every column uses the firmware's tempo estimator; only the onset function differs.\n");
    printf("  Firmware = the sample-rate detector on the device today (time domain, bass band).\n");
    printf("  bark full  = spectral flux over all %d Bark bands, linear magnitude\n", kNumBark);
    printf("  SFlux full = + log compression + SuperFlux max filter (Boeck & Widmer, DAFx-13)\n");
    printf("  log bass   = log compression, Bark bands 0..4 only (20..510 Hz, the kick)\n");
    printf("  SFlux bass = + the SuperFlux max filter, same band range\n\n");

    struct Tot { int windows = 0, ok = 0; size_t onsets = 0; } totFw, totAcf, tot[NV];
    printf("  %-7s %5s  %-14s %-14s", "Pattern", "true", "Firmware", "SFlux+ACF");
    for (int v = 0; v < NV; v++) printf(" %-14s", variants[v].name);
    printf("\n");

    for (const Pattern* pp : pats) {
      for (double b : bpms) {
        gPattern = pp;
        resetEngine(sens, autogain);
        track.bpm = b; track.bassAmp = bassAmp; track.masterAmp = amp;
        Result r = run(secs);
        printf("  %-7s %5.0f ", pp->name, b);

        auto sweep = [&](const std::vector<double>& on, Tot& acc, EstKind kind = EST_MEDIAN) {
          if (on.size() < 8) { printf(" %-14s", "-- (few)"); return; }
          double s0 = on.front(), s1 = on.back();
          int w = 0, ok = 0;
          for (double t = s0 + winMs; t <= s1; t += 500.0) {
            int val = estimate(kind, on, t - winMs, t);
            if (!val) continue;
            w++;
            if (fabs(val - b) / b < 0.04) ok++;
          }
          acc.windows += w; acc.ok += ok; acc.onsets += on.size();
          char buf[32];
          snprintf(buf, sizeof buf, "%4zu on %3d%%", on.size(), w ? ok * 100 / w : 0);
          printf(" %-14s", buf);
        };

        sweep(r.onsets, totFw);
        {
          // The decisive pairing: a broadband onset function with the estimator the literature
          // actually pairs it with. A median of the gaps assumes every gap IS the beat, which is
          // only true for a deliberately narrowband detector -- feed it a detector that hears
          // the hi-hats too and it reads half the period. Autocorrelation with harmonic summing
          // asks "which period explains ALL of these", which is the question a dense onset
          // function can answer.
          PsyCfg best{ true, true, true, 1, FFT_N, 384, 24, 60, 3, 0, 99, "SFlux+ACF" };
          track.bpm = b; track.bassAmp = bassAmp; track.masterAmp = amp;
          if (gPattern) track.kickPos = gPattern->pos;
          sweep(psyRun(secs, best), totAcf, EST_ACFHP);
        }
        for (int v = 0; v < NV; v++) {
          // psyRun rewinds the source itself and keeps its configuration; do not call
          // resetEngine() here, it would put the Track back to defaults.
          track.bpm = b; track.bassAmp = bassAmp; track.masterAmp = amp;
          if (gPattern) track.kickPos = gPattern->pos;
          sweep(psyRun(secs, variants[v]), tot[v]);
        }
        printf("\n");
      }
    }
    printf("\n  Overall (share of correct windows, and total onsets found):\n");
    printf("    %-11s %3d%%  (%d of %d windows, %zu onsets)\n", "Firmware",
           totFw.windows ? totFw.ok * 100 / totFw.windows : 0, totFw.ok, totFw.windows, totFw.onsets);
    printf("    %-11s %3d%%  (%d of %d windows)  <- broadband ODF + autocorrelation\n", "SFlux+ACF",
           totAcf.windows ? totAcf.ok * 100 / totAcf.windows : 0, totAcf.ok, totAcf.windows);
    for (int v = 0; v < NV; v++)
      printf("    %-11s %3d%%  (%d of %d windows, %zu onsets)\n", variants[v].name,
             tot[v].windows ? tot[v].ok * 100 / tot[v].windows : 0,
             tot[v].ok, tot[v].windows, tot[v].onsets);
    return 0;
  }


  if (mode == "psyscore") {
    // Scores the psychoacoustic ODF's ONSETS against ground truth, rather than scoring the tempo
    // derived from them. Needed to tell two very different failures apart: "it does not hear the
    // events" and "it hears them all, including the ones that are not the beat".
    if (track.trueBeats.empty() && !wav.loaded) {
      fprintf(stderr, "psyscore needs --file with --beats, or a synthetic pattern\n"); return 1;
    }
    resetEngine(sens, autogain);
    track.bpm = bpm; track.bassAmp = bassAmp; track.masterAmp = amp;
    if (gPattern) track.kickPos = gPattern->pos;
    Result r = run(secs);
    std::vector<double> truth = track.trueBeats;
    double span = truth.empty() ? secs : (truth.back() - truth.front());
    printf("Truth: %zu events over %.1f s = %.2f/s\n\n", truth.size(), span,
           span > 0 ? truth.size() / span : 0);
    printf("  %-12s %7s %7s  %6s %6s %6s   %s\n",
           "variant", "onsets", "per s", "prec", "recall", "F", "offset");
    auto show = [&](const char* nm, const std::vector<double>& on) {
      Score sc = score(on, truth);
      printf("  %-12s %7zu %7.2f  %5.0f%% %5.0f%% %6.3f   %+.1f ms\n", nm, on.size(),
             on.size() / secs, sc.p * 100, sc.rec * 100, sc.f, sc.offset);
    };
    // Tempo from the same onsets, through both estimators, so the pairing question is answered
    // on this signal too and not only on the synthetic patterns.
    double truthBpm = annotatedBPM(truth);
    auto tempoOf = [&](const std::vector<double>& on, EstKind k) {
      if (on.size() < 8) return 0;
      double s0 = on.front(), s1 = on.back();
      std::vector<int> v;
      for (double t = s0 + 8000.0; t <= s1; t += 500.0) {
        int e = estimate(k, on, t - 8000.0, t);
        if (e) v.push_back(e);
      }
      if (v.empty()) return 0;
      std::sort(v.begin(), v.end());
      return v[v.size() / 2];
    };
    show("firmware", r.onsets);
    struct V { PsyCfg c; };
    // w1 (the field before the band range) is the peak picker's lookahead in FRAMES. On the
    // device each frame is 32ms of added latency, so it is not a free parameter: w1=3 costs
    // ~96ms, about a fifth of a beat at 128 BPM. Two causal-er variants are measured alongside.
    const int NPSY = 6;
    PsyCfg vs[NPSY];
    vs[0] = PsyCfg{ true, false, false, 1, FFT_N, 384, 24, 60, 3, 0, 99, "bark full" };
    vs[1] = PsyCfg{ true, true,  false, 1, FFT_N, 384, 24, 60, 3, 0, 99, "+log" };
    vs[2] = PsyCfg{ true, true,  true,  1, FFT_N, 384, 24, 60, 3, 0, 99, "+max (SFlux)" };
    vs[3] = PsyCfg{ true, true,  true,  1, FFT_N, 384, 24, 60, 3, 0,  4, "SFlux bass" };
    vs[4] = PsyCfg{ true, true,  true,  1, FFT_N, 384, 24, 60, 2, 0, 99, "SFlux w1=2" };
    vs[5] = PsyCfg{ true, true,  true,  1, FFT_N, 384, 24, 60, 1, 0, 99, "SFlux w1=1" };
    for (int i = 0; i < NPSY; i++) {
      track.bpm = bpm; track.bassAmp = bassAmp; track.masterAmp = amp;
      if (gPattern) track.kickPos = gPattern->pos;
      show(vs[i].name, psyRun(secs, vs[i]));
    }
    printf("\n  Tempo from these onsets (median of 8s windows), truth %.1f BPM:\n", truthBpm);
    printf("    %-14s median %3d   ACF+H+prior %3d\n", "firmware",
           tempoOf(r.onsets, EST_MEDIAN), tempoOf(r.onsets, EST_ACFHP));
    for (int i = 0; i < NPSY; i++) {
      track.bpm = bpm; track.bassAmp = bassAmp; track.masterAmp = amp;
      if (gPattern) track.kickPos = gPattern->pos;
      std::vector<double> on = psyRun(secs, vs[i]);
      printf("    %-14s median %3d   ACF+H+prior %3d\n", vs[i].name,
             tempoOf(on, EST_MEDIAN), tempoOf(on, EST_ACFHP));
    }
    return 0;
  }


  if (mode == "psyband") {
    // How much BANDWIDTH does the onset function actually need? This decides whether the input
    // can be decimated before the FFT, which is by far the cheapest lever available: keeping the
    // window length in seconds constant while decimating by D leaves the bin width fs/N
    // unchanged, so the low-frequency resolution is identical -- but the transform shrinks from
    // N log N to (N/D) log(N/D). Everything above fs/2D simply stops existing.
    if (track.trueBeats.empty()) {
      fprintf(stderr, "psyband needs --file with --beats\n"); return 1;
    }
    std::vector<double> truth = track.trueBeats;
    printf("Bandwidth sweep. Truth %zu beats, %.1f BPM.\n", truth.size(), annotatedBPM(truth));
    printf("Bark band k covers up to %d Hz; the FFT could be decimated to just above that.\n\n",
           kBarkEdgesHz[kNumBark]);
    printf("  %-9s %-13s %6s  %5s %6s %6s   %s\n",
           "bands", "top edge", "onsets", "prec", "recall", "F", "min fs for it");
    for (int hi : { 2, 3, 4, 5, 6, 8, 10, 13, 16, 21 }) {
      PsyCfg c{ true, true, true, 1, FFT_N, 384, 24, 60, 3, 0, hi, "band" };
      track.bpm = bpm; track.bassAmp = bassAmp; track.masterAmp = amp;
      std::vector<double> on = psyRun(secs, c);
      Score sc = score(on, truth);
      int topHz = kBarkEdgesHz[std::min(hi + 1, kNumBark)];
      char rng[16]; snprintf(rng, sizeof rng, "0..%d", hi);
      printf("  %-9s %5d Hz      %6zu  %4.0f%% %5.0f%% %6.3f   %5d Hz (/%d)\n",
             rng, topHz, on.size(), sc.p * 100, sc.rec * 100, sc.f,
             2 * topHz, SAMPLING_FREQUENCY / std::max(1, 2 * topHz));
    }
    return 0;
  }

  if (mode == "csv") {          // one line of numbers, for shell-driven sweeps
    resetEngine(sens, autogain);
    track.bpm = bpm; track.bassAmp = bassAmp; track.masterAmp = amp;
    Result r = run(secs);
    Score s = score(r.onsets, track.trueBeats);
    printf("%.0f %zu %.0f %.0f %.3f %.1f %d %ld %d\n", bpm, r.onsets.size(), s.p * 100,
           s.rec * 100, s.f, s.err, medianBPM(r.bpmReadings), r.drops, r.drift);
    return 0;
  }

  if (mode == "single") {
    resetEngine(sens, autogain);
    track.bpm = bpm; track.bassAmp = bassAmp; track.masterAmp = amp;
    Result r = run(secs);
    Score s = score(r.onsets, track.trueBeats);
    printf("Track %.1f BPM (beat %.1f ms), %.0fs, sens=%d, bass=%.2f, level=%.1e%s\n",
           bpm, 60000.0 / bpm, secs, sens, bassAmp, amp, autogain ? ", AUTO-GAIN" : "");
    printf("  Onsets %zu against %zu true beats\n", r.onsets.size(), track.trueBeats.size());
    printf("  Precision %.0f%% / Recall %.0f%% / F %.3f\n", s.p * 100, s.rec * 100, s.f);
    printf("  Timing error %.1f ms (constant offset %.1f ms)\n", s.err, s.offset);
    printf("  Reported BPM %d (median), true %.0f\n", medianBPM(r.bpmReadings), bpm);
    printf("  Dropped samples %ld, clock drift %d per mille, ig=%d\n",
           r.drops, r.drift, tuneInputGainShift);
    printf("  internal: pk=%ld clip=%d env=%ld floor=%ld peak=%ld thr=%ld dyn=%ld trans=%d\n",
           (long)micPeak, micClipCount, (long)sdEnv, (long)sdFloor, (long)sdPeakStat,
           (long)sdThrBlock, (long)(sdHasDynamics?1:0), sdTransient ? 1 : 0);
    printf("  Bands: lo=%ld mi=%ld hi=%ld  mad=%ld mean=%ld\n",
           (long)lastBassEnergy, (long)lastMidEnergy, (long)lastHighEnergy,
           (long)sdVarMad, (long)sdVarMean);
    return 0;
  }

  if (mode == "trace") {
    resetEngine(sens, autogain);
    track.bpm = bpm; track.bassAmp = bassAmp; track.masterAmp = amp;
    std::mt19937 lr{99};
    uint32_t lastOn = 0; uint32_t lastBlock = 0;
    printf("   t(ms)   env   floor    thr  boost arm trans dyn  peaking  onset\n");
    while (simMicros < (uint64_t)(secs * 1e6)) {
      simMicros += 900 + (lr() % 400);
      uint32_t before = sdSampleClock;
      pollAudioEngine();
      if (sdSampleClock != before) {           // a block was processed
        lastBlock++;
        if (lastBlock % 2 == 0)
          printf("  %6llu %6ld  %6ld %6ld  %5ld  %d   %d    %d      %d   %s\n",
                 (unsigned long long)(simMicros / 1000), (long)sdEnv, (long)sdFloor,
                 (long)((sdThrBlock * sdBoost) >> 8), (long)sdBoost, sdArmed ? 1 : 0,
                 sdTransient ? 1 : 0, sdHasDynamics ? 1 : 0, sdPeaking ? 1 : 0,
                 sdLastOnsetMs != lastOn ? "<== ONSET" : "");
        if (sdLastOnsetMs != lastOn) lastOn = sdLastOnsetMs;
      }
    }
    return 0;
  }

  if (mode == "tempo") {
    printf("  BPM   Onsets  Precis.  Recall    F     TimingErr   reported  dropped    drift\n");
    for (double b : {90.0, 110.0, 125.0, 130.0, 133.0, 140.0, 150.0, 174.0}) {
      resetEngine(sens, autogain);
      track.bpm = b; track.bassAmp = bassAmp; track.masterAmp = amp;
      Result r = run(secs);
      Score s = score(r.onsets, track.trueBeats);
      printf("  %5.0f  %5zu   %5.0f%%   %5.0f%%  %.3f   %6.1f ms  %8d  %9ld  %4d\n",
             b, r.onsets.size(), s.p * 100, s.rec * 100, s.f, s.err,
             medianBPM(r.bpmReadings), r.drops, r.drift);
    }
    return 0;
  }

  if (mode == "sens") {
    printf("  sens  Onsets  Precis.  Recall    F     TimingErr   reported\n");
    for (int sv = 0; sv <= 100; sv += 10) {
      resetEngine(sv, autogain);
      track.bpm = bpm; track.bassAmp = bassAmp; track.masterAmp = amp;
      Result r = run(secs);
      Score s = score(r.onsets, track.trueBeats);
      printf("  %4d  %5zu   %5.0f%%   %5.0f%%  %.3f   %6.1f ms  %8d\n",
             sv, r.onsets.size(), s.p * 100, s.rec * 100, s.f, s.err, medianBPM(r.bpmReadings));
    }
    return 0;
  }

  if (mode == "level") {
    printf("  Level    ig  Onsets  Precis.  Recall    F     reported  clipping\n");
    for (double a : {2.0e7, 5.0e7, 1.0e8, 2.0e8, 5.0e8, 1.0e9}) {
      resetEngine(sens, autogain);
      track.bpm = bpm; track.bassAmp = bassAmp; track.masterAmp = a;
      Result r = run(secs);
      Score s = score(r.onsets, track.trueBeats);
      printf("  %.1e  %2d  %5zu   %5.0f%%   %5.0f%%  %.3f  %8d  %8d\n",
             a, tuneInputGainShift, r.onsets.size(), s.p * 100, s.rec * 100, s.f,
             medianBPM(r.bpmReadings), micClipCount);
    }
    return 0;
  }
  fprintf(stderr, "unknown mode: %s\n", mode.c_str());
  return 1;
}
