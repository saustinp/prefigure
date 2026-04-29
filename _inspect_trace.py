#!/usr/bin/env python3
"""Streaming reconnaissance of a Chrome DevTools Performance trace.

Prints headline stats: duration, event count, per-category breakdown,
per-thread breakdown, prefigure-related entries, and any /build network
calls.  Designed to run in O(events) memory; will not load the whole
file into RAM.
"""

import sys
from collections import Counter, defaultdict
import ijson


def inspect(path: str) -> None:
    print(f"\n=== {path} ===")
    print(f"file size: {__import__('os').path.getsize(path) / 1e6:.1f} MB")

    cat_counts = Counter()
    name_counts = Counter()
    phase_counts = Counter()
    thread_counts = Counter()
    process_names = {}
    thread_names = {}
    long_tasks = 0
    long_task_names = Counter()
    min_ts = None
    max_ts = None
    total_events = 0

    prefigure_events = 0
    prefigure_examples = []
    build_network_events = 0
    build_examples = []
    postmessage_events = 0
    fn_call_counts = Counter()

    with open(path, "rb") as f:
        for evt in ijson.items(f, "traceEvents.item"):
            total_events += 1
            ts = evt.get("ts")
            if ts is not None and isinstance(ts, (int, float)):
                if min_ts is None or ts < min_ts:
                    min_ts = ts
                if max_ts is None or ts > max_ts:
                    max_ts = ts

            ph = evt.get("ph", "")
            phase_counts[ph] += 1
            cat = evt.get("cat", "")
            name = evt.get("name", "")
            cat_counts[cat] += 1
            name_counts[name] += 1
            tid = evt.get("tid")
            pid = evt.get("pid")
            if tid is not None:
                thread_counts[(pid, tid)] += 1

            # metadata events identifying threads/processes
            if name == "thread_name":
                args = evt.get("args", {})
                thread_names[(pid, tid)] = args.get("name", "?")
            elif name == "process_name":
                args = evt.get("args", {})
                process_names[pid] = args.get("name", "?")

            # long tasks: complete events ('X' phase) >50ms
            if ph == "X":
                dur = evt.get("dur", 0)
                if dur and dur > 50_000:
                    long_tasks += 1
                    long_task_names[name] += 1

            # FunctionCall / Profile entries with the function name
            if name in ("FunctionCall", "v8.compile", "EvaluateScript"):
                args = evt.get("args", {}).get("data", {})
                fn = args.get("functionName") or args.get("url") or "<anon>"
                # truncate long urls
                if isinstance(fn, str):
                    fn_call_counts[fn[:80]] += 1

            # search for prefigure-ness
            blob = (str(name) + " " + str(evt.get("args", {})))[:500].lower()
            if "prefigure" in blob or "prefig-" in blob:
                prefigure_events += 1
                if len(prefigure_examples) < 5:
                    prefigure_examples.append({
                        "ts": ts, "ph": ph, "name": name, "cat": cat,
                        "args_excerpt": str(evt.get("args", {}))[:200],
                    })

            # /build network calls
            if "ResourceSendRequest" in name or "ResourceReceiveResponse" in name:
                args = evt.get("args", {}).get("data", {})
                url = args.get("url", "")
                if "/build" in url:
                    build_network_events += 1
                    if len(build_examples) < 6:
                        build_examples.append({
                            "ts": ts, "name": name, "url": url[:120]
                        })

            # postMessage between threads
            if name in ("MessagePort.postMessage", "MessagePort.dispatchMessage", "v8.postMessage"):
                postmessage_events += 1

    duration_s = ((max_ts or 0) - (min_ts or 0)) / 1e6
    print(f"trace duration:         {duration_s:.2f} s")
    print(f"total events:           {total_events:,}")
    print(f"long tasks (>50ms):     {long_tasks}")
    print(f"prefigure-tagged:       {prefigure_events}")
    print(f"/build network events:  {build_network_events}")
    print(f"postMessage events:     {postmessage_events}")

    print("\n  top 12 event categories:")
    for cat, c in cat_counts.most_common(12):
        print(f"    {c:>8,}  {cat}")

    print("\n  top 15 event names:")
    for name, c in name_counts.most_common(15):
        print(f"    {c:>8,}  {name}")

    print("\n  long-task names (>50ms):")
    for name, c in long_task_names.most_common(10):
        print(f"    {c:>4}  {name}")

    print("\n  threads (top 12 by event count):")
    for (pid, tid), c in thread_counts.most_common(12):
        proc = process_names.get(pid, "?")
        thread = thread_names.get((pid, tid), "?")
        print(f"    {c:>8,}  pid={pid:>5}  tid={tid:>6}  {proc!r:25} :: {thread!r}")

    print("\n  prefigure-tagged event examples:")
    for ex in prefigure_examples:
        print(f"    ts={ex['ts']}  ph={ex['ph']}  name={ex['name']!r}  args={ex['args_excerpt']}")

    print("\n  /build network events:")
    for ex in build_examples:
        print(f"    ts={ex['ts']}  name={ex['name']}  url={ex['url']}")


if __name__ == "__main__":
    for p in sys.argv[1:]:
        inspect(p)
