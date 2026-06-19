#!/usr/bin/env python3
"""Run the real order-book engine and serialize everything the figures need.

No numbers are hand-written into the page. This script:
  1. builds the C++ targets (`make site`, `make tsan`, `make test`),
  2. runs `build/site_export` several times and keeps the run whose latency
     hot-path throughput is the median (throughput is noisy on a loaded laptop;
     the median run is the honest representative),
  3. runs the test suite and the ThreadSanitizer pipeline and records the result,
  4. stamps platform / compiler / git / date metadata,
  5. writes site/data.json.

Usage:  PYTHONPATH=src python3 site/export_data.py
"""

import json
import os
import platform
import re
import subprocess
import sys
from datetime import date, timezone, datetime
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SITE = ROOT / "site"
REPLAY = "data/sample_replay.csv"
N_RUNS = 3  # site_export runs to take a median over


def run(cmd, **kw):
    return subprocess.run(cmd, cwd=ROOT, text=True, capture_output=True, **kw)


def build():
    for target in ("site", "tsan"):
        r = run(["make", target])
        if r.returncode != 0:
            sys.stderr.write(r.stdout + r.stderr)
            raise SystemExit(f"make {target} failed")
    # Build (don't necessarily run) the unit tests so we can execute them cleanly.
    run(["make", "build/order_book_tests", "build/protocol_tests"])


def collect_export():
    """Run site_export N times; keep the median-throughput run for stability."""
    runs = []
    for _ in range(N_RUNS):
        r = run(["./build/site_export", REPLAY])
        if r.returncode != 0:
            sys.stderr.write(r.stderr)
            raise SystemExit("site_export failed")
        runs.append(json.loads(r.stdout))
    runs.sort(key=lambda d: d["latency"]["throughput_median"])
    chosen = runs[len(runs) // 2]
    # Carry the best observed median across all runs as the peak figure, but the
    # chosen run's full distribution stays internally consistent.
    chosen["latency"]["throughput_peak"] = max(
        d["latency"]["throughput_max"] for d in runs
    )
    chosen["replay"]["throughput_peak"] = max(d["replay"]["throughput"] for d in runs)
    return chosen


def collect_verification():
    out = {}
    # Unit / protocol / replay correctness.
    tests = []
    checks = [
        ("order_book_tests", "build/order_book_tests",
         "Matching, partial/full fills, price-time priority, modify, replace, depth, SPSC FIFO"),
        ("protocol_tests", "build/protocol_tests",
         "40-byte binary message encode / decode round-trip"),
    ]
    for label, binary, desc in checks:
        r = run([f"./{binary}"])
        tests.append({
            "name": label,
            "desc": desc,
            "pass": r.returncode == 0,
            "detail": (r.stdout.strip().splitlines() or [""])[-1],
        })
    # Golden-file deterministic replay.
    r = run(["./build/replay_runner", REPLAY])
    expected = (ROOT / "tests/replay_expected.txt").read_text()
    tests.append({
        "name": "replay_golden",
        "desc": "Deterministic replay matches the committed golden output byte-for-byte",
        "pass": r.returncode == 0 and r.stdout == expected,
        "detail": "golden match" if r.stdout == expected else "MISMATCH",
    })
    out["tests"] = tests

    # ThreadSanitizer over the producer -> SPSC -> consumer path.
    r = run(["./build/tsan_pipeline", "10000"])
    race = ("WARNING: ThreadSanitizer" in (r.stdout + r.stderr))
    proc = re.search(r"processed=(\d+)", r.stdout)
    out["tsan"] = {
        "events": int(proc.group(1)) if proc else 0,
        "races": 0 if not race else 1,
        "clean": (not race) and r.returncode == 0,
    }
    return out


def meta():
    cxxv = run(["c++", "--version"]).stdout.splitlines()[0].strip()
    try:
        sha = run(["git", "rev-parse", "--short", "HEAD"]).stdout.strip()
    except Exception:
        sha = "unknown"
    cpu = platform.processor() or platform.machine()
    if sys.platform == "darwin":
        b = run(["sysctl", "-n", "machdep.cpu.brand_string"]).stdout.strip()
        if b:
            cpu = b
    return {
        "date": date.today().isoformat(),
        "generated_utc": datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%MZ"),
        "platform": f"{platform.system()} {platform.machine()}",
        "compiler": cxxv,
        "cpu": cpu,
        "git": sha,
        "standard": "C++17",
    }


def main():
    build()
    data = collect_export()
    data.update(collect_verification())
    data["meta"] = meta()
    # Derive a bounded-memory figure (MB) for the latency-bench book config.
    data["structure"]["book_mb"] = round(data["structure"]["book_bytes"] / (1 << 20), 1)
    data["structure"]["spsc_mb"] = round(data["structure"]["spsc_bytes"] / (1 << 20), 1)

    out = SITE / "data.json"
    out.write_text(json.dumps(data, indent=2))
    lat = data["latency"]
    rep = data["replay"]
    print(f"wrote {out}")
    print(f"  latency  p50={lat['p50']} mean={lat['mean']:.0f} ticks  "
          f"median throughput={lat['throughput_median']/1e6:.1f}M ev/s")
    print(f"  replay   throughput={rep['throughput']/1e6:.1f}M ev/s  p99={rep['p99']} ticks")
    print(f"  tsan     clean={data['tsan']['clean']}  "
          f"tests pass={sum(t['pass'] for t in data['tests'])}/{len(data['tests'])}")


if __name__ == "__main__":
    main()
