---
name: erpl-odp-replication
description: >
  Extracting and replicating SAP data through ODP (Operational Data Provisioning) with erpl_odp:
  sap_odp_show_contexts, sap_odp_show, sap_odp_describe (supports_delta), sap_odp_preview,
  sap_odp_read_full (threads, columns, filters as SIGN/OP/LOW/HIGH ranges), sap_odp_read_delta with a stable
  subscriber_process (auto DELTAINIT, resume, recover => true), ODQ_CHANGEMODE / ODQ_TSN change columns,
  the cheap probe sap_odp_get_last_modified, subscription and cursor management (sap_odp_show_subscriptions,
  sap_odp_show_cursors, sap_odp_get_subscriptions, PRAGMA sap_odp_close_delta_cursor, PRAGMA sap_odp_drop),
  and why the columns argument aggregates BW providers. Use when reading SAP extractors/DataSources (SAPI),
  BW InfoProviders (BW context), extraction-enabled CDS views (ABAP_CDS) or SLT, when building an
  incremental (delta) load, or when ODP row counts look wrong.
user-invocable: false
---

# erpl — ODP Full & Delta Replication Cheat Sheet

ODP is SAP's own extraction framework: the same queues BW, Datasphere and other tools consume. erpl reads it
over RFC. Objects live in **contexts** — `SAPI` (extractors/DataSources), `BW` (InfoProviders), `ABAP_CDS`
(extraction-enabled CDS views), `HANA`, and SLT contexts where configured.

## Critical gotchas

1. **`columns => [...]` aggregates on BW providers.** ODP returns key figures aggregated over the fields a
   request names: `sap_odp_read_full('BW', '0D_FC_C01$F', columns => ['CALYEAR'])` yields one row per
   year, not one per record. A SQL projection (`SELECT CALYEAR FROM sap_odp_read_full(…)`) does not — erpl
   requests every field and projects locally (erpl ≥ the release after v2026.11.09; older versions
   aggregated here too). Use `columns` only when you want SAP to aggregate or to cut the transfer of a
   non-aggregating source.
2. **`ODQ_*` columns are change metadata**, not source fields: `ODQ_CHANGEMODE`, `ODQ_ENTITYCNTR`,
   `ODQ_TSN`, `ODQ_UNITNO`, `ODQ_RECORDNO`. Never put them in `columns`.
3. **The first `sap_odp_read_delta` with a new `subscriber_process` is the initial load** (auto DELTAINIT): it
   returns the current snapshot and registers a server-side pointer. Later calls with the same name return
   only changes. Changing the name starts over with a new full snapshot.
4. **One reader per `subscriber_process`.** Two concurrent delta reads with the same name race the pointer.
5. **A delta read must be drained.** A scan that stops early (`LIMIT`, an aborted client) can strand the
   ODQ request open on SAP. Land the whole delta in a table; never `LIMIT` a delta read. After an
   interruption, `recover => true` re-delivers the last package.
6. **`filters` are SAP ranges, not SQL.** SQL `WHERE` on an ODP scan is applied by DuckDB after transfer;
   only `filters => [...]` restricts on SAP.
7. **Deletes depend on the source.** ODP reports deletes as `ODQ_CHANGEMODE = 'D'` only where the source
   tracks them; ABAP_CDS views with `byElement` delta report inserts and updates as `U` after-images and
   **no physical deletes** — use APE (`erpl-ape-cds`) when deletes matter.

## Discover

```sql
SELECT * FROM sap_odp_show_contexts();                               -- technical_name, text, release
SELECT * FROM sap_odp_show('BW', search => '*0D_FC*');              -- technical_name, text, semantics
SELECT technical_name, semantics_text, supports_full, supports_delta
FROM sap_odp_describe('BW', '0D_FC_C01$F');
SELECT * FROM sap_odp_preview('BW', '0D_FC_C01$F');                  -- a sample, no subscription
```

The ODP name carries a suffix for its `semantics`: `$F` transaction data/facts, `$P` master data/attributes,
`$T` texts, `$H` hierarchies. Copy `technical_name` exactly. `sap_odp_describe` also returns the field list.

## Full extraction

```sql
CREATE OR REPLACE TABLE fc_c01 AS
SELECT * FROM sap_odp_read_full('BW', '0D_FC_C01$F');
```

```sql
-- SAP-side selection: ranges in SAP's SIGN / OPTION form, OR-combined
CREATE OR REPLACE TABLE fc_c01_2007 AS
SELECT * FROM sap_odp_read_full('BW', '0D_FC_C01$F',
    filters => [{'FIELDNAME': 'CALYEAR', 'SIGN': 'I', 'OP': 'EQ', 'LOW': '2007', 'HIGH': ''}]);
```

| Parameter | Meaning |
|---|---|
| `threads` | Parallel package fetch (default 5) — full reads only |
| `columns` | Fields to request from SAP — **aggregates** on BW providers (gotcha 1) |
| `filters` | `LIST(STRUCT(FIELDNAME, SIGN, OP, LOW, HIGH))`; `OP` is `EQ`, `NE`, `GT`, `LT`, `GE`, `LE`, `BT`, `CP` |
| `secret` | Named `sap_rfc` secret |

A full read opens and closes its own request; it leaves no subscription behind.

## Delta extraction

```sql
-- First call: initial load + pointer registration. Same call later: only the changes.
CREATE OR REPLACE TABLE fc_c01_changes AS
SELECT * FROM sap_odp_read_delta('BW', '0D_FC_C01$F', 'SALES_DWH_FC_C01');

SELECT ODQ_CHANGEMODE, count(*) FROM fc_c01_changes GROUP BY ALL;
```

| Column | Meaning |
|---|---|
| `ODQ_CHANGEMODE` | `C` new, `U` after-image/update, `D` delete (where the source reports deletes) |
| `ODQ_ENTITYCNTR` | Entity counter of the record |
| `ODQ_TSN` | Transaction sequence number of the change — order changes by it |
| `ODQ_UNITNO`, `ODQ_RECORDNO` | Package and record position within the request |

`subscriber_process` is the identity of your pipeline on SAP: make it stable and descriptive
(`'<TEAM>_<TARGET>_<OBJECT>'`). Delta fetches run with one thread regardless of `threads`.

```sql
-- Re-deliver the last package after an interrupted run (the pointer does not advance)
SELECT * FROM sap_odp_read_delta('BW', '0D_FC_C01$F', 'SALES_DWH_FC_C01', recover => true);
```

`recover` cannot be combined with `columns` or `filters` — it reuses the original request's.

### Is there anything new? (cheap probe)

```sql
-- Compare the object's last change with this subscriber's pointer before reading
SELECT m.last_modified,
       (SELECT max(pointer) FROM sap_odp_show_cursors() WHERE subscriber_proc = 'SALES_DWH_FC_C01') AS pointer
FROM sap_odp_get_last_modified('BW', '0D_FC_C01$F') m;
```

## Subscriptions and cursors

```sql
SELECT queue_name, subscriber_name, subscriber_proc FROM sap_odp_show_subscriptions(ERPL_ONLY => true);
SELECT queue_name, subscriber_proc, pointer, is_closed, request_date
FROM sap_odp_show_cursors() WHERE subscriber_proc = 'SALES_DWH_FC_C01';
SELECT * FROM sap_odp_get_subscriptions('BW', '0D_FC_C01$F', subscriber_process => 'SALES_DWH_FC_C01');
```

| Goal | Statement | Effect |
|---|---|---|
| Pause / finish a pipeline cleanly | `PRAGMA sap_odp_close_delta_cursor('BW', 'SALES_DWH_FC_C01', '0D_FC_C01$F');` | Closes the open request; the subscription resumes from its pointer next time. Idempotent (`CLOSED` / `NOT_FOUND`) |
| Start over (new initial load) | `PRAGMA sap_odp_drop('BW', 'ERPL', 'SALES_DWH_FC_C01', '0D_FC_C01$F');` | Deletes the subscription; the next delta read is a full snapshot again |

`sap_odp_drop` takes `(context, subscriber_name, subscriber_process, odp_name)` — the subscriber name erpl
registers is the `subscriber_name` column of `sap_odp_show_subscriptions` (typically `ERPL`). Dropping is
destructive for the pipeline: confirm with the user before running it.

## Decision table

| Situation | Do |
|---|---|
| One-off snapshot | `sap_odp_read_full` with `SELECT *` into a table |
| Recurring incremental load | `sap_odp_read_delta` with a fixed `subscriber_process`; merge into the target (`erpl-pipelines`) |
| Delta returned nothing | Normal when nothing changed — check with `sap_odp_get_last_modified` |
| Run crashed mid-delta | Re-run with `recover => true`, then continue normally |
| Need to reload everything | `sap_odp_drop`, then the next delta read is an initial load |
| Need deletes from a CDS view | APE (`erpl-ape-cds`) |
| Counts or sums look wrong | `columns` on a BW provider aggregates (gotcha 1); on erpl ≤ v2026.11.09 any projection did — read `SELECT *` into a table first |

## Canonical delta cycle

```sql
-- 1. capability check
SELECT supports_delta FROM sap_odp_describe('BW', '0D_FC_C01$F');
-- 2. land the delta (first run = initial load)
CREATE OR REPLACE TABLE fc_c01_batch AS
SELECT * FROM sap_odp_read_delta('BW', '0D_FC_C01$F', 'SALES_DWH_FC_C01');
-- 3. apply fc_c01_batch to the target (see erpl-pipelines), then close the request
PRAGMA sap_odp_close_delta_cursor('BW', 'SALES_DWH_FC_C01', '0D_FC_C01$F');
```

See also: `erpl-choose-interface`, `erpl-pipelines` (applying deltas idempotently), `erpl-ape-cds`
(CDS with deletes), `erpl-operate` (stuck cursors, cleanup), `erpl-performance` (`erpl_odp_fetch_size`,
`erpl_odp_max_threads`).
