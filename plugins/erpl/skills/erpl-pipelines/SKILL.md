---
name: erpl-pipelines
description: >
  Building repeatable SAP-to-DuckDB loads with erpl: landing full extracts and deltas from sap_read_table,
  sap_odp_read_full / sap_odp_read_delta and sap_ape_read_full / sap_ape_read_delta in a persistent DuckDB
  database, Parquet or DuckLake; applying change batches idempotently (ODQ_CHANGEMODE, /1DH/OPERATION
  deletes) with DELETE + INSERT in one transaction; staging, load logging, scheduling with cron or systemd,
  one process per subscriber, -bail, and recovering failed runs (recover => true). Ends with where to go next:
  data quality, APIs, dashboards, forecasting via other DataZoo plugins. Use when someone wants a nightly or
  incremental SAP extract, needs to apply deltas correctly, or asks how to operationalise an erpl query.
user-invocable: false
---

# erpl — Landing SAP Data Repeatably Cheat Sheet

An erpl query is a read; a pipeline is that read made **repeatable, idempotent and observable**. The pattern
is the same for every source: read into a staging table, apply it to the target in one transaction, log it.

## Critical gotchas

1. **Open the warehouse file as the main database**: `duckdb -unsigned -bail warehouse.duckdb < job.sql`.
   Deltas cannot be re-read from SAP, so the stage, the APE replay spill (`erpl_ape.delta_spill`, always written
   to the *main* database — an `ATTACH … ; USE …` does not move it) and the BICS session store must survive the
   process. Never run a delta job on `:memory:`.
2. **Always `-bail`.** Without it the CLI keeps going after an error: a failed apply would still reach
   `COMMIT` and close the ODP request, and the next run overwrites the stage — the batch is lost.
3. **Stage first, apply second.** Never `INSERT INTO target SELECT * FROM sap_odp_read_delta(...)`.
   `CREATE TABLE stage AS SELECT …`, then apply. A stage left over from a failed run must be applied before the
   next extraction.
4. **Apply idempotently, last image wins.** Within one ODP batch a key can change several times (update, then
   delete). Keep the latest image per key first, then delete the batch's keys and insert the surviving
   non-deletes — so a replay (`recover => true`) or a re-run gives the same target.
5. **One process per `subscriber_process`**, and never two schedules for the same one.
6. **Deletes depend on the source**: APE reports `D` with keys only; ODP reports `D` only where the source
   tracks deletes; `sap_read_table` has no change tracking — reconcile with a full read.
7. **Name the secret in job SQL** (`secret => 'prod_sap'`) so another secret in scope can never redirect it.

## Full refresh (tables)

```sql
-- check:skip-live (job script for: duckdb -unsigned -bail warehouse.duckdb < job.sql)
CREATE SCHEMA IF NOT EXISTS sap;
CREATE OR REPLACE TABLE sap.sflight AS
SELECT *, now() AS _loaded_at FROM sap_read_table('SFLIGHT', secret => 'my_sap');
```

For a large table, refresh a window instead of everything (e.g. the last 90 days by a date key): delete that
window and insert it again in one transaction.

## Incremental (ODP delta)

The job: stage the batch, apply it, close the request.

```sql
-- check:skip-live (job script for: duckdb -unsigned -bail warehouse.duckdb < job.sql)
CREATE SCHEMA IF NOT EXISTS sap;
CREATE OR REPLACE TABLE sap.stage_fc AS
SELECT * FROM sap_odp_read_delta('BW', '0D_FC_C01$F', 'DWH_FC_C01', secret => 'my_sap');
-- … apply sap.stage_fc to the target as below …
PRAGMA sap_odp_close_delta_cursor('BW', 'DWH_FC_C01', '0D_FC_C01$F');
```

The apply, shown on a self-contained example (key `k`; replace with the source's key fields): the latest
image per key decides, so an update followed by a delete in the same batch deletes, and two updates insert
one row.

```sql
-- check:offline
CREATE TABLE target (k INTEGER, v VARCHAR);
INSERT INTO target VALUES (1, 'old'), (2, 'old'), (3, 'keep');
CREATE TABLE stage (k INTEGER, v VARCHAR, ODQ_CHANGEMODE VARCHAR, ODQ_TSN DECIMAL(23, 9),
                    ODQ_UNITNO INTEGER, ODQ_RECORDNO INTEGER);
INSERT INTO stage VALUES
    (1, 'u1', 'U', 1, 1, 1), (1, 'u2', 'U', 2, 1, 1),    -- two updates: u2 wins
    (2, 'u1', 'U', 1, 1, 2), (2, NULL, 'D', 2, 1, 2),    -- update, then delete: gone
    (4, 'new', 'C', 1, 1, 3);                            -- insert
BEGIN;
CREATE TEMP TABLE latest AS
SELECT * FROM stage
QUALIFY row_number() OVER (PARTITION BY k ORDER BY ODQ_TSN DESC, ODQ_UNITNO DESC, ODQ_RECORDNO DESC) = 1;
DELETE FROM target USING latest WHERE target.k = latest.k;
INSERT INTO target SELECT k, v FROM latest WHERE ODQ_CHANGEMODE <> 'D';
COMMIT;
SELECT CASE WHEN (SELECT string_agg(k || '=' || v, ',' ORDER BY k) FROM target) = '1=u2,3=keep,4=new'
            THEN 'ok' ELSE error('apply produced the wrong target') END;
```

## Incremental with deletes (APE delta)

```sql
-- check:skip-live (job script for: duckdb -unsigned -bail warehouse.duckdb < job.sql)
CREATE OR REPLACE TABLE stage_d AS
SELECT * FROM sap_ape_read_delta('ZERPL_APE_D', 'DWH_APE_D', secret => 'my_sap');
CREATE TABLE IF NOT EXISTS ape_d AS SELECT * EXCLUDE ("/1DH/OPERATION") FROM stage_d WHERE false;
BEGIN;
DELETE FROM ape_d USING (SELECT DISTINCT Rid FROM stage_d) k WHERE ape_d.Rid = k.Rid;
INSERT INTO ape_d SELECT * EXCLUDE ("/1DH/OPERATION") FROM stage_d WHERE "/1DH/OPERATION" <> 'D';
COMMIT;
```

This is the apply pattern API_REFERENCE gives for APE. APE rows carry no sequence number, so a key that
appears twice in one batch cannot be ordered — stop and investigate if
`SELECT Rid FROM stage_d GROUP BY Rid HAVING count(*) > 1` returns anything. After a crash during the read,
`sap_ape_read_delta(…, recover => true)` replays the batch from the spill in this same database file.

## Load log

```sql
-- check:offline
CREATE TABLE IF NOT EXISTS etl_log (
    run_at TIMESTAMP DEFAULT now(), source VARCHAR, subscriber VARCHAR, rows_in BIGINT, status VARCHAR);
CREATE TEMP TABLE stage_fc AS SELECT 1 AS k;   -- stands in for the staged batch
INSERT INTO etl_log (source, subscriber, rows_in, status)
SELECT '0D_FC_C01$F', 'DWH_FC_C01', count(*), 'applied' FROM stage_fc;
```

## Output formats

| Target | How |
|---|---|
| DuckDB file | the warehouse file as main database, tables as above |
| Parquet (partitioned) | `COPY stage TO 'lake/fc_c01' (FORMAT parquet, PARTITION_BY (CALYEAR), APPEND);` |
| DuckLake | `INSTALL ducklake; ATTACH 'ducklake:meta.ducklake' AS lake (DATA_PATH 'lake/');` then the same apply — snapshots give time travel. Keep the stage and the APE spill in the main database file |

## Scheduling

```text
# crontab: every night at 02:15 — the warehouse file is the main database, -bail stops at the first error
15 2 * * *  cd /srv/erpl && duckdb -unsigned -bail warehouse.duckdb < load_fc_c01.sql >> load.log 2>&1
```

Never several statements in one `duckdb -c` string. With systemd use a `.service` + `.timer`; set
`Environment=DATAZOO_DISABLE_TELEMETRY=1` if policy requires. Secrets: see `erpl-deploy`.

## When a run fails

| Failure | Do |
|---|---|
| During the SAP read (ODP) | Re-run the read with `recover => true` (same subscriber, no `columns`/`filters`), then apply |
| During the SAP read (APE) | `sap_ape_read_delta(…, recover => true)` replays from `erpl_ape.delta_spill`; then apply |
| During the apply | The stage still holds the batch: apply it again; do **not** read a new delta first |
| Need a clean reload | Drop the subscription (`sap_odp_drop` / `sap_ape_drop`, confirm first), empty the target, run again |

## Next steps with other DataZoo plugins

Each plugin is installed separately: `/plugin marketplace add DataZooDE/<repo>` → `/plugin install <repo>@<repo>`.

| Goal | Skill | Repo |
|---|---|---|
| Check completeness, freshness, null rates | `anofox-tabular-data-quality`, `anofox-tabular-profiling` | anofox-tabular |
| Reconcile a delta-built table against a full read | `anofox-tabular-diff` | anofox-tabular |
| Validate master data (VAT, IBAN, addresses), find PII | `anofox-tabular-master-data`, `anofox-tabular-pii` | anofox-tabular |
| Serve tables as REST or MCP endpoints | `flapi-rest`, `flapi-mcp`, `flapi-cache-design` | flapi |
| Dashboards and charts | `anofox-visualization-dashboards`, `anofox-visualization-charts` | anofox-visualization |
| Forecast demand or sales | `anofox-forecast-data-prep`, `anofox-forecast-models` | anofox-forecast |
| Material/BOM similarity on MARA/MAKT/STPO extracts | `anofox-similarity-erp-embeddings` | anofox-similarity |

Hand them the landed tables, not live SAP calls.

See also: `erpl-odp-replication`, `erpl-ape-cds`, `erpl-rfc-tables`, `erpl-deploy`, `erpl-operate`.
