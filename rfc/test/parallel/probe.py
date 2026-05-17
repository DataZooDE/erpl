"""Probe the running SAP system: function signatures and table row counts.

Captures findings to artifacts/probe/. Run once before the rest of the harness:

    uv run python rfc/test/parallel/probe.py
"""
from __future__ import annotations

import json
import os
import pathlib
import subprocess
import sys

REPO_ROOT = pathlib.Path(__file__).resolve().parents[3]
DUCKDB = REPO_ROOT / "build" / "debug" / "duckdb"
ADT_CREDS = REPO_ROOT / ".adt.creds"
ARTIFACTS = REPO_ROOT / "artifacts" / "probe"

DDIC_CANDIDATES = ["T002", "T100", "DD02L", "DD03L", "DD04L", "DD07L", "TADIR", "E071"]
TARGET_TABLES = ["/DMO/FLIGHT", "SFLIGHT", "SPFLI", "/DMO/TRAVEL", "/DMO/AGENCY"]
TARGET_VIEWS = ["DD02V", "V_T100"]
TARGET_CDS = ["/DMO/R_Booking_D", "/DMO/I_Carrier", "/DMO/I_Flight"]

# Skip function-descriptor inspection — empirically slow on this container and
# B8 will answer GET_SORTED behavior directly.
FUNCS_TO_INSPECT: list[str] = []


def load_creds() -> dict[str, str]:
    c = json.loads(ADT_CREDS.read_text())
    return {
        "ASHOST": c.get("host", "localhost"),
        "SYSNR": "00",
        "CLIENT": c.get("client", "001"),
        "USER": c.get("user", "DEVELOPER"),
        "PASSWORD": c.get("password", ""),
        "LANG": "EN",
    }


def secret_sql(creds: dict[str, str]) -> str:
    return f"""CREATE OR REPLACE SECRET abap_trial (
    TYPE sap_rfc,
    ASHOST '{creds["ASHOST"]}',
    SYSNR '{creds["SYSNR"]}',
    CLIENT '{creds["CLIENT"]}',
    USER '{creds["USER"]}',
    PASSWD '{creds["PASSWORD"]}',
    LANG '{creds["LANG"]}'
);
"""


def run_cli(sql: str, *, timeout: int = 600) -> tuple[int, str, str]:
    env = {**os.environ}
    nwrfc = str(REPO_ROOT / "nwrfcsdk" / "linux" / "lib")
    env["LD_LIBRARY_PATH"] = nwrfc + ":" + env.get("LD_LIBRARY_PATH", "")
    # The debug build is ASan/LSan/UBSan instrumented and produces a wall of
    # "leak" reports from libsapnwrfc.so static initialization on every exit.
    # Use the existing suppression file plus odr suppression, and silence the
    # leak channel during probing (we run TSAN/ASAN separately later).
    env["LSAN_OPTIONS"] = f"suppressions={REPO_ROOT / 'scripts' / 'lsan_suppress.txt'}:print_suppressions=0"
    env["ASAN_OPTIONS"] = "detect_odr_violation=0:detect_leaks=0"
    proc = subprocess.run(
        [str(DUCKDB), "-csv", "-c", sql],
        capture_output=True, text=True, timeout=timeout, env=env,
    )
    return proc.returncode, proc.stdout, proc.stderr


def main() -> int:
    ARTIFACTS.mkdir(parents=True, exist_ok=True)
    creds = load_creds()
    secret = secret_sql(creds)

    # 1. Function signatures via the parameter table that sap_rfc_describe_function returns.
    # The function returns a single row with fields {name, import[], export[], changing[], tables[]}.
    print("== Function signatures ==")
    sig_path = ARTIFACTS / "function_signatures.txt"
    with sig_path.open("w") as f:
        for fname in FUNCS_TO_INSPECT:
            f.write(f"\n=== {fname} ===\n")
            sql = secret + (
                f"SELECT i.name AS pname FROM sap_rfc_describe_function('{fname}') d, "
                f"unnest(d.import) AS t(i) "
                f"UNION ALL SELECT t.name FROM sap_rfc_describe_function('{fname}') d, "
                f"unnest(d.tables) AS t(t) "
                f"ORDER BY pname;"
            )
            rc, out, err = run_cli(sql, timeout=60)
            if rc != 0:
                # Strip the long ASan/leak preamble for the printed line.
                short_err = (err or "").strip().splitlines()
                msg = next((l for l in short_err if l and "leak" not in l.lower() and "san" not in l.lower()), "")[:120]
                f.write(f"[ERROR rc={rc}]\n{err}\n")
                print(f"  {fname}: NOT AVAILABLE ({msg})")
            else:
                f.write(out)
                has_sorted = "GET_SORTED" in out
                has_et_data = "ET_DATA" in out
                has_use_et = "USE_ET_DATA_4_RETURN" in out
                print(f"  {fname}: ok GET_SORTED={has_sorted} ET_DATA={has_et_data} USE_ET_DATA_4_RETURN={has_use_et}")

    # 2. Row counts for candidate tables / views / CDS
    print("\n== Row counts ==")
    counts_path = ARTIFACTS / "row_counts.csv"
    with counts_path.open("w") as f:
        f.write("kind,object_name,row_count,error\n")
        for kind, names in (
            ("table", DDIC_CANDIDATES + TARGET_TABLES),
            ("view", TARGET_VIEWS),
            ("cds", TARGET_CDS),
        ):
            for name in names:
                # Use a single-column scan with a small THREADS to count fast.
                # sap_describe_fields tells us a column name; fall back to *.
                sql = secret + f"SELECT COUNT(*) AS c FROM sap_read_table('{name}', THREADS=1);"
                try:
                    rc, out, err = run_cli(sql, timeout=60)
                except subprocess.TimeoutExpired:
                    f.write(f"{kind},{name},,TIMEOUT\n")
                    print(f"  {kind:5s} {name:30s} TIMEOUT (likely huge)")
                    continue
                if rc != 0:
                    msg = err.strip().replace("\n", " | ")[:200]
                    f.write(f"{kind},{name},,\"{msg}\"\n")
                    print(f"  {kind:5s} {name:30s} ERROR ({msg[:60]})")
                else:
                    lines = [l for l in out.strip().split("\n") if l]
                    count = lines[-1] if lines else ""
                    f.write(f"{kind},{name},{count},\n")
                    print(f"  {kind:5s} {name:30s} rows={count}")

    print(f"\nArtifacts written to {ARTIFACTS}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
