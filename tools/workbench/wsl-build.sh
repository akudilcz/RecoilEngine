#!/usr/bin/env bash
# Fast engine build for Windows hosts: mirrors the Windows checkout into the WSL
# filesystem (docker bind mounts from /mnt/c are very slow) and builds there.
#
# Usage (from Windows):
#   wsl -d Ubuntu -- bash /mnt/c/Workspace/bar/RecoilEngine/tools/workbench/wsl-build.sh            # full build + install
#   wsl -d Ubuntu -- bash /mnt/c/Workspace/bar/RecoilEngine/tools/workbench/wsl-build.sh target ...  # only these targets
#
# The full build runs the install step, which produces build-amd64-windows/install:
# a complete engine (stripped spring.exe + .dbg, unitsync.dll, base content, runtime
# files) that deploy.sh copies as-is.
set -euo pipefail

SRC_WIN="${SRC_WIN:-/mnt/c/Workspace/bar/RecoilEngine}"
SRC_WSL="${SRC_WSL:-$HOME/bar/RecoilEngine}"
JOBS="${JOBS:-$(nproc)}"

mkdir -p "$SRC_WSL"
rsync -a --delete --exclude 'build-*/' --exclude '.cache/' --exclude '.superpowers/' "$SRC_WIN/" "$SRC_WSL/"
cd "$SRC_WSL"

if [ $# -eq 0 ]; then
	docker-build-v2/build.sh -j "$JOBS" windows -DCMAKE_BUILD_TYPE=RELWITHDEBINFO
	exit
fi

if [ ! -f build-amd64-windows/build.ninja ]; then
	docker-build-v2/build.sh --configure windows -DCMAKE_BUILD_TYPE=RELWITHDEBINFO
fi
args=()
for t in "$@"; do args+=(-t "$t"); done
docker-build-v2/build.sh -j "$JOBS" --compile windows "${args[@]}"
