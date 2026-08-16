#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")" && pwd)"
SSH_PORT="${SSH_PORT:-2222}"
exec ssh -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null \
  -o ConnectTimeout=5 -i "$ROOT/id_ed25519" -p "$SSH_PORT" ubuntu@127.0.0.1 "$@"
