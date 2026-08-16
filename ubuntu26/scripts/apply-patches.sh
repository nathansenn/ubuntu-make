#!/bin/sh
# Apply ubuntu26/patches/series onto unpacked trees under ubuntu26/src.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
SRC="$ROOT/src"
SERIES="$ROOT/patches/series"

apply_one() {
  patchfile="$1"
  case "$patchfile" in
    zlib-*) dir=$(echo "$SRC"/zlib-1.3.dfsg+really1.3.1) ;;
    gzip-*) dir=$(echo "$SRC"/gzip-1.14) ;;
    tar-*) dir=$(echo "$SRC"/tar-1.35+dfsg) ;;
    grep-*) dir=$(echo "$SRC"/grep-3.12) ;;
    diffutils-*) dir=$(echo "$SRC"/diffutils-3.12) ;;
    git-*) dir=$(echo "$SRC"/git-2.53.0) ;;
    rust-coreutils-*) dir=$(echo "$SRC"/rust-coreutils-0.8.0) ;;
    gnu-coreutils-*) dir=$(echo "$SRC"/coreutils-9.7) ;;
    *) echo "unknown patch $patchfile" >&2; return 1 ;;
  esac
  echo "Applying $patchfile in $dir"
  patch -d "$dir" -p1 < "$ROOT/patches/$patchfile"
}

while read -r line; do
  [ -z "$line" ] && continue
  case "$line" in
    \#*) continue ;;
  esac
  # Allow "file.patch   # comment"
  line="${line%%#*}"
  line=$(printf '%s' "$line" | tr -d '[:space:]')
  [ -z "$line" ] && continue
  apply_one "$line"
done < "$SERIES"
