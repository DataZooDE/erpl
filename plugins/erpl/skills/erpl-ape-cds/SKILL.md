---
name: erpl-ape-cds
description: >
  Extracting SAP CDS views and entities with erpl_ape through SAP's ABAP Pipeline Engine: discovery with
  sap_ape_show, sap_ape_describe and sap_ape_preview; full snapshots with sap_ape_read_full (columns,
  chunk_size, wireformat; server-side filters are refused); delta reads with deletes via
  sap_ape_read_delta(cds, subscriber_process) and the /1DH/OPERATION column (U after-image, D delete);
  recovering an interrupted delta from the local erpl_ape.delta_spill with recover => true; subscriptions
  (sap_ape_show_subscriptions, PRAGMA sap_ape_drop, the ERPL_ prefix); readiness checks with
  PRAGMA sap_ape_ping, sap_ape_system_info and sap_ape_check_authorizations (read verdict);
  erpl_ape_allow_unreleased and erpl_ape_prepare_timeout. Use when extracting a CDS view (I_*, C_*, Z*),
  building an incremental CDS load that must see deletes, or when an APE read is slow to start, refused or
  unauthorised.
user-invocable: false
---

# erpl — CDS Extraction via the ABAP Pipeline Engine Cheat Sheet

`erpl_ape` reads CDS entities through SAP's own ABAP Pipeline Engine (`DHAPE_*`) — the same machinery SAP
uses for CDS replication. Nothing is installed in SAP, but the RFC user needs a dedicated role. Its
distinguishing feature: **delta with deletes**.

## Critical gotchas

1. **Check readiness before the first read.** `PRAGMA sap_ape_ping` fails when the engine is unreachable for
   the user; `sap_ape_check_authorizations('<ENTITY>')` lists every grant and its `verdict` — read `verdict`,
   not `present` (one row is a grant that must be *absent*).
2. **The first read is slow by design.** SAP prepares an initial load with a background job and returns
   neither data nor an error while it runs; expect tens of seconds even on small entities. The wait is
   bounded by `erpl_ape_prepare_timeout` (900 s).
3. **Delta needs two annotations on the view.** `@Analytics.dataExtraction.enabled` alone allows only full
   reads; delta also needs `@Analytics.dataExtraction.delta.changeDataCapture.automatic`. The engine's
   error says which is missing.
4. **Filter with SQL `WHERE`, not `filters`.** The engine's reader takes no filter on current releases, so
   `filters => …` is refused rather than silently ignored. `columns` is applied on the client (the wire
   still carries every column).
5. **Unreleased entities are refused** unless `SET erpl_ape_allow_unreleased = true;`. Released entities
   (`is_released`) are SAP's stable API — prefer them.
6. **Deletes arrive as keys only.** A `D` row has its key fields set and other columns blank/zero. Inserts
   arrive as `U`, the same as updates.
7. **A delta read is consumed when SAP hands it over.** There is no re-read from SAP; erpl keeps a copy of
   the last batch in `erpl_ape.delta_spill` for `recover => true`. The spill is always written to the **main
   database** of the process (an `ATTACH …; USE …` does not move it), so run delta jobs with the warehouse
   file as main database (`duckdb -unsigned -bail warehouse.duckdb < job.sql`). It holds business data in
   clear text: that file needs the same protection and backups as the target data.
8. **Amounts are currency-shifted** (JPY 106136.00, not the raw 1061.36 `sap_read_table` returns).

## Readiness

```sql
PRAGMA sap_ape_ping;                                   -- 'PONG' + ape_version
SELECT * FROM sap_ape_system_info();
SELECT auth_object, checked_values, verdict, note
FROM sap_ape_check_authorizations('ZERPL_APE_FLIGHT')
WHERE verdict <> 'ok';                                 -- empty = ready
```

| `verdict` | Meaning |
|---|---|
| `ok` | Required grant present (or the forbidden one absent) |
| `missing` | A required grant is missing |
| `granted, and must not be` | `S_DHAPEOP` is held and overrides the operator restriction |
| `unknown` | No verdict (e.g. no connection, no entity given) — never a pass; see `note` |

The role for the Basis team is produced by `/erpl:basis-handout`.

## Discover

```sql
SELECT cds_name, description, is_released, package FROM sap_ape_show(search => 'ZERPL%');
SELECT cds_name, description FROM sap_ape_show(search => 'I_%', released_only => true) LIMIT 20;
SELECT field_name, is_key, abap_type, duckdb_type, reference_field
FROM sap_ape_describe('ZERPL_APE_FLIGHT');
SELECT * FROM sap_ape_preview('ZERPL_APE_FLIGHT', max_rows => 10);   -- no graph, no subscription
```

## Full read

```sql
-- ZERPL_APE_FLIGHT is a custom view without a release contract (gotcha 5): allow it deliberately
SET erpl_ape_allow_unreleased = true;
CREATE OR REPLACE TABLE flights AS
SELECT * FROM sap_ape_read_full('ZERPL_APE_FLIGHT');

-- Fewer columns and a filter: both applied as rows stream in
SELECT Carrid, Connid, Fldate, Price
FROM sap_ape_read_full('ZERPL_APE_FLIGHT', columns => ['Carrid', 'Connid', 'Fldate', 'Price'])
WHERE Carrid = 'LH';
```

A full read creates a temporary subscription and erases it when the scan ends, even after `LIMIT` or an
error. `chunk_size` (records per round-trip, default 1000) is the transport knob.

## Delta read

`ZERPL_APE_D` below is a delta-enabled view (key `Rid`); `ZERPL_APE_FLIGHT` carries only the
initial-load annotation, so it serves full reads only.

```sql
-- check:skip-live (creates a delta subscription on SAP)
-- First call on a new subscriber_process: initial load (blank /1DH/OPERATION) + subscription
-- Later calls: only the changes since the previous call
CREATE OR REPLACE TABLE stage_d AS
SELECT * FROM sap_ape_read_delta('ZERPL_APE_D', 'DWH_APE_D');
```

| `/1DH/OPERATION` | Meaning | Row content |
|---|---|---|
| `''` (blank) | Initial load | full row |
| `U` | Insert or update | full after-image |
| `D` | Delete | key fields only |

Apply a batch idempotently — delete every key in the batch, then insert the non-deletes. The full job
(stage, apply, guard, scheduling) is in `erpl-pipelines`.

`subscriber_process`: stable, at most 30 characters, one reader at a time. A delta query returns what is
available and stops — schedule it (`erpl-pipelines`).

### Recover after a failure

```sql
-- check:skip-live (replays the spilled batch of the previous run)
SELECT * FROM sap_ape_read_delta('ZERPL_APE_D', 'DWH_APE_D', recover => true);
```

Replays the most recent batch from `erpl_ape.delta_spill` — no SAP connection needed, safe to repeat, cannot
be combined with `columns`, `filters`, `chunk_size` or `wireformat`. Apply it with the same idempotent
pattern. `DELETE FROM erpl_ape.delta_spill;` once a batch is safely applied if the data must not linger.

## Subscriptions

```sql
SELECT cds_name, subscriber_process, subscription_name, created_at, status
FROM sap_ape_show_subscriptions();                     -- erpl_only => true by default
```

```sql
-- check:skip-live (destructive: the next delta read is an initial load again)
PRAGMA sap_ape_drop('ZERPL_APE_D', 'DWH_APE_D');   -- DROPPED / NOT_FOUND / STILL_PRESENT / REFUSED: …
```

erpl stores subscriptions as `ERPL_<subscriber_process>` (what Basis sees in `DHCDC_MON`); you always pass
the plain name. Dropping resets a pipeline — confirm with the user first.

If a client was killed mid-scan, its graph keeps running on SAP and the subscription cannot be erased
("still in use by a running graph") until SAP's retention cleans it up — involve Basis.

## Decision table

| Situation | Do |
|---|---|
| CDS view, one-off | `sap_ape_read_full` (or `sap_read_table` for a quick look) |
| CDS view, recurring, deletes matter | `sap_ape_read_delta` + idempotent apply |
| View not delta-enabled | Ask the owner for the CDC annotation; meanwhile full reads |
| `REFUSED`/authorisation error | `sap_ape_check_authorizations('<ENTITY>')`, then `/erpl:basis-handout` |
| First read hangs | Expected while SAP prepares; check the application log named in the error after `erpl_ape_prepare_timeout` |
| Every read times out; SLG1 object `DHCDC` shows `DHCDC_RT/064 Cannot start ACP daemon` | No batch job waits for event `SAP_DHCDC_RT_START_ACP_DAEMON` (technical job repository) — Basis schedules it; nothing else in APE preparation runs without that daemon |
| First delta over an **empty** view times out | The initial load of an empty source delivers no package, which reads as a preparation timeout; check the source has rows |
| Delta crashed | `recover => true`, apply, then continue normally |

## Canonical delta cycle

Stage → apply → log, as a `-bail` job on the warehouse file: see `erpl-pipelines` ("Incremental with deletes
(APE delta)").

See also: `erpl-choose-interface` (APE vs ODP vs `sap_read_table`), `erpl-pipelines`, `erpl-operate`
(stuck subscriptions), `erpl-deploy` (the role, persistent databases).
