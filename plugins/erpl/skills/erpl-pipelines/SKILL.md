---
name: erpl-pipelines
description: >
  Building repeatable SAP-to-DuckDB loads with erpl: landing full extracts and deltas from sap_read_table,
  sap_odp_read_full / sap_odp_read_delta and sap_ape_read_full / sap_ape_read_delta in a persistent DuckDB
  database, Parquet or DuckLake; applying change batches idempotently (ODQ_CHANGEMODE, /1DH/OPERATION
  deletes) with DELETE + INSERT in one transaction; staging, load logging, scheduling with cron or systemd,
  one process per subscriber, and recovering failed runs (recover => true). Ends with where to go next:
  data quality, APIs, dashboards, forecasting via other DataZoo plugins. Use when someone wants a nightly or
  incremental SAP extract, needs to apply deltas correctly, or asks how to operationalise an erpl query.
user-invocable: false
---

# erpl — Landing SAP Data Repeatably Cheat Sheet

An erpl query is a read; a pipeline is that read made **repeatable, idempotent and observable**. The pattern
is the same for every source: read into a staging table, apply it to the target in one transaction, log it.

## Critical gotchas

1. **Use a persistent database file.** Deltas cannot be re-read from SAP; staging tables, the APE spill and
   the BICS session store all need to survive the process. `duckdb warehouse.duckdb`, not `:memory:`.
2. **Stage first, apply second.** Never `INSERT INTO target SELECT * FROM sap_odp_read_delta(...)` directly:
   if the apply fails, the delta is already consumed. `CREATE TABLE stage AS SELECT …`, then apply.
3. **Apply idempotently.** Delete the keys present in the batch, then insert the surviving rows — so a
   replay (`recover => true`) or a re-run gives the same target.
4. **One process per `subscriber_process`**, and never two schedules for the same one. Different
   subscribers may run in parallel.
5. **Deletes depend on the source**: APE reports `D` with keys only; ODP reports `D` only where the source
   tracks deletes; `sap_read_table` has no change tracking — reconcile with a full read.
6. **Pass secrets by name in scripts** (`secret => 'prod_sap'`) so a second secret in scope can never
   redirect the load.

## Full refresh (tables)

```sql
-- check:skip-live (writes to a persistent database file)
ATTACH 'warehouse.duckdb' AS wh;
CREATE SCHEMA IF NOT EXISTS wh.sap;
CREATE OR REPLACE TABLE wh.sap.sflight AS
SELECT *, now() AS _loaded_at FROM sap_read_table('SFLIGHT', secret => 'my_sap');
```

For a large table, refresh a window instead of everything (e.g. the last 90 days by a date key), deleting
that window first in the same transaction.

## Incremental (ODP delta)

```sql
-- check:skip-live (writes to a persistent database file)
ATTACH 'warehouse.duckdb' AS wh;
CREATE SCHEMA IF NOT EXISTS wh.sap;
CREATE OR REPLACE TABLE wh.sap.stage_fc AS
SELECT * FROM sap_odp_read_delta('BW', '0D_FC_C01$F', 'DWH_FC_C01');
BEGIN;
CREATE TABLE IF NOT EXISTS wh.sap.fc_c01 AS SELECT * FROM wh.sap.stage_fc WHERE false;
-- replace every record the batch touches (choose the provider's key fields)
DELETE FROM wh.sap.fc_c01 t USING wh.sap.stage_fc s
WHERE t.CALMONTH = s.CALMONTH AND t.D_FC_CUST = s.D_FC_CUST;
INSERT INTO wh.sap.fc_c01 SELECT * FROM wh.sap.stage_fc WHERE ODQ_CHANGEMODE <> 'D';
COMMIT;
PRAGMA sap_odp_close_delta_cursor('BW', 'DWH_FC_C01', '0D_FC_C01$F');
```

Within one batch a key can change several times: keep the last image by `ODQ_TSN`, `ODQ_UNITNO`,
`ODQ_RECORDNO` before inserting (`QUALIFY row_number() OVER (PARTITION BY <keys> ORDER BY ODQ_TSN DESC,
ODQ_UNITNO DESC, ODQ_RECORDNO DESC) = 1`).

## Incremental with deletes (APE delta)

```sql
-- check:skip-live (writes to a persistent database file)
ATTACH 'warehouse.duckdb' AS wh;
USE wh;
CREATE OR REPLACE TABLE stage_flight AS
SELECT * FROM sap_ape_read_delta('ZERPL_APE_FLIGHT', 'DWH_FLIGHTS');
BEGIN;
CREATE TABLE IF NOT EXISTS flights AS SELECT * EXCLUDE ("/1DH/OPERATION") FROM stage_flight WHERE false;
DELETE FROM flights t USING (SELECT DISTINCT Carrid, Connid, Fldate FROM stage_flight) k
WHERE t.Carrid = k.Carrid AND t.Connid = k.Connid AND t.Fldate = k.Fldate;
INSERT INTO flights SELECT * EXCLUDE ("/1DH/OPERATION") FROM stage_flight WHERE "/1DH/OPERATION" <> 'D';
COMMIT;
```

## Load log

```sql
-- check:skip-live (writes to a persistent database file)
CREATE TABLE IF NOT EXISTS etl_log (
    run_at TIMESTAMP DEFAULT now(), source VARCHAR, subscriber VARCHAR, rows_in BIGINT, status VARCHAR);
INSERT INTO etl_log (source, subscriber, rows_in, status)
SELECT '0D_FC_C01$F', 'DWH_FC_C01', count(*), 'applied' FROM wh.sap.stage_fc;
```

## Output formats

| Target | How |
|---|---|
| DuckDB file | `ATTACH 'warehouse.duckdb' AS wh;` and tables as above |
| Parquet (partitioned) | `COPY stage TO 'lake/fc_c01' (FORMAT parquet, PARTITION_BY (CALYEAR), APPEND);` |
| DuckLake | `INSTALL ducklake; ATTACH 'ducklake:meta.ducklake' AS lake (DATA_PATH 'lake/');` then the same DELETE/INSERT — snapshots give time travel |

## Scheduling

Run the job as a script through stdin (never several statements in `duckdb -c`):

```text
# crontab: every night at 02:15
15 2 * * *  cd /srv/erpl && duckdb -unsigned warehouse.duckdb < load_fc_c01.sql >> load.log 2>&1
```

With systemd use a `.service` + `.timer`; set `Environment=DATAZOO_DISABLE_TELEMETRY=1` if policy requires.
Secrets: a persistent secret of the service user, or `CREATE SECRET` at the top of the script fed from the
environment by your orchestrator — see `erpl-deploy`.

## When a run fails

| Failure | Do |
|---|---|
| Failed during the SAP read (ODP) | Re-run with `recover => true` to get the same package, then apply |
| Failed during the SAP read (APE) | `sap_ape_read_delta(…, recover => true)` replays from `erpl_ape.delta_spill` |
| Failed during the apply | The stage table still has the batch: re-run only the apply |
| Need a clean reload | Drop the subscription (`sap_odp_drop` / `sap_ape_drop`), truncate the target, run again |

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
