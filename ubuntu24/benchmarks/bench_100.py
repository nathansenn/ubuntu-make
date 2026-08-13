#!/usr/bin/env python3
"""End-to-end tool benches for the 100-way Ubuntu 24.04 survey."""
from __future__ import annotations

import os
import shutil
import subprocess
import time
from pathlib import Path

TMP = Path("/tmp/u24ways")
TMP.mkdir(exist_ok=True)
TEXT = TMP / "text32m"
RAND = TMP / "rand32m"
OUT = TMP / "out"


def best(cmd, n=4, warmup=True, env=None):
    if warmup:
        subprocess.run(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, env=env)
    times = []
    for _ in range(n):
        t0 = time.perf_counter()
        r = subprocess.run(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, env=env)
        if r.returncode != 0:
            return None
        times.append(time.perf_counter() - t0)
    times.sort()
    return times[0]


def mb_s(sec, mib=32.0):
    if not sec:
        return None
    return mib / sec


def row(name, a, b=None, unit="s"):
    if a is None:
        print(f"{name:<42} FAIL")
        return
    if b is None:
        print(f"{name:<42} {a:8.4f} {unit}")
        return
    print(f"{name:<42} {a:8.4f} -> {b:8.4f}  {a/b:5.2f}x")


def ensure_corpora():
    if not TEXT.exists() or TEXT.stat().st_size < 32 * 1024 * 1024:
        line = b"hello world this is a test line of text\n"
        TEXT.write_bytes(line * ((32 * 1024 * 1024) // len(line)))
    if not RAND.exists() or RAND.stat().st_size < 32 * 1024 * 1024:
        subprocess.run(["dd", "if=/dev/urandom", f"of={RAND}", "bs=1M", "count=32", "status=none"], check=True)


def main():
    ensure_corpora()
    patched_gzip = Path("/workspace/ubuntu24/src/gzip-1.12/gzip")
    patched_cmp = Path("/workspace/ubuntu24/src/diffutils-3.10/src/cmp")

    print("=== locale / grep / sort ===")
    env_c = os.environ.copy()
    env_c["LC_ALL"] = "C"
    env_utf = os.environ.copy()
    env_utf["LC_ALL"] = "C.UTF-8"
    row("grep -c -F text LC_ALL=C.UTF-8", best(["grep", "-c", "-F", "test line", str(TEXT)], env=env_utf))
    row("grep -c -F text LC_ALL=C", best(["grep", "-c", "-F", "test line", str(TEXT)], env=env_c))
    row("grep -c -E text UTF-8", best(["grep", "-c", "-E", "test line", str(TEXT)], env=env_utf))
    row("grep -c -F vs -E (C)", best(["grep", "-c", "-F", "test line", str(TEXT)], env=env_c),
        best(["grep", "-c", "-E", "test line", str(TEXT)], env=env_c))
    row("sort UTF-8", best(["sort", str(TEXT)], env=env_utf))
    row("sort C", best(["sort", str(TEXT)], env=env_c))
    row("sort -S 64M C", best(["sort", "-S", "64M", str(TEXT)], env=env_c))
    row("sort --parallel=1 C", best(["sort", "--parallel=1", str(TEXT)], env=env_c))
    row("sort --parallel=4 C", best(["sort", "--parallel=4", str(TEXT)], env=env_c))

    print("=== hash / cksum (already SIMD) ===")
    for tool in ("sha256sum", "md5sum", "cksum", "sha1sum"):
        if shutil.which(tool):
            t = best([tool, str(RAND)])
            print(f"{tool:<42} {t:8.4f}s  {mb_s(t):7.1f} MB/s")

    print("=== compressors stock ===")
    for cmd, label in (
        (["gzip", "-1c", str(RAND)], "gzip -1 random"),
        (["gzip", "-6c", str(TEXT)], "gzip -6 text"),
        (["gzip", "-dc", str(TEXT) + ".skip"], "skip"),
    ):
        if label == "skip":
            continue
        t = best(cmd)
        print(f"{label:<42} {t:8.4f}s  {mb_s(t):7.1f} MB/s")
    if patched_gzip.exists():
        row("gzip -1 random stock vs patched",
            best(["/usr/bin/gzip", "-1c", str(RAND)]),
            best([str(patched_gzip), "-1c", str(RAND)]))

    gz = TMP / "text32m.gz"
    if not gz.exists():
        subprocess.run(["gzip", "-6c", str(TEXT)], stdout=gz.open("wb"), check=True)
    row("gunzip stock", best(["/usr/bin/gzip", "-dc", str(gz)]))
    if patched_gzip.exists():
        row("gunzip patched", best([str(patched_gzip), "-dc", str(gz)]))
        row("gunzip stock vs patched",
            best(["/usr/bin/gzip", "-dc", str(gz)]),
            best([str(patched_gzip), "-dc", str(gz)]))

    for tool, args in (
        ("xz", ["xz", "-1c", str(TEXT)]),
        ("zstd", ["zstd", "-1c", str(TEXT)]),
        ("lz4", ["lz4", "-1c", str(TEXT)]),
        ("bzip2", ["bzip2", "-1c", str(TEXT)]),
        ("pigz", ["pigz", "-p1", "-1c", str(RAND)]),
    ):
        if shutil.which(tool):
            t = best(args)
            if t:
                print(f"{tool + ' -1':<42} {t:8.4f}s  {mb_s(t):7.1f} MB/s")

    print("=== tar blocking ===")
    tardir = TMP / "tardir"
    tardir.mkdir(exist_ok=True)
    if not (tardir / "text32m").exists():
        shutil.copy(TEXT, tardir / "text32m")
    if not (tardir / "rand32m").exists():
        shutil.copy(RAND, tardir / "rand32m")
    archive = TMP / "t.tar"
    for b in (20, 64, 128, 256, 512, 1024):
        t = best(["tar", "-b", str(b), "-cf", str(archive), "-C", str(tardir), "text32m", "rand32m"])
        print(f"tar -b {b:<5} cf                        {t:8.4f}s")

    print("=== cmp / wc / cat / dd ===")
    a256 = TMP / "a256"
    b256 = TMP / "b256"
    if not a256.exists():
        subprocess.run(["dd", "if=/dev/urandom", f"of={a256}", "bs=1M", "count=64", "status=none"], check=True)
        shutil.copy(a256, b256)
    row("cmp -s 64MiB stock", best(["/usr/bin/cmp", "-s", str(a256), str(b256)]))
    if patched_cmp.exists():
        row("cmp -s 64MiB patched", best([str(patched_cmp), "-s", str(a256), str(b256)]))
    row("wc -l text", best(["wc", "-l", str(TEXT)]))
    row("wc -c text", best(["wc", "-c", str(TEXT)]))
    row("cat text > /dev/null", best(["cat", str(TEXT)]))
    row("dd bs=8k", best(["dd", f"if={TEXT}", "of=/dev/null", "bs=8k", "status=none"]))
    row("dd bs=256k", best(["dd", f"if={TEXT}", "of=/dev/null", "bs=256k", "status=none"]))
    row("dd bs=1M", best(["dd", f"if={TEXT}", "of=/dev/null", "bs=1M", "status=none"]))

    print("=== base64 / tr / tee / yes ===")
    row("base64 encode 32M", best(["base64", str(RAND)]))
    row("tr a-z A-Z", best(["tr", "a-z", "A-Z"], env=None) if False else best(["bash", "-c", f"tr a-z A-Z < {TEXT}"]))
    row("tee > /dev/null", best(["bash", "-c", f"tee /dev/null < {TEXT} > /dev/null"]))
    row("head -c 32M", best(["head", "-c", "33554432", str(TEXT)]))
    row("tail -c 32M", best(["tail", "-c", "33554432", str(TEXT)]))

    print("=== awk / sed / mawk ===")
    row("mawk NR", best(["mawk", "{c++} END{print c}", str(TEXT)]))
    row("gawk NR", best(["gawk", "{c++} END{print c}", str(TEXT)]))
    row("gawk AWKBUFSIZE=256k", best(["gawk", "{c++} END{print c}", str(TEXT)],
                                     env={**os.environ, "AWKBUFSIZE": "262144"}))
    row("sed -n p", best(["sed", "-n", "p", str(TEXT)]))
    row("sed s/e/E/g", best(["sed", "s/e/E/g", str(TEXT)]))

    print("=== python ===")
    row("python hashlib.sha256", best(["python3", "-c",
        "import hashlib,pathlib; hashlib.sha256(pathlib.Path('%s').read_bytes()).hexdigest()" % RAND]))
    row("python json load 2M", best(["python3", "-c",
        "import json; json.loads('['+','.join(['{\"a\":%d}'%i for i in range(20000)])+']')"]))
    row("python zlib.decompress", best(["python3", "-c",
        "import zlib,pathlib; zlib.decompress(pathlib.Path('%s').read_bytes(),31)" % gz]))

    print("=== sysctl snapshot (not mutated) ===")
    for p in (
        "/proc/sys/vm/swappiness",
        "/proc/sys/vm/dirty_ratio",
        "/proc/sys/vm/dirty_background_ratio",
        "/proc/sys/vm/vfs_cache_pressure",
        "/sys/kernel/mm/transparent_hugepage/enabled",
    ):
        try:
            print(f"  {p}: {Path(p).read_text().strip()}")
        except OSError as e:
            print(f"  {p}: {e}")

    print("DONE")


if __name__ == "__main__":
    main()
