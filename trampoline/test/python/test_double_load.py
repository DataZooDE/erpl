#!/usr/bin/env python3
"""Regression test for issue #52: segfault on second ERPL load in same process.

Usage: python3 test_double_load.py /path/to/erpl.duckdb_extension
"""
import sys
import duckdb

if len(sys.argv) < 2:
    print(f"Usage: {sys.argv[0]} /path/to/erpl.duckdb_extension", file=sys.stderr)
    sys.exit(1)

ext_path = sys.argv[1]

# Connection 1: install and load
con = duckdb.connect(config={"allow_unsigned_extensions": True})
con.execute(f"INSTALL {ext_path!r}")
con.execute("LOAD erpl")
count = con.execute(
    "SELECT count(*) FROM duckdb_functions() WHERE function_name LIKE 'sap_%'"
).fetchone()[0]
assert count > 0, f"Connection 1: no sap_* functions registered (count={count})"
con.close()

# Connection 2: load without re-install — this was the SIGSEGV crash site before the fix
con2 = duckdb.connect(config={"allow_unsigned_extensions": True})
con2.execute("LOAD erpl")
count2 = con2.execute(
    "SELECT count(*) FROM duckdb_functions() WHERE function_name LIKE 'sap_%'"
).fetchone()[0]
assert count2 > 0, f"Connection 2: no sap_* functions after double load (count={count2})"
con2.close()

print(f"PASSED — double-connection load OK, {count2} sap_* functions on connection 2")
