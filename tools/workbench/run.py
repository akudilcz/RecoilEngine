#!/usr/bin/env python3
"""Recoil Workbench matrix runner: engines x profiles x repetitions, one engine launch per cell."""
import argparse
import collections
import datetime
import json
import os
import re
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
Cell = collections.namedtuple("Cell", "engine exe profile rep")

# named scenario sets; --only overrides
SUITES = {
    "smoke": "api_selftest,render_baseline,mass_move_500,weapon_range,ui_lowfps",   # ~12 min
    "standard": "api_selftest,render_baseline,mass_move_*,big_battle,weapon_range,ui_lowfps,unit_movement,ship_movement,unit_behaviours,air_attack",
    "determinism": "sync_repro",  # use with --spectate --seed N
    "full": "api_selftest,render_baseline,mass_move_*,big_battle,weapon_range_all,ui_lowfps,unit_movement,ship_movement,unit_behaviours,air_attack",  # hours
    "render": "render_baseline,mass_move_500",  # graphics cost per settings profile
}
# suites that sweep settings profiles unless --profile is given
SUITE_PROFILES = {
    "render": ["low", "default", "ultra"],
}


def expand_matrix(engines, profiles, reps):
    return [Cell(name, exe, prof, rep) for name, exe in engines.items() for prof in profiles for rep in range(reps)]


def render_startscript(template, map_name, seed=0):
    """seed 0 lets the engine pick a random seed; any other value makes the sim reproducible."""
    return template.replace("$MAP", map_name).replace("$SEED", str(seed))


def status_for(exit_code, timed_out):
    if timed_out:
        return "timeout"
    return {0: "ok", 1: "checks_failed"}.get(exit_code, "error")


def tail_lines(path, n):
    try:
        with open(path, encoding="utf-8", errors="replace") as f:
            return f.read().splitlines()[-n:]
    except OSError:
        return []


def prepare_cell(cell, args, out_root):
    """Writes the cell's start script and config, returns (cell_dir, command)."""
    cell_dir = os.path.join(out_root, cell.engine, cell.profile, f"rep{cell.rep}")
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

    cmd = [
        cell.exe,
        "--isolation", "--write-dir", args.data_dir,
        "--config", config_path,
        "--workbench", args.only,
        "--workbench-out", os.path.join(cell_dir, "results"),
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


def run_cell(cell, args, out_root):
    cell_dir, cmd = prepare_cell(cell, args, out_root)
    started = datetime.datetime.now().timestamp()
    timed_out, code = False, None
    try:
        code = subprocess.run(cmd, cwd=os.path.dirname(cell.exe), timeout=args.timeout + 120).returncode
    except subprocess.TimeoutExpired:
        timed_out = True

    fresh_log = collect_infolog(args.data_dir, cell_dir, started)
    infolog = os.path.join(cell_dir, "infolog.txt") if fresh_log else ""
    # per-frame sync checksums written by the sync_repro scenario (BAR dbg_synctest)
    synchash = os.path.join(args.data_dir, "synctest_synchash.json")
    if os.path.exists(synchash) and os.path.getmtime(synchash) >= started:
        shutil.move(synchash, os.path.join(cell_dir, "synchash.json"))
    status = status_for(code, timed_out)
    result = {
        "engine": cell.engine, "profile": cell.profile, "rep": cell.rep,
        "status": status, "exit_code": code,
        "results_dir": os.path.join(cell_dir, "results"),
        "infolog_tail": tail_lines(infolog, 40) if status in ("error", "timeout") else [],
        "log_issues": [{"kind": k, "message": m} for k, m in scan_infolog(tail_lines(infolog, 10**7))],
    }
    with open(os.path.join(cell_dir, "cell.json"), "w", encoding="utf-8") as f:
        json.dump(result, f, indent=2)
    return result


def parse_args(argv):
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--engine", action="append", required=True, help="NAME=PATH to spring.exe (repeatable)")
    p.add_argument("--data-dir", required=True, help="write/data dir with games/BAR.sdd and maps/")
    p.add_argument("--only", default=None, help="scenario name pattern(s), comma separated globs")
    p.add_argument("--suite", default=None, help="named scenario set: " + ", ".join(SUITES))
    p.add_argument("--profile", action="append", default=None, help="settings profile name (repeatable)")
    p.add_argument("--reps", type=int, default=1)
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
        engines[name] = path
    args.engines = engines
    return args


def main(argv=None):
    args = parse_args(argv if argv is not None else sys.argv[1:])
    out_root = os.path.join(args.out, datetime.datetime.now().strftime("%Y%m%d-%H%M%S"))
    cells = expand_matrix(args.engines, args.profile, args.reps)
    summary = []
    for i, cell in enumerate(cells, 1):
        print(f"[{i}/{len(cells)}] {cell.engine} / {cell.profile} / rep {cell.rep} ...", flush=True)
        r = run_cell(cell, args, out_root)
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
