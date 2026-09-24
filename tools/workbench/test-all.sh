#!/usr/bin/env bash
# Runs every local test suite of the fork in one go (from Windows):
#   wsl -d Ubuntu -- bash /mnt/c/Workspace/bar/RecoilEngine/tools/workbench/test-all.sh
# 1. workbench runner/report tests (Python)
# 2. engine unit tests (Catch2; built in the WSL mirror, run in the build image)
# 3. BAR Lua specs (busted, Lua 5.1)
# First run: installs lua5.1 + busted in WSL if missing (needs sudo).
set -uo pipefail
ENGINE_WIN="${ENGINE_WIN:-/mnt/c/Workspace/bar/RecoilEngine}"
BAR_WIN="${BAR_WIN:-/mnt/c/Workspace/bar/Beyond-All-Reason}"
BAR_WSL="${BAR_WSL:-$HOME/bar/BAR-spec}"
TESTS="${TESTS:-test_Workbench test_KeyInput test_ContainerUtil test_InfoTextureUpdate}"
failed=()

echo "== workbench runner tests"
(cd "$ENGINE_WIN" && python3 -m unittest discover -s tools/workbench/tests -q) || failed+=(runner)

echo "== engine unit tests: $TESTS"
# shellcheck disable=SC2086
bash "$ENGINE_WIN/tools/workbench/wsl-test.sh" $TESTS || failed+=(engine)

echo "== BAR Lua specs"
if ! command -v busted >/dev/null; then
	sudo apt-get update -qq && sudo apt-get install -y -qq lua5.1 liblua5.1-0-dev luarocks && sudo luarocks install busted
fi
mkdir -p "$BAR_WSL"
rsync -a --delete --exclude .git "$BAR_WIN/" "$BAR_WSL/"
log=$(mktemp)
(cd "$BAR_WSL" && busted --lua=lua5.1) > "$log" 2>&1
status=$?
tail -6 "$log"; rm -f "$log"
[ "$status" -eq 0 ] || failed+=(bar)

echo
if [ ${#failed[@]} -eq 0 ]; then echo "ALL PASSED"; else echo "FAILED: ${failed[*]}"; exit 1; fi
