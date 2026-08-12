# Ubuntu 24.04 hot-path benches

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

256 KiB is the plateau on this machine; 16 KiB is the clear loss. `tr` used `BUFSIZ` (8 KiB); `head` pipe path used `BUFSIZ`. Both now 256 KiB.

## gzip `UNALIGNED_OK`

Ubuntu `debian/rules` had `ifeq ($(buildarch), amd64)` but never sets `buildarch`, so `-DUNALIGNED_OK` was not applied. `tailor.h` now defines it on `__x86_64__`, and `debian/rules` tests `DEB_HOST_ARCH`.

## DISCARD

- Replacing zlib's braided CRC with PCLMUL **without** a working ifunc: no win (stays at 6 GB/s).
- zlib/gzip AVX2 or 8-byte `compare256` in `longest_match`: **0.91×** deflate-6 on a 24 MiB Python stdlib corpus (short matches dominate). Only helped highly repetitive dictionary text (~1.06×).
- Expecting gzip `-1` / deflate-6 to jump from checksum SIMD: LZ77/Huffman bound.
- xz CRC CLMUL, zstd BMI2, OpenSSL SHA-NI, coreutils `cksum` PCLMUL, glibc `memcpy`: already in Noble.
- Kernel / OpenSSL SHA / AVX2 `wc -l`: already in Noble.
