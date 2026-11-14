---
name: erpl-bics-queries
description: >
  Running SAP BW queries and cubes from DuckDB with erpl_bics (BICS, the protocol behind Analysis for
  Office): finding queries with sap_bics_show_queries / sap_bics_show_cubes / sap_bics_show(obj_type =>
  'QUERY'), describing them with sap_bics_describe and sap_bics_variables, the stateful session workflow
  sap_bics_begin (id, variables, hierarchy_variables, variant, rows, columns, filters) → sap_bics_rows /
  sap_bics_columns / sap_bics_filter / sap_bics_set_char_prop (DISPLAY KEY|TEXT|BOTH, SORT, TOTALS) /
  sap_bics_attributes / sap_bics_hidden_members → sap_bics_result, structure members, BW hierarchies with
  sap_bics_hierarchy, result shape and pivoting, the result cell budget (erpl_bics_max_result_memory),
  sap_bics_result_stats for empty results, and session cleanup with sap_bics_end / sap_bics_sessions /
  sap_bics_gc. Use when someone wants numbers from a BW query or InfoCube/aDSO, needs to fill BEx variables,
  drill down, filter or swap axes, or gets an empty, rejected or too-large BW result.
user-invocable: false
---

# erpl — SAP BW Queries via BICS Cheat Sheet

BICS executes a BW query the way Analysis for Office does: variables, restricted and calculated key figures,
conditions, hierarchies and BW authorizations all apply. erpl keeps the navigation **state** of each query
session in DuckDB and returns BW's result as a table.

## Critical gotchas

1. **It is a session, keyed by an id.** `sap_bics_begin(…, id => 'q1')` opens it; every later call names
   `'q1'`; `sap_bics_end('q1')` releases it. Each call stores a new version of the state (100–300 KB) in the
   table `erpl_bics.bics_state` of the main database — clean up (`sap_bics_end`, `sap_bics_gc`).
2. **Variables go on `sap_bics_begin`**, not on a later call: `variables => [{'NAME': …, 'LOW': …}]`. A
   mandatory variable left empty makes `sap_bics_result` fail with BW's message. List them first with
   `sap_bics_variables`.
3. **Discovery functions take named arguments.** `sap_bics_show('QUERY')` does not bind — write
   `sap_bics_show(obj_type => 'QUERY', search => '*SALES*')`, or use `sap_bics_show_queries`.
4. **Member values use BW's internal format** — ALPHA/NUMC keys with leading zeros, dates `YYYYMMDD`,
   compounded keys with the parent first (`'DE 11'`). erpl converts typed values like RSRT does
   (`'1001'` → `'0000001001'`, `'09.2004'` → `'200409'` for 0CALMONTH).
5. **BW cannot page a result.** A result larger than the cell budget fails with a suggested
   `erpl_bics_max_result_memory`; usually the right fix is a narrower query (`sap_bics_filter`, fewer
   characteristics on the rows).
6. **An empty result is not always "no data".** Run `sap_bics_result_stats('q1')`: it shows BW's state,
   messages, authorization flag and where every cell went — and contains no business data, so it can go into
   a ticket.
7. **Names match case-insensitively** (characteristics, query names, `op => 'set'`); an unknown one is an
   error listing the valid ones — read it.

## Find and describe

```sql
SELECT technical_name, text, cube_name FROM sap_bics_show_queries(search => '*SALES*');
SELECT technical_name, text FROM sap_bics_show_cubes(search => '0D_NW*');
SELECT technical_name, text, cube_name FROM sap_bics_show(obj_type => 'QUERY', search => '0D_FC_NW*');

-- Characteristics, key figures, variables of a query
SELECT technical_name, text, len(characteristics) AS chars, len(keyfigures) AS kfs
FROM sap_bics_describe('0D_NW_C01', '0D_FC_NW_C01_Q0007');
SELECT name, text, mandatory, var_type, selection_type, reference_char
FROM sap_bics_variables('0D_FC_NW_C01_Q0007');
```

## The session workflow

```sql
-- 1. open (a query or a cube); rows/columns can be set right here
SELECT * FROM sap_bics_begin('0D_NW_C01', id => 'q1', rows => ['0D_NW_CNTRY']);
-- 2. navigate
SELECT * FROM sap_bics_rows('q1', '0D_NW_PROD', op => 'ADD');
SELECT * FROM sap_bics_filter('q1', '0D_NW_CNTRY', 'DE', 'US', op => 'SET');
SELECT * FROM sap_bics_set_char_prop('q1', '0D_NW_CNTRY', 'DISPLAY', 'TEXT');
-- 3. read
SELECT * FROM sap_bics_result('q1');
-- 4. release
SELECT * FROM sap_bics_end('q1');
```

Every navigation call returns the session description (`return => 'DESCRIBE'`, default) or directly the
result (`return => 'RESULT'`).

| Call | Does |
|---|---|
| `sap_bics_rows(id, char…, op)` / `sap_bics_columns(id, char…, op)` | Put characteristics on an axis. `op`: `ADD` (default), `SET` (replace), `REMOVE` (make free) |
| `sap_bics_filter(id, char, member…, op)` | Keep only / add / exclude members. `op => 'SET'` without members clears the filter |
| `sap_bics_set_char_prop(id, char, 'DISPLAY', 'KEY'\|'TEXT'\|'BOTH')` | Key, text or both in the row labels |
| `sap_bics_set_char_prop(id, char, 'SORT', 'ASC'\|'DESC'\|'NONE')` | Sort members |
| `sap_bics_set_char_prop(id, char, 'TOTALS', 'SHOW'\|'HIDE')` | Show/hide (sub)totals; on the outermost row char it removes the overall result row |
| `sap_bics_attributes(id, char, attr…, op)` | Display attributes as extra `<char>__<attr>` columns (row axis only) |
| `sap_bics_hidden_members(id, structure, 'SHOW'\|'HIDE')` | Members the query hides with "Hide (can be shown)" |
| `sap_bics_filter(id, structure, element_uid…)` | Select key-figure structure members (uids from `sap_bics_structure_members`) |

### Variables

```sql
-- 0D_FC_AE_VAR_Q02 has two mandatory single-value variables (year, product hierarchy level 2)
SELECT name, mandatory, selection_type, reference_char FROM sap_bics_variables('0D_FC_AE_VAR_Q02');
SELECT * FROM sap_bics_begin('0D_FC_AE_VAR_Q02', id => 'q2',
    variables => [{'NAME': '0D_FC_P02', 'LOW': '2005'}, {'NAME': '0D_FC_P21', 'LOW': 'FC001'}]);
SELECT * FROM sap_bics_result('q2');
SELECT * FROM sap_bics_end('q2');
```

```text
Interval:          {'NAME': 'ZVAR_DATE', 'OP': 'BT', 'LOW': '20260101', 'HIGH': '20260131'}
Multiple values:   {'NAME': 'ZVAR_CNTRY', 'LOW': 'DE'}, {'NAME': 'ZVAR_CNTRY', 'LOW': 'FR'}   -- repeat NAME
Exclusion:         {'NAME': 'ZVAR_CNTRY', 'SIGN': 'E', 'LOW': 'US'}
Saved BEx variant: sap_bics_begin('MY_QUERY', id => 'q3', variant => 'MY_VARIANT')
```

`SIGN` defaults to `I`, `OP` to `EQ` (or `BT` when `HIGH` is set). Only input-ready variables are accepted;
exit variables fill themselves. Hierarchy variables go in `hierarchy_variables` — see
`references/variables.md` for every variable type and its rules.

## Result shape

`sap_bics_result` returns, left to right: one VARCHAR column per row-axis characteristic (label per
`DISPLAY`), one INTEGER `<char>_HIER_LEVEL` per row characteristic, one VARCHAR `<char>__<attr>` per active
display attribute, then one DOUBLE column per column-axis tuple (`0D_NW_NETV`, or joined for a cross-tab:
`0D_NW_NETV_2010`). With every structure on the rows there is one data column `VALUE`. Totals arrive as rows
whose label is BW's result text (e.g. `Overall Result`); filter them out or keep them deliberately.

The result is long-ish and ready for SQL. Pivoting, unpivoting and rendering recipes (terminal crosstab,
charts, export) are in `references/result-rendering.md`.

## Hierarchies

```sql
SELECT * FROM sap_bics_show_hierarchies(info_object => '0D_NW_PROD');
SELECT node_id, parent_id, node_name, node_value, level, path
FROM sap_bics_hierarchy('<hierarchy>') ORDER BY node_id;
```

## Sessions

```sql
SELECT state_id, latest_version, versions, query, stored_bytes, last_saved FROM sap_bics_sessions();
SELECT * FROM sap_bics_gc(keep_versions => 1);                      -- drop history, keep sessions
SELECT * FROM sap_bics_gc(older_than => INTERVAL '7 days');         -- drop stale sessions
```

Sessions live in the main database (a file database keeps them across restarts) or in the database named by
`SET erpl_bics_state_database = '<attached db>'`. A read-only main database falls back to an in-memory store.
`sap_bics_begin` with an existing id starts that session afresh.

## Decision table

| Situation | Do |
|---|---|
| Numbers exactly as users see them in BW | BICS (this skill) |
| Raw provider records, incremental | ODP `BW` context (`erpl-odp-replication`) |
| Don't know the characteristic names | `sap_bics_describe(cube, query)` → `characteristics` |
| Mandatory variable error | `sap_bics_variables`, then pass `variables` on `sap_bics_begin` |
| Result too large | Filter / fewer row characteristics; raise `erpl_bics_max_result_memory` only if needed |
| Empty or strange result | `sap_bics_result_stats('id')`; then `erpl-operate` |
| Many sessions accumulating | `sap_bics_end` per session, `sap_bics_gc` periodically |

## Canonical query

```sql
SELECT * FROM sap_bics_begin('0D_NW_C01', id => 'sales_by_country', rows => ['0D_NW_CNTRY']);
SELECT * FROM sap_bics_set_char_prop('sales_by_country', '0D_NW_CNTRY', 'DISPLAY', 'BOTH');
CREATE OR REPLACE TABLE sales_by_country AS SELECT * FROM sap_bics_result('sales_by_country');
SELECT * FROM sap_bics_end('sales_by_country');
SELECT * FROM sales_by_country;
```

The `/erpl:bics-find`, `/erpl:bics-run`, `/erpl:bics-navigate` and `/erpl:bics-sessions` commands drive
this workflow interactively.

See also: `erpl-bics-metadata` (BW models, lineage), `erpl-operate` (traces, `sap_bics_result_stats`
fields), `erpl-performance` (cell budget), `erpl-choose-interface` (BICS vs ODP).
