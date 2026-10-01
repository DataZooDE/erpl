# Troubleshooting

What to run, and what to paste into a bug report, when ERPL does not do what you expect.
Start with the first section: it is one query, and it is safe to share.

## BICS results that are empty, NULL or wrong: `sap_bics_result_stats`

`sap_bics_result_stats(state_id)` executes the result exactly like `sap_bics_result` and
returns **one row of counts and labels**. It never contains a cell value or a member text,
so it can go into a public ticket as is.

```sql
CREATE OR REPLACE TEMP TABLE s AS
    SELECT * FROM sap_bics_begin('<query or cube>', variables => [{'NAME':'<var>','SIGN':'I','OP':'EQ','LOW':'<value>','HIGH':''}]);
SET VARIABLE sid = (SELECT state_id FROM s LIMIT 1);

-- then whatever reproduces the problem, e.g.
SELECT * FROM sap_bics_rows(getvariable('sid'), '<characteristic>', op => 'ADD');

-- and finally
SELECT * FROM sap_bics_result_stats(getvariable('sid'));
```

What the columns tell you:

| Column(s) | Meaning |
|---|---|
| `error` | the error `sap_bics_result` would have raised, if any (cell budget exceeded, failed read) |
| `e_state`, `e_max_message_type`, `bw_messages` | BW's own verdict on the read: `e_state` 1 = data, 2 = executed but empty, 4 = larger than the cell budget; a message type `E` or `A` with the messages BW parked for the session |
| `no_authority` | BW opened the query without authority for the user in the secret |
| `n_rows`, `n_columns`, `data_columns`, `data_cells` | the shape BW returned; `data_cells = 0` with `n_rows > 0` means every measure is NULL, and BW did not send a single value |
| `cells_read`, `cells_placed`, `cells_null_valued`, `cells_dropped_*` | where the cells went; any `dropped` count is a mapping problem on our side, `null_valued` means BW sent cells without a value |
| `row_min/max`, `column_min/max` | the coordinate range BW used; a `column_max` above `data_columns` is a mapping problem |
| `bw_release`, `bw_support_package`, `bics_version`, `backend`, `duckdb_version`, `stream_result_tables`, `cell_value_rfc_type`, `rfc_user`, `rfc_host` | the environment, which is usually the first question anyway |

Two more things that help with an all-NULL result:

```sql
-- 1. the same stats with the SDK streaming path switched off
SET erpl_bics_stream_result_tables = false;
SELECT * FROM sap_bics_result_stats(getvariable('sid'));

-- 2. the extension versions
SELECT extension_name, extension_version FROM duckdb_extensions() WHERE extension_name LIKE 'erpl%';
```

If RSRT shows values for the same query and `data_cells` is 0 here, compare the analysis
authorizations of `rfc_user` with the user you used in RSRT: a technical RFC user without
them gets the layout and no data.

## Warnings in the trace

Anything ERPL decides to drop or work around is logged at `WARN`. These lines carry
counts and BW message texts, not data:

```sql
SET erpl_trace_enabled = true;
SET erpl_trace_level   = 'WARN';
SET erpl_trace_output  = 'file';            -- or 'console' / 'both'
SET erpl_trace_file_path = '/tmp/erpl.log';
```

Then run the failing statements and attach the `[WARN]` lines.

## The raw protocol: `erpl_bics_trace`

For a BICS problem the maintainers may ask for the exact request and response of each RFC
call. **These files contain your data** (cell values, member keys and texts, variable
values). Share them privately, or redact them.

```sql
SET erpl_bics_trace = true;
SET erpl_bics_trace_dir = '/tmp/erpl_bics_trace';
SET erpl_bics_stream_result_tables = false;  -- otherwise the big tables show as empty lists
```

One pair of files per call: `erpl_bics_trace.<timestamp>.<FUNCTION>.<conversation>.req.txt`
and `.resp.txt`. For a result problem the interesting one is `BICS_PROV_GET_RESULT_SET`
(`E_STATE`, `E_N_ROWS`, `E_N_COLUMNS`, `E_T_COLUMNS`, `E_T_ROWS`, `E_T_MEMBER`,
`E_T_DATA_CELLS`); for a session that cannot be navigated, `BICS_PROV_GET_INITIAL_STATE`.

## RFC problems: SDK trace

`sap_rfc_set_trace_level(level)` and `sap_rfc_set_trace_dir(dir)` switch on the NetWeaver RFC
SDK's own trace (`dev_rfc*.trc` / `rfc*.trc` files). Use it for connection, logon and
marshalling errors. The SDK trace also contains data.

## What to put in the ticket

1. The `sap_bics_result_stats` row (for BICS), or the exact error text.
2. The `[WARN]` lines of the trace, if any.
3. `duckdb_extensions()` versions, the DuckDB client (CLI, Python, ...) and OS.
4. The SAP release (`bw_release` from the stats row, or the kernel/component versions).
5. Whether the same query works in RSRT or Analysis for Office as the same user.
6. An offer of the private `erpl_bics_trace` files if asked.
