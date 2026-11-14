# Working with a BICS result

`sap_bics_result` returns a table: row-axis label columns, `<char>_HIER_LEVEL` columns, display-attribute
columns, then one DOUBLE per column-axis tuple. Persist it first, then shape it locally — every further
`sap_bics_result` call re-executes the query on BW.

```sql
-- check:skip-live (needs an open session 'q1')
CREATE OR REPLACE TABLE r AS SELECT * FROM sap_bics_result('q1');
DESCRIBE r;
```

## Remove totals and hierarchy-level columns

Result rows carry BW's result text as label (`Overall Result`, `Result`, or the logon-language equivalent
such as `Gesamtergebnis`). Hide them in BW instead of guessing the text:

```sql
-- check:skip-live (needs an open session 'q1')
SELECT * FROM sap_bics_set_char_prop('q1', '0D_NW_CNTRY', 'TOTALS', 'HIDE');
```

or locally with `SELECT * EXCLUDE (COLUMNS('_HIER_LEVEL$')) FROM r`.

## Long format (one row per label × measure)

```sql
-- check:skip-live (needs the table r)
UNPIVOT (SELECT * EXCLUDE (COLUMNS('_HIER_LEVEL$')) FROM r)
ON COLUMNS(* EXCLUDE ("0D_NW_CNTRY"))
INTO NAME measure VALUE amount;
```

## Terminal crosstab

For a readable crosstab put one characteristic on the rows and one on the columns in BW — the result *is*
the crosstab:

```sql
-- check:skip-live (illustrative characteristic names)
SELECT * FROM sap_bics_columns('q1', '0CALYEAR', op => 'SET');
CREATE OR REPLACE TABLE r AS SELECT * FROM sap_bics_result('q1');
```

Or pivot a long result in DuckDB: `PIVOT long_r ON measure USING sum(amount) GROUP BY "0D_NW_CNTRY"`.
In the CLI, `.mode box` (or `-box`) and `.maxrows` give a compact view; round key figures with
`round(col, 2)`.

## Charts, dashboards, APIs (other DataZoo plugins)

| Goal | Skill (plugin) | Install |
|---|---|---|
| SVG chart from the result table | `anofox-visualization-charts` (anofox-visualization) | `/plugin marketplace add DataZooDE/anofox-visualization` → `/plugin install anofox-visualization@anofox-visualization` |
| Dashboard over persisted results | `anofox-visualization-dashboards`, `anofox-visualization-serving` | same |
| Serve the result as REST/MCP | `flapi-rest`, `flapi-mcp` (flapi) | `/plugin marketplace add DataZooDE/flapi` → `/plugin install flapi@flapi` |

Hand those skills a persisted table (`r` above), never a live `sap_bics_result(...)` call.

## Export

```sql
-- check:skip-live (needs the table r)
COPY r TO 'result.parquet' (FORMAT parquet);
COPY r TO 'result.csv' (HEADER, DELIMITER ';');
```

Excel: `INSTALL excel; LOAD excel; COPY r TO 'result.xlsx' WITH (FORMAT xlsx, HEADER true);`.
