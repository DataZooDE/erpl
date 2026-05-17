# sap_read_table Parallelism Harness

Verifies the column-parallel scan path of `sap_read_table` against a running ABAP
Cloud Developer Trial container.

## Prerequisites

- `a4h` container running (`scripts/start-sap.sh`).
- Debug build present: `GEN=ninja make debug`.
- `.adt.creds` JSON at the repo root.
- Python tooling: `uv` (the project standard).

## Run

```bash
cd /home/jr/Projects/datazoo/erpl
uv run pytest rfc/test/parallel -v
```

Targeted runs:

```bash
uv run pytest rfc/test/parallel/test_alignment.py::test_b8_sapds_get_sorted -v
uv run pytest rfc/test/parallel/test_perf_ddic.py -v --capture=no
```

## What lives where

- `conftest.py` — credential loading, DuckDB CLI wrapper, fixture wiring.
- `oracles.py` — single-shot, full-row, single-transaction RFC oracle builder.
- `test_alignment.py` — scenarios B1–B8 (row-order alignment under parallelism).
- `test_breadth.py` — broad coverage across tables, views, CDS views.
- `test_perf_ddic.py` — DDIC-table throughput sweep across THREADS values.
- `test_connection_pressure.py` — concurrent multiprocess load.
- `test_global_scheduler.py` — proves/disproves the `SetThreads` session leak.
- `tsan_scenarios.sh` — TSAN re-run of every scenario.

Artifacts land under `../../../artifacts/` and oracles under `../../../build/oracle/`.
