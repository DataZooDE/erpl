"""Performance characterization on DDIC tables.

This is not a pass/fail test — it records timings for THREADS in {1,2,4,8,16}
on the largest DDIC tables that the trial container has, writing CSV to
artifacts/perf/ddic.csv.
"""
from __future__ import annotations

import csv
import pathlib
import time

import pytest


DDIC_CANDIDATES = ["T002", "T100", "DD02L", "DD03L", "DD04L", "TADIR", "E071"]
THREAD_VALUES = [1, 2, 4, 8, 16]


@pytest.fixture(scope="module")
def ddic_targets(cli_run):
    """Probe row counts and pick the largest 3 tables that fit a 120s budget."""
    candidates: list[tuple[str, int]] = []
    for t in DDIC_CANDIDATES:
        try:
            out = cli_run(
                f"SELECT COUNT(*)::BIGINT FROM sap_read_table('{t}', THREADS=1);",
                timeout=120,
            )
            n = int(out.strip().split("\n")[-1].strip('"'))
            candidates.append((t, n))
        except Exception:
            continue
    # Largest first
    candidates.sort(key=lambda x: -x[1])
    return candidates[:3]


def test_perf_sweep(cli_run, artifacts_dir: pathlib.Path, ddic_targets):
    if not ddic_targets:
        pytest.skip("no DDIC tables readable")
    perf_dir = artifacts_dir / "perf"
    perf_dir.mkdir(parents=True, exist_ok=True)
    csv_path = perf_dir / "ddic.csv"

    with csv_path.open("w", newline="") as fp:
        writer = csv.writer(fp)
        writer.writerow(["table", "row_count", "threads", "wall_seconds", "rows_per_sec"])
        for table, rows in ddic_targets:
            for threads in THREAD_VALUES:
                wall = _measure_one(cli_run, table, threads)
                rps = (rows / wall) if wall > 0 else 0.0
                writer.writerow([table, rows, threads, f"{wall:.2f}", f"{rps:.0f}"])
                print(f"  {table:10s} rows={rows:9d} threads={threads:2d} wall={wall:.2f}s rps={rps:.0f}")


def _measure_one(cli_run, table: str, threads: int) -> float:
    sql = f"SELECT COUNT(*) FROM sap_read_table('{table}', THREADS={threads});"
    t0 = time.perf_counter()
    cli_run(sql, timeout=1800)
    return time.perf_counter() - t0
