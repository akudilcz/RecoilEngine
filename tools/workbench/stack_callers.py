#!/usr/bin/env python3
"""Who calls the hot functions: reads `perf script -F comm,ip,sym --no-inline` output and, for
each leaf function (or the top N by self samples), lists the call chains above it.

    perf script -i perf.data -F comm,ip,sym --no-inline > stacks.txt
    python3 tools/workbench/stack_callers.py stacks.txt [--top 8] [--depth 3] [leaf substrings...]
"""
import argparse
import collections
import re
import sys

IDLE = ("moodycamel::", "linux_signal::wait_for", "ThreadPool::WorkerLoop", "spring_futex",
        "[unknown]", "syscall", "__GI_", "__syscall", "futex")


def parse(path):
    samples, cur = [], None
    for line in open(path, errors="replace"):
        if not line.strip():
            continue
        if not line[0].isspace():  # comm line starts a sample
            if cur:
                samples.append(cur)
            cur = []
            continue
        frame = re.sub(r"^\s*[0-9a-f]+\s+", "", line.rstrip())
        cur.append(re.sub(r"\(.*", "", frame))  # drop argument lists
    if cur:
        samples.append(cur)
    return samples


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("stacks")
    p.add_argument("leaves", nargs="*", help="leaf function substrings (default: the top self-time functions)")
    p.add_argument("--top", type=int, default=8)
    p.add_argument("--depth", type=int, default=3)
    args = p.parse_args(argv)

    samples = parse(args.stacks)
    busy = [s for s in samples if s and not s[0].startswith(IDLE)]
    print(f"{len(samples)} samples, {len(busy)} not idle-waiting")
    leaves = args.leaves or [f for f, _ in collections.Counter(s[0] for s in busy).most_common(args.top)]
    for leaf in leaves:
        hits = [s for s in busy if leaf in s[0]]
        chains = collections.Counter(" <- ".join(s[1:1 + args.depth]) for s in hits)
        print(f"\n== {leaf}: {len(hits)} samples ({100 * len(hits) / max(len(busy), 1):.1f}% of busy)")
        for chain, n in chains.most_common(5):
            print(f"  {100 * n / max(len(hits), 1):5.1f}%  {chain[:220]}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
