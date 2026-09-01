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
