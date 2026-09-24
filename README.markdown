# Recoil, tuned and tested

**A Recoil engine fork for [Beyond All Reason](https://www.beyondallreason.info) that plays smoother in big battles, fixes bugs upstream hasn't gotten to yet, and checks itself with a built-in testbench that plays the game for you.**

Paired with the game fork [akudilcz/Beyond-All-Reason](https://github.com/akudilcz/Beyond-All-Reason). Built on upstream [Recoil](https://github.com/beyond-all-reason/RecoilEngine) and kept level with it.

| | |
|---|---|
| **Tested** | 747 per-unit game-logic checks in about 3.5 minutes, plus a 2-minute smoke suite, C++ unit tests, and a bit-for-bit replay check |
| **Faster where it hurts** | ~15% less simulation time in a large battle (`big_battle`: 24.3 to 20.7 ms per frame, median of 3 runs; merged gameplay fixes also change that battle, so part of the gap may be a different fight) |
| **Fixes** | Bugs found by the testbench and code review (including two use-after-frees in our own patches and a GPU-timer hang in upstream), fixed, most with a test guarding them, plus 21 reviewed community PRs upstream hasn't merged |
| **Same game** | Our simulation patches don't change results: a 3,000-frame seeded battle replays identically, checksum for checksum, with and without them |

## For players

- **Big fights feel better.** Rendering and simulation patches cut work that upstream repeats every frame: model data re-uploaded only when it changes, water reflections skipped when no water is on screen, instanced grass, parallel line-of-sight and transform updates, fewer pathfinding allocations.
- **Controls that hold up when the frame rate drops.** With hundreds of units and low FPS, box selection could select nothing and shift-dragging a row of buildings placed only one. Both are fixed, and a test drags the mouse inside a single ~8 fps frame to keep them fixed.
- **Fewer crashes and glitches.** For example: a crash in `GetUnitsInPlanes`, strafing aircraft stuck in the air after a short move, LOS and radar overlays showing the wrong team after switching views, reflections left blank after a map reload.

There are no prebuilt downloads yet: build the engine (see below) and use it for local and single-player games. Online, everyone in a match needs the same engine build, as with any engine version.

## For developers

**The Recoil Workbench** is a test environment built into the engine. It launches the real engine and game, plays scripted scenarios the way a player would, and turns the result into numbers and pass/fail checks:

```bash
python tools/workbench/run.py --suite smoke --engine dev=path/to/spring.exe --data-dir path/to/data
```

- **Game logic, generated from the unit definitions.** Every ground unit, ship and aircraft moves, shoots, builds and gets transported on a flat test arena, at up to 30x game speed. New units are covered without writing a test.
- **Failures that explain themselves.** "Took 0 damage: weapon idle, target in range, line of fire blocked" rather than "check failed".
- **Performance you can compare.** Frame, CPU draw, GPU, sim and memory per scenario window, engine against engine, with a noise-aware verdict and a per-subsystem breakdown of where the time goes.
- **Determinism.** Seeded battles record a checksum every frame; the report names the first frame where two builds diverge.
- **Crash-proof runs.** Crashes, hangs and game-ending scenarios are recorded against the scenario, never a silent pass or a stuck dialog.

It has already found a use-after-free, a GPU-timer hang in upstream, GPU time going unmeasured at high frame rates, and a dozen flaws in its own tests. See [the findings log](doc/workbench/FINDINGS.md), [results](doc/workbench/RESULTS.md) and [how to write a scenario](tools/workbench/README.md).

**Run all the tests locally** (no CI minutes needed):

```bash
wsl -d Ubuntu -- bash /mnt/c/Workspace/bar/RecoilEngine/tools/workbench/test-all.sh
```

**Build on Windows.** Docker is slow on the Windows disk, so build from a WSL mirror, then deploy a runnable engine folder:

```bash
wsl -d Ubuntu -- bash /mnt/c/Workspace/bar/RecoilEngine/tools/workbench/wsl-build.sh
wsl -d Ubuntu -- bash /mnt/c/Workspace/bar/RecoilEngine/tools/workbench/deploy.sh /mnt/c/Workspace/bar/engine-dev
```

## What gets in

- **Community PRs**, one at a time after review, if they're low-to-medium risk: bug fixes, performance work, small UX improvements. No rewrites of core systems, no drafts, and no changes to default controls or balance.
- **Simulation changes must not change results.** They pass a replay-determinism check first.
- **Upstream** is merged regularly (`git fetch origin && git merge origin/master`), then verified with the workbench before it's pushed.

---

# Recoil is an open source real time strategy game engine

Visit the [Official Website](https://recoilengine.org)

## Get the engine sources

    git clone https://github.com/beyond-all-reason/RecoilEngine --recursive

Recoil is a fork and continuation of an RTS [engine](https://github.com/spring/spring) version 105.0

Visit our [Discord](https://discord.gg/GUpRg6Wz3e) for help, suggestions, bugs, community forum and everything Recoil related.

## Installation

You can use a pre-compiled binary, usually, you want to use an installer or a package prepared for your OS:

* <https://github.com/beyond-all-reason/RecoilEngine/releases>


## Compiling

### Preparation

Start with `master` as the primary branch.

Verify you're seeing tags:

```bash
>>> git tag
spring_bar_{BAR105}105.0-430-g2727993
spring_bar_{BAR105}105.1.1-1005-ga7ea1cc
spring_bar_{BAR105}105.1.1-1011-g325620e
spring_bar_{BAR105}105.1.1-1032-gf4d6126
spring_bar_{BAR105}105.1.1-1039-g895d540
spring_bar_{BAR105}105.1.1-1050-g5075cc0
...
```

If you aren't seeing these (often, when you've cloned your fork of the repository and not the upstream version), try the following:

```bash
git remote add upstream https://github.com/beyond-all-reason/RecoilEngine
git fetch --all --tags
```

Make sure `master` is pointing to upstream `master`:

```bash
git checkout master
git branch -u upstream/master
```

### Triggering a build

If you are just starting out and want to get an engine binary, we recommend using our Docker scripts documented in [docker-build-v2/](docker-build-v2/README.md).

If you want to compile the engine without Docker to use a different compiler, to have a better setup with code completion in an IDE, etc., you might want to follow the [building without Docker article](https://recoilengine.org/development/building-without-docker/).

## License

Our Terms are documented in the [LICENSE](LICENSE).

## AI Policy usage

Please adhere to the [AI usage policy](https://github.com/beyond-all-reason/RecoilEngine/blob/master/AI_POLICY.md), if you use such tools.
