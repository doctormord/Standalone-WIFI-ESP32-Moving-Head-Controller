#!/usr/bin/env python3
"""Generate a validation corpus of synthetic tracks with exact beat annotations.

Why this exists: the simulator's built-in track is a click pattern. It is good enough to find
bugs in the detector and useless for deciding whether one detection method beats another --
which is exactly the mistake made on 2026-09-02, when a method scored 81% in the simulator and
7% on the device. These tracks carry the things that actually break onset detectors: sustained
bass that never returns to silence, pads with no transient at all, swung timing, syncopated
kicks that do not sit on the beat, vibrato, and a breakdown with no drums in it.

The ground truth written next to each file is the BEAT GRID -- what a person taps -- and NOT the
kick positions. On syncopated material those are different, and conflating them is how a
detector gets credited for finding a pulse nobody hears.

    python3 sim/mktracks.py [outdir]        # default ./tracks

Each track produces <name>.wav (16 kHz mono, the rate the device samples at) and <name>.beats.
"""
import math
import os
import struct
import sys

import numpy as np

SR = 16000
RNG = np.random.default_rng(20260903)


# --- instruments ---------------------------------------------------------------
# Written out rather than sampled so every partial is known. Levels are deliberately not
# normalised per instrument: the relative balance is part of what makes a case hard.

def _env(n, attack, decay):
    t = np.arange(n) / SR
    a = np.clip(t / max(attack, 1e-6), 0, 1)
    return a * np.exp(-t / decay)


def kick(dur=0.30, f0=110.0, f1=45.0, click=0.30):
    n = int(dur * SR)
    t = np.arange(n) / SR
    # Pitch sweep: the drop from ~110 to ~45 Hz is what makes a kick read as a kick rather
    # than as a low sine, and it spreads energy across the two lowest Bark bands.
    f = f1 + (f0 - f1) * np.exp(-t / 0.035)
    body = np.sin(2 * np.pi * np.cumsum(f) / SR) * _env(n, 0.001, 0.075)
    tick = RNG.normal(0, 1, n) * np.exp(-t / 0.0035) * click
    return body + tick


def snare(dur=0.25, tone=190.0):
    n = int(dur * SR)
    t = np.arange(n) / SR
    noise = RNG.normal(0, 1, n)
    noise = noise - np.convolve(noise, np.ones(8) / 8, mode="same")   # crude high-pass
    body = np.sin(2 * np.pi * tone * t) * 0.45
    return (noise * 0.9 + body) * _env(n, 0.0008, 0.055)


def hat(open_=False):
    dur = 0.16 if open_ else 0.045
    n = int(dur * SR)
    t = np.arange(n) / SR
    noise = RNG.normal(0, 1, n)
    noise = noise - np.convolve(noise, np.ones(3) / 3, mode="same")   # brighter high-pass
    return noise * np.exp(-t / (0.045 if open_ else 0.010)) * 0.35


def bass(freq, dur, shape="saw", vib=0.0):
    """Sustained low note. The case that breaks level-based detection: energy in the kick's
    band that never returns to the floor, so a threshold placed on absolute level either
    misses the kick or fires continuously."""
    n = int(dur * SR)
    t = np.arange(n) / SR
    f = freq * (1.0 + vib * np.sin(2 * np.pi * 5.5 * t)) if vib else freq
    ph = 2 * np.pi * np.cumsum(np.full(n, f) if np.isscalar(f) else f) / SR
    if shape == "saw":
        w = sum(np.sin(ph * k) / k for k in range(1, 8))
    else:
        w = np.sin(ph)
    return w * _env(n, 0.006, dur * 0.7) * 0.5


def pad(freqs, dur, vib=0.0):
    """A chord with a slow attack and no transient. Contributes energy and no onset --
    the 'Flaeche' that made the tempo wander on hardware."""
    n = int(dur * SR)
    t = np.arange(n) / SR
    out = np.zeros(n)
    for f in freqs:
        ff = f * (1.0 + vib * np.sin(2 * np.pi * 5.0 * t + f)) if vib else f
        ph = 2 * np.pi * np.cumsum(np.full(n, ff) if np.isscalar(ff) else ff) / SR
        out += np.sin(ph) + 0.4 * np.sin(2 * ph)
    a = np.clip(t / 0.6, 0, 1) * np.clip((dur - t) / 0.6, 0, 1)
    return out / len(freqs) * a * 0.30


# --- sequencing ----------------------------------------------------------------

class Track:
    def __init__(self, name, bpm, bars, note):
        self.name, self.bpm, self.note = name, bpm, note
        self.beat = 60.0 / bpm
        self.dur = bars * 4 * self.beat
        self.buf = np.zeros(int(self.dur * SR) + SR)
        self.bars = bars

    def put(self, at_s, sig, gain=1.0):
        i = int(at_s * SR)
        if i < 0:
            return
        end = min(i + len(sig), len(self.buf))
        if end > i:
            self.buf[i:end] += sig[: end - i] * gain

    def each_bar(self):
        for b in range(self.bars):
            yield b, b * 4 * self.beat

    def beats(self):
        """Ground truth: every quarter note, for the whole track -- including any section with
        no drums in it. A listener keeps counting through a breakdown, so the annotation does."""
        return [i * self.beat for i in range(int(self.dur / self.beat))]

    def save(self, outdir):
        x = self.buf[: int(self.dur * SR)]
        peak = np.max(np.abs(x)) or 1.0
        x = np.tanh(x / peak * 1.25) * 0.89          # soft limit, as a master bus would
        pcm = (np.clip(x, -1, 1) * 32767).astype("<i2").tobytes()
        path = os.path.join(outdir, self.name + ".wav")
        with open(path, "wb") as f:
            f.write(b"RIFF" + struct.pack("<I", 36 + len(pcm)) + b"WAVEfmt ")
            f.write(struct.pack("<IHHIIHH", 16, 1, 1, SR, SR * 2, 2, 16))
            f.write(b"data" + struct.pack("<I", len(pcm)) + pcm)
        with open(os.path.join(outdir, self.name + ".beats"), "w") as f:
            f.write("# %s -- %.1f BPM -- %s\n" % (self.name, self.bpm, self.note))
            for b in self.beats():
                f.write("%.6f\n" % b)
        return path, len(self.beats())


def boombap(bpm=86):
    t = Track("boombap-%d" % bpm, bpm, 22,
              "hip-hop, syncopated kicks off the pulse, swung hats, sustained bass")
    B = t.beat
    swing = 0.16 * B                       # the 'and' lands late -- this is what swing is
    for bar, t0 in t.each_bar():
        for k in (0.0, 1.5, 2.75):         # kick: 1, the 'and' of 2, just before 4
            t.put(t0 + k * B, kick(), 1.0)
        for s in (1.0, 3.0):               # snare on 2 and 4
            t.put(t0 + s * B, snare(), 0.85)
        for i in range(8):                 # swung eighths
            off = swing if i % 2 else 0.0
            t.put(t0 + i * 0.5 * B + off, hat(open_=(i == 7)), 0.5)
        t.put(t0, bass(55 if bar % 2 else 62, 2.0 * B), 0.9)
        t.put(t0 + 2.5 * B, bass(49, 1.5 * B), 0.9)
    return t


def trap(bpm=70):
    t = Track("trap-%d" % bpm, bpm, 18,
              "half-time feel, sparse kicks, 808 sub with long decay, 1/16 hat rolls")
    B = t.beat
    for bar, t0 in t.each_bar():
        for k in (0.0, 2.75):
            t.put(t0 + k * B, kick(f0=95, f1=40), 1.0)
        t.put(t0 + 2.0 * B, snare(), 0.9)
        for i in range(16):                # 1/16 hats, with a triplet roll every fourth bar
            t.put(t0 + i * 0.25 * B, hat(), 0.34)
        if bar % 4 == 3:
            for i in range(6):
                t.put(t0 + 3.0 * B + i * B / 6.0, hat(), 0.4)
        # 808: sustained sub, deliberately as loud as the kick and never returning to silence
        t.put(t0, bass(41, 2.4 * B, shape="sine"), 1.15)
        t.put(t0 + 2.75 * B, bass(37, 1.2 * B, shape="sine"), 1.15)
    return t


def house(bpm=145):
    t = Track("house-%d" % bpm, bpm, 34,
              "four on the floor, open hat off-beat, sustained bass and pad")
    B = t.beat
    for bar, t0 in t.each_bar():
        for k in range(4):
            t.put(t0 + k * B, kick(), 1.0)
        for k in range(4):
            t.put(t0 + (k + 0.5) * B, hat(open_=True), 0.55)
        for k in (1, 3):
            t.put(t0 + k * B, snare(tone=220), 0.5)
        for k in range(4):
            t.put(t0 + (k + 0.5) * B, bass(58 if k % 2 else 65, 0.45 * B), 0.8)
        if bar % 4 == 0:
            t.put(t0, pad([220, 277, 330], 4 * B), 0.8)
    return t


def dnb(bpm=174):
    t = Track("dnb-%d" % bpm, bpm, 40,
              "two-step, kicks on 1 and the 'and' of 3, snare on 2 and 4, sub bass")
    B = t.beat
    for bar, t0 in t.each_bar():
        t.put(t0, kick(), 1.0)
        t.put(t0 + 2.5 * B, kick(), 1.0)
        for s in (1.0, 3.0):
            t.put(t0 + s * B, snare(), 0.95)
        for i in range(8):
            t.put(t0 + i * 0.5 * B, hat(open_=(i in (3, 7))), 0.4)
        t.put(t0, bass(43, 4 * B, shape="sine"), 1.0)
    return t


def techno(bpm=150):
    t = Track("techno-%d" % bpm, bpm, 36,
              "four on the floor under a loud sustained bass -- level never returns to the floor")
    B = t.beat
    for bar, t0 in t.each_bar():
        for k in range(4):
            t.put(t0 + k * B, kick(f0=120, f1=48), 1.0)
        for i in range(8):
            t.put(t0 + i * 0.5 * B, hat(), 0.3)
        t.put(t0, bass(52, 4 * B), 1.25)               # louder than the kick, on purpose
        if bar % 2 == 0:
            t.put(t0, pad([146, 174, 220], 8 * B), 0.7)
    return t


def strings(bpm=92):
    t = Track("strings-%d" % bpm, bpm, 24,
              "sustained strings WITH vibrato over a soft kick -- what SuperFlux is built for")
    B = t.beat
    for bar, t0 in t.each_bar():
        for k in (0.0, 2.0):
            t.put(t0 + k * B, kick(click=0.12), 0.62)   # soft, no click to lock onto
        t.put(t0 + 1.0 * B, snare(), 0.35)
        # Vibrato is the point: it moves energy between neighbouring bands every ~180ms, which
        # a plain spectral flux reads as a stream of onsets.
        t.put(t0, pad([294, 370, 440, 587], 4 * B, vib=0.035), 1.0)
        t.put(t0, bass(73, 4 * B, vib=0.02), 0.7)
    return t


def breakdown(bpm=132):
    t = Track("breakdown-%d" % bpm, bpm, 32,
              "drums, then 8 bars of pad only with NO beat, then drums again")
    B = t.beat
    for bar, t0 in t.each_bar():
        quiet = 12 <= bar < 20
        if quiet:
            t.put(t0, pad([196, 247, 294], 4 * B, vib=0.01), 1.0)
            continue
        for k in range(4):
            t.put(t0 + k * B, kick(), 1.0)
        for k in (1, 3):
            t.put(t0 + k * B, snare(), 0.8)
        for i in range(8):
            t.put(t0 + i * 0.5 * B, hat(open_=(i == 7)), 0.42)
        t.put(t0, bass(55, 4 * B), 0.85)
    return t


BUILDERS = [boombap, trap, house, dnb, techno, strings, breakdown]


def main():
    outdir = sys.argv[1] if len(sys.argv) > 1 else "tracks"
    os.makedirs(outdir, exist_ok=True)
    for b in BUILDERS:
        tr = b()
        path, nb = tr.save(outdir)
        print("%-16s %6.1f BPM  %5.1f s  %4d beats   %s"
              % (tr.name, tr.bpm, tr.dur, nb, tr.note))
    print("\nwrote to %s/" % outdir)


if __name__ == "__main__":
    main()
