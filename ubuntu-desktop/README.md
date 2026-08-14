# Ubuntu Desktop 26.04.1 (Resolute) — non-performance work

This tree is **Ubuntu Desktop**, not the 24.04 CLI userspace work in `ubuntu24/`.
The host stays Noble; Resolute is used as `deb-src` only.

- **Release:** Ubuntu 26.04.1 LTS (Resolute Raccoon), 6 Aug 2026
- **Shell:** GNOME 50 (`gnome-shell` 50.1-0ubuntu1.2 from resolute-updates)
- **Goal:** fetch the desktop seed, research **non-performance** bugs, and land
  focused correctness fixes (crashers, empty search, disposed objects, null
  deref). No I/O-buffer or SIMD work here.

`src/` is gitignored. Only patches, docs, the fetch script, and tests are
committed.

## Fetch sources

```bash
# once: Resolute deb-src (already present on this host as
# /etc/apt/sources.list.d/resolute-src.sources)
sudo apt-get update
./ubuntu-desktop/fetch-sources.sh
```

The script pulls the `ubuntu-desktop` / `ubuntu-desktop-minimal` seed plus the
GNOME 50 session stack (mutter, gdm3, nautilus, settings, portals, …).

## Apply the search-provider patch

```bash
cd ubuntu-desktop/src/gnome-shell-ubuntu-extensions-50.26.04.7ubuntu
patch -p1 < ../../patches/gnome-shell-ubuntu-extensions-search-providers.patch
```

## Tests

```bash
node ubuntu-desktop/tests/search-providers.test.mjs
```

These are logic tests of the bug patterns (unregister `splice(-1)`,
`get_id` TypeError, enable/disable race). They do not need a running GNOME
session.

## What is already fixed upstream / in 26.04.1

| Bug | Status in fetched tree |
|---|---|
| LP #2150103 / #2161808 — `_unregisterProvider` `splice(-1)` drops an arbitrary provider | **Fixed** in `gnome-shell` 50.1-0ubuntu1.2 (`debian/patches/ubuntu/search-Do-not-unregister-a-provider-that-was-not-register.patch`) |
| GDM session-type / login-screen crashers | Present in `gdm3` 50.1-0ubuntu0.1 |
| Settings remote-desktop nullable libsecret error | Present in `gnome-control-center` 50.3 (`remote-desktop-Correctly-handle-nullable-error-from-secre.patch`) |

The **trigger** in `gnome-shell-ubuntu-extensions` 50.26.04.7ubuntu was still
unfixed. That is what the patch in `patches/` addresses.

## Do not open a public PR

Work stays on `cursor/ubuntu-desktop-bugs-510a`. Commit and push the branch;
do not create or update a GitHub pull request.
