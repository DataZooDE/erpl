---
description: Diagnose a failing or suspicious erpl query — reproduce, collect trace, stats and versions, find the cause, draft a ticket
argument-hint: "[the failing SQL, error message, or what looks wrong]"
---

Diagnose this erpl problem: $ARGUMENTS

Use the `erpl-operate` skill (symptom table, traces, `sap_bics_result_stats`), plus the skill of the
interface involved (`erpl-rfc-tables`, `erpl-odp-replication`, `erpl-ape-cds`, `erpl-bics-queries`).

**How to run SQL.** DuckDB CLI with `-unsigned`, script through stdin, `LOAD erpl;` first, never several
statements in one `duckdb -c` string, never print passwords.

1. Reproduce: run the exact statement once and capture the full error text. If it is a BW result problem,
   keep the session id.
2. Re-run with `SET erpl_trace_enabled = true; SET erpl_trace_level = 'WARN'; SET erpl_trace_output =
   'console';` and keep the `[WARN]` lines.
3. Collect in one pass: `duckdb_extensions()` erpl versions, `version()`, `sap_rfc_backend()`,
   `sap_rfc_live_connections()`; for BICS `SELECT * FROM sap_bics_result_stats('<id>');`.
4. Classify with the symptom table in `erpl-operate`. Check the known traps first: several statements in
   `duckdb -c`; `columns` on an ODP BW provider (aggregates; any projection did on erpl ≤ v2026.11.09); `LIMIT` on a delta read; missing
   `ET_DATA` for string columns; unfilled BEx variables; analysis authorizations (`no_authority`,
   `data_cells = 0`).
5. State the root cause with the evidence, apply the smallest fix and re-run the reproduction.
6. If it is an erpl bug, draft an issue for github.com/DataZooDE/erpl using TROUBLESHOOTING.md's ticket
   list. Before including any trace, warn that `erpl_bics_trace` and SDK traces contain business data and
   leave them out unless the user decides otherwise. The `sap_bics_result_stats` row is safe to include.
