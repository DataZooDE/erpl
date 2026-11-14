---
description: List stored SAP BW (BICS) sessions and clean them up — end single sessions, drop history, remove stale ones
argument-hint: "[--db file] [end <id> | gc keep=N | gc older=7d]"
---

Manage BICS sessions: $ARGUMENTS

Use the `erpl-bics-queries` and `erpl-operate` skills.

**How to run SQL.** DuckDB CLI with `-unsigned`, script through stdin, `LOAD erpl;` first. Sessions live in
the database file they were created in (`--db`, else `erpl_bics.duckdb`; or the database named by
`erpl_bics_state_database`) — open that one. Listing needs no SAP connection.

1. List: `SELECT state_id, latest_version, versions, info_provider, query, stored_bytes, last_saved FROM
   sap_bics_sessions() ORDER BY last_saved DESC;` plus a total of `stored_bytes`.
2. Propose a cleanup from what you see (sessions untouched for days, many versions) and **ask before running
   anything** — these calls delete stored state:
   - one session: `SELECT * FROM sap_bics_end('<id>');`
   - history only: `SELECT * FROM sap_bics_gc(keep_versions => 1);`
   - stale sessions: `SELECT * FROM sap_bics_gc(older_than => INTERVAL '7 days');`
3. Report what was removed (`versions_removed`, `sessions_removed`) and the store size afterwards.
