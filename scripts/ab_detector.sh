#!/usr/bin/env bash
# Diagnose the bass onset detector on whatever is actually playing, judged on the thing that
# matters for a light: are the gaps between onsets a BEAT long, and is the reported tempo right.
#
#   ./scripts/ab_detector.sh <true-bpm> [host] [seconds]
#
# The true BPM is required. Without it the interval histogram cannot say whether a gap is a beat
# or a sixteenth -- and onset COUNT alone is actively misleading. Measured 2026-09-03: on a trap
# track one detector found seven times more onsets than another and reported a WORSE tempo,
# because everything it added was hi-hats. **Detection rate is not tempo correctness.** The
# number to read is "davon 1 Beat".
#
# Two passes: once with the tap anchor cleared (does it stand up unaided?) and once with the
# anchor set to the true tempo (does the anchor rescue it?). The anchor is the mechanism that
# actually carries syncopated material, so judging without it only tells half the story.

set -euo pipefail
BPM="${1:?usage: ab_detector.sh <true-bpm> [host] [seconds]}"
HOST="${2:-192.168.8.113}"
SECS="${3:-40}"
BASE="http://$HOST"
BEAT=$(python3 -c "print(60000.0/$BPM)")

pass() {
  local label="$1" anchor="$2"
  curl -s --max-time 5 "$BASE/audio_tune?auto=1" >/dev/null            # drop any stale anchor
  [ "$anchor" != "0" ] && curl -s --max-time 5 "$BASE/beat?bpm=$anchor" >/dev/null
  for _ in $(seq 1 25); do curl -s --max-time 2 -o /dev/null "$BASE/api/state"; done
  local end=$(( $(date +%s) + SECS )) OUT=""
  while [ "$(date +%s)" -lt "$end" ]; do
    V=$(curl -s --max-time 3 "$BASE/api/audio_debug" 2>/dev/null | python3 -c "
import sys,json;d=json.load(sys.stdin);print(d.get('bOn',0),d.get('tBPM',0))" 2>/dev/null) || continue
    G=$(curl -s --max-time 3 "$BASE/api/state" 2>/dev/null | python3 -c "
import sys,json;print(json.load(sys.stdin).get('bpm',0))" 2>/dev/null) || continue
    OUT="$OUT|$V $G"
  done
  python3 - "$label" "$BPM" "$BEAT" "$SECS" <<PY
import sys, statistics
label, bpm, beat, secs = sys.argv[1], float(sys.argv[2]), float(sys.argv[3]), float(sys.argv[4])
rows = [r.split() for r in "$OUT".split('|') if r.strip()]
ts, tb, gui = [], [], []
for r in rows:
    if len(r) < 3: continue
    v = int(r[0])
    if v and (not ts or v != ts[-1]): ts.append(v)
    tb.append(int(r[1])); gui.append(int(r[2]))
print(f"--- {label}")
print(f"  Onsets/s        {len(ts)/secs:5.2f}   (Beatrate {1000/beat:.2f}/s bei {bpm:.0f} BPM)")
gaps = sorted(g for a, b in zip(ts, ts[1:]) for g in [b - a] if 0 < g < 4000)
if gaps:
    med = gaps[len(gaps)//2]
    onbeat = sum(1 for g in gaps if 0.85 <= g/beat < 1.15)
    half   = sum(1 for g in gaps if 0.40 <= g/beat < 0.60)
    print(f"  Abstand-Median  {med:5d} ms -> {60000/med:5.0f} BPM")
    print(f"  davon 1 Beat    {100*onbeat/len(gaps):5.0f} %      1/2 Beat {100*half/len(gaps):3.0f} %"
          f"   ({len(gaps)} Abstaende)")
g = [x for x in gui if x > 0]
if g:
    ok = sum(1 for x in g if abs(x-bpm)/bpm < 0.05)
    print(f"  GUI-BPM         Median {int(statistics.median(g)):3d}   Bereich {min(g)}..{max(g)}"
          f"   innerhalb 5 %: {100*ok//len(g)} %   ({len(g)} Messungen)")
PY
}

echo "=== Detektor-Diagnose auf $HOST, Wahrheit ${BPM} BPM, ${SECS}s je Durchgang ==="
echo
pass "ohne Tap-Anker" 0
echo
pass "mit Tap-Anker auf ${BPM}" "$BPM"
