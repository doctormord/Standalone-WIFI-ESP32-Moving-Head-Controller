# Host simulator for the beat detector

Compiles the real `Audio_Engine.h` natively and drives it from a synthesised track whose beat
positions are known exactly. No microcontroller involved.

    cd sim && c++ -std=c++17 -O2 -I fake -I .. -o simbeat sim.cpp
    ./simbeat --bpm 130 --secs 60           # one run, full report
    ./simbeat --mode tempo --sens 100       # across 90..174 BPM
    ./simbeat --mode sens                   # across the sensitivity range
    ./simbeat --mode level --auto           # across a 50x input level range
    ./simbeat --mode trace --secs 4         # per-block state, for debugging
    ./simbeat --mode csv ...                # one line of numbers, for shell sweeps

## Validation corpus

    python3 mktracks.py tracks          # seven annotated tracks, 70..174 BPM
    ./simbeat --mode psyscore --file tracks/techno-150.wav --beats tracks/techno-150.beats

Deliberately away from 120 BPM and built around what actually breaks onset detectors: sustained
bass that never returns to silence, pads with no transient, swung timing, syncopated kicks,
vibrato, and a breakdown with no drums in it. The annotation is the BEAT GRID, not the kick
positions -- on syncopated material those differ, and conflating them credits a detector for
finding a pulse nobody hears. `tracks/` is gitignored; regenerate it, it is deterministic.

## Psychoacoustic front end (prototype, not firmware)

    ./simbeat --mode psy                                  # ablation, all patterns x tempi
    ./simbeat --mode psyscore --file X.wav --beats X.txt  # onset quality vs ground truth

A Bark-band / log-compressed / SuperFlux onset function built on the firmware's own integer FFT,
with each of the three perceptual steps switchable so its contribution can be measured on its
own. See `doc/content/proposal.md` section 5 for the measurements and for why none of it has
been ported. Short version: it hears everything (recall 96-99%), it hears more than the beat
(precision ~50% against kick-only truth, because the hi-hats really are there), and that makes
it incompatible with the median-of-gaps tempo estimator the device runs — a broadband onset
function needs autocorrelation, which is a different pipeline, not an upgrade.

The one clean win: logarithmic compression cuts the systematic timing offset from +14.2 ms to
+2.1 ms, beating the firmware's +10.7 ms on the same signal against the same ground truth.

## Scoring against a real recording

The synthesised track carries its own ground truth, so precision/recall/F are free there. For a
real file the beat positions have to be supplied, otherwise the only thing that can be compared
is the reported BPM — and that was how a conclusion drawn here (81%) survived until the device
disagreed with it (7%).

    ffmpeg -i track.mp3 -ac 1 -ar 16000 -f wav track.wav     # mp3 / stream -> what --file wants
    ./simbeat --mode single --file track.wav --beats track.beats --secs 60

`--beats` takes one beat time in **seconds** per line. A second tab- or space-separated column
is ignored, so an Audacity label export works unchanged; blank lines and `#` comments are
skipped. The annotated median inter-beat interval becomes the true tempo — `--bpm` describes the
synthetic generator and means nothing for a real file.

The reported constant offset is worth reading: it is the detector's own latency, and it only
becomes visible once there is real ground truth to measure against.
    ./simbeat --mode bands --file X.wav     # real audio, the three bands side by side
    ./simbeat --mode compare                # tempo estimators against each other
    ./simbeat --mode compare --pattern hiphop --secs 60

`--pattern four|hiphop|broken|dnb|half` chooses where the kicks sit inside the bar; the beat grid
stays the ground truth regardless, which is the whole point (see below). `--win` sets the
estimator's analysis window, `--pc`/`--ps` the tempo prior's centre and width, `--ar`/`--as` the
simulated tap anchor's error and the width of its prior.

Every detector parameter can be overridden from the command line (`--brl`, `--brf`, `--blo`,
`--bhi`, `--blk`, `--vmp`, `--mrp`, `--bst`, `--bsh`, `--pfp`, `--pmw`, `--tw`), so a sweep is a
shell loop. Set `LC_ALL=C` before parsing the output with awk, or a comma decimal separator will
silently read as zero.

## Why it exists

Three separate tuning runs on hardware were invalidated by the music changing underneath them --
the same setting measured 2.40 onsets/s and, minutes later, 0.98/s. Worse, a lockout tuned
against the then-current beat interval made a free-running oscillator look like a working
detector, and that was reported as a breakthrough. Here the ground truth is exact, a run is
deterministic, and a sweep takes seconds.

It found three real bugs within minutes of first running:

- `sdMinLevel = floor * 2`, borrowed from gibbedy/BeatDetector, was unsatisfiable: its "average"
  is an FFT bin magnitude that falls near zero between beats, ours is the median of an envelope
  that never falls that far. The gate sat above the envelope's own maximum, so nothing ever fired.
- The soft refractory never decayed. `sdBoost -= (sdBoost - 256) >> 11` in Q8 shifts a range of
  768 by 11 bits, which is always zero -- the threshold stayed at four times normal forever and
  the detector went deaf after its first onset. The same integer-deadband class of bug that had
  frozen the comparator reference.
- The 128ms envelope release reported half tempo at 174 BPM.

## What it models, and what it does not

Models: the integer arithmetic exactly (it is the same source), the 512-sample block structure,
the DMA ring depth and fill rate, and a main loop whose interval jitters like the measured one
(about 1ms, with occasional 15ms spikes). `simI2sDropped` counts samples the ring had no room
for, which is how the collection-path audio loss is verified as fixed.

Does not model: a real microphone in a real room. No reverb, no PA compression, no crowd, no DJ
riding the gain, and the synthetic track's balance between kick and bass is invented. Treat a
result here as a necessary condition, not a sufficient one -- it can prove a setting is broken,
it cannot prove one is right. Hardware confirmation still decides.

## Tempo estimation: what `--mode compare` measured (2026-09-02)

The device tracked four-on-the-floor correctly and lost hip-hop badly -- 98 BPM (a 612ms beat)
came out as 454ms, which is 3/4 of the beat to within 1%. `--pattern hiphop` reproduces that
exactly, which is what makes it a test case rather than a story.

The reason is structural. The estimator is the median of the gaps between consecutive kicks, and
that only answers the question if every gap IS the beat. Put the kicks at 0, 3/4, 1 1/4 and
2 1/2 beats and the gaps are 3/4, 1/2, 1 1/4 and 1 1/2 -- the beat is not among them, and no
median, mean or filter over that set can produce it.

Five methods, one run each, identical onsets, identical rolling windows, scored as the share of
8-second windows landing within 4% of the true tempo. Five patterns x six tempos:

    Median B    31%    the gap median over bass onsets -- what the firmware does today
    ACF+H B     15%    harmonic-summed autocorrelation, still bass only
    +Prior B    25%    ... plus a log-Gaussian tempo prior at 120 BPM
    +Prior A    81%    ... over bass AND mid AND high onsets together
    +Anker A   100%    ... with the prior centred on the tapped anchor instead

Two findings, in order of size.

**The material mattered more than the method.** Bass only: 15-31% whatever the maths. The same
maths over all three bands: 81%. This is obvious in hindsight and was missed for weeks -- a
boom-bap kick pattern genuinely does not state the beat, and nothing downstream can recover what
was never in the signal. The snare on 2 and 4 and the hats on the eighths state it plainly, and
both have been detected all along; they were simply never shown to the tempo estimator. The
per-band detectors built for triggering Mid/High light effects turn out to be the fix for a
problem that looked unrelated.

**Autocorrelation finds the period; only the anchor settles the octave.** Every remaining failure
at 81% is an octave or a 4/3, never a random number. A prior alone cannot fix this: to prefer 174
over 87 the prior must sit above 123 BPM, to prefer 90 over 180 it must sit below 127, so a
single Gaussian has a 4 BPM window in which it covers a 60-200 range. Sweeping centre 100-140 and
width 0.6-1.8 octaves is a flat plateau at 81% -- 120/0.9 is already as good as it gets, and
there is nothing to win by tuning it.

Centring that same prior on `tapAnchorBPM` instead scores 100% of windows in all thirty cases.
That is not the estimator being handed the answer: the simulated tap is 3% off, and the prior is
excluded from the sub-bin interpolation so the reported value cannot be an echo of the tap.
Robustness against a bad tap, as share of correct windows:

    tap error   -15%   +15%   +33%   x2 / half
    sigma 0.35   96%   100%    70%      obeyed
    sigma 0.40   96%   100%    82%      obeyed
    sigma 0.50   88%    99%    93%      obeyed

0.40 octaves is the chosen compromise: a sloppy tap is far more likely than a tap onto a
different subdivision. A deliberate half- or double-time tap is obeyed, which is the intended
behaviour already documented at `tempoMulMode` -- the octave is offered, not inferred.

### Not yet on the device

This is a simulator result. Porting it means: an 8-second onset-strength ring (10ms bins, ~800
entries), autocorrelation over lags 30..100 bins with four harmonics -- roughly 180k
multiply-accumulates per evaluation, so about 4-5ms once a second on the C3, under half a percent
of the loop. Estimated from the operation count, not measured; measure it before believing it.
The interval median and its agreement gate would come out in exchange.
