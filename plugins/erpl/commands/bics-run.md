---
description: Run a SAP BW query or cube — fill variables, set rows/columns/filters, fetch, render as table, chart, dashboard or file, and close the session
argument-hint: "<query or cube> [rows=… cols=… filter=char:v1,v2 var=NAME:value] [--render pivot|chart|dashboard|export] [--db file] [secret=name]"
---

Run this BW query: $ARGUMENTS

Use the `erpl-bics-queries` skill and its `references/variables.md` and `references/result-rendering.md`.

**How to run SQL.** DuckDB CLI with `-unsigned -bail`, script through stdin, `LOAD erpl;` first, never several
statements in one `duckdb -c` string, never print passwords. Use a **database file** (the `--db` argument,
else `erpl_bics.duckdb` in the current directory) so the session store and the result table persist. With
`secret=` given (or more than one `sap_rfc` secret in scope) pass `secret => '<name>'` to every call that
accepts it (`sap_bics_begin`, `sap_bics_variables`, `sap_bics_describe`, `sap_bics_result_stats`).

1. Resolve the object. If unsure whether it is a query or cube, or names are unknown, run the describe step
   of `/erpl:bics-find` first. Map every characteristic the user names to its technical name from
   `sap_bics_describe` — never guess.
2. Variables: `SELECT name, mandatory, input_enabled, selection_type, reference_char FROM
   sap_bics_variables('<query>');`. Ask for every mandatory input-ready variable that has no value (show the
   reference characteristic and selection type). Values go in BW's internal format (keys with leading zeros,
   dates `YYYYMMDD`).
3. Open with a fresh id (`run_<yyyymmdd_hhmmss>`), axes and variables in one call:
   `SELECT * FROM sap_bics_begin('<obj>', id => '<id>', rows => [...], columns => [...], variables => [...]);`
   Then filters: `sap_bics_filter('<id>', '<char>', '<v1>', …, op => 'SET')`; display texts if asked:
   `sap_bics_set_char_prop('<id>', '<char>', 'DISPLAY', 'TEXT'|'BOTH')`.
4. Fetch once and persist: `CREATE OR REPLACE TABLE bics_result AS SELECT * FROM sap_bics_result('<id>');`.
   On an error or an empty result run `SELECT * FROM sap_bics_result_stats('<id>');` and explain using the
   field table in `erpl-operate` (variables, authorizations, cell budget).
5. Render (default `pivot`):
   - `pivot`: show the persisted table as a compact table (≤ 50 rows, key figures rounded, totals marked);
     for a crosstab, put a characteristic on the columns in BW rather than pivoting locally.
   - `chart`: if the anofox-visualization plugin's `anofox-visualization-charts` skill is available, use it
     on `bics_result`; otherwise offer `/plugin marketplace add DataZooDE/anofox-visualization` →
     `/plugin install anofox-visualization@anofox-visualization` and fall back to `pivot`.
   - `dashboard`: hand `bics_result` (in the database file) to `anofox-visualization-dashboards` /
     `anofox-visualization-serving`.
   - `export`: `COPY bics_result TO '<file>.parquet' (FORMAT parquet);` (csv/xlsx on request).
6. Close: `SELECT * FROM sap_bics_end('<id>');` unless the user wants to keep navigating — then print the id
   and the exact follow-up `/erpl:bics-navigate <id> --db <file> secret=<name>`.
7. Print the SQL that was run as one reproducible script.
