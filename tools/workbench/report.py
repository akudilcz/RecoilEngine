#!/usr/bin/env python3
"""Recoil Workbench report: turns a run directory into a self-contained compare.html."""
import glob
import html
import json
import os
import sys


def median(values):
    s = sorted(values)
    n = len(s)
    if n == 0:
        return None
    mid = n // 2
    return s[mid] if n % 2 else (s[mid - 1] + s[mid]) / 2


def compare(baseline, candidate, rel_threshold=0.05):
    """Lower is better (times). A change counts only beyond both the relative threshold and the baseline spread."""
    if not baseline or not candidate:
        return "n/a"
    b, c = median(baseline), median(candidate)
    if b == 0:
        return "n/a"
    delta = c - b
    spread = max(baseline) - min(baseline)
    if abs(delta) <= max(abs(b) * rel_threshold, spread):
        return "same"
    return "regression" if delta > 0 else "improvement"


def compare_sync(baseline, candidate):
    """Compares two lists of {frame, checksum}; returns ("identical"|"diverged"|"n/a", first differing frame)."""
    if not baseline or not candidate:
        return "n/a", None
    for i, b in enumerate(baseline):
        if i >= len(candidate):
            return "diverged", b["frame"]
        c = candidate[i]
        if c["frame"] != b["frame"] or c["checksum"] != b["checksum"]:
            return "diverged", b["frame"]
    if len(candidate) > len(baseline):
        return "diverged", candidate[len(baseline)]["frame"]
    return "identical", None


def load_cells(out_root):
    with open(os.path.join(out_root, "summary.json"), encoding="utf-8") as f:
        cells = json.load(f)
    for cell in cells:
        cell["scenarios"] = {}
        for path in glob.glob(os.path.join(cell["results_dir"], "*.json")):
            if os.path.basename(path) == "run.json":
                continue
            with open(path, encoding="utf-8") as f:
                data = json.load(f)
            cell["scenarios"][data["scenario"]] = data
        sync_path = os.path.join(os.path.dirname(cell["results_dir"]), "synchash.json")
        cell["sync"] = None
        if os.path.exists(sync_path):
            with open(sync_path, encoding="utf-8") as f:
                cell["sync"] = json.load(f)
    return cells


def _metric_series(cells, engine, profile, scenario, window, metric):
    out = []
    for c in cells:
        if c["engine"] == engine and c["profile"] == profile:
            sc = c["scenarios"].get(scenario)
            for w in (sc or {}).get("windows", []):
                if w["name"] == window and w[metric]["count"] > 0:
                    out.append(w[metric]["p50"])
    return out


def write_report(out_root):
    cells = load_cells(out_root)
    engines = list(dict.fromkeys(c["engine"] for c in cells))
    base = engines[0] if engines else None
    rows, checks, errors = [], [], []

    keys = set()
    for c in cells:
        if c["status"] in ("error", "timeout"):
            errors.append(c)
        for name, sc in c["scenarios"].items():
            for w in sc.get("windows", []):
                keys.add((c["profile"], name, w["name"]))
            for chk in sc.get("checks", []):
                if not chk["pass"]:
                    checks.append((c["engine"], c["profile"], name, chk["name"], chk["detail"], c["results_dir"]))

    for profile, scenario, window in sorted(keys):
        for metric in ("frameTimeMs", "simTimeMs", "gpuTimeMs", "drawTimeMs"):
            bs = _metric_series(cells, base, profile, scenario, window, metric)
            for eng in engines:
                s = _metric_series(cells, eng, profile, scenario, window, metric)
                verdict = "baseline" if eng == base else compare(bs, s)
                rows.append((profile, scenario, window, metric, eng, median(s), verdict))

    esc = html.escape
    parts = ["<!doctype html><meta charset=utf-8><title>Workbench report</title>",
             "<style>body{font:14px system-ui;margin:24px}table{border-collapse:collapse}"
             "td,th{border:1px solid #ccc;padding:4px 8px}.regression{background:#fdd}"
             ".improvement{background:#dfd}</style>",
             f"<h1>Workbench report</h1><p>{esc(out_root)}</p>"]
    regs = [r for r in rows if r[6] == "regression"]
    imps = [r for r in rows if r[6] == "improvement"]
    parts.append(f"<h2>Summary</h2><p>{len(regs)} regressions, {len(imps)} improvements, "
                 f"{len(checks)} failed checks, {len(errors)} errored cells, "
                 f"{sum(len(c.get('log_issues', [])) for c in cells)} log issues.</p>")
    if errors:
        parts.append("<h2>Errors</h2><ul>")
        for c in errors:
            parts.append(f"<li>{esc(c['engine'])}/{esc(c['profile'])} rep {c['rep']}: {esc(c['status'])}"
                         f"<pre>{esc(chr(10).join(c['infolog_tail']))}</pre></li>")
        parts.append("</ul>")
    issues = [(c["engine"], c["profile"], i["kind"], i["message"]) for c in cells for i in c.get("log_issues", [])]
    if issues:
        parts.append("<h2>Log issues</h2><table><tr><th>engine</th><th>profile</th><th>kind</th><th>message</th></tr>")
        for eng, prof, kind, msg in issues:
            parts.append(f"<tr><td>{esc(eng)}</td><td>{esc(prof)}</td><td>{esc(kind)}</td><td>{esc(msg)}</td></tr>")
        parts.append("</table>")
    synced = [c for c in cells if c.get("sync")]
    if synced:
        base_sync = next((c["sync"] for c in synced if c["engine"] == base), synced[0]["sync"])
        parts.append("<h2>Simulation determinism</h2><p>Per-frame sync checksums of the seeded sync_repro battle, "
                     "compared with the first cell of the baseline engine.</p><table><tr><th>engine</th><th>profile</th>"
                     "<th>rep</th><th>frames</th><th>digest</th><th>vs baseline</th></tr>")
        for c in synced:
            verdict, frame = compare_sync(base_sync["checksums"], c["sync"]["checksums"])
            text = verdict if frame is None else f"diverged at run frame {frame}"
            cls = "regression" if verdict == "diverged" else ""
            parts.append(f"<tr class='{cls}'><td>{esc(c['engine'])}</td><td>{esc(c['profile'])}</td><td>{c['rep']}</td>"
                         f"<td>{c['sync'].get('frameCount')}</td><td>{esc(str(c['sync'].get('digest')))}</td>"
                         f"<td>{esc(text)}</td></tr>")
        parts.append("</table>")
    if checks:
        parts.append("<h2>Failed checks</h2><table><tr><th>engine</th><th>profile</th><th>scenario</th>"
                     "<th>check</th><th>detail</th><th>repro</th></tr>")
        for eng, prof, sc, name, detail, rdir in checks:
            cmd_path = os.path.join(os.path.dirname(rdir), "command.txt")
            parts.append(f"<tr><td>{esc(eng)}</td><td>{esc(prof)}</td><td>{esc(sc)}</td><td>{esc(name)}</td>"
                         f"<td>{esc(detail)}</td><td>{esc(cmd_path)}</td></tr>")
        parts.append("</table>")
    parts.append("<h2>Metrics (median of p50 per repetition, ms)</h2><table><tr><th>profile</th><th>scenario</th>"
                 "<th>window</th><th>metric</th><th>engine</th><th>median</th><th>vs baseline</th></tr>")
    for profile, scenario, window, metric, eng, med, verdict in rows:
        cls = verdict if verdict in ("regression", "improvement") else ""
        val = "" if med is None else f"{med:.2f}"
        parts.append(f"<tr class='{cls}'><td>{esc(profile)}</td><td>{esc(scenario)}</td><td>{esc(window)}</td>"
                     f"<td>{esc(metric)}</td><td>{esc(eng)}</td><td>{val}</td><td>{esc(verdict)}</td></tr>")
    parts.append("</table>")
    path = os.path.join(out_root, "compare.html")
    with open(path, "w", encoding="utf-8") as f:
        f.write("".join(parts))
    return path


if __name__ == "__main__":
    print(write_report(sys.argv[1]))
