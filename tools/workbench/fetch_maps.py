#!/usr/bin/env python3
"""Downloads the most played BAR maps into a data dir's maps/ folder.

Popularity = how often each map appears in the most recent public replays
(api.bar-rts.com). Download URLs come from BAR's live map list, the one the lobby uses.

    python3 tools/workbench/fetch_maps.py --data-dir /store/bar/data --top 20
"""
import argparse
import collections
import json
import os
import sys
import urllib.request

REPLAYS = "https://api.bar-rts.com/replays?page={page}&limit=500"
LIVE_MAPS = "https://maps-metadata.beyondallreason.dev/latest/live_maps.validated.json"


# some of BAR's endpoints refuse Python's default User-Agent
HEADERS = {"User-Agent": "recoil-workbench-fetch-maps/1.0"}


def get_json(url):
    with urllib.request.urlopen(urllib.request.Request(url, headers=HEADERS), timeout=60) as r:
        return json.load(r)


def download(url, dest):
    tmp = dest + ".part"
    with urllib.request.urlopen(urllib.request.Request(url, headers=HEADERS), timeout=300) as r, open(tmp, "wb") as f:
        while chunk := r.read(1 << 20):
            f.write(chunk)
    os.replace(tmp, dest)


def rank_maps(replays):
    """[(scriptName, count)] most played first; ties by name for a stable order."""
    counts = collections.Counter(r["Map"]["scriptName"] for r in replays if r.get("Map"))
    return sorted(counts.items(), key=lambda kv: (-kv[1], kv[0]))


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--data-dir", required=True)
    p.add_argument("--top", type=int, default=20)
    p.add_argument("--replays", type=int, default=5000, help="how many recent replays to count")
    args = p.parse_args(argv)

    replays = []
    for page in range(1, args.replays // 500 + 1):
        replays += get_json(REPLAYS.format(page=page))["data"]
    ranked = rank_maps(replays)
    live = {m["springName"]: m for m in get_json(LIVE_MAPS)}
    span = f"{replays[-1]['startTime'][:16]} .. {replays[0]['startTime'][:16]} UTC"
    print(f"{len(replays)} replays ({span}), {len(ranked)} distinct maps")

    maps_dir = os.path.join(args.data_dir, "maps")
    os.makedirs(maps_dir, exist_ok=True)
    got = 0
    for name, count in ranked:
        if got >= args.top:
            break
        m = live.get(name)
        if m is None:
            print(f"  skip {name} ({count} games): not in the live map list")
            continue
        got += 1
        dest = os.path.join(maps_dir, m["fileName"])
        share = 100.0 * count / len(replays)
        if os.path.exists(dest):
            print(f"{got:2}. {name:40} {count:5} games ({share:4.1f}%)  already present")
            continue
        download(m["downloadURL"], dest)
        print(f"{got:2}. {name:40} {count:5} games ({share:4.1f}%)  {os.path.getsize(dest) / 2**20:6.1f} MB")
    return 0


if __name__ == "__main__":
    sys.exit(main())
