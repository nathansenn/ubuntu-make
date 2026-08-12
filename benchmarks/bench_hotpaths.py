#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Microbenchmarks for Ubuntu Make hot paths.

Compares candidate algorithms/implementations and prints a keep/discard table.
Run from the repo root:  python3 benchmarks/bench_hotpaths.py
"""

from __future__ import annotations

import hashlib
import os
import re
import shutil
import statistics
import sys
import tarfile
import tempfile
import time
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from threading import Thread

import requests
import yaml

try:
    from yaml import CSafeLoader
except ImportError:
    CSafeLoader = None


PAYLOAD_MB = 32
ROUNDS = 5
WARMUP = 1


def _ns(fn, rounds=ROUNDS, warmup=WARMUP):
    for _ in range(warmup):
        fn()
    samples = []
    for _ in range(rounds):
        start = time.perf_counter()
        fn()
        samples.append(time.perf_counter() - start)
    return min(samples), statistics.median(samples)


def _report(name, baseline_med, candidate_med, extra=""):
    if baseline_med <= 0:
        speedup = float("inf")
    else:
        speedup = baseline_med / candidate_med if candidate_med else float("inf")
    keep = speedup >= 1.05
    delta_pct = (1.0 - (candidate_med / baseline_med)) * 100 if baseline_med else 0
    decision = "KEEP" if keep else "DISCARD"
    print("  {:<48}  base={:8.4f}s  cand={:8.4f}s  {:+6.1f}%  {:8.2f}x  {} {}".format(
        name, baseline_med, candidate_med, delta_pct, speedup, decision, extra))
    return keep, speedup


def bench_copy_buffers(payload):
    print("\n== copy buffer sizes ({} MiB, median of {}) ==".format(PAYLOAD_MB, ROUNDS))
    results = {}
    dest_dir = tempfile.mkdtemp()
    try:
        for size in (8 * 1024, 16 * 1024, 64 * 1024, 256 * 1024, 1024 * 1024):
            dest = os.path.join(dest_dir, "out-{}".format(size))

            def run(buf=size, out=dest):
                with open(payload, "rb") as src, open(out, "wb") as dst:
                    while True:
                        chunk = src.read(buf)
                        if not chunk:
                            break
                        dst.write(chunk)

            _min, med = _ns(run)
            results[size] = med
            print("  {:>8}  median={:.4f}s  min={:.4f}s".format(size, med, _min))
    finally:
        shutil.rmtree(dest_dir)
    best = min(results, key=results.get)
    keep, _ = _report("larger-copy-buffer vs 8KiB", results[8 * 1024], results[best],
                      extra="best={}".format(best))
    return {"copy_best_size": best, "copy_keep": keep, "copy_results": results}


def bench_hash_fusion(payload):
    print("\n== hash-while-copy vs copy-then-hash (sha256, {} MiB) ==".format(PAYLOAD_MB))
    dest_dir = tempfile.mkdtemp()
    try:
        dest = os.path.join(dest_dir, "out")

        def two_pass():
            with open(payload, "rb") as src, open(dest, "wb") as dst:
                while True:
                    chunk = src.read(256 * 1024)
                    if not chunk:
                        break
                    dst.write(chunk)
            with open(dest, "rb") as f:
                hashlib.sha256(f.read()).hexdigest()

        def one_pass():
            h = hashlib.sha256()
            with open(payload, "rb") as src, open(dest, "wb") as dst:
                while True:
                    chunk = src.read(256 * 1024)
                    if not chunk:
                        break
                    dst.write(chunk)
                    h.update(chunk)
            h.hexdigest()

        def file_digest_after():
            with open(payload, "rb") as src, open(dest, "wb") as dst:
                while True:
                    chunk = src.read(256 * 1024)
                    if not chunk:
                        break
                    dst.write(chunk)
            with open(dest, "rb") as f:
                hashlib.file_digest(f, "sha256").hexdigest()

        _, two = _ns(two_pass)
        _, one = _ns(one_pass)
        _, fdg = _ns(file_digest_after)
        keep_one, _ = _report("hash-while-copy vs two-pass", two, one)
        keep_fd, _ = _report("file_digest-after vs two-pass", two, fdg)
        return {"hash_fusion_keep": keep_one, "file_digest_keep": keep_fd, "two": two, "one": one, "fdg": fdg}
    finally:
        shutil.rmtree(dest_dir)


def bench_yaml():
    print("\n== YAML CSafeLoader vs pure-Python safe_load ==")
    doc = yaml.dump({"frameworks": {
        "cat-{}".format(i): {"fw-{}".format(j): {"path": "/home/user/tools/{}".format(j)}
                             for j in range(20)}
        for i in range(30)
    }})
    data = doc * 20

    def py_load():
        yaml.load(data, Loader=yaml.SafeLoader)

    _, py_med = _ns(py_load, rounds=20)
    if CSafeLoader is None:
        print("  CSafeLoader unavailable")
        return {"yaml_keep": False}
    def c_load():
        yaml.load(data, Loader=CSafeLoader)
    _, c_med = _ns(c_load, rounds=20)
    keep, _ = _report("CSafeLoader vs SafeLoader", py_med, c_med)
    return {"yaml_keep": keep, "py": py_med, "c": c_med}


_TAG_RE = re.compile(r"<[^<]+?>")


def bench_strip_tags():
    print("\n== strip_tags compiled regex vs re.sub per call ==")
    html = ("<html><body>" + ("<p>license <a href='x'>text</a> more</p>\n" * 2000) + "</body></html>") * 5

    def naive():
        re.sub("<[^<]+?>", "", html)

    def compiled():
        _TAG_RE.sub("", html)

    _, naive_med = _ns(naive, rounds=30)
    _, compiled_med = _ns(compiled, rounds=30)
    keep, _ = _report("compiled-tag-regex vs re.sub", naive_med, compiled_med)
    return {"strip_keep": keep, "naive": naive_med, "compiled": compiled_med}


def bench_logging():
    print("\n== logging eager .format vs lazy %% interpolation (WARNING level) ==")
    import logging
    log = logging.getLogger("bench.hotpath")
    log.setLevel(logging.WARNING)
    progress = {"url": {"current": 123, "size": 456}}

    def eager():
        for _ in range(20000):
            log.debug("Deliver download update: {}".format(progress))

    def lazy():
        for _ in range(20000):
            log.debug("Deliver download update: %s", progress)

    _, eager_med = _ns(eager, rounds=8)
    _, lazy_med = _ns(lazy, rounds=8)
    keep, _ = _report("lazy-logging vs eager-format", eager_med, lazy_med)
    return {"logging_keep": keep, "eager": eager_med, "lazy": lazy_med}


def bench_string_join():
    print("\n== list-join vs += for framework listing ==")
    rows = ["\t{}: description of framework {}\n".format(i, i) for i in range(5000)]

    def plus_eq():
        out = ""
        for row in rows:
            out += row
        return out

    def join_list():
        parts = []
        for row in rows:
            parts.append(row)
        return "".join(parts)

    _, plus_med = _ns(plus_eq, rounds=30)
    _, join_med = _ns(join_list, rounds=30)
    keep, _ = _report("list-join vs +=", plus_med, join_med)
    return {"join_keep": keep, "plus": plus_med, "join": join_med}


def bench_listdir_vs_scandir(payload_parent):
    print("\n== os.scandir vs os.listdir for extract-move ==")
    tmp = tempfile.mkdtemp()
    src = os.path.join(tmp, "src")
    os.makedirs(src)
    for i in range(400):
        with open(os.path.join(src, "f{}".format(i)), "wb") as f:
            f.write(b"x" * 64)
    dest_a = os.path.join(tmp, "a")
    dest_b = os.path.join(tmp, "b")

    def listdir_move():
        os.makedirs(dest_a, exist_ok=True)
        for name in os.listdir(src):
            shutil.copy2(os.path.join(src, name), os.path.join(dest_a, name))

    def scandir_move():
        os.makedirs(dest_b, exist_ok=True)
        with os.scandir(src) as it:
            for entry in it:
                shutil.copy2(entry.path, os.path.join(dest_b, entry.name))

    _, listdir_med = _ns(listdir_move, rounds=8)
    _, scandir_med = _ns(scandir_move, rounds=8)
    shutil.rmtree(tmp)
    keep, _ = _report("scandir-move vs listdir-move", listdir_med, scandir_med)
    return {"scandir_keep": keep}


def bench_tar_bufsize(payload):
    print("\n== tarfile extract bufsize 10KiB vs 256KiB ==")
    tmp = tempfile.mkdtemp()
    archive = os.path.join(tmp, "a.tar.gz")
    srcdir = os.path.join(tmp, "src")
    os.makedirs(srcdir)
    # several medium files so extract is not dominated by gzip of one blob
    chunk = open(payload, "rb").read(1024 * 1024)
    for i in range(16):
        with open(os.path.join(srcdir, "f{}".format(i)), "wb") as f:
            f.write(chunk)
    with tarfile.open(archive, "w:gz") as tar:
        tar.add(srcdir, arcname="src")

    def extract(bufsize):
        dest = os.path.join(tmp, "out-{}".format(bufsize))
        if os.path.exists(dest):
            shutil.rmtree(dest)
        os.makedirs(dest)
        with tarfile.open(archive, "r:gz", bufsize=bufsize) as tar:
            tar.extractall(dest)

    _, small = _ns(lambda: extract(10240), rounds=3, warmup=1)
    _, large = _ns(lambda: extract(256 * 1024), rounds=3, warmup=1)
    shutil.rmtree(tmp)
    keep, _ = _report("tar bufsize 256KiB vs 10KiB", small, large)
    return {"tar_keep": keep, "small": small, "large": large}


def bench_http_stream(payload):
    print("\n== HTTP stream chunk size against local server ==")
    handler_dir = os.path.dirname(payload)
    name = os.path.basename(payload)

    class Handler(SimpleHTTPRequestHandler):
        def __init__(self, *args, **kwargs):
            super().__init__(*args, directory=handler_dir, **kwargs)

        def log_message(self, fmt, *args):
            return

    server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    thread = Thread(target=server.serve_forever, daemon=True)
    thread.start()
    url = "http://127.0.0.1:{}/{}".format(server.server_address[1], name)
    dest_dir = tempfile.mkdtemp()
    results = {}
    try:
        for size in (8 * 1024, 64 * 1024, 256 * 1024):
            dest = os.path.join(dest_dir, "out-{}".format(size))

            def run(buf=size, out=dest):
                with requests.get(url, stream=True) as r:
                    r.raise_for_status()
                    h = hashlib.sha256()
                    with open(out, "wb") as dst:
                        for data in r.raw.stream(amt=buf, decode_content=False):
                            dst.write(data)
                            h.update(data)
                    h.hexdigest()

            _, med = _ns(run, rounds=3, warmup=1)
            results[size] = med
            print("  {:>8}  median={:.4f}s".format(size, med))
    finally:
        server.shutdown()
        shutil.rmtree(dest_dir)
    best = min(results, key=results.get)
    keep, _ = _report("http-stream larger chunk vs 8KiB", results[8 * 1024], results[best],
                      extra="best={}".format(best))
    return {"http_best": best, "http_keep": keep, "http_results": results}


def bench_apt_cache():
    print("\n== apt.Cache() construction vs empty-bucket short-circuit ==")
    try:
        import apt
    except ImportError:
        print("  apt unavailable")
        return {"apt_keep": False}

    def open_cache():
        apt.Cache()

    def empty_short_circuit():
        bucket = []
        if not bucket:
            return True
        apt.Cache()
        return True

    _, cache_med = _ns(open_cache, rounds=3, warmup=1)
    _, empty_med = _ns(empty_short_circuit, rounds=30, warmup=1)
    keep, _ = _report("empty-bucket skip vs apt.Cache()", cache_med, empty_med)
    return {"apt_keep": keep, "cache": cache_med, "empty": empty_med}


def bench_download_center_integration(payload):
    print("\n== DownloadCenter integration (hash-while-stream, {} MiB) ==".format(PAYLOAD_MB))
    sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), "..")))
    from umake.network.download_center import DownloadCenter, DownloadItem
    from umake.tools import Checksum, ChecksumType

    handler_dir = os.path.dirname(payload)
    name = os.path.basename(payload)
    expected = hashlib.sha256()
    with open(payload, "rb") as f:
        expected.update(f.read())
    digest = expected.hexdigest()

    class Handler(SimpleHTTPRequestHandler):
        def __init__(self, *args, **kwargs):
            super().__init__(*args, directory=handler_dir, **kwargs)

        def log_message(self, fmt, *args):
            return

    server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    thread = Thread(target=server.serve_forever, daemon=True)
    thread.start()
    url = "http://127.0.0.1:{}/{}".format(server.server_address[1], name)

    def run():
        done = []

        def on_done(result):
            done.append(result)

        item = DownloadItem(url, Checksum(ChecksumType.sha256, digest))
        DownloadCenter([item], on_done)
        timeout = time.perf_counter() + 30
        while not done:
            if time.perf_counter() > timeout:
                raise RuntimeError("DownloadCenter timed out")
            time.sleep(0.01)
        result = done[0][url]
        if result.error:
            raise RuntimeError(result.error)
        result.fd.close()

    _, med = _ns(run, rounds=3, warmup=1)
    server.shutdown()
    print("  DownloadCenter+sha256 median={:.4f}s  BLOCK_SIZE={}".format(
        med, DownloadCenter.BLOCK_SIZE))
    return {"download_center_s": med}
    print("Python", sys.version.replace("\n", " "))
    tmp = tempfile.mkdtemp()
    payload = os.path.join(tmp, "payload.bin")
    print("Preparing {} MiB payload...".format(PAYLOAD_MB))
    with open(payload, "wb") as f:
        block = os.urandom(1024 * 1024)
        for _ in range(PAYLOAD_MB):
            f.write(block)

    decisions = {}
    decisions.update(bench_copy_buffers(payload))
    decisions.update(bench_hash_fusion(payload))
    decisions.update(bench_yaml())
    decisions.update(bench_strip_tags())
    decisions.update(bench_logging())
    decisions.update(bench_string_join())
    decisions.update(bench_listdir_vs_scandir(tmp))
    decisions.update(bench_tar_bufsize(payload))
    decisions.update(bench_http_stream(payload))
    decisions.update(bench_apt_cache())
    decisions.update(bench_download_center_integration(payload))

    shutil.rmtree(tmp)
    print("\n== KEEP/DISCARD summary ==")
    for key, value in decisions.items():
        if key.endswith("_keep") or key.endswith("_best") or key.endswith("_best_size"):
            print("  {}: {}".format(key, value))
    return 0


if __name__ == "__main__":
    sys.exit(main())
