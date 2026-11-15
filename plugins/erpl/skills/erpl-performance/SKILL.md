---
name: erpl-performance
description: >
  Tuning erpl extraction speed and memory: the shared knobs threads / erpl_*_max_threads and fetch_size /
  erpl_*_fetch_size, sap_read_table column parallelism vs row partitions (partitions, erpl_rfc_partitions,
  erpl_rfc_partition_window_rows), wide-table memory (erpl_rfc_read_table_batch_budget alias
  erpl_rfc_fetch_size), persistent RFC connections (erpl_rfc_persistent_connections,
  erpl_rfc_max_persistent_connections), filter pushdown (erpl_rfc_pushdown_filters), ODP package size and
  parallel package fetch (erpl_odp_fetch_size, erpl_odp_max_threads), APE chunk_size, the BICS result
  budget (erpl_bics_max_result_memory, erpl_bics_max_data_cells), and when several processes beat more
  threads. Use when an SAP extraction is slow, uses too much memory, or overloads the SAP system.
user-invocable: false
---

# erpl — Performance Tuning Cheat Sheet

## Critical gotchas

1. **The biggest lever is reading less.** A pushed-down `WHERE` and a short column list beat every
   parallelism setting. Tune knobs only after the query is selective.
2. **Every knob is transport only.** `threads`, `fetch_size`, `partitions` never change which rows come
   back (except row *order* with `partitions`). If results differ, it is not the knob.
3. **SAP's capacity is the ceiling, not erpl's.** Parallel calls each take a dialog work process; raising
   `threads` past what the system has free only queues. Measure on the target system — numbers from a
   trial or test system do not transfer — and agree production parallelism with Basis.
4. **Never derive defaults from one measurement.** Make settings configurable per environment.

## Shared names across sources

| | Named parameter | Session setting | Unit |
|---|---|---|---|
| Parallelism | `threads` | `erpl_rfc_max_threads`, `erpl_odp_max_threads` | concurrent SAP calls (`0` = erpl decides) |
| Fetch granule | `fetch_size` | `erpl_rfc_fetch_size`, `erpl_odp_fetch_size` | RFC: concurrent result rows; ODP: package bytes |

## `sap_read_table`

| Table shape | Use | Why |
|---|---|---|
| Wide (many columns) | `threads` (parallel across columns) | One call per projected column |
| Narrow and long | `partitions => 4..8` | Splits rows into windows read in parallel; unordered |
| Very wide and memory-bound | lower `erpl_rfc_fetch_size` | Bounds rows × columns held at once |

```sql
SELECT * FROM sap_read_table('SFLIGHT', threads => 4);
SELECT CARRID, CONNID FROM sap_read_table('SFLIGHT', partitions => 4);
SET erpl_rfc_fetch_size = 262144;              -- default 1310720 concurrent rows
```

- `partitions` pages with 32-bit `ROWSKIPS`: windows beyond row 2,147,483,647 are refused — narrow with
  `WHERE`. Rows can shift between windows if the table is written during the read.
- `erpl_rfc_persistent_connections` (default on) reuses one connection per column during a scan;
  `erpl_rfc_max_persistent_connections` (16) caps how many a scan holds.
- `erpl_rfc_pushdown_filters = false` only for diagnosis — it never improves speed.

## ODP

```sql
SELECT * FROM sap_odp_read_full('BW', '0D_FC_C01$F', threads => 4);
SET erpl_odp_fetch_size = 8388608;             -- bytes per package; default 2 MiB
```

Delta reads fetch packages one at a time regardless of `threads`. Selective `filters` are the main lever.

## APE

`chunk_size` (records per round-trip, default 1000) on `sap_ape_read_full` / `sap_ape_read_delta`. The
first read is dominated by SAP's preparation job, not transfer. `columns` saves DuckDB memory but not
network: the engine streams every column.

## BICS

BW builds the whole result or nothing; there is no paging. The budget is memory:

```sql
SET erpl_bics_max_result_memory = '8GB';       -- default: DuckDB's memory_limit
SET erpl_bics_max_data_cells = 0;              -- 0 = derive from the memory budget
```

The "too large to read in one go" error names the exact value to set. Before raising it, narrow the query
(`sap_bics_filter`, fewer row characteristics) — the cost is on the BW server too. Persist results with
`CREATE TABLE … AS SELECT * FROM sap_bics_result(...)`: each call re-executes the query.

## Scaling out

For very large extracts, several *processes* — each reading a disjoint `WHERE` range or a different object —
scale better than more threads in one process, because SAP-side limits apply per client program. Agree the
number of concurrent processes with Basis and watch `SM50`/`SM66` (work processes) during the first run.

## Measure

```sql
-- check:skip-live (needs a timer; DuckDB CLI)
.timer on
SELECT count(*) FROM sap_read_table('SFLIGHT', COLUMNS => ['CARRID']);
SELECT sap_rfc_connections_opened(), sap_rfc_connections_closed(), sap_rfc_live_connections();
```

`EXPLAIN ANALYZE` shows where time goes on the DuckDB side; `SET erpl_trace_level = 'INFO'` logs RFC calls
and pushed-down filters.

See also: `erpl-rfc-tables`, `erpl-odp-replication`, `erpl-bics-queries`, `erpl-operate`.
