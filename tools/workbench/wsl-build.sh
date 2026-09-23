#!/usr/bin/env bash
# Fast engine build for Windows hosts: mirrors the Windows checkout into the WSL
# filesystem (docker bind mounts from /mnt/c are very slow) and builds there.
#
# Usage (from Windows): wsl -d Ubuntu -- bash /mnt/c/Workspace/bar/RecoilEngine/tools/workbench/wsl-build.sh [targets...]
# Default targets: engine-legacy basecontent
set -euo pipefail

SRC_WIN="${SRC_WIN:-/mnt/c/Workspace/bar/RecoilEngine}"
SRC_WSL="${SRC_WSL:-$HOME/bar/RecoilEngine}"
JOBS="${JOBS:-$(nproc)}"
TARGETS=("$@")
[ ${#TARGETS[@]} -eq 0 ] && TARGETS=(engine-legacy basecontent)

mkdir -p "$SRC_WSL"
rsync -a --delete --exclude 'build-*/' --exclude '.cache/' "$SRC_WIN/" "$SRC_WSL/"

cd "$SRC_WSL"
if [ ! -f build-amd64-windows/build.ninja ]; then
	docker-build-v2/build.sh --configure windows -DCMAKE_BUILD_TYPE=RELWITHDEBINFO
fi

args=()
for t in "${TARGETS[@]}"; do args+=(-t "$t"); done
docker-build-v2/build.sh -j "$JOBS" --compile windows "${args[@]}"
