---
description: Diagnose a failing or suspicious erpl query — classify it, reproduce it safely, collect trace, stats and versions, find the cause, draft a ticket
argument-hint: "[the failing SQL, error message, or what looks wrong] [--db file] [secret=name]"
---

Diagnose this erpl problem: $ARGUMENTS

Use the `erpl-operate` skill (symptom table, traces, `sap_bics_result_stats`), plus the skill of the
interface involved (`erpl-rfc-tables`, `erpl-rfc-functions`, `erpl-odp-replication`, `erpl-ape-cds`,
`erpl-bics-queries`).

**How to run SQL.** DuckDB CLI with `-unsigned -bail`, script through stdin, `LOAD erpl;` first, never several
statements in one `duckdb -c` string. Use the same database file the failing job uses (`--db`) and name the
secret on every SAP call. Never print passwords; never `SELECT secret_string` or `*` from `duckdb_secrets()`.

0. **Classify before running anything** — some statements must not simply be run again:

   | Statement | How to reproduce |
   |---|---|
   | Reads without state: `sap_read_table`, `sap_odp_read_full`, `sap_ape_read_full`, `sap_*_show*`, `sap_*_describe*`, `sap_bics_result` | Re-run as is |
   | `sap_odp_read_delta` | **Do not re-run** — it would consume the next changes. Use `recover => true` with the same context, object and subscriber (no `columns`/`filters`), in the same database file, or diagnose from the error text alone |
   | `sap_ape_read_delta` | **Do not re-run.** `recover => true` with the same entity and subscriber (no `columns`, `filters`, `chunk_size`, `wireformat`), in the same database file — it replays the spill without SAP |
   | `sap_rfc_invoke` (runs at bind time), drops, `sap_bics_gc`/`sap_bics_end`, `INSERT`/`DELETE`/`COPY` | Ask the user before running anything; prefer explaining from the error text |
   | A whole job script | Never replay it; reproduce only the failing statement per the rows above |

1. Reproduce once and capture the full error text. For a BW result problem keep the session id.
2. Run the reproduction again with `SET erpl_trace_enabled = true; SET erpl_trace_level = 'WARN';
   SET erpl_trace_output = 'console';` and keep the `[WARN]` lines (they can contain BW message texts).
3. Collect in one pass: erpl versions from `duckdb_extensions()`, `version()`, `sap_rfc_backend()`,
   `sap_rfc_live_connections()`; for BICS `SELECT * FROM sap_bics_result_stats('<id>');`.
4. Classify with the symptom table in `erpl-operate`. Check the known traps: several statements in
   `duckdb -c`; a job without `-bail`; `columns` on an ODP BW provider (see gotcha 1 of
   `erpl-odp-replication`); `LIMIT` on a delta read; missing `ET_DATA` for string columns; unfilled BEx
   variables; analysis authorizations (`no_authority`, `data_cells = 0`).
5. State the root cause with the evidence, apply the smallest fix, and re-run the reproduction (only if it is
   in the first row of the table in step 0).
6. If it is an erpl bug, draft an issue for github.com/DataZooDE/erpl using TROUBLESHOOTING.md's ticket list.
   The `sap_bics_result_stats` row is safe to include. Before including any trace, say that
   `erpl_bics_trace` and SDK traces contain business data and leave them out unless the user decides
   otherwise.
