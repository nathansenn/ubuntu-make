#!/usr/bin/env bash
# Boot Ubuntu 26.04 (Resolute) with a full desktop seed in QEMU.
# Prefers KVM; falls back to TCG when /dev/kvm is unusable (nested guests).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
IMG_URL="${IMG_URL:-https://cloud-images.ubuntu.com/releases/26.04/release/ubuntu-26.04-server-cloudimg-amd64.img}"
BASE_IMG="$ROOT/ubuntu-26.04-server-cloudimg-amd64.img"
DISK="$ROOT/resolute-desktop.qcow2"
SEED="$ROOT/seed.iso"
KEY="$ROOT/id_ed25519"
SSH_PORT="${SSH_PORT:-2222}"
MEM_MB="${MEM_MB:-4096}"
CPUS="${CPUS:-2}"
DISK_GB="${DISK_GB:-40}"
ACCEL="${ACCEL:-auto}"
SEED_USER_DATA="${SEED_USER_DATA:-$ROOT/user-data.yaml}"

if [[ ! -f "$KEY" ]]; then
  ssh-keygen -t ed25519 -N "" -f "$KEY" -C "resolute-perf"
fi
PUB="$(cat "${KEY}.pub")"
sed "s|SSH_PUBKEY_PLACEHOLDER|$PUB|" "$SEED_USER_DATA" > "$ROOT/user-data.generated"

cloud-localds "$SEED" "$ROOT/user-data.generated" "$ROOT/meta-data.yaml"

if [[ ! -f "$BASE_IMG" ]]; then
  echo "Downloading $IMG_URL"
  wget -O "$BASE_IMG.partial" "$IMG_URL"
  mv "$BASE_IMG.partial" "$BASE_IMG"
fi

if [[ ! -f "$DISK" ]]; then
  qemu-img create -f qcow2 -F qcow2 -b "$BASE_IMG" "$DISK" "${DISK_GB}G"
fi

OVMF_CODE="${OVMF_CODE:-/usr/share/OVMF/OVMF_CODE_4M.fd}"
if [[ ! -f "$OVMF_CODE" ]]; then
  OVMF_CODE="/usr/share/OVMF/OVMF_CODE.fd"
fi
OVMF_VARS_TEMPLATE="${OVMF_VARS_TEMPLATE:-/usr/share/OVMF/OVMF_VARS_4M.fd}"
OVMF_VARS="$ROOT/OVMF_VARS.fd"
if [[ ! -f "$OVMF_VARS" && -f "$OVMF_VARS_TEMPLATE" ]]; then
  cp "$OVMF_VARS_TEMPLATE" "$OVMF_VARS"
fi

if [[ "$ACCEL" == auto ]]; then
  if [[ -r /dev/kvm ]] && [[ -w /dev/kvm ]]; then
    ACCEL=kvm
  else
    ACCEL=tcg
  fi
fi

QEMU_CPU=( -cpu host )
if [[ "$ACCEL" == tcg ]]; then
  QEMU_CPU=()
fi

EXTRA_ARGS=()
if [[ -f "$OVMF_VARS" ]]; then
  EXTRA_ARGS+=( -drive if=pflash,format=raw,file="$OVMF_VARS" )
fi

exec qemu-system-x86_64 \
  -name resolute-perf \
  -machine q35,accel="$ACCEL" \
  "${QEMU_CPU[@]}" \
  -m "$MEM_MB" \
  -smp "$CPUS" \
  -drive if=pflash,format=raw,readonly=on,file="$OVMF_CODE" \
  "${EXTRA_ARGS[@]}" \
  -drive file="$DISK",if=virtio,format=qcow2 \
  -drive file="$SEED",if=virtio,format=raw,readonly=on \
  -netdev user,id=net0,hostfwd=tcp:127.0.0.1:${SSH_PORT}-:22 \
  -device virtio-net-pci,netdev=net0 \
  -device virtio-rng-pci \
  -display none \
  -serial mon:stdio
