---
name: erpl-rfc-functions
description: >
  Calling SAP RFC function modules and BAPIs from DuckDB with erpl_rfc: sap_rfc_invoke(function, {params}
  [, path, secret]) with scalar, structure (STRUCT) and table (LIST of STRUCT) arguments, unnesting one
  output table with path => '/TABLE', reading the BAPI RETURN messages, and finding and describing function
  modules with sap_rfc_show_function, sap_rfc_show_groups and sap_rfc_describe_function (import, export,
  changing, tables parameters with their DuckDB types). Use when a user wants to call a BAPI or RFC function
  module (BAPI_*_GETLIST, BAPI_*_GETDETAIL, RFC_*, Z function modules), needs to know a function's
  parameters, or gets a type or "parameter not found" error from sap_rfc_invoke.
user-invocable: false
---

# erpl — Calling Function Modules & BAPIs Cheat Sheet

`sap_rfc_invoke` calls one RFC-enabled function module and returns its exports as columns: scalars as
scalar columns, structures as STRUCT, tables as LIST of STRUCT. `path` turns one output table into rows.

## Critical gotchas

1. **The call happens when the query is bound.** Even `DESCRIBE SELECT * FROM sap_rfc_invoke(...)` calls the
   function module. Never wrap a function with side effects in an exploratory query.
2. **A BAPI error is not an SQL error.** BAPIs report failure in their `RETURN` table (`TYPE` `E` or `A`)
   and the SQL statement still succeeds. Always read `RETURN`.
3. **Each call is its own RFC session.** A later `sap_rfc_invoke('BAPI_TRANSACTION_COMMIT')` runs in a new
   session and commits nothing from an earlier call. Use erpl for reading; write BAPIs that need an
   explicit commit do not work this way.
4. **Parameter and field names are SAP's, in upper case** (`'AIRLINE'`, `'DATE_RANGE'`, `'SIGN'`). Look them
   up with `sap_rfc_describe_function`. Optional parameters can be omitted.
5. **Types follow the DDIC:** a `DATS` parameter takes a `DATE` (`DATE '2024-01-31'`), `NUMC` a string with
   leading zeros (`'0400'`), packed numbers a `DECIMAL`/number.
6. **`S_RFC` must allow the function group** of every module you call (`sap_rfc_authorizations()` lists
   erpl's own needs; user-called modules show as `<user-specified>`).

## `sap_rfc_invoke(function_name, {params} [, path, secret])`

```sql
-- Scalar import → scalar exports
SELECT * FROM sap_rfc_invoke('STFC_CONNECTION', {'REQUTEXT': 'Hello SAP'});

-- One output table as rows
SELECT * FROM sap_rfc_invoke('BAPI_FLIGHT_GETLIST', {'AIRLINE': 'LH'}, path => '/FLIGHT_LIST');

-- A table parameter (ranges) as a LIST of STRUCT
SELECT count(*) FROM sap_rfc_invoke('BAPI_FLIGHT_GETLIST',
    {'AIRLINE': 'LH',
     'DATE_RANGE': [{'SIGN': 'I', 'OPTION': 'BT', 'LOW': DATE '2017-01-01', 'HIGH': DATE '2017-12-31'}]},
    path => '/FLIGHT_LIST');

-- An export structure
SELECT * FROM sap_rfc_invoke('BAPI_FLIGHT_GETDETAIL',
    {'AIRLINEID': 'LH', 'CONNECTIONID': '0400', 'FLIGHTDATE': DATE '2017-02-06'},
    path => '/FLIGHT_DATA');
```

Without `path`, every export, changing and table parameter is a column; nested values come back as STRUCT
or LIST and can be unnested in SQL:

```sql
SELECT r.type, r.message
FROM (SELECT unnest("RETURN") AS r
      FROM sap_rfc_invoke('BAPI_FLIGHT_GETLIST', {'AIRLINE': 'XX'}));
```

### Checking the BAPI `RETURN`

```sql
-- Rows with TYPE 'E' (error) or 'A' (abort) mean the call failed
SELECT TYPE, ID, NUMBER, MESSAGE
FROM sap_rfc_invoke('BAPI_FLIGHT_GETLIST', {'AIRLINE': 'XX'}, path => '/RETURN')
WHERE TYPE IN ('E', 'A');
```

## Finding and describing function modules

```sql
SELECT FUNCNAME, GROUPNAME, STEXT FROM sap_rfc_show_function(FUNCNAME => 'BAPI_FLIGHT*');
SELECT * FROM sap_rfc_show_function(GROUPNAME => 'SAPBC_BAPI_SFLIGHT');
SELECT name, text FROM sap_rfc_show_groups(GROUPNAME => 'SAPBC*');

-- Signature: one row with import / export / changing / tables lists
SELECT name, text, function_group, remote_callable FROM sap_rfc_describe_function('BAPI_FLIGHT_GETLIST');

-- One row per import parameter, with the DuckDB type to pass
SELECT p.name, p.duckdb_type, p.optional, p.text
FROM (SELECT unnest(import) AS p FROM sap_rfc_describe_function('BAPI_FLIGHT_GETLIST'));
```

`sap_rfc_describe_function` returns `name`, `text`, `function_group`, `remote_callable` (`R` = callable via
RFC), the lists `import`, `export`, `changing`, `tables` (each element: `name`, `text`, `abap_type`,
`duckdb_type`, `direction`, `length`, `decimals`, `default_value`, `optional`) and `source`. Only remote-
enabled modules can be called.

## Decision table

| Need | Use |
|---|---|
| Rows of a table | `sap_read_table` (`erpl-rfc-tables`) — faster and pushes filters |
| A business object with its logic, texts, derived fields (`BAPI_*_GETDETAIL`) | `sap_rfc_invoke` |
| One output table as rows | `path => '/TABLE_NAME'` |
| Several output tables | call once without `path` and `unnest` each column, or call once per table |
| Find which BAPI exists | `sap_rfc_show_function(FUNCNAME => 'BAPI_<OBJECT>*')` |
| Function with side effects | Do not call it from exploratory SQL (gotcha 1) |

## Canonical pattern

```sql
-- 1. find and inspect
SELECT FUNCNAME, STEXT FROM sap_rfc_show_function(FUNCNAME => 'BAPI_FLIGHT*');
SELECT p.name, p.duckdb_type, p.optional
FROM (SELECT unnest(import) AS p FROM sap_rfc_describe_function('BAPI_FLIGHT_GETLIST'));
-- 2. call and keep the rows
CREATE OR REPLACE TABLE lh_flights AS
SELECT * FROM sap_rfc_invoke('BAPI_FLIGHT_GETLIST', {'AIRLINE': 'LH'}, path => '/FLIGHT_LIST');
-- 3. confirm SAP reported no error
SELECT TYPE, MESSAGE FROM sap_rfc_invoke('BAPI_FLIGHT_GETLIST', {'AIRLINE': 'LH'}, path => '/RETURN')
WHERE TYPE IN ('E', 'A');
```

See also: `erpl-rfc-tables` (raw tables), `erpl-choose-interface`, `erpl-setup` (authorizations).
