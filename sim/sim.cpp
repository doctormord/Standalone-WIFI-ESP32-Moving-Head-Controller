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

  static double env(double t, double tau) { return t < 0 ? 0.0 : exp(-t / tau); }

  double next() {
    double t  = (double)n / sr;
    double bp = 60.0 / bpm;

    long bi = (long)floor(t / bp);
    if (bi != beatIdx) {                       // a beat starts here
      beatIdx = bi;
      kickT = 0; kickPhase = 0;
      trueBeats.push_back(bi * bp);
      if (bi % 4 == 1 || bi % 4 == 3) { snareT = 0; snarePhase = 0; }
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
// Real audio, when a file is given instead of the synthesiser. Mono-summed and
// resampled to the device's 16kHz, with a lowpass first: 44.1k -> 16k without one
// would fold everything above 8kHz straight down into the bands we detect on.
// ---------------------------------------------------------------------------
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
  std::vector<double> onsets;      // sample-clock ms, as the firmware timestamps them
  std::vector<int>    bpmReadings;
  long   drops = 0;
  int    drift = 0;
};

Result run(double seconds) {
  Result r;
  std::mt19937 loopRng{99};
  uint32_t lastOnset = 0;
  double nextSampleAt = 0.5;   // let the detector settle before recording anything

  while (simMicros < (uint64_t)(seconds * 1e6)) {
    // The measured device loop: about a millisecond, with occasional longer spikes when the DMX
    // frame and the web server land in the same iteration (loopMax read 16-20ms on hardware).
    uint64_t step = 900 + (loopRng() % 400);
    if (loopRng() % 250 == 0) step += 15000;
    simMicros += step;

    pollAudioEngine();

    if (sdLastOnsetMs != lastOnset) {
      lastOnset = sdLastOnsetMs;
      if (simMicros > nextSampleAt * 1e6) r.onsets.push_back((double)sdLastOnsetMs);
    }
    static uint64_t lastBpmAt = 0;
    if (simMicros - lastBpmAt > 200000) { lastBpmAt = simMicros; r.bpmReadings.push_back(globalBPM); }
  }
  r.drops = simI2sDropped;
  r.drift = sdClkDriftPpt;
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

void resetEngine(int sens, bool autogain) {
  // Fresh state for every run, so one case cannot colour the next.
  simMicros = 0; track = Track();
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
  trackedBPM = 0; globalBPM = 120; ioiIdx = ioiCount = 0;
  hwAudioEnabled = true; hwAudioSensitivity = sens; autoGain = autogain;
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
}

int main(int argc, char** argv) {
  double bpm = 130, secs = 60, wavGain = 1.0;
  const char* wavPath = nullptr;
  int sens = 60; bool autogain = false; std::string mode = "single";
  double bassAmp = 0.55, amp = 2.0e8;
  for (int i = 1; i < argc; i++) {
    std::string a = argv[i];
    auto val = [&]() { return atof(argv[++i]); };
    if      (a == "--bpm")  bpm = val();
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
    else if (a == "--wavgain") wavGain = val();
  }

  if (wavPath) {
    if (!wav.load(wavPath)) { fprintf(stderr, "WAV konnte nicht gelesen werden: %s\n", wavPath); return 1; }
    wav.gain = wavGain;
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
      { "High", &sdHigh, "ueber 1273 Hz", 0, {} },
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

    printf("Datei: %s\n", wavPath ? wavPath : "(synthetisch)");
    if (wav.loaded) printf("  %.1f s Material, auf %d Hz gewandelt, %.0fx geschleift auf %.0fs\n",
                           wav.lengthSec(), SAMPLING_FREQUENCY, secs / wav.lengthSec(), secs);
    printf("  ig=%d, verworfene Samples %ld, Uhrenabweichung %d Promille\n\n",
           tuneInputGainShift, simI2sDropped, sdClkDriftPpt);
    printf("  Band   Bereich          Onsets  pro s   Abstand   ergibt   Streuung\n");
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
    printf("Track %.1f BPM (Beat %.1f ms), %.0fs, sens=%d, Bass=%.2f, Pegel=%.1e%s\n",
           bpm, 60000.0 / bpm, secs, sens, bassAmp, amp, autogain ? ", AUTO-GAIN" : "");
    printf("  Onsets %zu bei %zu echten Beats\n", r.onsets.size(), track.trueBeats.size());
    printf("  Treffer %.0f%% / Vollstaendigkeit %.0f%% / F %.3f\n", s.p * 100, s.rec * 100, s.f);
    printf("  Zeitfehler %.1f ms (konstanter Versatz %.1f ms)\n", s.err, s.offset);
    printf("  gemeldete BPM %d (Median), wahr %.0f\n", medianBPM(r.bpmReadings), bpm);
    printf("  verworfene Samples %ld, Uhrenabweichung %d Promille, ig=%d\n",
           r.drops, r.drift, tuneInputGainShift);
    printf("  intern: pk=%ld clip=%d env=%ld floor=%ld peak=%ld thr=%ld dyn=%ld trans=%d fft=%d sd=%d\n",
           (long)micPeak, micClipCount, (long)sdEnv, (long)sdFloor, (long)sdPeakStat,
           (long)sdThrBlock, (long)(sdHasDynamics?1:0), sdTransient ? 1 : 0, audioUseFFT ? 1 : 0, sdEnabled ? 1 : 0);
    printf("  Baender: lo=%ld mi=%ld hi=%ld  mad=%ld mean=%ld\n",
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
    printf("  BPM   Onsets  Treffer  Vollst.   F     Zeitfehler  gemeldet  verworfen  Drift\n");
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
    printf("  sens  Onsets  Treffer  Vollst.   F     Zeitfehler  gemeldet\n");
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
    printf("  Pegel    ig  Onsets  Treffer  Vollst.   F     gemeldet  Clipping\n");
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
  fprintf(stderr, "unbekannter Modus: %s\n", mode.c_str());
  return 1;
}
