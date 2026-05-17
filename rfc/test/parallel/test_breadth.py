"""Broad coverage across SAP object kinds: transparent tables, DDIC views,
CDS views. Verifies that parallel scans work and produce stable hashes for
each kind.
"""
from __future__ import annotations

import pytest


OBJECTS = [
    # (kind, name, threads-1 row count is expected to be small enough to run)
    ("table", "/DMO/FLIGHT"),
    ("table", "SFLIGHT"),
    ("table", "SPFLI"),
    ("table", "/DMO/TRAVEL"),
    ("table", "/DMO/AGENCY"),
    ("table", "T002"),
    ("cds",   "/DMO/R_Booking_D"),
    ("cds",   "/DMO/I_Carrier"),
]


@pytest.mark.parametrize("kind,name", OBJECTS, ids=[f"{k}:{n}" for k, n in OBJECTS])
def test_parallel_scan_works_and_is_stable(cli_run, kind, name):
    sql = f"""
    SELECT md5(string_agg(row_to_json(t)::VARCHAR, '' ORDER BY row_to_json(t)::VARCHAR))
    FROM (SELECT * FROM sap_read_table('{name}', THREADS=4)) AS t;
    """
    hashes = []
    for _ in range(3):
        out = cli_run(sql, timeout=300).strip().split("\n")[-1].strip('"')
        hashes.append(out)
    assert len(set(hashes)) == 1, f"{kind} {name}: hash drift across 3 runs: {hashes}"
