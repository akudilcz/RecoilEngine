#!/usr/bin/env bash
# Resolve a Windows crash stack from an infolog against spring.exe/spring.dbg.
# Usage (from Windows): wsl -d Ubuntu -- bash .../tools/workbench/symbolize.sh <engine dir> <infolog>
set -euo pipefail
ENGINE="$1"; INFOLOG="$2"
IMAGE="${IMAGE:-ghcr.io/beyond-all-reason/recoil-build-amd64-windows}"
BASE=$(grep -oE "0x[0-9a-f]+\s+spring$" "$INFOLOG" | head -1 | awk '{print $1}')
# crash stacks are logged as "Error: (N) ...", hang-detector stacks as "Warning: (N) ..."
# (repeated every interval); keep the first occurrence of each frame
ADDRS=$(grep -E "(Error|Warning):[[:space:]]+\([0-9]+\) .*spring\.exe \[0x" "$INFOLOG" | grep -oE "\[0x[0-9a-f]+\]" | tr -d '[]' | awk '!seen[$0]++' | head -30)
docker run --rm -v "$ENGINE:/eng:ro" "$IMAGE" bash -c '
	PREF=$(objdump -p /eng/spring.exe | awk "/ImageBase/{print \$2}")
	for a in '"$(echo $ADDRS)"'; do
		off=$(( a - '"$BASE"' + 0x$PREF ))
		printf "%s " "$a"; addr2line -f -C -i -e /eng/spring.dbg $(printf "0x%x" $off) | paste -sd" " | cut -c1-220
	done'
