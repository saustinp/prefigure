#!/usr/bin/env python3
"""Extract every FunctionCall event on a Web Worker thread, with name +
duration + timestamp.  The goal is to disambiguate three explanations
for "9 dropdown clicks → 18 worker FunctionCalls":

  A) Each click affects both prefigure clones (name collision on `pos`),
     producing 2 distinct compiles per click.  All ~18 calls long.
  B) Each render fires 2 worker FunctionCalls (compile + postMessage
     result handler).  Half short (~1ms), half long (~13ms).
  C) User clicked both dropdowns (~9+9 clicks).  All ~18 long, but
     timestamps cluster in two groups (one per dropdown).
"""

import sys, os
from collections import defaultdict
import ijson


def drill_workers(path: str) -> None:
    print(f"\n=== {path} ({os.path.getsize(path) / 1e6:.1f} MB) ===")

    thread_names = {}
    process_names = {}
    fn_calls_by_thread = defaultdict(list)

    with open(path, "rb") as f:
        for evt in ijson.items(f, "traceEvents.item"):
            name = evt.get("name", "")
            pid = evt.get("pid")
            tid = evt.get("tid")
            args = evt.get("args", {}) or {}
            if name == "thread_name":
                thread_names[(pid, tid)] = args.get("name", "?")
                continue
            if name == "process_name":
                process_names[pid] = args.get("name", "?")
                continue
            if name == "FunctionCall" and evt.get("ph") == "X":
                data = args.get("data", {}) or {}
                fn_calls_by_thread[(pid, tid)].append({
                    "ts": evt.get("ts"),
                    "dur": evt.get("dur", 0),
                    "name": data.get("functionName") or "<anon>",
                    "url": data.get("url", ""),
                    "line": data.get("lineNumber"),
                    "col": data.get("columnNumber"),
                })

    # Filter to threads named like a Web Worker.
    worker_threads = [k for k, name in thread_names.items() if "Worker" in (name or "")]
    if not worker_threads:
        print("no worker threads found")
        return

    for key in worker_threads:
        proc = process_names.get(key[0], "?")
        thread = thread_names.get(key, "?")
        calls = sorted(fn_calls_by_thread.get(key, []), key=lambda c: c["ts"] or 0)
        if not calls:
            continue
        print(f"\n--- {thread!r} (pid={key[0]} tid={key[1]}) -- {len(calls)} FunctionCalls ---")
        # full table
        print(f"{'idx':>3}  {'ts (s)':>10}  {'dur(ms)':>8}  {'name':24}  {'where':50}")
        t0 = calls[0]["ts"]
        for i, c in enumerate(calls):
            url = c["url"] or ""
            short_url = url.rsplit("/", 1)[-1][:42] if url else ""
            where = f"{short_url}:{c['line']}" if c["line"] is not None else short_url
            rel_t = ((c["ts"] or 0) - t0) / 1e6 if c["ts"] else 0
            dur_ms = (c["dur"] or 0) / 1000.0
            print(f"  {i:>3}  {rel_t:>10.3f}  {dur_ms:>8.2f}  {c['name'][:24]:24}  {where[:50]}")

        # quick categorization
        durs = [c["dur"] / 1000.0 for c in calls]
        short = [d for d in durs if d < 2.0]
        long_ = [d for d in durs if d >= 2.0]
        print(f"\n  short (<2ms): {len(short)}  long (>=2ms): {len(long_)}")
        if long_:
            long_sorted = sorted(long_)
            print(f"  long-duration distribution: min={min(long_):.2f}ms  median={long_sorted[len(long_)//2]:.2f}ms  max={max(long_):.2f}ms")

        # cluster the timestamps to see if they form ~9 burst groups (1 per click) or ~18 (one per click per clone)
        if len(calls) >= 2:
            gaps = [(calls[i+1]["ts"] - calls[i]["ts"]) / 1e6 for i in range(len(calls)-1)]
            gaps_sorted = sorted(gaps)
            print(f"  inter-call gaps (sec): min={min(gaps):.3f}  median={gaps_sorted[len(gaps)//2]:.3f}  max={max(gaps):.3f}")
            # how many calls fall within 0.05s of the previous (suggests "compile + postMsg" pair)
            tight_pairs = sum(1 for g in gaps if g < 0.05)
            wide_jumps = sum(1 for g in gaps if g > 0.5)
            print(f"  tight pairs (<50ms apart): {tight_pairs}    wide jumps (>0.5s apart): {wide_jumps}")


if __name__ == "__main__":
    for p in sys.argv[1:]:
        drill_workers(p)
