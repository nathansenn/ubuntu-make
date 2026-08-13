#!/usr/bin/env python3
"""Python allocator / GC microbenches for Ubuntu 24.04 memory handling."""

import gc
import os
import resource
import sys
import time


def rss_kb() -> int:
    return resource.getrusage(resource.RUSAGE_SELF).ru_maxrss


def bench_alloc(label: str, n: int = 200_000, size: int = 64) -> None:
    gc.collect()
    t0 = time.perf_counter()
    hold = [bytearray(size) for _ in range(n)]
    dt = time.perf_counter() - t0
    print(
        f"py-alloc {label} n={n} size={size} time={dt:.4f}s "
        f"rate={n / dt / 1e3:.1f} kalloc/s rss={rss_kb()}kB"
    )
    del hold
    gc.collect()


def bench_churn(label: str, n: int = 1_000_000, size: int = 64) -> None:
    gc.collect()
    t0 = time.perf_counter()
    acc = 0
    for i in range(n):
        b = bytearray(size)
        b[0] = i & 255
        acc += b[0]
    dt = time.perf_counter() - t0
    print(
        f"py-churn {label} n={n} size={size} time={dt:.4f}s "
        f"rate={n / dt / 1e3:.1f} kalloc/s rss={rss_kb()}kB sink={acc}"
    )


def main() -> None:
    label = os.environ.get("PYTHONMALLOC", "default-pymalloc")
    gc_state = os.environ.get("PY_GC", "on")
    if gc_state == "off":
        gc.disable()
    elif gc_state == "high":
        gc.set_threshold(7000, 10, 10)
    print(
        f"# python {sys.version.split()[0]} PYTHONMALLOC={label} "
        f"gc={gc.isenabled()} threshold={gc.get_threshold()}"
    )
    # Warm the interpreter and allocator before the timed loops.
    bench_alloc(label + "-warmup", n=20_000)
    bench_alloc(label)
    bench_churn(label)


if __name__ == "__main__":
    main()
