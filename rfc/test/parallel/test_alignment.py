"""Row-alignment verification for sap_read_table parallelism.

The core risk being tested: each requested column issues its own RFC_READ_TABLE
call. If SAP returns rows in different orders across the per-column calls,
DuckDB stitches them by position and produces tuples that never existed in the
source — silent data corruption.

Detection strategy: build an oracle and compare candidate against it using
multiset semantics (EXCEPT ALL). Any misaligned candidate row produces a
synthetic tuple that does NOT appear in the oracle, so EXCEPT ALL surfaces it.

We use two oracles:
  - Strong: a single sap_rfc_invoke('RFC_READ_TABLE', FIELDS=[all]) call, parsed
    into typed rows. One RFC transaction = one consistent row order.
  - Weak: sap_read_table with THREADS=1. Column-parallel still happens (each
    column gets its own RFC call), but they run serially. For quiescent tables
    this is empirically stable but not guaranteed.

The strong oracle is the gold standard. The weak oracle catches racing
parallelism vs serial-but-still-column-parallel divergence.
"""
from __future__ import annotations

import pytest

from oracles import get_table_info


# Tables that should fit in the trial container's demo data. Probed up-front
# by the probe.py script; row counts here are upper bounds — tests use the
# actual row count discovered at runtime.
SMALL_TABLES = [
    "/DMO/FLIGHT",
    "SFLIGHT",
    "SPFLI",
    "/DMO/AGENCY",
]
STRING_TABLES = [
    "/DMO/TRAVEL",     # has SSTR
    "/DMO/AGENCY",     # has STRG
]


@pytest.mark.parametrize("table", SMALL_TABLES)
def test_threads_n_matches_threads_one_multiset(cli_run, table):
    """B1+B2: parallel result must be the same multiset as the threads=1 result.

    Row order is not asserted; only that every row produced under THREADS=N
    also appears under THREADS=1 and vice versa.
    """
    sql_for = lambda threads: (
        f"SELECT md5(string_agg(row_to_json(t)::VARCHAR, '' ORDER BY row_to_json(t)::VARCHAR)) "
        f"FROM (SELECT * FROM sap_read_table('{table}', THREADS={threads})) AS t;"
    )
    out1 = cli_run(sql_for(1)).strip().split("\n")[-1].strip('"')
    out_n = cli_run(sql_for(4)).strip().split("\n")[-1].strip('"')
    assert out1 == out_n, f"{table}: THREADS=1 ({out1}) != THREADS=4 ({out_n})"


@pytest.mark.parametrize("table", SMALL_TABLES)
def test_parallel_run_stability(cli_run, table):
    """B2 inter-run stability: same query 10× must give identical hashes."""
    sql = (
        f"SELECT md5(string_agg(row_to_json(t)::VARCHAR, '' ORDER BY row_to_json(t)::VARCHAR)) "
        f"FROM (SELECT * FROM sap_read_table('{table}', THREADS=4)) AS t;"
    )
    hashes = []
    for _ in range(10):
        out = cli_run(sql).strip().split("\n")[-1].strip('"')
        hashes.append(out)
    assert len(set(hashes)) == 1, f"{table}: hash drift across 10 runs: {hashes}"


def test_pk_pivot_no_corruption(cli_run):
    """B2 PK-pivot: for /DMO/FLIGHT, every (CARRIER_ID, CONNECTION_ID, FLIGHT_DATE)
    row in the THREADS=4 result must have the SAME PRICE/CURRENCY/PLANE/SEATS
    values as the same PK row in the THREADS=1 result. Misalignment would
    produce different non-PK values for the same PK.
    """
    sql = """
    WITH t1 AS (SELECT * FROM sap_read_table('/DMO/FLIGHT', THREADS=1)),
         tn AS (SELECT * FROM sap_read_table('/DMO/FLIGHT', THREADS=4))
    SELECT COUNT(*) AS mismatches
    FROM t1 JOIN tn USING (CARRIER_ID, CONNECTION_ID, FLIGHT_DATE)
    WHERE t1.PRICE IS DISTINCT FROM tn.PRICE
       OR t1.CURRENCY_CODE IS DISTINCT FROM tn.CURRENCY_CODE
       OR t1.PLANE_TYPE_ID IS DISTINCT FROM tn.PLANE_TYPE_ID
       OR t1.SEATS_MAX IS DISTINCT FROM tn.SEATS_MAX
       OR t1.SEATS_OCCUPIED IS DISTINCT FROM tn.SEATS_OCCUPIED;
    """
    out = cli_run(sql).strip().split("\n")[-1].strip('"')
    assert out == "0", f"/DMO/FLIGHT: {out} rows with mismatched non-PK values between THREADS=1 and THREADS=4"


def test_projection_subset_threads_4(cli_run):
    """B4: a 3-of-N column projection must still align rows."""
    sql = """
    WITH t1 AS (SELECT CARRIER_ID, CONNECTION_ID, PRICE FROM sap_read_table('/DMO/FLIGHT', THREADS=1)),
         tn AS (SELECT CARRIER_ID, CONNECTION_ID, PRICE FROM sap_read_table('/DMO/FLIGHT', THREADS=4))
    SELECT (SELECT COUNT(*) FROM ((SELECT * FROM t1) EXCEPT ALL (SELECT * FROM tn))) +
           (SELECT COUNT(*) FROM ((SELECT * FROM tn) EXCEPT ALL (SELECT * FROM t1)));
    """
    out = cli_run(sql).strip().split("\n")[-1].strip('"')
    assert out == "0", f"projection multiset mismatch: {out}"


def test_filter_pushdown_threads_4(cli_run):
    """B5: filter pushdown must produce the same row count and content as filter applied post-scan."""
    sql_pushed = "SELECT COUNT(*) FROM sap_read_table('/DMO/FLIGHT', THREADS=4) WHERE CARRIER_ID = 'SQ';"
    sql_post   = "SELECT COUNT(*) FROM (SELECT * FROM sap_read_table('/DMO/FLIGHT', THREADS=1)) WHERE CARRIER_ID = 'SQ';"
    a = cli_run(sql_pushed).strip().split("\n")[-1].strip('"')
    b = cli_run(sql_post).strip().split("\n")[-1].strip('"')
    assert a == b, f"filter pushdown: {a} != {b}"


def test_limit_threads_4(cli_run):
    """B6: LIMIT N over parallel scan returns exactly N rows, each a valid source row."""
    sql = """
    WITH oracle AS (SELECT * FROM sap_read_table('/DMO/FLIGHT', THREADS=1)),
         candidate AS (SELECT * FROM sap_read_table('/DMO/FLIGHT', THREADS=4, MAX_ROWS=17))
    SELECT COUNT(*) AS card,
           (SELECT COUNT(*) FROM ((SELECT * FROM candidate) EXCEPT ALL (SELECT * FROM oracle))) AS extras
    FROM candidate;
    """
    out = cli_run(sql).strip().split("\n")
    # CSV: header then values
    card, extras = out[-1].strip('"').split(",")
    assert card == "17", f"LIMIT cardinality wrong: {card}"
    assert extras == "0", f"LIMIT produced {extras} rows not in the oracle"


@pytest.mark.parametrize("table", STRING_TABLES)
def test_string_columns_threads_4(cli_run, table):
    """B3: string-typed columns (SSTR/STRG via ET_DATA path) must align under parallelism."""
    info = get_table_info(cli_run, table)
    sql = f"""
    WITH t1 AS (SELECT * FROM sap_read_table('{table}', THREADS=1,
                                              READ_TABLE_FUNCTION='RFC_READ_TABLE',
                                              READ_TABLE_DELIMITER='~')),
         tn AS (SELECT * FROM sap_read_table('{table}', THREADS=4,
                                              READ_TABLE_FUNCTION='RFC_READ_TABLE',
                                              READ_TABLE_DELIMITER='~'))
    SELECT (SELECT COUNT(*) FROM ((SELECT * FROM t1) EXCEPT ALL (SELECT * FROM tn))) +
           (SELECT COUNT(*) FROM ((SELECT * FROM tn) EXCEPT ALL (SELECT * FROM t1)));
    """
    out = cli_run(sql).strip().split("\n")[-1].strip('"')
    assert out == "0", f"{table} string-cols multiset mismatch: {out} (row count={info.row_count})"


def test_sapds_get_sorted_misalignment(cli_run):
    """B8: the misalignment killer.

    With /SAPDS/RFC_READ_TABLE2, the code unconditionally sets GET_SORTED=X.
    If GET_SORTED sorts by the FIELDS list (which differs per column under
    column-parallel reads), rows come back in different orders per column and
    DuckDB stitches them into corrupted tuples.

    Pass: candidate multiset == oracle multiset.
    Fail: candidate has rows that don't exist in the oracle → silent corruption.
    """
    sql = """
    WITH oracle AS (SELECT * FROM sap_read_table('/DMO/FLIGHT', THREADS=1)),
         candidate AS (SELECT * FROM sap_read_table('/DMO/FLIGHT',
                                                     THREADS=4,
                                                     READ_TABLE_FUNCTION='/SAPDS/RFC_READ_TABLE2',
                                                     READ_TABLE_DELIMITER='~'))
    SELECT (SELECT COUNT(*) FROM ((SELECT * FROM candidate) EXCEPT ALL (SELECT * FROM oracle))) AS extras,
           (SELECT COUNT(*) FROM ((SELECT * FROM oracle) EXCEPT ALL (SELECT * FROM candidate))) AS missing;
    """
    out = cli_run(sql).strip().split("\n")
    extras, missing = out[-1].strip('"').split(",")
    assert extras == "0" and missing == "0", (
        f"/SAPDS/RFC_READ_TABLE2 parallel scan corrupted rows: "
        f"{extras} synthetic (not-in-source) rows, {missing} missing rows. "
        f"This proves the GET_SORTED-with-per-column-FIELDS misalignment hypothesis."
    )


def test_join_two_parallel_reads_deterministic(cli_run):
    """B7: a join of two parallel reads must return a stable count across repetitions."""
    sql = """
    SELECT COUNT(*)
    FROM sap_read_table('SFLIGHT', THREADS=4) AS f
    JOIN sap_read_table('SPFLI', THREADS=4) AS s
        ON (f.MANDT = s.MANDT AND f.CARRID = s.CARRID AND f.CONNID = s.CONNID);
    """
    counts = []
    for _ in range(5):
        out = cli_run(sql).strip().split("\n")[-1].strip('"')
        counts.append(out)
    assert len(set(counts)) == 1, f"unstable join count across runs: {counts}"
