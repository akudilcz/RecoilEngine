#!/usr/bin/env python3
"""Recoil Workbench matrix runner: engines x profiles x repetitions, one engine launch per cell."""
import argparse
import collections
import datetime
import json
import os
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
Cell = collections.namedtuple("Cell", "engine exe profile rep")


def expand_matrix(engines, profiles, reps):
    return [Cell(name, exe, prof, rep) for name, exe in engines.items() for prof in profiles for rep in range(reps)]


def render_startscript(template, map_name):
    return template.replace("$MAP", map_name)


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


def run_cell(cell, args, out_root):
    cell_dir = os.path.join(out_root, cell.engine, cell.profile, f"rep{cell.rep}")
    os.makedirs(cell_dir, exist_ok=True)
    with open(os.path.join(HERE, "templates", "startscript.txt"), encoding="utf-8") as f:
        script = render_startscript(f.read(), args.map)
    script_path = os.path.join(cell_dir, "startscript.txt")
    with open(script_path, "w", encoding="utf-8") as f:
        f.write(script)

    cmd = [
        cell.exe,
        "--isolation", "--write-dir", args.data_dir,
        "--config", os.path.join(HERE, "profiles", cell.profile + ".cfg"),
        "--workbench", args.only,
        "--workbench-out", os.path.join(cell_dir, "results"),
        "--workbench-timeout", str(args.timeout),
        "--workbench-profile", cell.profile,
        script_path,
    ]
    with open(os.path.join(cell_dir, "command.txt"), "w", encoding="utf-8") as f:
        f.write(subprocess.list2cmdline(cmd))

    timed_out, code = False, None
    try:
        code = subprocess.run(cmd, cwd=os.path.dirname(cell.exe), timeout=args.timeout + 120).returncode
    except subprocess.TimeoutExpired:
        timed_out = True

    infolog = os.path.join(args.data_dir, "infolog.txt")
    if os.path.exists(infolog):
        shutil.copy(infolog, os.path.join(cell_dir, "infolog.txt"))
    status = status_for(code, timed_out)
    result = {
        "engine": cell.engine, "profile": cell.profile, "rep": cell.rep,
        "status": status, "exit_code": code,
        "results_dir": os.path.join(cell_dir, "results"),
        "infolog_tail": tail_lines(infolog, 40) if status in ("error", "timeout") else [],
    }
    with open(os.path.join(cell_dir, "cell.json"), "w", encoding="utf-8") as f:
        json.dump(result, f, indent=2)
    return result


def parse_args(argv):
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--engine", action="append", required=True, help="NAME=PATH to spring.exe (repeatable)")
    p.add_argument("--data-dir", required=True, help="write/data dir with games/BAR.sdd and maps/")
    p.add_argument("--only", default="*", help="scenario name pattern(s), comma separated globs")
    p.add_argument("--profile", action="append", default=None, help="settings profile name (repeatable)")
    p.add_argument("--reps", type=int, default=1)
    p.add_argument("--timeout", type=int, default=1800)
    p.add_argument("--map", default="Red Comet Remake 1.8")
    p.add_argument("--out", default=os.path.join(HERE, "results"))
    p.add_argument("--no-report", action="store_true")
    args = p.parse_args(argv)
    args.profile = args.profile or ["default"]
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
