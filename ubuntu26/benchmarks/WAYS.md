# Ubuntu 26.04 leftover ideas

Compared to the 24.04 catalog in the sibling `ubuntu-24-perf` work.

## KEEP

1. zlib 1.3.1 still has no amd64 PCLMUL/SSSE3 ifunc — port the 24.04 SIMD patch.
2. gzip 1.14 CRC is done; inflate stored-block / dist=1 / `copy_block` / fixed Huffman / `UNALIGNED_OK` are not.
3. tar still defaults to `-b20`.
4. grep 3.12 `GOOD_READSIZE_MIN` is 96 KiB.
5. diffutils 3.12 and git 2.53 still use 8 KiB floors.

## DISCARD (measured or already fast)

1. gzip 1.14 CRC — upstream slice-by-8 + PCLMUL.
2. GNU coreutils 9.7 `IO_BUFSIZE` / `wc` — already 256 KiB.
3. uutils `wc` — already 256 KiB.
4. uutils `yes` 16→256 KiB — warm write plateau already hit.
5. uutils `tee` 32→256 KiB — same plateau.
6. xz 5.8.3 — `CRC_X86_CLMUL` already on.
7. zstd 1.5.7 / OpenSSL 3.5 — already SIMD.
8. Raising tar blocking under TCG virtio — slower.

## Not pursued

- Nested KVM on this runner does not schedule vCPUs; TCG boots the guest.
- Full `ubuntu-desktop` vs `ubuntu-desktop-minimal` is a cloud-init package-set difference only.
