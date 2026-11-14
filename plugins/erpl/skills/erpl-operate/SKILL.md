---
name: erpl-operate
description: >
  Operating and troubleshooting erpl: tracing (erpl_trace_enabled / erpl_trace_level / erpl_trace_output /
  erpl_trace_file_path, the BICS protocol trace erpl_bics_trace, the SAP SDK trace sap_rfc_set_trace_level),
  diagnosing empty or wrong BW results with sap_bics_result_stats, connection leaks with
  sap_rfc_live_connections, housekeeping of ODP cursors and subscriptions (sap_odp_show_cursors,
  sap_odp_show_subscriptions, sap_odp_close_delta_cursor, sap_odp_drop), APE subscriptions
  (sap_ape_show_subscriptions, sap_ape_drop, erpl_ape.delta_spill), the BICS session store
  (sap_bics_sessions, sap_bics_gc, sap_bics_end, erpl_bics_state_database), common error messages and what
  to put in a bug report. Use when an erpl query fails, hangs, returns nothing or too much, when SAP
  resources pile up (cursors, subscriptions, sessions, connections), or when preparing a support ticket.
user-invocable: false
---

# erpl — Operations & Troubleshooting Cheat Sheet

## Critical gotchas

1. **Traces can contain business data.** `erpl_trace_*` at `WARN` carries counts and BW messages; the BICS
   protocol trace (`erpl_bics_trace`) and the SAP SDK trace (`dev_rfc*.trc`) contain cell values, keys and
   texts. Share those privately or redact them.
2. **Clean up deliberately, never broadly.** On a shared SAP system the cursor/subscription lists contain
   other people's pipelines. Filter by your own `subscriber_process`, and confirm with the user before any
   `sap_odp_drop`, `sap_ape_drop`, `sap_bics_end` or `sap_bics_gc`.
3. **An interrupted ODP delta can leave a request open that SAP will not close** (`ILLEGAL_REQ_STATE_…`).
   It is harmless for other subscribers; do not try to "fix" it by restarting SAP. Use a new
   `subscriber_process` if the old one is unusable.
4. **`sap_rfc_live_connections()` should be 0 between queries.** Anything else holds SAP work processes.

## First response to a failure

```sql
SET erpl_trace_enabled = true;
SET erpl_trace_level = 'WARN';         -- TRACE, DEBUG, INFO, WARN, ERROR
SET erpl_trace_output = 'console';     -- 'file' or 'both' with erpl_trace_file_path
-- re-run the failing statement, then:
SELECT sap_rfc_backend(), sap_rfc_live_connections();
SELECT extension_name, extension_version FROM duckdb_extensions() WHERE extension_name LIKE 'erpl%';
```

`/erpl:diagnose` runs this collection and assembles a ticket-ready summary.

| Symptom / message | Likely cause | Next step |
|---|---|---|
| `RFC_COMMUNICATION_FAILURE`, timeout | Network, SAProuter, SAP down/overloaded | `PRAGMA sap_rfc_ping`; check host/port/router |
| `RFC_LOGON_FAILURE`, user locked | Credentials; lock after repeated failures | Fix the secret; unlock in SU01 |
| `no RFC authorization` | `S_RFC` missing for a function group | `sap_rfc_authorizations()` → Basis |
| `Secret '*' not found` in a CLI one-liner | Statements in one `duckdb -c` string | Pipe a script through stdin |
| `sap_read_table` fails on a string column | No `ET_DATA` support | `READ_TABLE_FUNCTION`; see `erpl-rfc-tables` |
| ODP numbers look aggregated | `columns` on a BW provider | See gotcha 1 of `erpl-odp-replication` |
| APE read waits long, then times out | SAP prepare job not finished | Check the application log named in the error; `erpl_ape_prepare_timeout` |
| BICS result empty / all NULL | Authorizations, variables, budget | `sap_bics_result_stats` (below) |
| BICS "too large to read in one go" | Cell budget | Filter, or the suggested `erpl_bics_max_result_memory` |

## BW results: `sap_bics_result_stats`

```sql
-- check:skip-live (needs an open session)
SELECT error, e_state, e_max_message_type, bw_messages, no_authority,
       n_rows, data_cells, cells_placed, cells_null_valued, rfc_user, bw_release
FROM sap_bics_result_stats('q1');
```

| Field | Reading |
|---|---|
| `e_state` | 1 data, 2 executed but empty, 4 over the cell budget |
| `e_max_message_type`, `bw_messages` | `E`/`A` = BW refused; messages say why (variables, authorizations) |
| `no_authority` | The RFC user lacks analysis authorization |
| `data_cells = 0` with `n_rows > 0` | Layout without values — usually authorizations of `rfc_user` |
| `cells_dropped_*` > 0 | A mapping problem in erpl — report it |

The row has no business data and can be pasted into a public ticket. Compare with RSRT as the *same* user.
More: `erpl_bics_stream_result_tables = false` for a comparison read, and the protocol trace
(`SET erpl_bics_trace = true; SET erpl_bics_trace_dir = '/tmp/bics_trace';`).

## Housekeeping

```sql
-- ODP: your cursors and subscriptions
SELECT queue_name, subscriber_proc, pointer, is_closed, request_date
FROM sap_odp_show_cursors() WHERE subscriber_proc LIKE 'DWH_%';
SELECT queue_name, subscriber_name, subscriber_proc FROM sap_odp_show_subscriptions(ERPL_ONLY => true);

-- APE: erpl's subscriptions (prefix ERPL_), and the local replay buffer
SELECT cds_name, subscriber_process, created_at, status FROM sap_ape_show_subscriptions();

-- BICS: stored sessions
SELECT state_id, versions, query, stored_bytes, last_saved FROM sap_bics_sessions();
```

| Resource | Release (confirm first) |
|---|---|
| Finished ODP delta pipeline | `PRAGMA sap_odp_close_delta_cursor(ctx, proc, odp_name)` — resumable |
| Obsolete ODP subscription | `PRAGMA sap_odp_drop(ctx, subscriber_name, proc, odp_name)` — next read is a full load |
| Obsolete APE subscription | `PRAGMA sap_ape_drop(cds, proc)` — `DROPPED` / `NOT_FOUND` / `STILL_PRESENT` |
| APE "still in use by a running graph" | A killed client left the graph running; SAP's retention clears it — involve Basis |
| Applied APE batches | `DELETE FROM erpl_ape.delta_spill WHERE subscriber_process = '…';` |
| BICS session | `sap_bics_end('id')` |
| BICS history / stale sessions | `sap_bics_gc(keep_versions => 1)`, `sap_bics_gc(older_than => INTERVAL '7 days')` |

`/erpl:bics-sessions` lists and cleans BICS sessions interactively.

## SAP SDK trace (connection and logon problems)

```sql
-- check:skip-live (writes trace files)
PRAGMA sap_rfc_set_trace_dir('/tmp/rfc_trace');
PRAGMA sap_rfc_set_trace_level(2);     -- 0 off … 3 full
PRAGMA sap_rfc_ping;
PRAGMA sap_rfc_set_trace_level(0);
```

## What to put in a ticket

1. The exact error text, or the `sap_bics_result_stats` row.
2. The `[WARN]` lines of the erpl trace.
3. `duckdb_extensions()` versions, DuckDB client (CLI/Python/…) and OS, `sap_rfc_backend()`.
4. SAP release (from the stats row or the system).
5. Whether the same works in SAP (SE16, RSRT, ODQMON) as the same user.
6. Protocol/SDK traces only on request, privately.

See also: `erpl-performance`, `erpl-odp-replication`, `erpl-ape-cds`, `erpl-bics-queries`, `erpl-setup`.
