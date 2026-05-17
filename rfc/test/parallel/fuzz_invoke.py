"""Fuzz `sap_rfc_invoke` against a diverse set of real SAP RFC function modules
to exercise different parameter shapes (scalars, structs, deep structs, tables,
empty calls) and different result shapes.

Run:
    uv run python rfc/test/parallel/fuzz_invoke.py
"""
from __future__ import annotations

import dataclasses
import json
import os
import pathlib
import subprocess
import sys
import time

REPO_ROOT = pathlib.Path(__file__).resolve().parents[3]
DUCKDB = REPO_ROOT / "build" / "debug" / "duckdb"
ADT_CREDS = REPO_ROOT / ".adt.creds"
ARTIFACTS = REPO_ROOT / "artifacts" / "fuzz"


def secret() -> str:
    c = json.loads(ADT_CREDS.read_text())
    return f"""CREATE OR REPLACE SECRET abap_trial (
    TYPE sap_rfc, ASHOST '{c.get("host", "localhost")}', SYSNR '00',
    CLIENT '{c.get("client", "001")}', USER '{c.get("user", "DEVELOPER")}',
    PASSWD '{c.get("password", "")}', LANG 'EN'
);
"""


def run_cli(sql: str, *, timeout: int = 120) -> tuple[int, str, str]:
    env = {**os.environ}
    env["LD_LIBRARY_PATH"] = f"{REPO_ROOT / 'nwrfcsdk' / 'linux' / 'lib'}:{env.get('LD_LIBRARY_PATH', '')}"
    env["LSAN_OPTIONS"] = f"suppressions={REPO_ROOT / 'scripts' / 'lsan_suppress.txt'}:print_suppressions=0"
    env["ASAN_OPTIONS"] = "detect_odr_violation=0:detect_leaks=0"
    try:
        proc = subprocess.run(
            [str(DUCKDB), "-csv", "-c", secret() + sql],
            capture_output=True, text=True, timeout=timeout, env=env,
        )
        return proc.returncode, proc.stdout, proc.stderr
    except subprocess.TimeoutExpired:
        return 124, "", f"TIMEOUT after {timeout}s"


@dataclasses.dataclass
class Case:
    name: str            # Human-readable test name
    sql: str             # The SELECT statement (just the sap_rfc_invoke + projection)
    expects_rows: bool = True   # True → expect at least one row; False → OK if empty (e.g. SHOWUSERLIST output)
    note: str = ""              # description of what this exercises


# ----------------------------------------------------------------------
# Hand-crafted test cases covering different RFC patterns
# ----------------------------------------------------------------------
CASES: list[Case] = [
    # Pattern 1: zero-argument function returning a single struct
    Case(
        name="RFC_PING (no args, no return data)",
        sql="SELECT COUNT(*) FROM sap_rfc_invoke('RFC_PING');",
        note="Smoke test for the empty-args path",
    ),

    # Pattern 2: simple echo, single scalar input, two scalar outputs
    Case(
        name="STFC_CONNECTION (scalar in, two scalars out)",
        sql=(
            "SELECT ECHOTEXT, RESPTEXT FROM sap_rfc_invoke("
            "  'STFC_CONNECTION', {'REQUTEXT': 'Hello SAP from DuckDB'}"
            ");"
        ),
        note="Smoke test for the named-adapter scalar marshalling",
    ),

    # Pattern 3: zero-arg, struct return
    Case(
        name="RFC_SYSTEM_INFO (no args, struct return)",
        sql=(
            "SELECT (RFCSI_EXPORT).RFCDEST, (RFCSI_EXPORT).RFCSYSID, (RFCSI_EXPORT).RFCDBHOST "
            "FROM sap_rfc_invoke('RFC_SYSTEM_INFO');"
        ),
        note="Tests path-less invocation returning a struct parameter",
    ),

    # Pattern 4: scalar input, table outputs (RFC_GET_FUNCTION_INTERFACE)
    Case(
        name="RFC_GET_FUNCTION_INTERFACE (scalar in, table out)",
        sql=(
            "SELECT COUNT(*) FROM sap_rfc_invoke("
            "  'RFC_GET_FUNCTION_INTERFACE', {'FUNCNAME': 'STFC_CONNECTION'},"
            "  path := '/PARAMS'"
            ");"
        ),
        note="Tests result-set path navigation to a table parameter",
    ),

    # Pattern 5: input struct with embedded sub-struct (BAPI flight scenario)
    Case(
        name="BAPI_FLIGHT_GETLIST (nested struct input, table output)",
        sql=(
            "SELECT COUNT(*) FROM sap_rfc_invoke("
            "  'BAPI_FLIGHT_GETLIST', "
            "  {'AIRLINE': 'LH', 'DESTINATION_FROM': {'AIRPORTID': 'FRA'},"
            "   'DESTINATION_TO': {'AIRPORTID': 'JFK'}},"
            "  path := '/FLIGHT_LIST'"
            ");"
        ),
        note="Tests AdaptStructure for nested struct args",
    ),

    # Pattern 6: function with table-typed input (DDIF_FIELDINFO_GET)
    Case(
        name="DDIF_FIELDINFO_GET (scalar in, multiple table outs)",
        sql=(
            "SELECT COUNT(*) FROM sap_rfc_invoke("
            "  'DDIF_FIELDINFO_GET', {'TABNAME': 'SFLIGHT'},"
            "  path := '/DFIES_TAB'"
            ");"
        ),
        note="Common DDIC introspection function with table result",
    ),

    # Pattern 7: STFC_DEEP_STRUCTURE — deep nested struct (RFC test function)
    # Note: param names use NO underscores on this trial container.
    # IMPORTSTRUCT is of ABAP type STFCCPLXT with fields I (INT), C (CHAR),
    # STR (STRING), XSTR (XSTRING).
    Case(
        name="STFC_DEEP_STRUCTURE (deep struct echo)",
        sql=(
            "SELECT (ECHOSTRUCT).I, (ECHOSTRUCT).C, (ECHOSTRUCT).STR "
            "FROM sap_rfc_invoke("
            "  'STFC_DEEP_STRUCTURE', "
            "  {'IMPORTSTRUCT': {'I': 42, 'C': 'AB', 'STR': 'top', 'XSTR': ''}}"
            ");"
        ),
        note="Tests AdaptStructure round-trip with mixed INT/CHAR/STRING/XSTRING fields",
    ),

    # Pattern 8: STFC_DEEP_TABLE — deep table input/output (echo)
    Case(
        name="STFC_DEEP_TABLE (table input round-trip)",
        sql=(
            "SELECT COUNT(*) FROM sap_rfc_invoke("
            "  'STFC_DEEP_TABLE', "
            "  {'IMPORT_TAB': [{'I': 1, 'C': 'AA', 'STR': 'a', 'XSTR': ''},"
            "                  {'I': 2, 'C': 'BB', 'STR': 'b', 'XSTR': ''}]},"
            "  path := '/EXPORT_TAB'"
            ");"
        ),
        note="Tests AdaptTable: pushing list-of-struct from DuckDB into RFC table param",
    ),

    # Pattern 9: BAPI_USER_GETLIST — table out, optional filters
    Case(
        name="BAPI_USER_GETLIST (optional filters, table return)",
        sql=(
            "SELECT COUNT(*) FROM sap_rfc_invoke("
            "  'BAPI_USER_GETLIST', {'MAX_ROWS': 5},"
            "  path := '/USERLIST'"
            ");"
        ),
        note="Tests integer scalar param",
    ),

    # Pattern 10: BAPI_USER_GET_DETAIL — string in, many structs+tables out
    Case(
        name="BAPI_USER_GET_DETAIL (string in, mixed struct+table out)",
        sql=(
            "SELECT (ADDRESS).FULLNAME, (LOGONDATA).USTYP "
            "FROM sap_rfc_invoke("
            "  'BAPI_USER_GET_DETAIL', {'USERNAME': 'DEVELOPER'}"
            ");"
        ),
        note="Tests no-path invocation returning many output params",
    ),

    # Pattern 11: RFC_FUNCTION_SEARCH — multiple optional args + table out
    Case(
        name="RFC_FUNCTION_SEARCH (multiple scalar filters, table out)",
        sql=(
            "SELECT COUNT(*) FROM sap_rfc_invoke("
            "  'RFC_FUNCTION_SEARCH', {'FUNCNAME': 'STFC*', 'GROUPNAME': '*'},"
            "  path := '/FUNCTIONS'"
            ");"
        ),
        note="Pattern-match arg",
    ),

    # Pattern 12: RFC_GET_LOCAL_DESTINATIONS — no args, table out (may be empty on trial)
    Case(
        name="RFC_GET_LOCAL_DESTINATIONS (no args, table out)",
        sql=(
            "SELECT COUNT(*) FROM sap_rfc_invoke("
            "  'RFC_GET_LOCAL_DESTINATIONS',"
            "  path := '/LOCALDEST'"
            ");"
        ),
        expects_rows=True,
        note="No-arg + path navigation (table name LOCALDEST, no underscore)",
    ),

    # Pattern 13: BAPI_HELPVALUES_GET — multiple scalar inputs
    Case(
        name="BAPI_HELPVALUES_GET (multiple scalars in)",
        sql=(
            "SELECT COUNT(*) FROM sap_rfc_invoke("
            "  'BAPI_HELPVALUES_GET', "
            "  {'OBJTYPE': 'BUS1006', 'METHOD': 'GETLIST', 'PARAMETER': 'COMPANYDATA',"
            "   'FIELD': 'COMPANY'},"
            "  path := '/HELPVALUES'"
            ");"
        ),
        note="Multiple scalar args, table named HELPVALUES (no underscore)",
    ),

    # Pattern 14: STFC_PERFORMANCE — many scalar params with defaults
    # The trial container exposes a slimmer interface: CHECKTAB, LGET0332, LGET1000, LGIT0332, LGIT1000.
    # The LGET*/LGIT* params are RFCTYPE_NUM (zero-padded numeric strings).
    Case(
        name="STFC_PERFORMANCE (mixed-type scalar params)",
        sql=(
            "SELECT COUNT(*) > 0 FROM sap_rfc_invoke("
            "  'STFC_PERFORMANCE', "
            "  {'CHECKTAB': ' ', 'LGET0332': '0', 'LGET1000': '0',"
            "                    'LGIT0332': '0', 'LGIT1000': '0'}"
            ");"
        ),
        note="NUM-typed params must be passed as numeric strings, not integers",
    ),

    # Pattern 16: RFC_PING — truly no params, no returns (the synthetic-ok-column path)
    Case(
        name="RFC_PING (no params, no returns; synthetic ok column)",
        sql="SELECT ok FROM sap_rfc_invoke('RFC_PING');",
        note="Regression test: previously crashed bind with 'must return at least one column'",
    ),

    # Pattern 15: Edge case: missing required argument — should give a clean error
    Case(
        name="EDGE: BAPI_USER_GET_DETAIL without USERNAME (expected error)",
        sql="SELECT * FROM sap_rfc_invoke('BAPI_USER_GET_DETAIL');",
        expects_rows=False,  # this should error, not return rows
        note="Validate clean error path when required arg missing",
    ),
]


def classify(rc: int, stderr: str) -> str:
    if rc == 0:
        return "OK"
    if "RFC_ABAP_EXCEPTION" in stderr or "RFC_ABAP_MESSAGE" in stderr:
        return "ABAP_EXCEPTION"
    if "RFC_COMMUNICATION_FAILURE" in stderr or "Timeout while opening" in stderr:
        return "SAP_UNAVAILABLE"
    if "Parameter " in stderr and "not found" in stderr:
        return "PARAM_NOT_FOUND"
    if "TIMEOUT after" in stderr:
        return "TIMEOUT"
    if "Invalid Error" in stderr or "Failed to invoke function" in stderr:
        # SDK side hit something — could be ABAP exception or client bug
        if "RFC_EXCEPTION" in stderr:
            return "RFC_EXCEPTION"
        return "INVOKE_FAILED"
    return "OTHER_ERROR"


def main() -> int:
    ARTIFACTS.mkdir(parents=True, exist_ok=True)
    report_path = ARTIFACTS / f"fuzz_{int(time.time())}.txt"
    print(f"== Fuzz {len(CASES)} sap_rfc_invoke cases ==")
    results = []
    with report_path.open("w") as f:
        for i, case in enumerate(CASES, 1):
            t0 = time.perf_counter()
            rc, out, err = run_cli(case.sql, timeout=120)
            elapsed = time.perf_counter() - t0
            cls = classify(rc, err)

            # For "expects rows" cases, also check that we actually got non-zero
            extra = ""
            if case.expects_rows and cls == "OK":
                last = out.strip().split("\n")[-1].strip().strip('"')
                # Try parsing as int first; if non-numeric, just say "ok"
                try:
                    n = int(last.split(",")[0])
                    if n == 0:
                        extra = " (ZERO ROWS!)"
                except ValueError:
                    pass

            outcome = f"[{cls}] {case.name} ({elapsed:.1f}s){extra}"
            print(f"  {i:>2}. {outcome}")
            results.append((cls, case.name, elapsed, extra, err[:500] if err else ""))
            f.write(f"\n=== Case {i}: {case.name} ===\n")
            f.write(f"NOTE: {case.note}\n")
            f.write(f"SQL: {case.sql}\n")
            f.write(f"OUTCOME: {cls}{extra}  ({elapsed:.1f}s)\n")
            if rc != 0:
                f.write(f"STDERR (first 500 chars):\n{err[:500]}\n")
            else:
                f.write(f"STDOUT (first 500 chars):\n{out[:500]}\n")

    # Summary
    print("\n=== Summary ===")
    by_outcome: dict[str, int] = {}
    for cls, *_ in results:
        by_outcome[cls] = by_outcome.get(cls, 0) + 1
    for cls, n in sorted(by_outcome.items()):
        print(f"  {cls:20s} {n}")
    print(f"\nReport: {report_path}")

    # Exit non-zero only if there are client-side bugs (OK or known ABAP exceptions are fine)
    suspicious = sum(n for cls, n in by_outcome.items()
                     if cls in ("INVOKE_FAILED", "OTHER_ERROR", "TIMEOUT", "PARAM_NOT_FOUND"))
    return 0 if suspicious == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
