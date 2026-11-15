---
description: Navigate an open SAP BW query session step by step — drill down, swap axes, filter, sort, show texts or attributes — re-rendering after each step
argument-hint: "<state id> [first step, e.g. 'drill down by product' | 'only DE and FR' | 'swap axes'] [--db file] [secret=name]"
---

Navigate the BICS session: $ARGUMENTS

Use the `erpl-bics-queries` skill.

**How to run SQL.** DuckDB CLI with `-unsigned`, script through stdin, `LOAD erpl;` first, never several
statements in one `duckdb -c` string, never print passwords. Always use the **same database file** the
session was opened in (`--db`, else `erpl_bics.duckdb`): the session state lives in its
`erpl_bics.bics_state` table, and each command run is a new process. With `secret=` given, pass
`secret => '<name>'` to every call that accepts it.

1. Check the session exists: `SELECT state_id, latest_version, query, last_saved FROM sap_bics_sessions()
   WHERE state_id = '<id>';`. If not, offer `/erpl:bics-run`.
2. Show where it stands: `SELECT * FROM sap_bics_describe(id => '<id>');` — `state_rows`,
   `state_columns`, `state_free` (each with its display, sort and totals setting) and `variables`.
3. Translate each requested step into one call, using technical names from the description:

   | Step | Call |
   |---|---|
   | drill down by X | `sap_bics_rows('<id>', 'X', op => 'ADD')` |
   | replace rows by X | `sap_bics_rows('<id>', 'X', op => 'SET')` |
   | X across | `sap_bics_columns('<id>', 'X', op => 'ADD')` |
   | remove X | `sap_bics_rows('<id>', 'X', op => 'REMOVE')` (or columns) |
   | swap axes | `rows ... op => 'SET'` with the former columns, then `columns ... op => 'SET'` with the former rows |
   | only / add / exclude members | `sap_bics_filter('<id>', 'X', 'v1', …, op => 'SET' / 'ADD' / 'REMOVE')`; clear: `op => 'SET'` without members |
   | texts / keys / both | `sap_bics_set_char_prop('<id>', 'X', 'DISPLAY', 'TEXT' / 'KEY' / 'BOTH')` |
   | sort | `sap_bics_set_char_prop('<id>', 'X', 'SORT', 'ASC' / 'DESC' / 'NONE')` |
   | hide totals | `sap_bics_set_char_prop('<id>', 'X', 'TOTALS', 'HIDE')` |
   | show attribute A of X | `sap_bics_attributes('<id>', 'X', 'A')` |
   | show hidden key figures | `sap_bics_hidden_members('<id>', '<structure>', 'SHOW')` |

4. After each step: `CREATE OR REPLACE TABLE bics_result AS SELECT * FROM sap_bics_result('<id>');` and
   render it as `/erpl:bics-run` does (pivot by default). Before each step note the current axes, filters
   and properties from `sap_bics_describe(id => '<id>')`, so a successful step can be undone by setting
   exactly those again. If BW **rejects** a step, report its message and change nothing: the session already
   kept its previous version, and an "opposite" call would alter a valid state.
5. Keep a running list of the steps' SQL and print it at the end, followed by `sap_bics_end('<id>')` as the
   suggested close (run it only if the user says so).
