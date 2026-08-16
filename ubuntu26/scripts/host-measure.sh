#!/bin/sh
# Build stock vs patched 26.04 zlib/gzip on the host and emit RESULTS fragment.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
SRC="$ROOT/src"
OUT="${1:-$ROOT/benchmarks/HOST_MEASURE.md}"
WORK=/tmp/u26-host-measure
rm -rf "$WORK"
mkdir -p "$WORK"

cc -O2 -o "$WORK/bench_resolute" "$ROOT/benchmarks/bench_resolute.c" -lz
cc -O2 -o "$WORK/bench_yes_buf" "$ROOT/benchmarks/bench_yes_buf.c"

# Stock zlib 1.3.1
mkdir -p "$WORK/zlib-stock"
cd "$WORK/zlib-stock"
dpkg-source -x "$SRC"/zlib_1.3.dfsg+really1.3.1-1ubuntu3.dsc >/dev/null
ZDIR=$(find . -maxdepth 1 -type d -name 'zlib-*' | head -1)
cd "$ZDIR"
./configure --static >/dev/null
make -j"$(nproc)" libz.a >/dev/null
cp libz.a "$WORK/libz-stock.a"

# Patched zlib
mkdir -p "$WORK/zlib-patched"
cd "$WORK/zlib-patched"
dpkg-source -x "$SRC"/zlib_1.3.dfsg+really1.3.1-1ubuntu3.dsc >/dev/null
ZDIR=$(find . -maxdepth 1 -type d -name 'zlib-*' | head -1)
cd "$ZDIR"
patch -p1 < "$ROOT/patches/zlib-1.3.1-x86-simd.patch"
./configure --static >/dev/null
make -j"$(nproc)" libz.a >/dev/null
cp libz.a "$WORK/libz-patched.a"

cc -O2 -o "$WORK/bench_z_stock" "$ROOT/benchmarks/bench_resolute.c" "$WORK/libz-stock.a"
cc -O2 -o "$WORK/bench_z_patched" "$ROOT/benchmarks/bench_resolute.c" "$WORK/libz-patched.a"

# Stock / patched gzip 1.14
mkdir -p "$WORK/gzip-stock" "$WORK/gzip-patched"
cd "$WORK/gzip-stock"
dpkg-source -x "$SRC"/gzip_1.14-1~exp2ubuntu1.1.dsc >/dev/null
cd gzip-1.14
./configure >/dev/null
# Skip texinfo; gzip.o needs a configured tree (gnulib).
make -j"$(nproc)" gzip || make gzip
cp gzip "$WORK/gzip-stock.bin"

cp -a "$WORK/gzip-stock/gzip-1.14" "$WORK/gzip-patched-tree"
cd "$WORK/gzip-patched-tree"
patch -p1 < "$ROOT/patches/gzip-1.14-inflate-hotpaths.patch"
rm -f inflate.o bits.o gzip
make inflate.o bits.o gzip
cp gzip "$WORK/gzip-patched.bin"

dd if=/dev/urandom of="$WORK/rand32m" bs=1M count=32 status=none
# pipefail-safe text corpus
python3 -c 'open("/tmp/u26-host-measure/text32m","wb").write((b"the quick brown fox jumps over the lazy dog\n")*700000)'
head -c 33554432 "$WORK/text32m" > "$WORK/text32m.bin" || true
"$WORK/gzip-stock.bin" -6 -c "$WORK/text32m.bin" > "$WORK/text32m.gz"
"$WORK/gzip-stock.bin" -1 -c "$WORK/rand32m" > "$WORK/rand32m.gz"

{
  echo "# Host measurement of Ubuntu 26.04 sources"
  echo
  echo "Generated: $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  echo
  echo '```'
  uname -a
  grep -m1 'model name' /proc/cpuinfo || true
  echo
  echo '== stock libz (26.04 1.3.1) =='
  "$WORK/bench_z_stock"
  echo
  echo '== patched libz (PCLMUL CRC + SSSE3 Adler) =='
  "$WORK/bench_z_patched"
  echo
  echo '== yes write plateau =='
  "$WORK/bench_yes_buf"
  echo
  echo '== gzip -dc stock vs inflate patch =='
  /usr/bin/time -f "text stock   %e s" "$WORK/gzip-stock.bin" -dc "$WORK/text32m.gz" > /dev/null
  /usr/bin/time -f "text patched %e s" "$WORK/gzip-patched.bin" -dc "$WORK/text32m.gz" > /dev/null
  /usr/bin/time -f "rand stock   %e s" "$WORK/gzip-stock.bin" -dc "$WORK/rand32m.gz" > /dev/null
  /usr/bin/time -f "rand patched %e s" "$WORK/gzip-patched.bin" -dc "$WORK/rand32m.gz" > /dev/null
  echo
  echo '== tar -b20 vs -b512 =='
  /usr/bin/time -f "tar cf -b20  %e s" tar -b 20 -c -f "$WORK/a.tar" -C "$WORK" rand32m text32m.bin
  /usr/bin/time -f "tar cf -b512 %e s" tar -b 512 -c -f "$WORK/b.tar" -C "$WORK" rand32m text32m.bin
  echo '```'
} | tee "$OUT"
echo "Wrote $OUT"
