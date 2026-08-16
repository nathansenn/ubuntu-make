#!/usr/bin/env bash
# Full stock-vs-candidate sweep on Ubuntu 26.04.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
OUT="${1:-$HERE/RESULTS.md}"
TMP=/tmp/u26bench
mkdir -p "$TMP"
dd if=/dev/urandom of="$TMP/rand32m" bs=1M count=32 status=none
# `yes` exits SIGPIPE when head closes; do not fail the suite.
yes "the quick brown fox jumps over the lazy dog $RANDOM" | head -c 33554432 > "$TMP/text32m" || true

{
  echo "# Ubuntu 26.04 VM / userspace benches"
  echo
  echo "Generated: $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  echo
  echo '```'
  cat /etc/os-release
  uname -a
  nproc
  echo
  echo '== package versions =='
  dpkg-query -W zlib1g gzip coreutils gnu-coreutils grep tar git xz-utils zstd openssl python3 2>/dev/null || true
  echo
  echo '== cpu =='
  grep -m1 'model name' /proc/cpuinfo || true
  echo
  echo '== isolated zlib / read() =='
  cc -O2 -o "$TMP/bench_resolute" "$HERE/bench_resolute.c" -lz
  "$TMP/bench_resolute"
  echo
  timeit() {
    local label="$1"; shift
    TIMEFORMAT="$label %R s"
    time "$@"
  }

  echo '== gzip -dc (text / random) =='
  gzip -6 -c "$TMP/text32m" > "$TMP/text32m.gz"
  gzip -1 -c "$TMP/rand32m" > "$TMP/rand32m.gz"
  for f in text32m.gz rand32m.gz; do
    timeit "$f stock" gzip -dc "$TMP/$f" > /dev/null
  done
  echo
  echo '== tar default blocking =='
  timeit "tar cf default" tar -c -f "$TMP/a.tar" -C "$TMP" text32m rand32m
  mkdir -p "$TMP/x"
  timeit "tar xf default" tar -x -f "$TMP/a.tar" -C "$TMP/x"
  timeit "tar cf -b512" tar -b 512 -c -f "$TMP/b.tar" -C "$TMP" text32m rand32m
  mkdir -p "$TMP/y"
  timeit "tar xf -b512" tar -b 512 -x -f "$TMP/b.tar" -C "$TMP/y"
  echo
  echo '== cmp identical 256 MiB =='
  dd if=/dev/zero of="$TMP/z1" bs=1M count=256 status=none
  cp "$TMP/z1" "$TMP/z2"
  timeit "cmp -s" cmp -s "$TMP/z1" "$TMP/z2"
  echo
  echo '== sha256sum / cksum / zstd / xz =='
  timeit "sha256sum" sha256sum "$TMP/rand32m" >/dev/null
  timeit "cksum" cksum "$TMP/rand32m" >/dev/null
  timeit "zstd -1" zstd -1 -c "$TMP/text32m" >/dev/null
  timeit "zstd -d" bash -c 'zstd -1 -c "$0" | zstd -d >/dev/null' "$TMP/text32m"
  timeit "xz -1" xz -1 -c "$TMP/text32m" >/dev/null
  echo
  echo '== python zlib =='
  python3 - <<'PY'
import time, zlib
data = open("/tmp/u26bench/rand32m","rb").read()
t=time.perf_counter(); zlib.crc32(data); print("py crc32 %.3fs" % (time.perf_counter()-t))
comp=zlib.compress(data, 6)
t=time.perf_counter(); zlib.decompress(comp); print("py decompress-6 %.3fs" % (time.perf_counter()-t))
PY
  echo
  echo '== strings / nm-style I/O =='
  timeit "wc -c" wc -c "$TMP/rand32m"
  timeit "wc -l text" wc -l "$TMP/text32m"
  echo '```'
} | tee "$OUT"
echo "Wrote $OUT"
