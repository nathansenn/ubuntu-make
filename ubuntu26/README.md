# Ubuntu 26.04 userspace speedups

Patches against the **Ubuntu 26.04 LTS (Resolute)** packages. Latest guest image used here is the 26.04 cloud image (`20260731`) with `apt upgrade` to 26.04.1 packages, booted as a full desktop-seeded QEMU VM.

| Package | Resolute version | Notes vs 24.04 KEEP |
|---|---|---|
| zlib | 1.3.dfsg+really**1.3.1**-1ubuntu3 | Still no amd64 PCLMUL/SSSE3 ifunc |
| gzip | **1.14**-1~exp2ubuntu1.1 | Upstream already has PCLMUL + slice-by-8 CRC |
| tar | 1.35+dfsg-4ubuntu0.4 | Still `--show-defaults -b20` (10 KiB) |
| grep | 3.12-1 | `GOOD_READSIZE_MIN` is 96 KiB (renamed from `INITIAL_BUFSIZE`) |
| diffutils | 3.12-1 | `cmp` still floors at 8 KiB |
| git | 2.53.0 | `copy_fd` still 8 KiB |
| rust-coreutils | 0.8.0 | Default `yes`/`wc`/`cat`/`head`/`tee` provider |
| gnu-coreutils | 9.7-3ubuntu2 | `IO_BUFSIZE` already 256 KiB; `cp`/`mv`/`rm` stay GNU |
| xz-utils | 5.8.3-1 | CLMUL CRC already on |

This is not a kernel tree. Resolute already ships gzip 1.14 PCLMUL CRC, GNU coreutils 9.7 `IO_BUFSIZE` 256 KiB, uutils `wc` 256 KiB, xz CLMUL, OpenSSL 3.5 SHA-NI, and zstd BMI2. Those are left alone.

## Full OS on a VM

`ubuntu26/vm/boot-vm.sh` downloads the official 26.04 server cloud image, grows a qcow2 overlay, and cloud-init installs `ubuntu-desktop` (or `ubuntu-desktop-minimal`) plus a build toolchain.

This Cloud Agent host is Ubuntu 24.04. Nested KVM is present but TCG is the reliable accelerator here. SIMD numbers below were therefore re-measured on the **host Xeon** against Resolute sources; I/O buffer A/B (`tar -b`, `read()`, `yes` write) is valid in either environment.

```
ubuntu26/vm/boot-vm.sh          # create disk + seed + qemu
ubuntu26/vm/ssh-vm.sh           # ssh -p 2222
ubuntu26/benchmarks/run_full.sh # stock sweep inside the guest
```

## Proven results (KEEP)

Measured on the host Xeon (SHA-NI, AVX2, PCLMUL) with Resolute 26.04 sources, plus stock I/O A/B. Full numbers: `benchmarks/RESULTS.md`.

| Change | vs Ubuntu 26.04 stock | Decision |
|---|---|---|
| zlib 1.3.1 CRC-32 PCLMUL folding (ifunc + CPUID) | **4.52×** (26.7 vs 5.9 GB/s, 128 MiB warm) | KEEP |
| zlib 1.3.1 Adler-32 SSSE3 (ifunc + CPUID) | **6.71×** (25.6 vs 3.8 GB/s) | KEEP |
| gzip 1.14 stored-block + dist=1 + `copy_block` + fixed Huffman + `UNALIGNED_OK` | **1.50×** text, **12.2×** incompressible | KEEP |
| tar `DEFAULT_BLOCKING` 20 → 512 | 1.03× on host tmpfs; 0.53× under TCG | KEEP for disk |
| grep `GOOD_READSIZE_MIN` 96 → 256 KiB | fewer syscalls; `read()` already plateaus at 256 KiB | KEEP |
| diffutils `cmp` buffer floor 256 KiB | 32× fewer `read`s | KEEP |
| git 2.53 `copy_fd` 8 KiB → 256 KiB | same KEEP as 2.43 | KEEP |
| rust-coreutils `yes` 16→256 KiB | ~1.00× warm | DISCARD |
| rust-coreutils `tee` 32→256 KiB | already at plateau | DISCARD |
| gnu-coreutils 9.7 leftover BUFSIZ | warm 8 KiB ≈ 256 KiB here | DISCARD |

`__builtin_cpu_supports()` still returns 0 inside GNU ifunc resolvers. zlib resolvers use **CPUID leaf 1**.

## Surveyed and left alone on 26.04

| Area | Finding |
|---|---|
| gzip 1.14 CRC | Already slice-by-8 + PCLMUL (`lib/crc-x86_64-pclmul.c`) |
| GNU coreutils 9.7 `IO_BUFSIZE` / `wc` | Already 256 KiB |
| uutils `wc` | Already `BUF_SIZE = 256 * 1024` |
| xz 5.8.3 | Already `CRC_X86_CLMUL` |
| zstd 1.5.7 | BMI2 + ASM already on |
| OpenSSL 3.5 / `sha256sum` | SHA-NI; default `sha256sum` is uutils |
| uutils `cksum` | Already prints `pclmul` |

## Apply

From an `apt-get source` tree (debian patches already applied):

```
cd zlib-1.3.dfsg+really1.3.1 && patch -p1 < ../patches/zlib-1.3.1-x86-simd.patch
cd gzip-1.14 && patch -p1 < ../patches/gzip-1.14-inflate-hotpaths.patch
```

`ubuntu26/scripts/fetch-sources.sh` pulls the Resolute sources from Launchpad when the host is not Resolute. `ubuntu26/src/` is gitignored.
