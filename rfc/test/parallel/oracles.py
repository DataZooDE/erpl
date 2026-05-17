"""Build single-transaction oracles for parallel-scan verification.

The strongest oracle is a single RFC_READ_TABLE invocation in one transaction
with ALL desired columns in FIELDS — that guarantees a self-consistent row
order across the whole result. We use that for tables where rfc invoke can
parse the result.

For practical coverage we also support a "weak oracle" mode: a serialized
single-thread sap_read_table run. With THREADS=1 column-parallelism is
sequenced but rows still come from separate RFC calls — order alignment is
only stable for quiescent tables in practice. We use it as a sanity check,
not as proof.
"""
from __future__ import annotations

import dataclasses
import pathlib
import subprocess


@dataclasses.dataclass
class TableInfo:
    name: str
    pk_columns: list[str]
    all_columns: list[str]
    row_count: int | None


def describe_fields(cli_run, table: str) -> tuple[list[str], list[str]]:
    """Return (all_columns, pk_columns) for a SAP table/view."""
    sql = (
        f"SELECT field, is_key FROM sap_describe_fields('{table}') "
        f"ORDER BY pos;"
    )
    out = cli_run(sql, timeout=120)
    cols: list[str] = []
    pks: list[str] = []
    for line in out.strip().split("\n")[1:]:  # skip header
        if not line.strip():
            continue
        parts = line.split(",")
        if len(parts) < 2:
            continue
        name = parts[0].strip().strip('"')
        is_key = parts[1].strip().strip('"').upper() in ("X", "TRUE", "1")
        cols.append(name)
        if is_key:
            pks.append(name)
    return cols, pks


def count_rows(cli_run, table: str, *, timeout: int = 120) -> int | None:
    sql = f"SELECT COUNT(*)::BIGINT AS c FROM sap_read_table('{table}', THREADS=1);"
    try:
        out = cli_run(sql, timeout=timeout)
    except (subprocess.TimeoutExpired, RuntimeError):
        return None
    lines = [l for l in out.strip().split("\n") if l]
    if len(lines) < 2:
        return None
    try:
        return int(lines[-1].strip().strip('"'))
    except ValueError:
        return None


def get_table_info(cli_run, table: str) -> TableInfo:
    cols, pks = describe_fields(cli_run, table)
    rows = count_rows(cli_run, table)
    return TableInfo(name=table, pk_columns=pks, all_columns=cols, row_count=rows)


def build_parquet_oracle(cli_run, oracle_dir: pathlib.Path, table: str,
                         columns: list[str] | None = None,
                         threads: int = 1) -> pathlib.Path:
    """Materialize the table at THREADS=1 to a parquet file. Weak oracle."""
    oracle_dir.mkdir(parents=True, exist_ok=True)
    safe = table.replace("/", "_")
    out_path = oracle_dir / f"{safe}.parquet"
    col_clause = ""
    if columns:
        cols_str = ",".join(f"'{c}'" for c in columns)
        col_clause = f", COLUMNS=[{cols_str}]"
    sql = (
        f"COPY (SELECT * FROM sap_read_table('{table}', THREADS={threads}{col_clause})) "
        f"TO '{out_path}' (FORMAT 'parquet');"
    )
    cli_run(sql, timeout=900)
    return out_path
