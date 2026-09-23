#!/usr/bin/env bash
# Copies the installed Windows engine produced by a full wsl-build.sh run
# (build-amd64-windows/install) into a runnable engine folder on the Windows disk.
#
# Usage (from Windows): wsl -d Ubuntu -- bash /mnt/c/Workspace/bar/RecoilEngine/tools/workbench/deploy.sh [dest]
# Default dest: /mnt/c/Workspace/bar/engine
set -euo pipefail

SRC_WSL="${SRC_WSL:-$HOME/bar/RecoilEngine}"
INSTALL="$SRC_WSL/build-amd64-windows/install"
DEST="${1:-/mnt/c/Workspace/bar/engine}"

[ -f "$INSTALL/spring.exe" ] || { echo "no $INSTALL/spring.exe: run a full wsl-build.sh (no target arguments) first" >&2; exit 1; }
[ -f "$INSTALL/unitsync.dll" ] || { echo "install has no unitsync.dll; the engine would not find its own data dir" >&2; exit 1; }

mkdir -p "$DEST"
rsync -a --delete "$INSTALL/" "$DEST/"
echo "deployed $(stat -c %y "$DEST/spring.exe") -> $DEST"
