#!/usr/bin/env python3
"""Recoil Workbench matrix runner: engines x profiles x repetitions, one engine launch per cell."""
import argparse
import collections
import concurrent.futures
import datetime
import fnmatch
import json
import os
import re
import shutil
import subprocess
import sys
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
Cell = collections.namedtuple("Cell", "engine exe profile rep")

# named scenario sets; --only overrides
SUITES = {
    "smoke": "api_selftest,lua_api_regressions,render_baseline,mass_move_500,weapon_range,ui_lowfps",   # ~2 min
    "standard": "api_selftest,lua_api_regressions,render_baseline,mass_move_*,big_battle,weapon_range,ui_lowfps,ui_lategame,unit_movement,ship_movement,unit_behaviours,air_attack",
    "determinism": "sync_repro",  # use with --spectate --seed N
    "full": "api_selftest,lua_api_regressions,render_baseline,mass_move_*,big_battle,weapon_range_all,ui_lowfps,ui_lategame,ui_lategame_lowfps,unit_movement,ship_movement,unit_behaviours,air_attack",  # hours
    "render": "render_baseline,mass_move_500",  # graphics cost per settings profile
    # every generated game-logic check, at max sim speed (~3.5 min)
    "logic": "unit_behaviours,unit_movement,ship_movement,air_attack,weapon_range_all",
}
# suites that sweep settings profiles unless --profile is given
SUITE_PROFILES = {
    "render": ["low", "default", "ultra"],
}


def expand_matrix(engines, profiles, reps):
    # repetitions outermost: a, b, a, b, ... so machine drift (warm-up, thermals, background
    # load) spreads over every engine instead of biasing whichever ran last
    return [Cell(name, exe, prof, rep) for rep in range(reps) for prof in profiles for name, exe in engines.items()]


def render_startscript(template, map_name, seed=0):
    """seed 0 lets the engine pick a random seed; any other value makes the sim reproducible."""
    return template.replace("$MAP", map_name).replace("$SEED", str(seed))


def status_for(exit_code, timed_out):
    if timed_out:
        return "timeout"
    return {0: "ok", 1: "checks_failed"}.get(exit_code, "error")


# rough relative wall-clock cost of scenarios at max sim speed, used to balance --jobs shards
SCENARIO_COSTS = {
    "weapon_range_all": 130, "unit_movement": 36, "ship_movement": 10, "unit_behaviours": 5,
    "air_attack": 4, "big_battle": 60, "mass_move_5000": 60, "mass_move_2000": 40, "sync_repro": 150,
}
# a shard's private write dir links these from the shared data dir and copies these files;
# everything else (LuaUI widget config, infolog, cache) stays private to the instance
SHARED_DIRS = ("games", "maps", "music", "pool", "packages")
SHARED_FILES = ("devmode.txt", "uikeys.txt")
STATUS_RANK = {"ok": 0, "checks_failed": 1, "error": 2, "timeout": 3}


def known_scenarios(data_dir, exe):
    """Scenario names (file basenames) in the game checkout and the engine's springcontent."""
    names = set()
    game_dir = os.path.join(data_dir, "games", "BAR.sdd", "workbench", "scenarios")
    if os.path.isdir(game_dir):
        names.update(os.path.splitext(f)[0] for f in os.listdir(game_dir) if f.endswith(".lua"))
    content = os.path.join(os.path.dirname(exe), "base", "springcontent.sdz")
    if os.path.exists(content):
        with zipfile.ZipFile(content) as z:
            for n in z.namelist():
                if n.startswith("workbench/scenarios/") and n.endswith(".lua"):
                    names.add(os.path.splitext(os.path.basename(n))[0])
    return sorted(names)


def resolve_scenarios(patterns, names):
    """Expands comma-separated globs against known names, in pattern order; unknown literals are
    kept so the engine reports them as unmatched."""
    out = []
    for pat in patterns.split(","):
        hits = [n for n in names if fnmatch.fnmatchcase(n, pat)] or [pat]
        out.extend(h for h in hits if h not in out)
    return out


def shard(names, n, costs):
    """Greedy longest-first split into at most n shards of similar cost; no empty shards."""
    shards = [[] for _ in range(max(1, n))]
    load = [0] * len(shards)
    for name in sorted(names, key=lambda x: -costs.get(x, 1)):
        i = load.index(min(load))
        shards[i].append(name)
        load[i] += costs.get(name, 1)
    return [s for s in shards if s]


def make_write_dir(data_dir, path):
    """A private engine write dir sharing the game and maps with data_dir (directory junctions
    on Windows, symlinks elsewhere), so several engine instances can run at once."""
    os.makedirs(path, exist_ok=True)
    for d in SHARED_DIRS:
        src, dst = os.path.join(data_dir, d), os.path.join(path, d)
        if os.path.isdir(src) and not os.path.exists(dst):
            if os.name == "nt":
                import _winapi
                _winapi.CreateJunction(os.path.abspath(src), dst)
            else:
                os.symlink(os.path.abspath(src), dst)
    for f in SHARED_FILES:
        if os.path.exists(os.path.join(data_dir, f)):
            shutil.copyfile(os.path.join(data_dir, f), os.path.join(path, f))
    return path


def tail_lines(path, n):
    try:
        with open(path, encoding="utf-8", errors="replace") as f:
            return f.read().splitlines()[-n:]
    except OSError:
        return []


def prepare_cell(cell, args, out_root, scenarios=None, write_dir=None, shard_name=None):
    """Writes the cell's start script and config, returns (cell_dir, command). With a shard, the
    shard's files go to <cell>/<shard_name>/ while results still land in <cell>/results."""
    base_dir = os.path.join(out_root, cell.engine, cell.profile, f"rep{cell.rep}")
    cell_dir = os.path.join(base_dir, shard_name) if shard_name else base_dir
    os.makedirs(cell_dir, exist_ok=True)
    # --spectate: the local player only watches and both teams are NullAI, so no widget can
    # issue timing-dependent orders (required for bit-identical sync_repro runs)
    template = "startscript_spectate.txt" if args.spectate else "startscript.txt"
    with open(os.path.join(HERE, "templates", template), encoding="utf-8") as f:
        script = render_startscript(f.read(), args.map, args.seed)
    script_path = os.path.join(cell_dir, "startscript.txt")
    with open(script_path, "w", encoding="utf-8") as f:
        f.write(script)
    # the engine writes settings back into its --config file, so give each cell its own copy
    config_path = os.path.join(cell_dir, "springsettings.cfg")
    shutil.copyfile(os.path.join(HERE, "profiles", cell.profile + ".cfg"), config_path)
    extra = {}
    if args.filter:
        # generated scenarios (one case per unit type, ...) only run cases matching these globs
        extra["WorkbenchFilter"] = args.filter
    if args.sim_speed:
        # every scenario at this sim speed ("max" or a factor), e.g. to profile the sim flat out
        extra["WorkbenchSimSpeed"] = args.sim_speed
    if extra:
        with open(config_path, "a", encoding="utf-8") as f:
            f.write("\n" + "".join(f"{k} = {v}\n" for k, v in extra.items()))

    cmd = [
        cell.exe,
        "--isolation", "--write-dir", write_dir or args.data_dir,
        "--config", config_path,
        "--workbench", ",".join(scenarios) if scenarios else args.only,
        "--workbench-out", os.path.join(base_dir, "results"),
        "--workbench-timeout", str(args.timeout),
        "--workbench-profile", cell.profile,
        script_path,
    ]
    with open(os.path.join(cell_dir, "command.txt"), "w", encoding="utf-8") as f:
        f.write(subprocess.list2cmdline(cmd))
    return cell_dir, cmd


_ISSUE_PATTERNS = [
    # (kind, regex); first match wins per line
    ("widget_load_failed", re.compile(r"Failed to load: (\S+)\s+\((?!no GetInfo\(\) call)(.*)")),
    ("fatal", re.compile(r"\bFatal:\s*(.*)")),
    ("lua_error", re.compile(r"\[(?:LuaUI|LuaRules|LuaGaia|LuaIntro|LuaMenu)\] Error:?\s*(.*)")),
    # a scenario that fails to load silently drops out of glob suites; callin errors reach no check.
    # (synced function errors are not listed: they reach the scenario through ctx.call)
    ("scenario_load_failed", re.compile(r"\[Workbench\] Error: failed to load (.*)")),
    ("scenario_callin_error", re.compile(r"\[Workbench\] Error: (\S+ Unit\w+: .*)")),
]
_TIMESTAMP = re.compile(r"^\[t=[^\]]*\](\[f=[^\]]*\])?\s*")


def scan_infolog(lines):
    """Returns [(kind, message)] for widget load failures, Lua errors and fatal errors, de-duplicated."""
    issues, seen = [], set()
    for line in lines:
        for kind, pattern in _ISSUE_PATTERNS:
            if pattern.search(line):
                msg = _TIMESTAMP.sub("", line).strip()
                if (kind, msg) not in seen:
                    seen.add((kind, msg))
                    issues.append((kind, msg))
                break
    return issues


def collect_infolog(data_dir, cell_dir, started):
    """Copies this run's infolog into the cell; a log older than the launch belongs to a previous run."""
    infolog = os.path.join(data_dir, "infolog.txt")
    if not os.path.exists(infolog) or os.path.getmtime(infolog) < started:
        return False
    shutil.copy(infolog, os.path.join(cell_dir, "infolog.txt"))
    return True


def run_cell(cell, args, out_root, scenarios=None, write_dir=None, shard_name=None):
    cell_dir, cmd = prepare_cell(cell, args, out_root, scenarios, write_dir, shard_name)
    data_dir = write_dir or args.data_dir
    started = datetime.datetime.now().timestamp()
    timed_out, code = False, None
    try:
        code = subprocess.run(cmd, cwd=os.path.dirname(cell.exe), timeout=args.timeout + 120).returncode
    except subprocess.TimeoutExpired:
        timed_out = True

    fresh_log = collect_infolog(data_dir, cell_dir, started)
    infolog = os.path.join(cell_dir, "infolog.txt") if fresh_log else ""
    # per-frame sync checksums written by the sync_repro scenario (BAR dbg_synctest)
    synchash = os.path.join(data_dir, "synctest_synchash.json")
    if os.path.exists(synchash) and os.path.getmtime(synchash) >= started:
        shutil.move(synchash, os.path.join(cell_dir, "synchash.json"))
    status = status_for(code, timed_out)
    result = {
        "engine": cell.engine, "profile": cell.profile, "rep": cell.rep,
        "status": status, "exit_code": code,
        "results_dir": os.path.join(os.path.join(out_root, cell.engine, cell.profile, f"rep{cell.rep}"), "results"),
        "infolog_tail": tail_lines(infolog, 40) if status in ("error", "timeout") else [],
        "log_issues": [{"kind": k, "message": m} for k, m in scan_infolog(tail_lines(infolog, 10**7))],
    }
    with open(os.path.join(cell_dir, "cell.json"), "w", encoding="utf-8") as f:
        json.dump(result, f, indent=2)
    return result


def run_cell_sharded(cell, args, out_root):
    """Runs the cell's scenarios as args.jobs engine instances at once and merges their results."""
    names = resolve_scenarios(args.only, known_scenarios(args.data_dir, cell.exe))
    shards = shard(names, args.jobs, SCENARIO_COSTS)
    if len(shards) == 1:
        return run_cell(cell, args, out_root)
    work = []
    for i, sc in enumerate(shards):
        wd = make_write_dir(args.data_dir, os.path.join(out_root, "_writedirs", f"{cell.engine}-{cell.profile}-{cell.rep}-{i}"))
        work.append((sc, wd, f"shard{i}"))
    with concurrent.futures.ThreadPoolExecutor(len(work)) as pool:
        parts = list(pool.map(lambda w: run_cell(cell, args, out_root, *w), work))
    worst = max(parts, key=lambda r: STATUS_RANK.get(r["status"], 2))
    merged = dict(worst)
    merged["shards"] = [{"scenarios": w[0], "status": r["status"], "exit_code": r["exit_code"]} for w, r in zip(work, parts)]
    merged["log_issues"] = [i for r in parts for i in r["log_issues"]]
    base_dir = os.path.join(out_root, cell.engine, cell.profile, f"rep{cell.rep}")
    with open(os.path.join(base_dir, "cell.json"), "w", encoding="utf-8") as f:
        json.dump(merged, f, indent=2)
    return merged


def parse_args(argv):
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--engine", action="append", required=True, help="NAME=PATH to spring.exe (repeatable)")
    p.add_argument("--data-dir", required=True, help="write/data dir with games/BAR.sdd and maps/")
    p.add_argument("--only", default=None, help="scenario name pattern(s), comma separated globs")
    p.add_argument("--suite", default=None, help="named scenario set: " + ", ".join(SUITES))
    p.add_argument("--profile", action="append", default=None, help="settings profile name (repeatable)")
    p.add_argument("--filter", default=None,
                   help="case name globs for generated scenarios, e.g. a unit name (comma separated)")
    p.add_argument("--sim-speed", default=None,
                   help="run every scenario at this sim speed ('max' or a factor); timings are then not real-time")
    p.add_argument("--reps", type=int, default=1)
    p.add_argument("--jobs", type=int, default=1,
                   help="engine instances per cell, each running a share of the scenarios (logic suites; "
                        "timings from parallel instances are not comparable; on a 14-thread machine the sim's own "
                        "thread pool already saturates the CPU and 3 jobs were slower than 1: measure first)")
    p.add_argument("--timeout", type=int, default=1800)
    p.add_argument("--map", default="Red Comet Remake 1.8")
    p.add_argument("--seed", type=int, default=0, help="FixedRNGSeed for reproducible runs (0 = random)")
    p.add_argument("--spectate", action="store_true",
                   help="local player spectates, NullAI controls both teams (determinism runs)")
    p.add_argument("--out", default=os.path.join(HERE, "results"))
    p.add_argument("--no-report", action="store_true")
    args = p.parse_args(argv)
    if args.suite is not None and args.suite not in SUITES:
        p.error(f"unknown suite '{args.suite}' (known: {', '.join(SUITES)})")
    if args.only is None:
        args.only = SUITES[args.suite] if args.suite else SUITES["smoke"]
    args.profile = args.profile or SUITE_PROFILES.get(args.suite, ["default"])
    for prof in args.profile:
        if not os.path.exists(os.path.join(HERE, "profiles", prof + ".cfg")):
            p.error(f"unknown profile '{prof}' (no profiles/{prof}.cfg)")
    engines = {}
    for spec in args.engine:
        name, _, path = spec.partition("=")
        if not path or not os.path.exists(path):
            p.error(f"--engine {spec}: expected NAME=PATH to an existing spring.exe")
        engines[name] = os.path.abspath(path)  # the engine runs with its own dir as cwd
    args.engines = engines
    return args


def main(argv=None):
    args = parse_args(argv if argv is not None else sys.argv[1:])
    out_root = os.path.join(args.out, datetime.datetime.now().strftime("%Y%m%d-%H%M%S"))
    cells = expand_matrix(args.engines, args.profile, args.reps)
    summary = []
    for i, cell in enumerate(cells, 1):
        print(f"[{i}/{len(cells)}] {cell.engine} / {cell.profile} / rep {cell.rep} ...", flush=True)
        r = run_cell_sharded(cell, args, out_root) if args.jobs > 1 else run_cell(cell, args, out_root)
        print(f"    -> {r['status']} (exit {r['exit_code']})", flush=True)
        summary.append(r)
    with open(os.path.join(out_root, "summary.json"), "w", encoding="utf-8") as f:
        json.dump(summary, f, indent=2)
    if not args.no_report:
        import report
        path = report.write_report(out_root)
        print(f"report: {path}")
    return 0 if all(r["status"] == "ok" for r in summary) else 1


if __name__ == "__main__":
    sys.exit(main())
