# Ubuntu 24.04 running-app memory handling

Measured on this Xeon (4 CPUs, 15 GiB RAM, no swap). Kernel THP mode is
`always [madvise] never` with defrag `madvise`. glibc 2.39, Python 3.12.3.

Harness: `bench_memory.c`, `bench_memory.py`, and `thp_preload.c` (LD_PRELOAD
probe for stock binaries; unaligned `malloc` still forms 0 huge pages).
Reproduce:

```sh
cc -O2 -pthread -o ubuntu24/benchmarks/bench_memory ubuntu24/benchmarks/bench_memory.c
ubuntu24/benchmarks/bench_memory thp 256 1
ubuntu24/benchmarks/bench_memory readsort 128 1
ubuntu24/benchmarks/bench_memory arena
ubuntu24/benchmarks/bench_memory trim
python3 ubuntu24/benchmarks/bench_memory.py
PYTHONMALLOC=malloc python3 ubuntu24/benchmarks/bench_memory.py
```

No source patch shipped from this pass. The numbers below are why.

## What Noble already does right

| Knob | Noble default | Why it stays |
|---|---|---|
| THP | `madvise` | `always` causes latency/RSS spikes in Redis, JVMs, databases |
| glibc arenas | cap `8 × nCPU` (32 here) | throughput; see below |
| glibc tcache | 7 slots/bin, max 1032 B | disabling it is **0.26×** alloc rate |
| mmap/trim threshold | 128 KiB, dynamic to 512 KiB | 256 KiB I/O buffers that `malloc` once per process are one `mmap` |
| Python pymalloc | on | correct for long-running interpreters |
| Python GC | `(700, 10, 10)` | cycle collection; batch jobs can raise it themselves |
| `sort` budget | `max(avail, total/8)` with margins | `-S 64M` as a new default was **0.73×** (WAYS #87) |
| `sort` merge I/O | already 256 KiB | WAYS #60 |

Patching glibc to `madvise(MADV_HUGEPAGE)` on every large `mmap` would turn
those mappings into THP=`always` for the apps that already suffer from it.
Those apps use glibc `malloc` (or would inherit it). **DISCARD**.

## Transparent huge pages on large app heaps

`MADV_HUGEPAGE` on a plain `malloc` of 64–256 MiB formed **0 AnonHugePages**.
The kernel only backs a mapping with 2 MiB pages when the address and length
are 2 MiB-aligned. `posix_memalign(2 MiB)` + `MADV_HUGEPAGE` does form them
(256 MiB → 262144 kB AnonHuge).

| Workload | malloc | 2 MiB + THP | Decision |
|---|---|---|---|
| 256 MiB sequential first-touch | fill 0.0807 s | fill 0.0396 s (**2.04×**) | sequential-only |
| 256 MiB `qsort` of `uint64` | 0.1924 s | 0.1943 s (**0.99×**) | no win |
| 128 MiB `read` + line `qsort` (run 1) | 0.119 + 0.934 s | 0.055 + 1.227 s (**0.82×** overall) | regression |
| 128 MiB `read` + line `qsort` (run 2) | 0.063 + 0.795 s | 0.096 + 0.748 s (~1.02×) | noise |
| stock `sort -S 128M` 4M lines / 124 MiB | 3.23–3.30 s, RSS 135 MiB | not applied | — |

GNU `sort` is compare-bound after the buffer is filled. THP helps the
page-fault side of a sequential fill and does not help (and can hurt)
pointer-chasing line compare. **DISCARD** as a `sort` default.

`MADV_COLLAPSE` (Linux 6.1+) and `MADV_WILLNEED` were in the same noise band
once the buffer was 2 MiB-aligned. `MADV_WILLNEED` without alignment did not
create huge pages.

Use 2 MiB alignment + `MADV_HUGEPAGE` only in an app whose hot path is
sequential scans of a large anonymous buffer, not a general distro default.

## glibc arenas, tcache, trim

8 threads, mixed 64 B–16 KiB alloc/free (400k iters/thread), then a hold of
512 live allocs/thread:

| `GLIBC_TUNABLES` | alloc rate | vs default | hold RSS |
|---|---|---|---|
| (default) | 83.8k kops/s | 1.00× | 99 MiB |
| `glibc.malloc.arena_max=1` | 26.3k kops/s | **0.31×** | 120 MiB |
| `glibc.malloc.arena_max=2` | 41.4k kops/s | **0.74×** | (churn RSS same) |
| `glibc.malloc.tcache_count=0` | 21.9k kops/s | **0.26×** | — |
| `glibc.malloc.tcache_count=32` | 72.8k kops/s | **0.87×** | — |

`arena_max=1` is not a free RSS win on this host and it serializes the
allocator. **DISCARD** as a distro-wide `environment.d` default.

`malloc_trim(0)` after freeing 16 MiB in 256 KiB chunks did not move RSS
(18328 kB before and after): those sizes are already `mmap`/`munmap`.

### Opt-in (per service, not `/etc`)

For a **memory-tight, mostly-idle daemon** that is not alloc-bound, a unit
file can set:

```
Environment=GLIBC_TUNABLES=glibc.malloc.arena_max=2
```

Measure RSS and request latency on that service before keeping it. Example
drop-in: `ubuntu24/config/99-ubuntu24-memory.conf.example`.

## Python 3.12 (running interpreters)

Warmed process, 200k live `bytearray(64)` then 1M churn allocs:

| Setting | hold kalloc/s | churn kalloc/s | RSS |
|---|---|---|---|
| pymalloc + GC `(700,10,10)` | 3950 | 6249 | 39.6 MiB |
| `PYTHONMALLOC=malloc` + GC | 7916 | 8156 | 39.5 MiB |
| pymalloc + `gc.disable()` | 11107 | 10397 | 39.8 MiB |
| pymalloc + threshold `(7000,10,10)` | 9392 | 11130 | 39.8 MiB |

`PYTHONMALLOC=malloc` wins this microbench because it skips pymalloc + default
GC traffic. It fragments worse over days in a web worker. CPython’s pymalloc
default stays. **DISCARD** a distro `PYTHONMALLOC` change.

`gc.disable()` is faster for a short script that creates no cycles. It is
wrong as a process-wide default (leaks). A batch job can call it itself.

Python 3.13+ mimalloc is not in Noble’s 3.12. **DISCARD** a backport.

## Other running-app ideas checked here

| Idea | Result |
|---|---|
| glibc auto-THP on large `malloc` `mmap`s | same as THP=`always` for those apps; **DISCARD** |
| `sort` `posix_memalign(2MiB)` + `MADV_HUGEPAGE` | compare-bound; **DISCARD** |
| `sort -S 64M` / `--parallel=4` defaults | already discarded in WAYS #87–#88 |
| `PYTHONMALLOC=malloc` / GC threshold | workload-specific; **DISCARD** as default |
| `jemalloc` / `tcmalloc` `LD_PRELOAD` | not installed; **DISCARD** as a distro preload |
| `malloc_trim` after every large free | no-op for mmap-sized chunks |
| `posix_fadvise(DONTNEED)` after `cp`/`cat` | helps *other* apps’ cache, hurts the next reader; **DISCARD** as default (`nocache` exists) |
| 256 KiB `xalignalloc` in `cat`/`cp`/`copy.c` | one page-aligned `mmap` per invocation; not a loop |
| gzip 32 KiB window / tar 256 KiB record | below a huge page |
| THP `always`, `mitigations=off`, disable snapd | already discarded in WAYS #91/#94/#96 |

## What to do for a running application

1. Leave THP at `madvise`. If *your* app sequentially scans a ≥2 MiB anonymous
   buffer, align it to 2 MiB and call `madvise(MADV_HUGEPAGE)` in that app.
2. Do not set `GLIBC_TUNABLES` globally. Pin `arena_max` only on a measured
   daemon that is RSS-bound and not alloc-bound.
3. Leave Python on pymalloc. For a one-shot number-crunch, `gc.disable()` in
   the script is enough.
4. Prefer the 256 KiB I/O floor already shipped in `ubuntu24/patches/` over
   allocator tweaks for CLI throughput.
