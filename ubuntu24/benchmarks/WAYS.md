# 100 Ubuntu 24.04 performance ideas — measured

Host: Intel Xeon (SHA-NI, AVX2, AVX-512, PCLMUL, VPCLMUL), Ubuntu 24.04.4, gcc as shipped.
Harness: `bench_100.c`, `bench_100.py`, plus earlier isolated CRC/Adler/gzip benches in `RESULTS.md`.

**KEEP** = we ship a source patch or it is already the right Noble default.
**DISCARD** = measured no win, already fast, wrong bottleneck, or unsafe (security).

| # | Idea | Result | Decision |
|---|---|---|---|
| 1 | zlib CRC-32 PCLMUL + ifunc (CPUID leaf 1) | 6.0 → 24.6 GB/s (**4.07×**) | **KEEP** |
| 2 | zlib Adler-32 SSSE3 + ifunc | 3.6 → 24.0 GB/s (**6.69×**) | **KEEP** |
| 3 | gzip `updcrc` PCLMUL | 0.50 → 20.9 GB/s (**41.8×**) | **KEEP** |
| 4 | gzip `UNALIGNED_OK` on amd64 (`debian/rules` + `tailor.h`) | Ubuntu tested unset `$(buildarch)` | **KEEP** |
| 5 | gzip inflate stored-block bulk `memcpy` | **12.6×** vs CRC-only gunzip of random | **KEEP** |
| 6 | gzip inflate dist=1 `memset` + 3-byte unroll | **1.68×** vs CRC-only gunzip of text | **KEEP** |
| 7 | gzip cache fixed Huffman tables | bundled with #6 | **KEEP** |
| 8 | gzip `copy_block` chunked `memcpy` | **1.09×** vs prior patched `gzip -1` random | **KEEP** |
| 9 | Python `zlib.decompress` via patched libz | 606 → 666 MB/s (**1.10×**) | **KEEP** |
| 10 | coreutils `wc` `BUFFER_SIZE` 16→256 KiB | **1.50×** `read()` (16 KiB 12.2 vs 256 KiB 18.3 GB/s) | **KEEP** |
| 11 | coreutils `IO_BUFSIZE` 128→256 KiB | **1.02×** (plateau; matches upstream table) | **KEEP** |
| 12 | coreutils `tr` 8→256 KiB | same plateau as #10 | **KEEP** |
| 13 | coreutils `head` pipe/copy 8→256 KiB | same plateau | **KEEP** |
| 14 | coreutils `tail` pipe/copy/skip 8→256 KiB | same plateau | **KEEP** |
| 15 | coreutils `tee` 8→256 KiB | same plateau | **KEEP** |
| 16 | coreutils `tac` 8→256 KiB | same plateau | **KEEP** |
| 17 | coreutils `yes` write floor 8→256 KiB | **1.30×** (0.186 → 0.143 s / 256 MiB) | **KEEP** |
| 18 | grep `INITIAL_BUFSIZE` 96→256 KiB | fewer syscalls; I/O already plateaued | **KEEP** |
| 19 | tar `DEFAULT_BLOCKING` 20→512 (10→256 KiB) | **1.36×** `cf`, **1.23×** `xf` | **KEEP** |
| 20 | diffutils `cmp`/`diff -q` 256 KiB floor | ~1.01× (memcmp-bound); 64× fewer `read`s | **KEEP** |
| 21 | `read()` 4 KiB → 256 KiB | 12.5 → 21.2 GB/s (**1.69×**) this host | **KEEP** (policy) |
| 22 | `fread`+`setvbuf` 8→256 KiB | 15.3 → 21.6 GB/s (**1.41×**) | **KEEP** (policy) |
| 23 | `dd bs=8k` → `bs=256k` | 0.0030 → 0.0022 s (**1.36×**) | **KEEP** (`dd` already uses `io_blksize`) |
| 24 | `read()` 256→512 KiB | 21.2 → 21.4 GB/s | **DISCARD** (noise / plateau) |
| 25 | `read()` 256→1024 KiB | 21.2 → 18.4 GB/s (**0.87×**) | **DISCARD** |
| 26 | zlib AVX2 `compare256` / `longest_match` | **0.91×** deflate-6 mixed corpus | **DISCARD** |
| 27 | zlib `inflate_fast` AVX2/AVX-512 copies | max match 258 B; overlap dominates | **DISCARD** |
| 28 | zlib `inflate_fast` `memcpy` when `dist>=len` | ~0–3% after #5/#6 | **DISCARD** |
| 29 | PCLMUL CRC without working ifunc | stays at 6 GB/s | **DISCARD** |
| 30 | Expect gzip `-6` to jump from CRC SIMD | LZ77/Huffman bound | **DISCARD** |
| 31 | xz/liblzma CRC CLMUL | already CPUID-dispatched | **ALREADY** |
| 32 | xz 5.6 range-decoder asm onto 5.4.5 | large cherry-pick after backdoor revert | **DISCARD** |
| 33 | zstd `DYNAMIC_BMI2` + ASM | already on (hundreds of `tzcnt`/`pdep`) | **ALREADY** |
| 34 | OpenSSL SHA-NI for `sha256sum` | 1.57 GB/s stock | **ALREADY** |
| 35 | `md5sum` via libcrypto | 0.74 GB/s stock | **ALREADY** |
| 36 | `sha1sum` via libcrypto | 1.73 GB/s stock | **ALREADY** |
| 37 | coreutils `cksum` PCLMUL | 8.3 GB/s; prints hardware support | **ALREADY** |
| 38 | AVX2 `wc -l` | 32 MiB in 2.7 ms stock | **ALREADY** |
| 39 | glibc `memcpy` AVX2/AVX-512 ifunc | stock `memcpy` already vectorized | **ALREADY** |
| 40 | glibc `memchr` AVX2/AVX-512 ifunc | stock `memchr` already vectorized | **ALREADY** |
| 41 | `cat`/`cp` `copy_file_range` | already in Noble coreutils | **ALREADY** |
| 42 | bzip2 PCLMUL CRC on output | CRC ~0–9% of decompress (82% only on zeros) | **DISCARD** |
| 43 | lz4 extra library SIMD | compiler SSE2 + `tzcnt` already | **ALREADY** |
| 44 | pigz-local CRC/SIMD | calls `libz`; inherits #1/#2 | **DISCARD** |
| 45 | libpng `--enable-intel-sse` | `timepng` ~1.00× (inflate-bound) | **DISCARD** |
| 46 | sed 256 KiB `setvbuf` | `sed -n p` 39 ms; `s///` 309 ms (regex) | **DISCARD** |
| 47 | findutils FTS / 256 KiB | stat/`readdir` bound | **DISCARD** |
| 48 | mawk 256 KiB I/O | already reads 256 KiB | **ALREADY** |
| 49 | gawk default `AWKBUFSIZE=256k` | 62.4 → 60.8 ms (**1.03×**) | **DISCARD** |
| 50 | jq 4→256 KiB `fread` | parse/execute bound | **DISCARD** |
| 51 | libxml2 `INPUT_CHUNK` / `MINLEN` | 250 B lookahead; I/O already 4 KiB | **DISCARD** |
| 52 | rsync `IO_BUFFER_SIZE` 32→256 KiB | file map already 256 KiB; sockets per-conn | **DISCARD** |
| 53 | less `LBUFSIZE` 8→256 KiB | pager LRU, not sequential drain | **DISCARD** |
| 54 | procps `ps`/`top` larger `/proc` reads | open/close per PID, files are KB | **DISCARD** |
| 55 | util-linux `hexdump` 256 KiB | format/printf bound (sys ~1%) | **DISCARD** |
| 56 | util-linux `more` `copy_file` 8→256 KiB | already ~3 GB/s; interactive common case | **DISCARD** |
| 57 | util-linux `rev`/`column` | wchar/getline bound | **DISCARD** |
| 58 | sqlite3 page I/O / SIMD | 4K pages; no SIMD; mmap opt-in | **DISCARD** |
| 59 | wget `dlbufsize` 64→256 KiB | `fflush` per chunk; network bound | **DISCARD** |
| 60 | coreutils `sort` 256 KiB I/O | merge buffer already 256 KiB | **ALREADY** |
| 61 | coreutils `cut`/`uniq`/`paste`/`fold` | `getc` / line parse | **DISCARD** |
| 62 | coreutils `basenc`/`base64` 30→256 KiB | 32 MiB encode 23 ms (CPU tables) | **DISCARD** |
| 63 | coreutils `od` `BUFSIZ` skip | skip/format, not throughput | **DISCARD** |
| 64 | `tail`/`head` backward `file_lines` 256 KiB | reads extra from EOF | **DISCARD** |
| 65 | make 4.3 / bash 5.2 hash | Jenkins already; fork/exec bound | **DISCARD** |
| 66 | patch 2.7.6 `bufsize` 8→256 KiB | `getc` parse + plan-A whole-file | **DISCARD** |
| 67 | unzip PCLMUL CRC / 256 KiB | inflate bound; no in-tree SIMD CRC | **DISCARD** |
| 68 | zip PCLMUL CRC / 256 KiB | deflate window 32 KiB; i386 CRC asm on | **DISCARD** |
| 69 | cpio 512→256 KiB defaults | archive block is format; `-C` exists | **DISCARD** |
| 70 | pcre2 enable JIT/SIMD | already `--enable-jit` on amd64 | **ALREADY** |
| 71 | xxhash `DISPATCH=1` | AVX2/AVX-512 already on | **ALREADY** |
| 72 | brotli debian SIMD flags | no x86 SIMD to enable | **DISCARD** |
| 73 | file(1) 256 KiB read | already reads 7 MiB | **DISCARD** |
| 74 | e2fsprogs 256 KiB / CRC SIMD | 4K `pread` + fadvise; slice-by-64 CRC | **DISCARD** |
| 75 | python3.12 hashlib SIMD | `hashlib` already OpenSSL SHA-NI | **ALREADY** |
| 76 | python3.12 `_json` SIMD | no SIMD layer exists | **DISCARD** |
| 77 | git OpenSSL SHA-NI | `NO_OPENSSL=1` + SHA1DC on purpose | **DISCARD** |
| 78 | git 256 KiB pack reads | 1 GiB mmap window; 4 KiB stream by design | **DISCARD** |
| 79 | curl 16→256 KiB receive | `CURL_MAX_WRITE_SIZE` API contract | **DISCARD** |
| 80 | `sendfile` vs `read`/`write` 32 MiB | 2.6 GB/s vs ~21 GB/s cached `read` (write-bound) | **DISCARD** as default |
| 81 | `mmap`+touch vs `read` 256 KiB | 16.3 vs 21.2 GB/s | **DISCARD** (read wins here) |
| 82 | `posix_fadvise(SEQUENTIAL)` + 256 KiB `read` | 21.8 vs 21.2 GB/s | **DISCARD** (noise) |
| 83 | replace `getc` with `fread` in line tools | `getc` 425 MB/s vs `fread` 21 GB/s isolated, but tools are parse-bound | **DISCARD** without rewrite |
| 84 | hand-rolled byte scan vs `memchr` | glibc already SIMD | **DISCARD** |
| 85 | `LC_ALL=C` default for grep | `-c -F` 0.7 vs 0.6 ms (noise); user-settable | **DISCARD** |
| 86 | `LC_ALL=C` default for sort | 88 vs 84 ms (**1.05×**) | **DISCARD** |
| 87 | `sort -S 64M` default | 84 → 115 ms (**0.73×**) | **DISCARD** |
| 88 | `sort --parallel=4` default | 71 vs 72 ms | **DISCARD** |
| 89 | `grep -F` instead of `-E` default | 0.96× this corpus | **DISCARD** |
| 90 | `vm.swappiness=10` | Noble default 60 is sane with swapfile | **DISCARD** |
| 91 | THP `always` | Noble is `madvise` (correct default) | **DISCARD** |
| 92 | `tcp_fastopen=3` global | default 1 (client); server-only | **DISCARD** as distro default |
| 93 | BBR congestion control | default cubic; WAN-only | **DISCARD** as distro default |
| 94 | `mitigations=off` | security | **DISCARD** |
| 95 | `noatime` instead of `relatime` | relatime already avoids hot atime | **DISCARD** |
| 96 | disable snapd/apport/oomd “for speed” | no steady-state win; breaks features | **DISCARD** |
| 97 | zram swap | off on x86; only helps low-RAM | **DISCARD** here |
| 98 | cpufreq `performance` governor | `schedutil` is the right default | **DISCARD** as distro default |
| 99 | `kernel.sched_autogroup_enabled=0` | obsolete gaming tweak | **DISCARD** |
| 100 | raise `vm.dirty_ratio` / `vfs_cache_pressure` | defaults 20/100 are sane | **DISCARD** |

## Score

| Decision | Count |
|---|---|
| **KEEP** (we patched, or policy already applied) | 23 |
| **ALREADY** (Noble already has the fast path) | 16 |
| **DISCARD** (no proven win, unsafe, or wrong bottleneck) | 61 |

Shipped patches: `ubuntu24/patches/` (zlib, gzip, coreutils, grep, tar, diffutils).

## Memory / running applications (follow-up)

Full write-up and harness: `MEMORY.md`. None of these beat Noble's defaults
on this host, so no new patch.

| # | Idea | Result | Decision |
|---|---|---|---|
| 101 | `madvise(MADV_HUGEPAGE)` on `malloc` ≥2 MiB (`sort` heap) | 0 AnonHugePages (needs 2 MiB alignment) | **DISCARD** |
| 102 | `posix_memalign(2MiB)` + `MADV_HUGEPAGE` on `sort` buffer | fill up to 2×; line `qsort` 0.76–1.06×; overall no proven win | **DISCARD** |
| 103 | `MADV_COLLAPSE` / `MADV_WILLNEED` on the same buffer | same noise band as #102 | **DISCARD** |
| 104 | glibc `madvise` THP on every large `mmap` | equivalent to THP=`always` for those apps | **DISCARD** |
| 105 | `GLIBC_TUNABLES=glibc.malloc.arena_max=1` global | **0.31×** alloc rate; hold RSS not better here | **DISCARD** |
| 106 | `arena_max=2` global | **0.74×** alloc rate | **DISCARD** as default (opt-in per daemon) |
| 107 | `tcache_count=0` | **0.26×** alloc rate | **DISCARD** |
| 108 | `tcache_count=32` | **0.87×** vs default 7 | **DISCARD** |
| 109 | `malloc_trim(0)` after 256 KiB frees | RSS unchanged (already `munmap`) | **DISCARD** |
| 110 | `PYTHONMALLOC=malloc` distro default | faster 64 B microbench; worse long-run fragmentation | **DISCARD** |
| 111 | Python `gc.disable()` / threshold 7000 default | faster batch alloc; leaks / workload-specific | **DISCARD** as default |
| 112 | `jemalloc`/`tcmalloc` distro `LD_PRELOAD` | not installed; unsafe as a global preload | **DISCARD** |
| 113 | `posix_fadvise(DONTNEED)` after `cp`/`cat` | steals cache from the next reader | **DISCARD** as default |

## How to reproduce

```sh
cc -O3 -o ubuntu24/benchmarks/bench_100 ubuntu24/benchmarks/bench_100.c
ubuntu24/benchmarks/bench_100
python3 ubuntu24/benchmarks/bench_100.py
make -C ubuntu24/benchmarks bench_memory
ubuntu24/benchmarks/bench_memory arena
python3 ubuntu24/benchmarks/bench_memory.py
```
