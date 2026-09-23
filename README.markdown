# akudilcz/RecoilEngine: a high-performance, well-tested Recoil fork

This fork of [Recoil](https://github.com/beyond-all-reason/RecoilEngine) exists to keep a **faster, bug-free variant of Beyond All Reason** running. It is paired with the game fork [akudilcz/Beyond-All-Reason](https://github.com/akudilcz/Beyond-All-Reason).

It carries three things on top of upstream:

1. **Performance patches**: rendering, simulation, pathfinding and Lua API optimisations, all behaviour-neutral for the synced simulation. Examples: model uniforms re-uploaded only when they change, batched units under construction, water reflection skipped when no water is in view, instanced grass, parallel LOS status and transform snapshots, a per-frame `GetVisibleUnits` cache, and fewer allocations in QTPFS.
2. **Fixes**: our own (e.g. drag-build at low frame rates losing Shift because key modifiers were read from the end of the event batch) plus reviewed community pull requests that upstream hasn't merged yet.
3. **The Recoil Workbench** *(in development)*: a test environment built into the engine that runs the real engine and game like a player would, measures performance, checks every unit's behaviour, and verifies the simulation replays bit-identically. It gates everything that goes into this fork. Design: [`doc/superpowers/specs/2026-09-23-workbench-design.md`](doc/superpowers/specs/2026-09-23-workbench-design.md).

### Rules for what goes in

- Community PRs are merged one at a time after review, if they are low-to-medium risk: bug fixes, performance work, and minor UX or gameplay improvements. Major rewrites of core systems, drafts, and changes to default controls or balance stay out.
- Changes to synced simulation code must not change results. Anything that could must pass a desync/replay check first.
- Multiplayer note: synced changes only desync against players running a *different* engine build. Single-player and local games are unaffected; for online play, everyone needs the same build.

### Building (Windows host)

Docker builds are very slow when the source lives on the Windows disk, so build from a WSL mirror:

    wsl -d Ubuntu -- bash /mnt/c/Workspace/bar/RecoilEngine/tools/workbench/wsl-build.sh

This rsyncs the checkout into WSL and runs `docker-build-v2/build.sh` for `engine-legacy` and `basecontent`. Pass other targets as arguments.

### Staying in sync with upstream

`origin` points at upstream, `fork` at this repository:

    git fetch origin
    git merge origin/master      # resolve conflicts where upstream touched the same code
    git push fork master

Upstream changes go through the workbench before they're pushed, once it's available.

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
