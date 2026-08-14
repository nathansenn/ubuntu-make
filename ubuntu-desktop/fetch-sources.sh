#!/usr/bin/env bash
# Fetch Ubuntu 26.04.1 (Resolute) desktop seed sources into ubuntu-desktop/src.
# Host can stay on Noble; only deb-src for resolute* is required.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
SRC="$ROOT/src"
mkdir -p "$SRC"
cd "$SRC"

# Core GNOME 50 session + seed packages that are actual desktop source.
# Binary-only / firmware / printer / font packages are omitted.
PACKAGES=(
  gnome-shell
  mutter
  gdm3
  nautilus
  gnome-control-center
  gnome-settings-daemon
  gnome-shell-ubuntu-extensions
  gnome-session
  ubuntu-settings
  yaru-theme
  ubuntu-meta
  gnome-menus
  gnome-initial-setup
  gnome-keyring
  gnome-remote-desktop
  gnome-bluetooth3
  gnome-calculator
  gnome-calendar
  gnome-characters
  gnome-clocks
  gnome-disk-utility
  gnome-font-viewer
  gnome-logs
  gnome-snapshot
  gnome-text-editor
  gnome-software
  loupe
  papers
  ptyxis
  seahorse
  showtime
  simple-scan
  baobab
  file-roller
  evince
  orca
  yelp
  zenity
  xdg-desktop-portal
  xdg-desktop-portal-gnome
  xdg-desktop-portal-gtk
  xdg-user-dirs-gtk
  xdg-terminal-exec
  pipewire
  wireplumber
  update-manager
  update-notifier
  software-properties
  language-selector
  ubuntu-release-upgrader
  ubuntu-drivers-common
  ubuntu-wallpapers
  gsettings-ubuntu-schemas
  at-spi2-core
  ibus
)

fetch_one() {
  local pkg="$1"
  # Prefer resolute-updates, then resolute, then any suite apt can see.
  apt-get source --download-only "${pkg}/resolute-updates" 2>/dev/null \
    || apt-get source --download-only "${pkg}/resolute" 2>/dev/null \
    || apt-get source --download-only "$pkg" \
    || echo "skip: $pkg (no source)" >&2
}

echo "Fetching ${#PACKAGES[@]} desktop source packages into $SRC"
for pkg in "${PACKAGES[@]}"; do
  echo "==> $pkg"
  fetch_one "$pkg"
done

# Unpack any dsc that is not already extracted.
# Do not use a bare glob + ls: with nullglob, a miss becomes `ls -d` on cwd.
shopt -s nullglob
for dsc in ./*.dsc; do
  srcname="$(sed -n 's/^Source: //p' "$dsc" | head -1)"
  matches=("${srcname}-"*/)
  if ((${#matches[@]})); then
    continue
  fi
  # Wallpapers orig is ~900 MiB of images; skip unpack.
  if [[ "$srcname" == "ubuntu-wallpapers" ]]; then
    echo "skip unpack: $srcname (image orig)"
    continue
  fi
  echo "unpack $dsc"
  dpkg-source -x "$dsc" >/dev/null
done

echo "done. Trees:"
ls -1d -- */ 2>/dev/null | sed 's#/##'
