#!/usr/bin/env bash
# Build and run engine unit tests (linux target) in the WSL mirror.
# Usage (from Windows): wsl -d Ubuntu -- bash /mnt/c/Workspace/bar/RecoilEngine/tools/workbench/wsl-test.sh [test_target ...]
# Default: test_Workbench
set -euo pipefail
SRC_WIN="${SRC_WIN:-/mnt/c/Workspace/bar/RecoilEngine}"
SRC_WSL="${SRC_WSL:-$HOME/bar/RecoilEngine-test}"
JOBS="${JOBS:-$(nproc)}"
TARGETS=("$@"); [ ${#TARGETS[@]} -eq 0 ] && TARGETS=(test_Workbench)
mkdir -p "$SRC_WSL"
rsync -a --delete --exclude 'build-*/' --exclude '.cache/' --exclude '.superpowers/' "$SRC_WIN/" "$SRC_WSL/"
cd "$SRC_WSL"
[ -f build-amd64-linux/build.ninja ] || docker-build-v2/build.sh --configure linux -DCMAKE_BUILD_TYPE=RELWITHDEBINFO
args=(); for t in "${TARGETS[@]}"; do args+=(-t "$t"); done
docker-build-v2/build.sh -j "$JOBS" --compile linux "${args[@]}"
# run inside the build image: it has the runtime libraries (e.g. SDL2) the tests link against
source docker-build-v2/images_versions.sh
IMAGE="${CONTAINER_IMAGE:-ghcr.io/beyond-all-reason/recoil-build-amd64-linux@${image_version[amd64-linux]}}"
for t in "${TARGETS[@]}"; do
	docker run --rm -v "$PWD/build-amd64-linux:/build/out:ro" "$IMAGE" "/build/out/test/$t"
done
