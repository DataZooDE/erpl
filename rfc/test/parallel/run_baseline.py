"""Focused baseline runner — exercises the critical scenarios in one shot
and produces a single report. Useful for quick pre/post-fix comparison.

Run:
    uv run python rfc/test/parallel/run_baseline.py
"""
from __future__ import annotations

import json
import os
import pathlib
import subprocess
import sys
import time

REPO_ROOT = pathlib.Path(__file__).resolve().parents[3]
DUCKDB = REPO_ROOT / "build" / "debug" / "duckdb"
ADT_CREDS = REPO_ROOT / ".adt.creds"
ARTIFACTS = REPO_ROOT / "artifacts" / "baseline"


def secret() -> str:
    c = json.loads(ADT_CREDS.read_text())
    return f"""CREATE OR REPLACE SECRET abap_trial (
    TYPE sap_rfc,
    ASHOST '{c.get("host", "localhost")}',
    SYSNR '00',
    CLIENT '{c.get("client", "001")}',
    USER '{c.get("user", "DEVELOPER")}',
    PASSWD '{c.get("password", "")}',
    LANG 'EN'
);
"""


def run(sql: str, *, timeout: int = 300, retries: int = 2) -> str:
    env = {**os.environ}
    env["LD_LIBRARY_PATH"] = f"{REPO_ROOT / 'nwrfcsdk' / 'linux' / 'lib'}:{env.get('LD_LIBRARY_PATH', '')}"
    env["LSAN_OPTIONS"] = f"suppressions={REPO_ROOT / 'scripts' / 'lsan_suppress.txt'}:print_suppressions=0"
    env["ASAN_OPTIONS"] = "detect_odr_violation=0:detect_leaks=0"
    last = ""
    for attempt in range(retries + 1):
        try:
            proc = subprocess.run(
                [str(DUCKDB), "-csv", "-c", secret() + sql],
                capture_output=True, text=True, timeout=timeout, env=env,
            )
        except subprocess.TimeoutExpired:
            last = f"timeout after {timeout}s (attempt {attempt + 1})"
            continue
        if proc.returncode == 0:
            return proc.stdout
        last = f"rc={proc.returncode}\nSTDERR: {proc.stderr[:500]}\nSTDOUT: {proc.stdout[:500]}"
        if "RFC_COMMUNICATION_FAILURE" not in (proc.stderr or "") and "Timeout while opening an RFC connection" not in (proc.stderr or ""):
            break
    raise RuntimeError(f"failed after {retries + 1} attempts: {last}\nSQL: {sql[:300]}")


def last_value(out: str) -> str:
    lines = [l for l in out.strip().split("\n") if l]
    return lines[-1].strip().strip('"') if lines else ""


class Result:
    def __init__(self, name: str):
        self.name = name
        self.passed: bool | None = None
        self.detail: str = ""
        self.duration: float = 0.0

    def record(self, ok: bool, detail: str = ""):
        self.passed = ok
        self.detail = detail

    def __str__(self) -> str:
        status = "PASS" if self.passed else ("FAIL" if self.passed is False else "SKIP")
        return f"[{status}] {self.name} ({self.duration:.1f}s) — {self.detail}"


def scenario_b1_multiset(name: str = "B1: THREADS=1 vs THREADS=4 multiset") -> Result:
    r = Result(name); t0 = time.perf_counter()
    sql = (
        "SELECT md5(string_agg(j, '' ORDER BY j)) FROM ("
        "  SELECT row_to_json(t)::VARCHAR AS j FROM ("
        "    SELECT * FROM sap_read_table('/DMO/FLIGHT', THREADS={threads})"
        "  ) AS t"
        ") sub;"
    )
    h1 = last_value(run(sql.replace("{threads}", "1")))
    h4 = last_value(run(sql.replace("{threads}", "4")))
    r.record(h1 == h4, f"h1={h1} h4={h4}")
    r.duration = time.perf_counter() - t0
    return r


def scenario_b2_stability(name: str = "B2: 10-run hash stability THREADS=4") -> Result:
    r = Result(name); t0 = time.perf_counter()
    sql = (
        "SELECT md5(string_agg(j, '' ORDER BY j)) FROM ("
        "  SELECT row_to_json(t)::VARCHAR AS j FROM ("
        "    SELECT * FROM sap_read_table('/DMO/FLIGHT', THREADS=4)"
        "  ) AS t"
        ") sub;"
    )
    hashes = [last_value(run(sql)) for _ in range(10)]
    r.record(len(set(hashes)) == 1, f"unique={len(set(hashes))} hashes={hashes[:3]}…")
    r.duration = time.perf_counter() - t0
    return r


def scenario_b2_pk_pivot(name: str = "B2: PK-pivot /DMO/FLIGHT no corruption") -> Result:
    r = Result(name); t0 = time.perf_counter()
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
    v = last_value(run(sql))
    r.record(v == "0", f"mismatches={v}")
    r.duration = time.perf_counter() - t0
    return r


def scenario_b3_string_cols(name: str = "B3: /DMO/TRAVEL string cols (ET_DATA) THREADS=4 multiset") -> Result:
    r = Result(name); t0 = time.perf_counter()
    sql = """
    WITH t1 AS (SELECT * FROM sap_read_table('/DMO/TRAVEL', THREADS=1,
                                              READ_TABLE_FUNCTION='RFC_READ_TABLE',
                                              READ_TABLE_DELIMITER='~')),
         tn AS (SELECT * FROM sap_read_table('/DMO/TRAVEL', THREADS=4,
                                              READ_TABLE_FUNCTION='RFC_READ_TABLE',
                                              READ_TABLE_DELIMITER='~'))
    SELECT (SELECT COUNT(*) FROM ((SELECT * FROM t1) EXCEPT ALL (SELECT * FROM tn))) +
           (SELECT COUNT(*) FROM ((SELECT * FROM tn) EXCEPT ALL (SELECT * FROM t1)));
    """
    v = last_value(run(sql))
    r.record(v == "0", f"multiset diff={v}")
    r.duration = time.perf_counter() - t0
    return r


def scenario_b5_filter(name: str = "B5: filter pushdown vs post-filter") -> Result:
    r = Result(name); t0 = time.perf_counter()
    a = last_value(run("SELECT COUNT(*) FROM sap_read_table('/DMO/FLIGHT', THREADS=4) WHERE CARRIER_ID = 'SQ';"))
    b = last_value(run("SELECT COUNT(*) FROM (SELECT * FROM sap_read_table('/DMO/FLIGHT', THREADS=1)) WHERE CARRIER_ID = 'SQ';"))
    r.record(a == b, f"pushed={a} post={b}")
    r.duration = time.perf_counter() - t0
    return r


def scenario_b6_limit(name: str = "B6: LIMIT=17 THREADS=4 valid rows") -> Result:
    r = Result(name); t0 = time.perf_counter()
    sql = """
    WITH oracle AS (SELECT * FROM sap_read_table('/DMO/FLIGHT', THREADS=1)),
         candidate AS (SELECT * FROM sap_read_table('/DMO/FLIGHT', THREADS=4, MAX_ROWS=17))
    SELECT COUNT(*)::VARCHAR || '|' ||
           (SELECT COUNT(*) FROM ((SELECT * FROM candidate) EXCEPT ALL (SELECT * FROM oracle)))::VARCHAR
    FROM candidate;
    """
    v = last_value(run(sql))
    card, extras = v.split("|") if "|" in v else ("?", "?")
    r.record(card == "17" and extras == "0", f"card={card} extras-not-in-oracle={extras}")
    r.duration = time.perf_counter() - t0
    return r


def scenario_b8_sapds_misalign(name: str = "B8: /SAPDS/RFC_READ_TABLE2 misalignment killer") -> Result:
    r = Result(name); t0 = time.perf_counter()
    sql = """
    WITH oracle AS (SELECT * FROM sap_read_table('/DMO/FLIGHT', THREADS=1)),
         candidate AS (SELECT * FROM sap_read_table('/DMO/FLIGHT',
                                                     THREADS=4,
                                                     READ_TABLE_FUNCTION='/SAPDS/RFC_READ_TABLE2',
                                                     READ_TABLE_DELIMITER='~'))
    SELECT
      (SELECT COUNT(*) FROM ((SELECT * FROM candidate) EXCEPT ALL (SELECT * FROM oracle)))::VARCHAR
      || '|' ||
      (SELECT COUNT(*) FROM ((SELECT * FROM oracle) EXCEPT ALL (SELECT * FROM candidate)))::VARCHAR;
    """
    v = last_value(run(sql))
    extras, missing = v.split("|") if "|" in v else ("?", "?")
    r.record(extras == "0" and missing == "0", f"synthetic={extras} missing={missing}")
    r.duration = time.perf_counter() - t0
    return r


def scenario_scheduler_leak(name: str = "Scheduler: thread count stable across THREADS=2") -> Result:
    """Confirm sap_read_table(THREADS=N) does not leak its setting into the
    rest of the DuckDB session. We read the threads setting before and after,
    via current_setting('threads').
    """
    r = Result(name); t0 = time.perf_counter()
    out_before = last_value(run("SELECT current_setting('threads');"))
    last_value(run("SELECT COUNT(*) FROM sap_read_table('/DMO/FLIGHT', THREADS=2);"))
    out_after = last_value(run("SELECT current_setting('threads');"))
    # current_setting returns the requested thread count; if that's mutated
    # by sap_read_table, after != before.
    r.record(out_before == out_after, f"before={out_before} after={out_after}")
    r.duration = time.perf_counter() - t0
    return r


def scenario_scheduler_leak_same_session(name: str = "Scheduler: same-session thread setting stable") -> Result:
    """The leak is per-DB-instance, but each `run()` here spawns a fresh CLI
    process. To test the actual leak we need a single-session scenario: read
    threads, run sap_read_table(THREADS=2), read threads, all in one CLI
    invocation.
    """
    r = Result(name); t0 = time.perf_counter()
    sql = (
        "SELECT current_setting('threads') AS t;"
        "SELECT COUNT(*) FROM sap_read_table('/DMO/FLIGHT', THREADS=2);"
        "SELECT current_setting('threads') AS t;"
    )
    out = run(sql)
    lines = [l.strip().strip('"') for l in out.strip().split("\n") if l.strip()]
    # CSV output groups stmt outputs: ["t","16","count_star()","40","t","2"] for example.
    t_vals: list[str] = []
    for i, l in enumerate(lines):
        if l == "t" and i + 1 < len(lines):
            t_vals.append(lines[i + 1])
    if len(t_vals) != 2:
        r.record(False, f"could not parse threads readings: {t_vals} from {lines}")
    else:
        r.record(t_vals[0] == t_vals[1], f"before={t_vals[0]} after={t_vals[1]}")
    r.duration = time.perf_counter() - t0
    return r


def scenario_join_stability(name: str = "B7: SFLIGHT⋈SPFLI count stable across 5 runs") -> Result:
    r = Result(name); t0 = time.perf_counter()
    sql = """
    SELECT COUNT(*)
    FROM sap_read_table('SFLIGHT', THREADS=4) f
    JOIN sap_read_table('SPFLI', THREADS=4) s
        ON (f.MANDT = s.MANDT AND f.CARRID = s.CARRID AND f.CONNID = s.CONNID);
    """
    counts = [last_value(run(sql)) for _ in range(5)]
    r.record(len(set(counts)) == 1, f"counts={counts}")
    r.duration = time.perf_counter() - t0
    return r


SCENARIOS = [
    scenario_b1_multiset,
    scenario_b2_stability,
    scenario_b2_pk_pivot,
    scenario_b3_string_cols,
    scenario_b5_filter,
    scenario_b6_limit,
    scenario_b8_sapds_misalign,
    scenario_scheduler_leak,
    scenario_scheduler_leak_same_session,
    scenario_join_stability,
]


def main() -> int:
    ARTIFACTS.mkdir(parents=True, exist_ok=True)
    print("== Pre/Post-fix baseline ==")
    results: list[Result] = []
    for fn in SCENARIOS:
        try:
            r = fn()
        except Exception as e:
            r = Result(fn.__name__)
            r.record(False, f"EXCEPTION: {str(e)[:300]}")
        results.append(r)
        print(" ", r)

    summary_path = ARTIFACTS / f"summary_{int(time.time())}.txt"
    with summary_path.open("w") as f:
        f.write("# Baseline run\n\n")
        for r in results:
            f.write(str(r) + "\n")
    print(f"\nSummary written to {summary_path}")

    failed = [r for r in results if r.passed is False]
    return 0 if not failed else 1


if __name__ == "__main__":
    sys.exit(main())
