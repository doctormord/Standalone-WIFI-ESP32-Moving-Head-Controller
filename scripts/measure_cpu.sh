#!/usr/bin/env bash
# Measure where the CPU actually goes on the device, instead of arguing about it from estimates.
#
# This settles the question that has been open since the audio rework: is the ESP32-C3 short of
# CPU at all? Answered on 2026-09-03 -- it is not. updateEngines 4.3%, audio block 9.5% (of which
# the FFT 3.8%), together ~14% of one core at a loop rate of 172/s. That also closes the SoC
# question: an FPU (only the ESP32, S3, H4 and P4 have one -- NOT the C5, C6 or S2) would buy
# nothing today. Re-run it after any change that adds work to the audio or engine path.
#
# Usage:
#   ./scripts/measure_cpu.sh [host] [seconds]
#     host defaults to movinghead.local, seconds to 30.
#
# READ THIS BEFORE INTERPRETING THE OUTPUT
#
#   engUs is the duration of the LAST updateEngines() call. audUs is the cost of processing one
#   full 512-sample audio block -- the many calls that find no data yet do not write it. fftUs
#   is fftRun() alone. All are single instantaneous samples, not means. engMax / audMax / loopMax are maxima over a rolling 5-SECOND window, all three
#   reset together, so a max seen once is real and a max of zero only means the window restarted.
#
#   These definitions were WRONG until 2026-09-03: fftUs was timed from the top of the audio
#   block and so included the sample scaling, auto-gain and the entire sample-rate detector,
#   and audUs was overwritten by the empty calls. The first measurement taken with them was
#   unusable -- the part appeared to cost more than the whole. If a number here looks
#   impossible, suspect the definition before the device.
#
#   fftUs is 0 whenever the FFT did not run. The FFT is demand-gated: it only runs while a
#   client has asked for the spectrum within the last ~2s, and /api/state does NOT renew that
#   lease. To measure the FFT you have to keep the lease alive by polling /api/audio_debug --
#   and that polling is itself load on a server that handles one request at a time. That is why
#   this script runs two phases and reports them separately rather than mixing them.
#
#   The load percentages below are derived as  lps * engUs / 10000  (percent of one core), using
#   the instantaneous engUs as if it were the mean. Treat them as an order of magnitude. The
#   honest upper bound is  lps * engMax / 10000.
#
# WHAT TO SET BEFORE RUNNING
#   Measure what you actually run: patch the number of fixtures you use, and switch on the FX
#   you use (Movement especially -- it is the soft-float heavy one). A measurement taken with
#   everything idle answers a question nobody asked. The script prints the context it found so
#   the numbers stay self-describing.

set -euo pipefail

HOST="${1:-movinghead.local}"
SECS="${2:-30}"
BASE="http://$HOST"

j() { python3 -c "import sys,json;d=json.load(sys.stdin);print(d.get('$1',''))"; }

echo "=== Moving Head Horizon -- CPU measurement ==="
STATE=$(curl -s --max-time 4 "$BASE/api/state") || { echo "No answer from $HOST" >&2; exit 1; }
echo "build : $(printf '%s' "$STATE" | j bld)"
echo "host  : $HOST   duration: ${SECS}s per phase"
echo "bpm   : $(printf '%s' "$STATE" | j bpm)   mic on: $(printf '%s' "$STATE" | j hwA)   asmEvery: $(printf '%s' "$STATE" | j asmEvery)"
echo

phase() {
  local label="$1" keep_fft="$2"
  local n=0 lps=0 eng=0 aud=0 fft=0 engmax=0 audmax=0 loopmax=0 fftseen=0
  local end=$(( $(date +%s) + SECS ))
  while [ "$(date +%s)" -lt "$end" ]; do
    if [ "$keep_fft" = "1" ]; then curl -s --max-time 3 "$BASE/api/audio_debug" >/dev/null || true; fi
    S=$(curl -s --max-time 4 "$BASE/api/state") || continue
    read -r a b c d e f g h <<<"$(printf '%s' "$S" | python3 -c "
import sys,json
d=json.load(sys.stdin)
print(d.get('lps',0), d.get('engUs',0), d.get('audUs',0), d.get('fftUs',0),
      d.get('engMax',0), d.get('audMax',0), d.get('loopMax',0))")"
    n=$((n+1)); lps=$((lps+a)); eng=$((eng+b)); aud=$((aud+c))
    if [ "$d" -gt 0 ]; then fft=$((fft+d)); fftseen=$((fftseen+1)); fi
    [ "$e" -gt "$engmax" ] && engmax=$e
    [ "$f" -gt "$audmax" ] && audmax=$f
    [ "$g" -gt "$loopmax" ] && loopmax=$g
  done
  [ "$n" -gt 0 ] || { echo "$label: no samples"; return; }
  python3 - "$label" "$n" "$lps" "$eng" "$aud" "$fft" "$fftseen" "$engmax" "$audmax" "$loopmax" <<'PY'
import sys
label,n,lps,eng,aud,fft,fftseen,engmax,audmax,loopmax = sys.argv[1], *map(int, sys.argv[2:])
lps_a, eng_a, aud_a = lps/n, eng/n, aud/n
fft_a = fft/fftseen if fftseen else 0.0
print(f"--- {label}  ({n} samples)")
print(f"  loop rate        {lps_a:8.0f} /s        worst loop gap {loopmax:5d} ms")
print(f"  updateEngines    {eng_a:8.1f} us  ->  {lps_a*eng_a/10000:5.1f} % of one core"
      f"   (5s max {engmax} us -> {lps_a*engmax/10000:.1f} %)")
print(f"  audio block      {aud_a:8.1f} us  ->  {aud_a*31.25/10000:5.1f} % at 31 blocks/s"
      f"   (5s max {audmax} us)   [contains the two lines below]")
if fftseen:
    print(f"    of which FFT   {fft_a:8.1f} us  ->  {fft_a*31.25/10000:5.1f} % if run on EVERY block"
          f"   (seen in {fftseen}/{n})")
else:
    print("    of which FFT          -- not running (demand-gated, no lease held)")
PY
  echo
}

phase "PHASE 1: normal operation, FFT demand-gated (no spectrum client)" 0
phase "PHASE 2: FFT lease held open -- this is the always-on FFT cost, plus the polling itself" 1

cat <<'NOTE'
How to read the two phases together:
  * The FFT line in phase 2 is the cost of ONE transform, scaled to 31.25 blocks a second --
    the number to check before anything is made to run the FFT unconditionally.
  * Phase 2's loop rate will be lower than phase 1's -- part of that is the FFT, part is this
    script's own polling. Do not attribute the whole difference to the FFT.
  * If updateEngines dominates and the loop rate is healthy, the C3 is not the bottleneck and
    an FPU would buy nothing today. Record the numbers in doc/content/handover.md either way.
NOTE
