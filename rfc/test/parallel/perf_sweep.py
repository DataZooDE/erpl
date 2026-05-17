"""DDIC perf sweep — record wall-clock for various THREADS on DD02L and T100.

T100 has 615k rows, DD02L has 164k. TADIR (1.3M) is skipped to keep the run
under 30 min. Writes CSV to artifacts/perf/ddic.csv.

Reads only a single-column subset to keep per-row cost low (the test exercises
SAP-side throughput, not DuckDB conversion).
"""
from __future__ import annotations

import csv
import json
import os
import pathlib
import subprocess
import sys
import time

REPO_ROOT = pathlib.Path(__file__).resolve().parents[3]
DUCKDB = REPO_ROOT / "build" / "debug" / "duckdb"
ADT_CREDS = REPO_ROOT / ".adt.creds"
ARTIFACTS = REPO_ROOT / "artifacts" / "perf"

TABLES = [
    ("DD02L", 164656, ["TABNAME", "TABCLASS"]),    # 2-col scan
    ("T100",  615666, ["SPRSL", "ARBGB", "MSGNR"]),  # 3-col scan
]
THREAD_VALUES = [1, 2, 4, 8]


def secret() -> str:
    c = json.loads(ADT_CREDS.read_text())
    return f"""CREATE OR REPLACE SECRET abap_trial (
    TYPE sap_rfc, ASHOST '{c.get("host", "localhost")}',
    SYSNR '00', CLIENT '{c.get("client", "001")}',
    USER '{c.get("user", "DEVELOPER")}', PASSWD '{c.get("password", "")}', LANG 'EN'
);
"""


def run_timed(sql: str, *, timeout: int = 600) -> float:
    env = {**os.environ}
    env["LD_LIBRARY_PATH"] = f"{REPO_ROOT / 'nwrfcsdk' / 'linux' / 'lib'}:{env.get('LD_LIBRARY_PATH', '')}"
    env["LSAN_OPTIONS"] = f"suppressions={REPO_ROOT / 'scripts' / 'lsan_suppress.txt'}:print_suppressions=0"
    env["ASAN_OPTIONS"] = "detect_odr_violation=0:detect_leaks=0"
    t0 = time.perf_counter()
    proc = subprocess.run(
        [str(DUCKDB), "-csv", "-c", secret() + sql],
        capture_output=True, text=True, timeout=timeout, env=env,
    )
    if proc.returncode != 0:
        raise RuntimeError(f"rc={proc.returncode}\nSTDERR: {proc.stderr[:500]}\nSTDOUT: {proc.stdout[:300]}")
    return time.perf_counter() - t0


def main() -> int:
    ARTIFACTS.mkdir(parents=True, exist_ok=True)
    csv_path = ARTIFACTS / "ddic.csv"
    rows: list[list] = []
    with csv_path.open("w", newline="") as fp:
        writer = csv.writer(fp)
        writer.writerow(["table", "row_count", "columns", "threads", "wall_seconds", "rows_per_sec"])
        for table, row_count, columns in TABLES:
            cols_list = "[" + ",".join(f"'{c}'" for c in columns) + "]"
            for threads in THREAD_VALUES:
                sql = (
                    f"SELECT COUNT(*) FROM sap_read_table("
                    f"'{table}', COLUMNS={cols_list}, THREADS={threads}"
                    f");"
                )
                try:
                    wall = run_timed(sql, timeout=900)
                except Exception as e:
                    print(f"  {table} threads={threads}: FAILED {str(e)[:150]}")
                    writer.writerow([table, row_count, len(columns), threads, "ERROR", str(e)[:80]])
                    continue
                rps = row_count / wall if wall > 0 else 0
                writer.writerow([table, row_count, len(columns), threads, f"{wall:.2f}", f"{rps:.0f}"])
                rows.append([table, threads, wall, rps])
                print(f"  {table:8s} rows={row_count:7d} cols={len(columns)} threads={threads:2d} "
                      f"wall={wall:6.2f}s rps={rps:7.0f}")

    print(f"\nCSV: {csv_path}")
    # Quick speedup summary per table
    print("\n=== Speedup vs THREADS=1 ===")
    by_table: dict[str, list[tuple[int, float]]] = {}
    for table, threads, wall, _rps in rows:
        by_table.setdefault(table, []).append((threads, wall))
    for table, runs in by_table.items():
        runs.sort()
        if not runs:
            continue
        baseline = next((w for t, w in runs if t == 1), None)
        if not baseline:
            continue
        print(f"  {table}:")
        for t, w in runs:
            speedup = baseline / w if w > 0 else 0.0
            print(f"    THREADS={t}: {w:6.2f}s  speedup={speedup:.2f}x")

    return 0


if __name__ == "__main__":
    sys.exit(main())
