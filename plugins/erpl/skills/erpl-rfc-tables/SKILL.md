---
name: erpl-rfc-tables
description: >
  Reading SAP tables, DDIC views and CDS views into DuckDB with erpl_rfc: sap_read_table (COLUMNS, FILTER,
  MAX_ROWS, threads, partitions, fetch_size, READ_TABLE_FUNCTION such as /SAPDS/RFC_READ_TABLE2,
  READ_TABLE_DELIMITER, secret), WHERE filter pushdown into RFC_READ_TABLE OPTIONS, ATTACH '' AS sap
  (TYPE sap_rfc, TABLES globs) as a virtual catalog, data-dictionary discovery with sap_show_tables and
  sap_describe_fields, and the SAP-to-DuckDB type mapping (NUMC stays VARCHAR, DATS→DATE, CURR→DECIMAL, RAW→BLOB,
  string columns needing ET_DATA). Use when reading or joining SAP tables (SFLIGHT, BKPF, MARA, Z-tables),
  finding a table or its fields, mounting SAP as a database, or when a table read is slow, wrong-typed or
  fails on string columns.
user-invocable: false
---

# erpl — Reading SAP Tables Cheat Sheet

`sap_read_table` reads a table, DDIC view or CDS view through `RFC_READ_TABLE` (or a compatible function
module) and streams it into DuckDB with real types. It is a full read every time — there is no change
tracking; for recurring incremental loads see `erpl-choose-interface`.

## Critical gotchas

1. **Filter, or SAP sends everything.** A `WHERE` on `sap_read_table(...)` is pushed to SAP when it can be
   (table below); otherwise the whole table crosses the wire and DuckDB filters afterwards. The rows are the
   same either way — only volume and time differ.
2. **Never filter on the client field.** `MANDT` (DDIC type `CLNT`) predicates are not pushed —
   `RFC_READ_TABLE` rejects them. The secret's `client` already restricts the read to one client.
3. **Name only the columns you need.** `COLUMNS => [...]` or a projection in `SELECT` cuts the transfer.
   Wide tables (BSEG, ACDOCA) are bounded by `erpl_rfc_fetch_size`; see `erpl-performance`.
4. **String/XSTRING columns need `ET_DATA`.** Standard `RFC_READ_TABLE` returns them only with SAP Note
   2246160 installed. Otherwise erpl fails fast naming the column; select fewer columns or set
   `READ_TABLE_FUNCTION => '/SAPDS/RFC_READ_TABLE2'` (or a Z-reader) with a `READ_TABLE_DELIMITER`.
5. **NUMC stays text.** Document numbers, material numbers etc. arrive as VARCHAR with their leading zeros
   (`'0000004711'`). Compare as strings, or `CAST(... AS BIGINT)` deliberately.
6. **`ATTACH` lists nothing by default.** `SHOW TABLES FROM sap` is empty unless `TABLES` is given; tables are
   resolved on demand when referenced by name.
7. **Partitioned reads are unordered.** `partitions => N` returns the same rows in any order; add `ORDER BY`.

## `sap_read_table(table_name [, …])`

| Parameter | Meaning |
|---|---|
| `table_name` | Table, DDIC view or CDS view (e.g. `'SFLIGHT'`, `'/DMO/R_BOOKING_D'`) |
| `COLUMNS` | `LIST(VARCHAR)` of field names to read |
| `FILTER` | Raw ABAP `WHERE` clause sent to SAP as is (`'CARRID = ''LH'''`) |
| `MAX_ROWS` | Stop after this many rows (`0` = all) |
| `threads` | Parallel calls across *columns* (wide tables). `0` = erpl decides |
| `partitions` | Parallel workers across *rows* (narrow, long tables). Rows unordered |
| `fetch_size` | Concurrent result rows per round-trip; transport only |
| `READ_TABLE_FUNCTION` / `READ_TABLE_DELIMITER` | Alternative reader FM and its delimiter |
| `secret` | Named `sap_rfc` secret |

```sql
-- Basic reads; WHERE is pushed to SAP
SELECT * FROM sap_read_table('SFLIGHT') WHERE CARRID = 'LH';

SELECT CARRID, CONNID, FLDATE, PRICE, CURRENCY
FROM sap_read_table('SFLIGHT')
WHERE FLDATE BETWEEN DATE '2024-01-01' AND DATE '2024-12-31';

-- Explicit column list, raw ABAP filter and a row cap
SELECT * FROM sap_read_table('SFLIGHT',
    COLUMNS => ['CARRID', 'CONNID', 'FLDATE', 'SEATSOCC'],
    FILTER => 'CARRID = ''LH''',
    MAX_ROWS => 100);
```

### What `WHERE` pushes to SAP

| Predicate | Pushed |
|---|---|
| `=`, `<>`, `<`, `>`, `<=`, `>=` | yes |
| `AND` / `OR` over one column, `BETWEEN` | yes (all arms or none) |
| `IN (...)` | yes, up to a ~4000-character clause |
| `IS NULL` / `IS NOT NULL` | no (ABAP has no NULL) |
| Anything on `MANDT` / a `CLNT` field | no |
| `TIMESTAMP`, `BLOB`, `FLOAT`, `DOUBLE` literals | no |
| Functions or expressions on the column (`upper(CITYFROM) = …`) | no |

Non-pushed predicates are still applied by erpl, so the result is always correct. To prove a discrepancy is
not caused by pushdown, compare with `SET erpl_rfc_pushdown_filters = false;` — the rows must be identical.

## Joining SAP tables with each other and with local data

```sql
-- Flights with their route, filtered on both sides at SAP
SELECT f.CARRID, f.CONNID, f.FLDATE, p.CITYFROM, p.CITYTO, f.SEATSOCC, f.SEATSMAX
FROM sap_read_table('SFLIGHT') f
JOIN sap_read_table('SPFLI', COLUMNS => ['CARRID', 'CONNID', 'CITYFROM', 'CITYTO']) p
  USING (CARRID, CONNID)
WHERE f.CARRID = 'LH';
```

Join keys are not pushed from one SAP table into the other: filter each side explicitly. For a reused SAP
table, read it once into a local table (`CREATE TABLE spfli AS SELECT * FROM sap_read_table('SPFLI');`) and
join locally.

## `ATTACH` — SAP as a DuckDB catalog

```sql
ATTACH '' AS sap (TYPE sap_rfc);                               -- default secret, on-demand tables
SELECT CARRID, CARRNAME FROM sap.SCARR;

ATTACH '' AS sap_named (TYPE sap_rfc, SECRET 'my_sap');        -- named secret
ATTACH '' AS sap_flight (TYPE sap_rfc, TABLES 'SFLIGHT,SPFLI,SCARR,S*BOOK');  -- exact names and globs
SHOW TABLES FROM sap_flight;
DETACH sap;
```

Attached tables are views over `sap_read_table`, so pushdown and types are identical. `ATTACH` accepts only
`TYPE`, `SECRET`, `TABLES`, `read_table_function` and `read_table_delimiter` — connection parameters such as
`USER`/`PASSWORD` belong in the secret, not in `ATTACH`. Names with a namespace need quoting:
`sap."/DMO/TRAVEL"`.

## Finding tables and fields

```sql
SELECT * FROM sap_show_tables(TABLENAME => '*FLIGHT*');        -- name pattern (* or %)
SELECT * FROM sap_show_tables(TEXT => '%booking%');            -- description text
SELECT pos, is_key, field, text, sap_type, length, decimals, check_table
FROM sap_describe_fields('SFLIGHT');
SELECT field, text FROM sap_describe_fields('SFLIGHT', LANGUAGE => 'D');   -- German texts
```

`sap_show_tables` returns `table_name`, `text` and `class` (`TRANSP`, `VIEW`, `POOL`, `CLUSTER`);
CDS entities are not in that list — `sap_ape_show` finds them. `sap_describe_fields` works for tables, DDIC
views and CDS views; `check_table` and `ref_table`/`ref_field` give foreign keys and the currency/unit field
of amounts.

## Types

| DDIC type | DuckDB | Note |
|---|---|---|
| CHAR, CLNT, LANG, CUKY, UNIT | VARCHAR | right-trimmed |
| NUMC, ACCP | VARCHAR | leading zeros kept |
| INT1 / INT2 / INT4 / INT8 | TINYINT / SMALLINT / BIGINT / BIGINT | |
| DEC, CURR, QUAN | DECIMAL(p, s) | precision capped at 38 |
| FLTP | DOUBLE | |
| DATS / TIMS | DATE / TIME | the initial value (`00000000` / `000000`) becomes NULL |
| UTCLONG, UTCS, UTCM | TIMESTAMP | |
| STRING, SSTR, LCHR | VARCHAR | needs `ET_DATA` (gotcha 4) |
| RAW, LRAW, RAWSTRING | BLOB | decoded from RFC_READ_TABLE's hex; e.g. RAW(16) UUIDs |

Amounts (`CURR`) are stored as in the database — their decimal places are not shifted for currencies like
JPY; join `ref_field` (e.g. `CURRENCY`) to interpret them. The full mapping is in API_REFERENCE.md
("SAP ↔ DuckDB Type Mapping"). An unknown RFC type becomes VARCHAR unless
`SET erpl_rfc_strict_type_check = true;`.

## Alternative reader function modules

```sql
-- Per query
SELECT * FROM sap_read_table('SFLIGHT',
    READ_TABLE_FUNCTION => '/SAPDS/RFC_READ_TABLE2', READ_TABLE_DELIMITER => '|');

-- For the session
SET erpl_rfc_read_table_function = '/SAPDS/RFC_READ_TABLE2';
SET erpl_rfc_read_table_delimiter = '|';
```

Precedence, highest first: per-query parameter → `ATTACH` option → secret field → session setting → default
`RFC_READ_TABLE`. Any read-only FM with the `RFC_READ_TABLE` interface works; it needs `S_RFC` for its
function group. Inspect one with `sap_rfc_describe_function('Z_MY_READ_TABLE')`.

## Decision table

| Situation | Do |
|---|---|
| Large table, few rows needed | Push a selective `WHERE` (key fields, dates) |
| Wide table (100+ columns) | Project the needed `COLUMNS`; if memory is the problem lower `erpl_rfc_fetch_size` |
| Narrow, very long table | `partitions => 4..8`, then measure |
| String column fails | Drop the column or use an `ET_DATA`-capable `READ_TABLE_FUNCTION` |
| Same SAP table used in many queries | Materialise once with `CREATE TABLE … AS` |
| Need change tracking | ODP or APE (`erpl-odp-replication`, `erpl-ape-cds`) |

## Canonical extraction

```sql
-- 1. find and understand the table
SELECT * FROM sap_show_tables(TABLENAME => 'SFLIGHT');
SELECT field, is_key, sap_type, text FROM sap_describe_fields('SFLIGHT');
-- 2. read only what is needed into a local table
CREATE OR REPLACE TABLE sflight_lh AS
SELECT CARRID, CONNID, FLDATE, PRICE, CURRENCY, SEATSMAX, SEATSOCC
FROM sap_read_table('SFLIGHT')
WHERE CARRID = 'LH';
-- 3. the connection is released when the scan ends
SELECT sap_rfc_live_connections();
```

See also: `erpl-choose-interface` (table vs CDS vs ODP), `erpl-performance` (threads, partitions,
fetch size), `erpl-rfc-functions` (BAPIs instead of raw tables), `erpl-pipelines` (landing reads in a
persistent database).
