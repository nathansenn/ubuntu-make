# Ubuntu 24.04 userspace speedups

Patches against the **Ubuntu 24.04 LTS (Noble)** packages fetched with `apt-get source` on this host:

| Package | Ubuntu version |
|---|---|
| zlib | 1.3.dfsg-3.1ubuntu2.1 |
| gzip | 1.12-1ubuntu3.2 |
| coreutils | 9.4-3ubuntu6.2 |
| grep | 3.11-4build1 |
| tar | 1.35+dfsg-3ubuntu0.4 |
| diffutils | 3.10-1build1 |

This is not a kernel tree. Noble already ships AVX2 `wc -l`, OpenSSL SHA-NI `sha256sum`, PCLMUL `cksum`, and `copy_file_range` in `cat`/`cp`. Those are left alone.

## What was missing on x86_64

Ubuntu's zlib 1.3 already has SIMD CRC on **POWER** and **s390x**, but `libz.so.1.3` on amd64 had **no PCLMUL CRC-32 and no SSSE3 Adler-32**. gzip 1.12 still used a byte-at-a-time CRC table and a per-byte stored-block inflate. coreutils `wc` (non-AVX2 paths) read 16 KiB at a time. GNU tar defaulted to a **10 KiB** record (blocking factor 20).

## Proven results (KEEP)

Measured on this Xeon (SHA-NI, AVX2, AVX-512, PCLMUL). Full numbers: `benchmarks/RESULTS.md`. One hundred candidates with KEEP/DISCARD: `benchmarks/WAYS.md`.

| Change | vs Ubuntu stock | Decision |
|---|---|---|
| zlib CRC-32 PCLMUL folding (ifunc + CPUID) | **4.07×** (24.6 vs 6.0 GB/s) | KEEP |
| zlib Adler-32 SSSE3 (ifunc + CPUID) | **6.69×** (24.0 vs 3.6 GB/s) | KEEP |
| gzip `updcrc` PCLMUL | **41.8×** (20.9 vs 0.50 GB/s) | KEEP |
| gzip `UNALIGNED_OK` on amd64 | Ubuntu `debian/rules` tested `$(buildarch)` which is unset; fix + `tailor.h` | KEEP |
| gzip inflate: stored-block bulk copy | **12.6×** vs CRC-only gzip on incompressible (`gzip -1` random) | KEEP |
| gzip inflate: overlap unroll + fixed Huffman cache | **1.68×** vs CRC-only gzip on repetitive text | KEEP |
| Python `zlib.decompress` via new `libz` | **1.10×** (666 vs 606 MB/s) | KEEP |
| coreutils `wc` `BUFFER_SIZE` 16 KiB → 256 KiB | **1.50×** read throughput vs 16 KiB | KEEP |
| coreutils `IO_BUFSIZE` 128 KiB → 256 KiB | **1.02×** (matches upstream comment table) | KEEP |
| coreutils `tr`/`head`/`tail`/`tee`/`tac` 8 KiB → 256 KiB | same I/O plateau as `wc` | KEEP |
| coreutils `yes` write floor 8 KiB → 256 KiB | **1.30×** sequential write | KEEP |
| gzip `copy_block` chunked `memcpy` | **1.09×** vs prior patched `gzip -1` on random | KEEP |
| grep `INITIAL_BUFSIZE` 96 KiB → 256 KiB | fewer syscalls; I/O already plateaued | KEEP |
| tar `DEFAULT_BLOCKING` 20 → 512 (10 KiB → 256 KiB) | **1.36×** `tar cf`, **1.23×** `tar xf` | KEEP |
| diffutils `cmp`/`diff -q` buffer floor 256 KiB | memcmp-bound here (~1.01×); 64× fewer `read`s | KEEP |

`__builtin_cpu_supports()` returns 0 inside GNU ifunc resolvers (libgcc CPU init has not run yet). Resolvers use **CPUID leaf 1** instead. Direct calls to the SIMD kernels were ~4×/6×; ifunc with the builtin stayed at scalar speed until that fix.

gzip inflate numbers above are isolated against a build that already had PCLMUL `updcrc` (so they are not double-counting the CRC win). End-to-end vs stock Ubuntu gzip is larger (text **8.0×**, random **26×**) because CRC and inflate stack.

## Surveyed and left alone (already fast or no proven win)

| Area | Finding |
|---|---|
| zlib AVX2 `compare256` / `longest_match` | **DISCARD** — 0.91× on mixed Python/stdlib corpus (short matches); 1.06× only on highly repetitive text |
| zlib `inflate_fast` `memcpy` when `dist>=len` | **DISCARD** — leftover non-overlap copies; 0–3% expected after stored/dist=1 wins |
| zlib `inflate_fast` AVX2/AVX-512 copy | **DISCARD** — max match 258 B; Chromium SSE2 chunkcopy is the only credible port, not a small patch |
| xz/liblzma CRC | Already CPUID-dispatches CLMUL |
| xz 5.4.5 vs 5.6 range-decoder asm | **DISCARD** here — large cherry-pick onto the post-backdoor 5.4.5 base |
| zstd 1.5.5 | `DYNAMIC_BMI2` + ASM already on (hundreds of `tzcnt`/`pdep`) |
| OpenSSL / `sha256sum` / `md5sum` | libcrypto + SHA-NI |
| `cksum` | PCLMUL already (`cksum: using pclmul hardware support`) |
| bzip2 PCLMUL CRC | **DISCARD** — CRC is ~0–9% of decompress except pathological RLE |
| lz4 1.9.4 | Compiler SSE2 + `tzcnt`; no extra library SIMD |
| pigz 2.8 | Thin pthread + `libz`; inherits zlib SIMD; no pigz-local CRC |
| libpng SSE filters | **DISCARD** — `timepng` ~1.00×; inflate dominates |
| sed / findutils / mawk / jq / libxml2 | Line/regex/stat bound; not a 256 KiB `read()` plateau |
| rsync | Rolling SIMD already on; MD5 ASM disabled on purpose (OpenSSL + CET) |
| less / procps / util-linux hexdump | Interactive or format-bound |
| sqlite3 / wget | Already tuned (64 KiB wget download; sqlite page I/O) |
| coreutils `sort` / `cut` / `uniq` | sort merge buffer already 256 KiB; cut/uniq are `getc` |
| make 4.3 / bash 5.2 | Already has Jenkins hash; not a CPU bottleneck |
| glibc `memcpy`/`memchr` | Already AVX2/AVX-512 ifunc |
| unzip / zip / cpio | Inflate/deflate bound; zip already has i386 CRC asm; cpio 512 B is format |
| pcre2 / xxhash / brotli | JIT+SSE2, AVX2 dispatch, and encoder TZCNT already on |
| file / gawk / e2fsprogs / patch | Wrong bottleneck (magic, parse, FS-block, line) |
| python3.12 hashlib/json | OpenSSL SHA-NI already; json has no SIMD; zlib win is libz |
| git | SHA1DC on purpose (`NO_OPENSSL=1`); pack I/O is mmap |
| curl | 16 KiB is API contract; TLS/socket bound |

## Apply

From an `apt-get source` tree (debian patches already applied):

```sh
patch -p1 -d zlib-1.3.dfsg   < ubuntu24/patches/zlib-1.3-x86-simd.patch
patch -p1 -d gzip-1.12       < ubuntu24/patches/gzip-1.12-pclmul-crc32.patch
patch -p1 -d coreutils-9.4   < ubuntu24/patches/coreutils-9.4-256k-iobuf.patch
patch -p1 -d grep-3.11       < ubuntu24/patches/grep-3.11-256k-buf.patch
patch -p1 -d tar-1.35+dfsg   < ubuntu24/patches/tar-1.35-256k-blocking.patch
patch -p1 -d diffutils-3.10  < ubuntu24/patches/diffutils-3.10-256k-cmpbuf.patch
```

Or drop the patches into each package's `debian/patches/` and add them to `series`.

Fetch sources:

```sh
ubuntu24/scripts/fetch-sources.sh
```

Rebuild zlib:

```sh
cd ubuntu24/src/zlib-1.3.dfsg
./configure && make -j"$(nproc)" test
```

## Layout

- `patches/` — diffs against Ubuntu-patched upstream
- `benchmarks/` — isolated CRC / `read()` harness, `WAYS.md` (100 ideas), `bench_100.c` / `bench_100.py`
- `scripts/fetch-sources.sh` — `apt-get source` helper
- `src/` — local unpacked trees (gitignored; not committed)
