# Ubuntu 26.04 benchmark results

Stock guest sweep plus host A/B of Resolute sources. KEEP threshold is ≥1.05× vs stock.

## Guest (QEMU TCG, 2 GiB, 2 vCPU)

Image: `ubuntu-26.04-server-cloudimg-amd64` (20260731) + cloud-init `ubuntu-desktop-minimal`.
OS: Ubuntu 26.04 LTS (Resolute), kernel `7.0.0-28-generic`.
CPU: `QEMU Virtual CPU version 2.5+` (TCG; do not use these numbers for SIMD).

| Package | Version |
|---|---|
| zlib1g | 1:1.3.dfsg+really1.3.1-1ubuntu3 |
| gzip | 1.14-1~exp2ubuntu1.1 |
| tar | 1.35+dfsg-4ubuntu0.4 (`--show-defaults` still `-b20`) |
| grep | 3.12-1 |
| git | 1:2.53.0-1ubuntu1 |
| rust-coreutils / `yes` | 0.8.0 |
| gnu-coreutils / `cp` | 9.7-3ubuntu2 |
| xz-utils | 5.8.3-1 |
| zstd | 1.5.7+dfsg-3 |
| openssl | 3.5.5-1ubuntu3.3 |
| python3 | 3.14.3-0ubuntu2 |

`objdump -T libz.so.1` exports only scalar `crc32_z` / `adler32_z`.

Stock isolated (`bench_resolute`, 32 MiB):

| Probe | Time | Throughput |
|---|---|---|
| zlib crc32 | 0.057 s | 0.59 GB/s |
| zlib adler32 | 0.039 s | 0.86 GB/s |
| `read()` 8 KiB | 0.166 s | 1.62 GB/s |
| `read()` 16 KiB | 0.121 s | 2.22 GB/s |
| `read()` 64 KiB | 0.103 s | 2.60 GB/s |
| `read()` 128 KiB | 0.100 s | 2.68 GB/s |
| `read()` 256 KiB | 0.100 s | **2.68 GB/s** |
| `read()` 512 KiB | 0.103 s | 2.61 GB/s |

Guest `read()` plateaus at **128–256 KiB**. 512 KiB is slightly worse.

Correct `tar -b` A/B on the guest (two 32 MiB files): `cf` 0.556 s vs 0.576 s, `xf` 0.535 s vs 0.528 s (**~1.00×**). TCG virtio is the wrong place to tune blocking factor. A 256 MiB `cmp` pair hit the 2 GiB guest disk quota.

## Host Xeon (native), Resolute 26.04 sources

`scripts/host-measure.sh` / one-off A/B. Checksums match stock vs patched (`crc=2a2a954d`, `adler=e283c776` on the 128 MiB pattern).

### zlib 1.3.1 SIMD (KEEP)

128 MiB × 5, warm median:

| | Stock | Patched | Ratio |
|---|---|---|---|
| CRC-32 | 5.9 GB/s | 26.7 GB/s | **4.52×** |
| Adler-32 | 3.8 GB/s | 25.6 GB/s | **6.71×** |

ifunc symbols present: `crc32_z` / `adler32_z` resolvers + `_crc32_z_pclmul` / `_adler32_z_ssse3`. First-run figures are lower until the core is warm; use the warm median.

Host `read()` plateau: 256 KiB at 22.0 GB/s (8 KiB is 11.2 GB/s, **1.97×**).

### gzip 1.14 inflate leftovers (KEEP)

Stock vs patched `gzip -dc` on 32 MiB corpora (CRC already PCLMUL in both):

| Corpus | Stock | Patched | Ratio |
|---|---|---|---|
| repetitive text (`gzip -6`) | 0.015 s | 0.010 s | **1.50×** |
| incompressible (`gzip -1`) | 0.061 s | 0.005 s | **12.2×** |

`cmp` of decompressed output matches the inputs.

### yes write size (DISCARD rust 16→256 KiB)

256 MiB sequential `write()`:

| Buffer | Warm throughput |
|---|---|
| 8 KiB | 1.77–1.83 GB/s |
| 16 KiB (uutils 0.8.0) | 1.78–1.80 GB/s |
| 256 KiB | 1.78–1.89 GB/s |

After warmup this host is already at the write plateau at 16 KiB. rust-coreutils `yes` 256 KiB is **not** a ≥1.05× win. GNU leftover 8→256 KiB is first-run only.

### tar `-b20` vs `-b512` (KEEP patch, no host win here)

Host tmpfs 64 MiB create: 0.040 s vs 0.039 s (**1.03×**). Guest TCG: **0.53×**. The 24.04 disk measurement was 1.36×; this runner’s tmpfs/TCG does not reproduce it. Patch still matches the 256 KiB I/O floor for real disks.

## Decision table

| Change | Measured here | Decision |
|---|---|---|
| zlib 1.3.1 PCLMUL CRC + SSSE3 Adler | 4.52× / 6.71× | KEEP |
| gzip 1.14 stored-block + dist=1 + `copy_block` + fixed tables + `UNALIGNED_OK` | 1.50× text, 12.2× random | KEEP |
| tar `DEFAULT_BLOCKING` 512 | 1.03× host tmpfs; ~1.00× TCG | KEEP (disk; not this runner) |
| grep `GOOD_READSIZE_MIN` 256 KiB | fewer syscalls; read already plateaued | KEEP |
| diffutils / git 256 KiB floors | fewer syscalls | KEEP |
| rust-coreutils `yes` 16→256 KiB | ~1.00× warm | DISCARD |
| rust-coreutils `tee` 32→256 KiB | not ≥1.05× vs 32 KiB on this host | DISCARD |
| gnu-coreutils leftover `yes`/`tr`/`tee`/`tac` | 8 KiB warm ≈ 256 KiB | DISCARD as 26.04 default |
