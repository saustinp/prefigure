#!/usr/bin/env python3
"""Drill into a Chrome DevTools trace for prefigure-relevant signals.

Specifically:
- Identify the worker thread that runs the prefigure WASM compile.
- Distribution of FunctionCall durations on that thread (~Pyodide work).
- Count async-task lifecycle: Scheduled -> Run vs Scheduled -> Canceled.
- Sample stacks from cancellations to find the abort caller.
"""

import sys, os
from collections import Counter, defaultdict
import ijson


def drill(path: str) -> None:
    print(f"\n=== {path} ({os.path.getsize(path) / 1e6:.1f} MB) ===")

    thread_names = {}
    process_names = {}
    fn_durations_by_thread = defaultdict(list)
    long_funcs_per_thread = defaultdict(Counter)
    async_task_events = Counter()
    async_task_status = defaultdict(lambda: {"scheduled": 0, "run": 0, "canceled": 0})
    cancel_stack_top = Counter()
    schedule_stack_top = Counter()
    abort_callsites = Counter()

    with open(path, "rb") as f:
        for evt in ijson.items(f, "traceEvents.item"):
            name = evt.get("name", "")
            pid = evt.get("pid")
            tid = evt.get("tid")
            args = evt.get("args", {}) or {}

            if name == "thread_name":
                thread_names[(pid, tid)] = args.get("name", "?")
            elif name == "process_name":
                process_names[pid] = args.get("name", "?")

            # Async task lifecycle
            if name == "v8::Debugger::AsyncTaskScheduled":
                data = args.get("data", {}) or {}
                task_name = data.get("taskName") or data.get("name") or "?"
                async_task_status[task_name]["scheduled"] += 1
                # Top of stack at scheduling
                stack = data.get("stackTrace") or []
                if stack:
                    top = stack[0]
                    fn = top.get("functionName") or "<anon>"
                    url = top.get("url") or ""
                    line = top.get("lineNumber")
                    schedule_stack_top[f"{fn} @ {url.split('/')[-1]}:{line}"] += 1
            elif name == "v8::Debugger::AsyncTaskRun":
                data = args.get("data", {}) or {}
                task_name = data.get("taskName") or data.get("name") or "?"
                async_task_status[task_name]["run"] += 1
            elif name == "v8::Debugger::AsyncTaskCanceled":
                data = args.get("data", {}) or {}
                task_name = data.get("taskName") or data.get("name") or "?"
                async_task_status[task_name]["canceled"] += 1
                stack = data.get("stackTrace") or []
                if stack:
                    top = stack[0]
                    fn = top.get("functionName") or "<anon>"
                    url = top.get("url") or ""
                    line = top.get("lineNumber")
                    cancel_stack_top[f"{fn} @ {url.split('/')[-1]}:{line}"] += 1

            # FunctionCall duration buckets (ph='X' has dur)
            if name == "FunctionCall" and evt.get("ph") == "X":
                dur = evt.get("dur")
                if dur is not None:
                    fn_durations_by_thread[(pid, tid)].append(dur)
                    if dur > 50_000:  # >50ms long task
                        data = args.get("data", {}) or {}
                        fn_name = data.get("functionName") or "<anon>"
                        url = data.get("url") or ""
                        long_funcs_per_thread[(pid, tid)][f"{fn_name} @ {url.split('/')[-1] or url}"] += 1

            # any "abort" mentioned in name or args
            if "abort" in name.lower():
                abort_callsites[name] += 1

    print("\nasync task lifecycle (taskName: scheduled / run / canceled):")
    for tn, s in sorted(async_task_status.items(), key=lambda kv: -kv[1]["canceled"])[:20]:
        print(f"  {tn:30s}  sch={s['scheduled']:>6}  run={s['run']:>6}  cancel={s['canceled']:>6}")

    print("\ntop frames at AsyncTaskScheduled (top 12, source of dispatches):")
    for k, c in schedule_stack_top.most_common(12):
        print(f"  {c:>6}  {k}")

    print("\ntop frames at AsyncTaskCanceled (top 12, source of aborts):")
    for k, c in cancel_stack_top.most_common(12):
        print(f"  {c:>6}  {k}")

    print("\nFunctionCall duration distribution per thread (top 6 threads):")
    by_count = sorted(fn_durations_by_thread.items(), key=lambda kv: -len(kv[1]))[:6]
    for (pid, tid), durs in by_count:
        proc = process_names.get(pid, "?")
        thread = thread_names.get((pid, tid), "?")
        if not durs:
            continue
        durs_sorted = sorted(durs)
        n = len(durs_sorted)
        median_us = durs_sorted[n // 2]
        p95_us = durs_sorted[int(n * 0.95)]
        max_us = durs_sorted[-1]
        sum_ms = sum(durs_sorted) / 1000.0
        print(f"  {thread!r:30s}  n={n:>6}  median={median_us/1000:.2f}ms  p95={p95_us/1000:.2f}ms  max={max_us/1000:.2f}ms  total_running={sum_ms:.0f}ms")

    print("\nlong (>50ms) FunctionCalls per thread (top 8):")
    for (pid, tid), counter in long_funcs_per_thread.items():
        thread = thread_names.get((pid, tid), "?")
        for fn, c in counter.most_common(4):
            print(f"  {thread!r:25s}  {c:>3}  {fn}")

    print("\nevent names containing 'abort' or 'cancel':")
    for k, c in abort_callsites.most_common(10):
        print(f"  {c:>6}  {k}")


if __name__ == "__main__":
    for p in sys.argv[1:]:
        drill(p)
