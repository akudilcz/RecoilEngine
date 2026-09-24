#!/usr/bin/env bash
# Samples a running workbench scenario with perf (Linux): starts the scenario on the
# headless engine, attaches once "[Workbench] scenario <name>" appears in the infolog,
# records SECONDS of call-graph samples, then writes self/inclusive hot-function reports.
#   [SIM_SPEED=max|1|...] tools/workbench/profile.sh <scenario> [seconds] [engine] [data-dir]
# Default SIM_SPEED=max: the sim runs flat out, so it (not idle waiting) dominates the samples.
# Needs perf and kernel.perf_event_paranoid <= 1 (sudo sysctl kernel.perf_event_paranoid=-1).
set -euo pipefail
SCENARIO="$1"
SECONDS_TO_RECORD="${2:-40}"
ENGINE="${3:-/store/bar/RecoilEngine/build-amd64-linux/spring-headless}"
DATA="${4:-/store/bar/data}"
HERE="$(cd "$(dirname "$0")" && pwd)"
OUT="$HERE/results/profile-$SCENARIO-$(date +%Y%m%d-%H%M%S)"
mkdir -p "$OUT"

python3 "$HERE/run.py" --only "$SCENARIO" --seed 1234 --no-report --timeout 900 --sim-speed "${SIM_SPEED:-max}" \
	--engine "prof=$ENGINE" --data-dir "$DATA" --out "$OUT/run" > "$OUT/run.log" 2>&1 &
RUN=$!

# wait for the scenario (not loading) to start
# match the engine by a command line that *starts* with its path: a plain -f match would
# also hit run.py (whose command line contains the path), and the engine renames its main
# thread, so the process name is not the binary name either
PROC="^$ENGINE"
until grep -q "\[Workbench\] scenario $SCENARIO" "$DATA/infolog.txt" 2>/dev/null && pgrep -f "$PROC" >/dev/null; do
	sleep 1
	kill -0 $RUN 2>/dev/null || { echo "run ended before the scenario started"; tail "$OUT/run.log"; exit 1; }
done
PID=$(pgrep -n -f "$PROC")
sleep 3 # let the scenario's setup (spawning) pass

# DWARF unwinding: the engine is built without frame pointers
perf record -F 499 -g --call-graph dwarf,32768 -p "$PID" -o "$OUT/perf.data" -- sleep "$SECONDS_TO_RECORD" 2> "$OUT/perf-record.log"
wait $RUN || true

perf report -i "$OUT/perf.data" --no-children --sort symbol --stdio --percent-limit 0.3 -g none 2>/dev/null > "$OUT/self.txt"
perf report -i "$OUT/perf.data" --children --sort symbol --stdio --percent-limit 1 -g none 2>/dev/null > "$OUT/inclusive.txt"
perf report -i "$OUT/perf.data" --no-children --sort dso --stdio 2>/dev/null > "$OUT/by_library.txt"
perf report -i "$OUT/perf.data" --no-children --sort comm --stdio 2>/dev/null > "$OUT/by_thread.txt"
echo "$OUT"
