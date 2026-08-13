#!/bin/sh
# Fetch Ubuntu 24.04 (Noble) userspace sources used by the speedup patches.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
SRC="$ROOT/src"
mkdir -p "$SRC"

if [ "$(id -u)" -eq 0 ]; then
  SUDO=
else
  SUDO=sudo
fi

if [ -f /etc/apt/sources.list.d/ubuntu.sources ]; then
  $SUDO sed -i 's/^Types: deb$/Types: deb deb-src/' /etc/apt/sources.list.d/ubuntu.sources || true
fi

$SUDO apt-get update -qq
cd "$SRC"
apt-get source zlib gzip coreutils grep tar diffutils

echo "Unpacked under $SRC"
ls -d "$SRC"/*/
