# Ubuntu 24.04 hot-path benches

One hundred candidates (KEEP / ALREADY / DISCARD): `WAYS.md`.

Host: Intel Xeon, Ubuntu 24.04.4 LTS, gcc as shipped. 32 MiB buffers unless noted.

## zlib 1.3 (vs `/lib/x86_64-linux-gnu/libz.so.1`)

Correctness: 0 mismatches vs stock `crc32`/`adler32` and Python `zlib` for lengths 0..80 plus 81..1 MiB, including chained updates. `make test` OK.

| Entry | GB/s | vs stock |
|---|---|---|
| `crc32_z` ifunc (CPUID → PCLMUL) | 24.57 | **4.07×** |
| `crc32_z_default` (braided table) | 6.03 | 1.00× (stock is this) |
| `_crc32_z_pclmul` direct | 24.03 | — |
| `adler32_z` ifunc (CPUID → SSSE3) | 24.03 | **6.69×** |
| `adler32_z_default` | 3.59 | 1.00× |
| `_adler32_z_ssse3` direct | 23.23 | — |

Ifunc using `__builtin_cpu_supports()` stayed at scalar speed (resolver runs before libgcc CPU init). CPUID leaf 1 in the resolver selected SIMD.

Python `zlib.decompress` of 32 MiB random (level 6): stock 605.5 MB/s, new libz 665.7 MB/s (**1.10×**). Compress-6 unchanged (deflate-bound).

## gzip 1.12 `updcrc`

Same IEEE CRC-32 as zlib; gzip's register is already inverted.

| Implementation | GB/s | vs byte-wise |
|---|---|---|
| byte-wise table (stock `updcrc`) | 0.50 | 1.00× |
| PCLMUL fold | 20.91 | **41.8×** |

Match OK (`val=aeeefab0` on the 32 MiB pattern).

## `read()` block size (32 MiB file, 8 loops)

| Buffer | GB/s | vs 16 KiB |
|---|---|---|
| 16 KiB (old `wc` `BUFFER_SIZE`) | 12.18 | 1.00× |
| 64 KiB | 18.05 | 1.48× |
| 128 KiB (old coreutils `IO_BUFSIZE`) | 17.91 | 1.47× |
| 256 KiB (new) | 18.25 | **1.50×** |
| 512 KiB | 17.99 | 1.48× |

256 KiB is the plateau on this machine; 16 KiB is the clear loss. `tr` used `BUFSIZ` (8 KiB); `head`/`tail` pipe paths used `BUFSIZ`. Those now 256 KiB.

## gzip `UNALIGNED_OK`

Ubuntu `debian/rules` had `ifeq ($(buildarch), amd64)` but never sets `buildarch`, so `-DUNALIGNED_OK` was not applied. `tailor.h` now defines it on `__x86_64__`, and `debian/rules` tests `DEB_HOST_ARCH`.

## gzip 1.12 inflate (32 MiB payloads, `gzip -dc`)

Isolated against a build that already had PCLMUL `updcrc` (`crc-only`) so the inflate patch is not credited with the CRC win.

| Corpus | stock | crc-only | full (crc+inflate) | full vs crc-only |
|---|---|---|---|---|
| repetitive text (`gzip -6`) | 417 MB/s | 1957 MB/s | 3298 MB/s | **1.68×** |
| incompressible (`gzip -1` random) | 254 MB/s | 538 MB/s | 6816 MB/s | **12.6×** |
| zeros (`gzip -1`) | 461 MB/s | 10820 MB/s | 10890 MB/s | 1.01× (already output-bound) |

Correctness: patched `gzip -dc` matched stock output on all three corpora (`cmp` OK).

Changes: stored-block bulk `memcpy` from `inbuf` (was `NEEDBITS(8)` per byte); overlapping LZ77 copy uses `memset` for dist=1 and a 3-byte unroll otherwise; fixed Huffman tables built once.

## gzip 1.12 `copy_block` (`gzip -1` stored output)

Isolated against the previous patched binary (PCLMUL CRC + inflate + `UNALIGNED_OK`).

| Corpus | no-copyblock | with-copyblock | vs prior |
|---|---|---|---|
| incompressible (`gzip -1` random) | 50.1 MB/s | 54.7 MB/s | **1.09×** |
| repetitive text (`gzip -1`) | 2631 MB/s | 2622 MB/s | 1.00× (deflate_fast, not stored) |

Correctness: patched `-1`/`-6` round-tripped vs stock `cmp`.

## coreutils `yes` write floor (256 MiB to a file)

| Buffer | best | vs 8 KiB |
|---|---|---|
| 8 KiB (`BUFSIZ`) | 0.186 s | 1.00× |
| 256 KiB | 0.143 s | **1.30×** |

`tee`/`tac`/`head` remaining pipe/copy paths use the same 8→256 KiB `read()` plateau already measured above.

## tar 1.35 default blocking (stock `/usr/bin/tar -b`)

64 MiB payload (`text32m` + `rand32m`). `-b 20` is the historical default (10 KiB records); `-b 512` is 256 KiB.

| Command | `-b 20` | `-b 512` | vs 20 |
|---|---|---|---|
| `tar cf` | 0.0646 s | 0.0476 s | **1.36×** |
| `tar xf` | 0.0270 s | 0.0220 s | **1.23×** |

## diffutils 3.10 `cmp -s` (256 MiB identical files)

| Binary | best | vs stock |
|---|---|---|
| `/usr/bin/cmp` (4 KiB `st_blksize`) | 0.0827 s | 1.00× |
| patched (256 KiB floor) | 0.0817 s | 1.01× |

End-to-end is memcmp-bound at ~3.1 GB/s on this host. The patch still cuts `read` syscalls ~64× and matches the 256 KiB policy used by coreutils/grep.

## DISCARD

- Replacing zlib's braided CRC with PCLMUL **without** a working ifunc: no win (stays at 6 GB/s).
- zlib/gzip AVX2 or 8-byte `compare256` in `longest_match`: **0.91×** deflate-6 on a 24 MiB Python stdlib corpus (short matches dominate). Only helped highly repetitive dictionary text (~1.06×).
- zlib `inflate_fast` AVX2/AVX-512 widened copies: max match 258 B; overlap (`dist=1`) dominates; not a small patch.
- Expecting gzip `-1` / deflate-6 to jump from checksum SIMD: LZ77/Huffman bound.
- xz CRC CLMUL, zstd BMI2, OpenSSL SHA-NI, coreutils `cksum` PCLMUL, glibc `memcpy`: already in Noble.
- Kernel / OpenSSL SHA / AVX2 `wc -l`: already in Noble.
- bzip2 output-path PCLMUL CRC: ~0–9% of decompress except pathological RLE.
- libpng `--enable-intel-sse`: `timepng` ~1.00× (inflate-bound).
- pigz-local CRC/SIMD: pigz already calls `libz` `crc32`/`deflate`.
- sed / findutils / mawk / jq / libxml2 / less / procps / sqlite3 256 KiB: wrong bottleneck.
- rsync `IO_BUFFER_SIZE` 32→256 KiB: file map already 256 KiB; socket buffers are per-connection RAM.
- xz 5.6 range-decoder asm onto Ubuntu's 5.4.5: large cherry-pick after the backdoor revert.
- zlib `inffast` `memcpy` when `dist>=len`: leftover non-overlap copies after stored/dist=1 wins.
- unzip/zip/cpio 256 KiB or PCLMUL CRC: inflate/deflate bound; zip already has i386 CRC asm.
- pcre2/xxhash/brotli debian SIMD flags: already enabled on amd64.
- file/gawk/e2fsprogs/patch/make: wrong bottleneck.
- python3.12 SHA/json: OpenSSL SHA-NI already; no json SIMD.
- git SHA-NI / 256 KiB: `NO_OPENSSL=1` + SHA1DC by policy; mmap packs.
- curl 16→256 KiB: API contract; TLS/socket bound.

## Running-app memory (no new KEEP)

See `MEMORY.md`. Headline measurements on this 4-CPU Xeon:

| Test | Default | Treatment | vs default |
|---|---|---|---|
| 256 MiB first-touch, 2 MiB + `MADV_HUGEPAGE` | 0.0807 s | 0.0396 s | 2.04× fill only |
| 128 MiB `read` + line `qsort` + THP (run 1) | 1.053 s | 1.282 s | **0.82×** |
| glibc 8-thread alloc, `arena_max=1` | 83.8k kops/s | 26.3k | **0.31×** |
| glibc `tcache_count=0` | 83.8k kops/s | 21.9k | **0.26×** |
| Python 200k × 64 B, `PYTHONMALLOC=malloc` | 3950 kalloc/s | 7916 | 2.00× microbench only |
| `malloc_trim` after 16 MiB / 256 KiB | 18328 kB RSS | 18328 kB | no change |
| stock `sort -S 128M` 4M lines / 124 MiB | 3.23–3.30 s, 135 MiB RSS | — | already sized from physmem |

THP needs 2 MiB alignment to form huge pages. It helps sequential fill and
does not help GNU `sort`'s compare phase. No memory patch shipped.

## Wave 2 leftover sweep (no new KEEP)

32 MiB cached text/random on this host. Full table: `WAYS.md` #114–#213.

The only candidate that looked like a win was `seq` 8→256 KiB (`stdbuf -o256K`
on `seq 1 20000000`): a cold run was ~2×, but 8 warmed runs were **median
0.99×** (0.467 vs 0.473 s). `seq` is increment+format bound; `yes` was the
write-only case that actually kept 256 KiB.

`od -tx1` is ~8 MB/s (`xprintf` per field). `iconv` UTF-8→UTF-8 is 0.11 s vs
`cat` 0.02 s (validation, not a small patch). gzip in/out buffers are already
256 KiB. Line tools (`cut`/`nl`/`paste`/…) stay parse-bound vs `wc -l` 0.015 s.

Python `open(..., buffering=256*1024)` reads 32 MiB in 0.026 s vs 0.035 s at
the 8 KiB default (**1.37×**) but would charge every file object 256 KiB.
Leave `io.DEFAULT_BUFFER_SIZE` at 8192; apps that drain large files can set
`buffering=` themselves.
