#!/bin/sh
# Fetch Ubuntu 26.04 (Resolute) userspace sources used by the speedup patches.
# Prefers apt-get source on a Resolute system; otherwise downloads from Launchpad.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
SRC="$ROOT/src"
mkdir -p "$SRC"

if [ "$(id -u)" -eq 0 ]; then
  SUDO=
else
  SUDO=sudo
fi

is_resolute() {
  [ -f /etc/os-release ] && grep -q 'VERSION_CODENAME=resolute' /etc/os-release
}

if is_resolute; then
  if [ -f /etc/apt/sources.list.d/ubuntu.sources ]; then
    $SUDO sed -i 's/^Types: deb$/Types: deb deb-src/' /etc/apt/sources.list.d/ubuntu.sources || true
  fi
  $SUDO apt-get update -qq
  cd "$SRC"
  apt-get source zlib gzip tar grep git diffutils coreutils rust-coreutils xz-utils
  echo "Unpacked under $SRC"
  ls -d "$SRC"/*/
  exit 0
fi

python3 - "$SRC" <<'PY'
import json, os, subprocess, sys, urllib.request

dest = sys.argv[1]
os.makedirs(dest, exist_ok=True)
pkgs = [
    "zlib",
    "gzip",
    "tar",
    "grep",
    "git",
    "diffutils",
    "coreutils",
    "rust-coreutils",
    "xz-utils",
]
base = (
    "https://api.launchpad.net/1.0/ubuntu/+archive/primary"
    "?ws.op=getPublishedSources&exact_match=true&status=Published"
    "&distro_series=https://api.launchpad.net/1.0/ubuntu/resolute"
    "&source_name="
)

def get(url):
    with urllib.request.urlopen(url) as r:
        return json.load(r)

for pkg in pkgs:
    d = get(base + pkg)
    entries = d.get("entries") or []
    if not entries:
        raise SystemExit(f"no published source for {pkg}")
    # Prefer Updates pocket when present.
    entries.sort(key=lambda e: (e.get("pocket") != "Updates", e.get("date_published") or ""))
    e = entries[0]
    print(pkg, e["source_package_version"], e.get("pocket"), flush=True)
    files = get(e["self_link"] + "?ws.op=sourceFileUrls")
    # Launchpad returns a list of URLs.
    urls = files if isinstance(files, list) else files.get("entries") or files
    if isinstance(urls, dict):
        urls = list(urls.values()) if all(isinstance(v, str) for v in urls.values()) else []
    for url in urls:
        name = url.rsplit("/", 1)[-1]
        path = os.path.join(dest, name)
        if os.path.exists(path) and os.path.getsize(path) > 0:
            print("  have", name, flush=True)
            continue
        print("  get", name, flush=True)
        urllib.request.urlretrieve(url, path + ".partial")
        os.replace(path + ".partial", path)

print("Downloaded under", dest)
PY
