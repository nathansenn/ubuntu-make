# Ubuntu 24.04 userspace speedups

Patches against the **Ubuntu 24.04 LTS (Noble)** packages fetched with `apt-get source` on this host:

| Package | Ubuntu version |
|---|---|
| zlib | 1.3.dfsg-3.1ubuntu2.1 |
| gzip | 1.12-1ubuntu3.2 |
| coreutils | 9.4-3ubuntu6.2 |
| grep | 3.11-4build1 |

This is not a kernel tree. Noble already ships AVX2 `wc -l`, OpenSSL SHA-NI `sha256sum`, PCLMUL `cksum`, and `copy_file_range` in `cat`/`cp`. Those are left alone.

## What was missing on x86_64

Ubuntu's zlib 1.3 already has SIMD CRC on **POWER** and **s390x**, but `libz.so.1.3` on amd64 had **no PCLMUL CRC-32 and no SSSE3 Adler-32**. gzip 1.12 still used a byte-at-a-time CRC table. coreutils `wc` (non-AVX2 paths) read 16 KiB at a time.

## Proven results (KEEP)

Measured on this Xeon (SHA-NI, AVX2, AVX-512, PCLMUL). Full numbers: `benchmarks/RESULTS.md`.

| Change | vs Ubuntu stock | Decision |
|---|---|---|
| zlib CRC-32 PCLMUL folding (ifunc + CPUID) | **4.07×** (24.6 vs 6.0 GB/s) | KEEP |
| zlib Adler-32 SSSE3 (ifunc + CPUID) | **6.69×** (24.0 vs 3.6 GB/s) | KEEP |
| gzip `updcrc` PCLMUL | **41.8×** (20.9 vs 0.50 GB/s) | KEEP |
| Python `zlib.decompress` via new `libz` | **1.10×** (666 vs 606 MB/s) | KEEP |
| coreutils `wc` `BUFFER_SIZE` 16 KiB → 256 KiB | **1.50×** read throughput vs 16 KiB | KEEP |
| coreutils `IO_BUFSIZE` 128 KiB → 256 KiB | **1.02×** (matches upstream comment table) | KEEP |
| grep `INITIAL_BUFSIZE` 96 KiB → 256 KiB | fewer syscalls; I/O already plateaued | KEEP |

`__builtin_cpu_supports()` returns 0 inside GNU ifunc resolvers (libgcc CPU init has not run yet). Resolvers use **CPUID leaf 1** instead. Direct calls to the SIMD kernels were ~4×/6×; ifunc with the builtin stayed at scalar speed until that fix.

## Apply

From an `apt-get source` tree (debian patches already applied):

```sh
patch -p1 -d zlib-1.3.dfsg   < ubuntu24/patches/zlib-1.3-x86-simd.patch
patch -p1 -d gzip-1.12       < ubuntu24/patches/gzip-1.12-pclmul-crc32.patch
patch -p1 -d coreutils-9.4   < ubuntu24/patches/coreutils-9.4-256k-iobuf.patch
patch -p1 -d grep-3.11       < ubuntu24/patches/grep-3.11-256k-buf.patch
```

Or drop the zlib/gzip/coreutils/grep patches into each package's `debian/patches/` and add them to `series`.

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
- `benchmarks/` — isolated CRC / `read()` harness
- `scripts/fetch-sources.sh` — `apt-get source` helper
- `src/` — local unpacked trees (gitignored; not committed)
