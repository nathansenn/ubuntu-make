# AGENTS.md

## Cursor Cloud specific instructions

Ubuntu Make (`umake`) is a single Python 3 command-line application (no long-running
server). It installs developer environments/toolchains onto the machine. There is one
"service": the `umake` CLI itself.

### Environment

- Dependencies are installed into a virtualenv at `env/` created with
  `--system-site-packages`. This is required: `python-apt` (`apt`) and `PyGObject`
  (`gi`) are consumed from the system site-packages (installed via apt), while the
  remaining requirements come from `requirements.txt` via pip. Do not create a plain
  venv without `--system-site-packages` or `import apt` / `import gi` will fail.
- The update script recreates/refreshes `env/`. System apt packages
  (`gir1.2-gtk-3.0`, `libgirepository1.0-dev`, `python3-apt`, `python3-gi`,
  `python3-cairo`, `fakeroot`, `gettext`, `python3-venv`) are provided by the VM
  snapshot, not by the update script.

### Running the app

- Run the local CLI with `env/bin/python bin/umake ...` (do not rely on a globally
  installed `umake`). Examples: `--help`, `--list-available`, `--list-installed`.
- Global flags (`-v`, `-y/--assume-yes`, `-r/--remove`) must come BEFORE the
  category/framework, e.g. `umake -r go go-lang`.
- Installing a framework prompts interactively for an install path. Pass the path as a
  positional argument to run non-interactively, e.g.
  `env/bin/python bin/umake go go-lang /tmp/go-demo`. Piping to stdin does not reliably
  answer the prompt (it raises `EOFError`).
- Framework install state is tracked in `~/.config/umake`; umake skips re-installing an
  already-installed framework. Use `umake -r <category> <framework>` to remove it first
  if you need a clean re-install.

### Lint / test / build

- Lint (matches CI `style_test`): `env/bin/python runtests pep8`. NOTE: the repo's
  `runtests pep8` target points at the empty `tests/__init__.py`, so it collects 0
  tests and exits 0 (this is the same behavior CI relies on). The real style checks
  live in `tests/test_style.py`.
- Tests (matches CI `small_tests`): `env/bin/python runtests small`. See `runtests`
  and `README.md` for the `small`/`medium`/`large`/`pep8` test types.
- There is no build step for development; `setup.py` is only for packaging/releases.

### Known pre-existing failures (version drift, not env setup issues)

This codebase targets Python 3.8 / older library versions but runs here on Python 3.12
with modern libraries. The following fail regardless of environment setup and should
NOT be "fixed" as part of setup:

- `tests/small/test_download_center.py` secure/progress tests: rely on
  `urllib3.exceptions.SubjectAltNameWarning` (removed in modern urllib3) and on old
  progress-reporting chunk counts.
- `tests/small/test_frameworks_loader.py` run/setup tests: test fixtures build an
  argparse `Namespace` missing the newer `assume_yes` attribute that the code reads.
- `tests/test_style.py`: a much newer `pycodestyle` reports extra warnings the repo
  never cleaned up, and `flake8` is only on PATH inside `env/` (the subprocess call
  can't find it). ~345/361 small tests pass.
