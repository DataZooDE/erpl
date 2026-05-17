"""Verify that sap_read_table(THREADS=N) does not leak the thread setting to
the rest of the DuckDB session.

Pre-fix: sap_rfc.cpp:676 calls scheduler.SetThreads(max_threads, 1), which
mutates the global TaskScheduler's requested_thread_count. RelaunchThreads()
applies it at the next CleanupInternal — so any subsequent query (or PRAGMA
threads) is affected.

Post-fix: max_threads is enforced by batching tasks in Step(), not by mutating
the scheduler. PRAGMA threads should be unchanged before/after.
"""
from __future__ import annotations


def test_pragma_threads_preserved(cli_run):
    """Run a sap_read_table with THREADS=2, then check that the configured
    thread setting is unchanged.

    `PRAGMA threads` is not a query-style pragma in current DuckDB; use
    `current_setting('threads')` instead.
    """
    sql = (
        "SELECT current_setting('threads') AS t;"
        "SELECT COUNT(*) FROM sap_read_table('/DMO/FLIGHT', THREADS=2);"
        "SELECT current_setting('threads') AS t;"
    )
    out = cli_run(sql)
    lines = [l.strip().strip('"') for l in out.strip().split("\n") if l.strip()]
    # CSV output groups stmt results: ["t","32","count_star()","40","t","32"]
    t_vals: list[str] = []
    for i, l in enumerate(lines):
        if l == "t" and i + 1 < len(lines):
            t_vals.append(lines[i + 1])
    assert len(t_vals) == 2, f"expected 2 thread readings, got: {t_vals} from {lines}"
    assert t_vals[0] == t_vals[1], (
        f"thread setting changed across sap_read_table: before={t_vals[0]} after={t_vals[1]}"
    )
