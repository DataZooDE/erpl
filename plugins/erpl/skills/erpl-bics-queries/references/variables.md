# BEx variables in `sap_bics_begin`

Variables are filled when the session opens and are stored with it, so every later call on the session
stays restricted. Find what a query needs:

```sql
SELECT name, text, mandatory, input_enabled, is_exit_variable, var_type, selection_type, entry_type,
       reference_char
FROM sap_bics_variables('0D_FC_AE_VAR_Q02');
```

## `variables => LIST(STRUCT(NAME, SIGN, OP, LOW, HIGH))`

| Field | Rule |
|---|---|
| `NAME` | Technical variable name; matched upper case |
| `SIGN` | `I` include (default) or `E` exclude |
| `OP` | `EQ`, `NE`, `GT`, `GE`, `LT`, `LE`, `BT`, `NB`, `CP`, `NP`; default `BT` when `HIGH` is set, else `EQ` |
| `LOW` | Value or lower bound, in BW's internal format |
| `HIGH` | Upper bound — only with `BT`/`NB`; any other `OP` with a `HIGH` is rejected |

| `selection_type` | Rows allowed |
|---|---|
| `SINGLE_VALUE` | exactly one row, `OP = 'EQ'` |
| `INTERVAL` | exactly one row |
| `MULTIPLE_VALUES`, `SELECTION_OPTION` | repeat `NAME`, one row per value/range |
| `PRECALCULATED_VALUE_SET` | as BW defines it |

| `var_type` | Value |
|---|---|
| `CHARACTERISTIC_VALUE` | Member keys (`'2005'`, `'FC001'`, `'0000001001'`) |
| `FORMULA` | A plain number with `.` as decimal separator (`'2.5'`; `'2,5'` means 25 to BW) |
| `TEXT` | Text |
| `HIERARCHY` | The hierarchy name, via `hierarchy_variables` |
| `HIERARCHY_NODE` | A node, via `hierarchy_variables` — applying it as a node is not supported yet (erpl#227) |

Formats: dates `YYYYMMDD` (an ISO date `'2026-01-31'` is converted for date-typed variables), NUMC/ALPHA
keys with leading zeros. `entry_type = 'MANDATORY_NOT_INITIAL'` refuses an empty `LOW`.

## `hierarchy_variables => LIST(STRUCT(NAME, LOW, HIERARCHY_NAME, HIERARCHY_VERSION, HIERARCHY_DUE_DATE))`

`HIERARCHY_DUE_DATE` is `'YYYY-MM-DD'` or `'YYYYMMDD'`; empty means 9999-12-31 (the current hierarchy).
`variables` and `hierarchy_variables` can be combined.

## `variant => 'NAME'`

A saved BEx variant (`RSRVARIANTDIR`) fills the variables instead; an unknown name is an error listing the
query's variants.

## What erpl refuses, and why

- A variable that is not input-ready (customer-exit variables, InfoProvider sessions) — BW would ignore it
  and return an unrestricted result. The error lists the input-ready ones.
- More rows than the selection type allows — BW would silently use only the first.
- A value BW rejects, replaces by `#`, or changes on read-back — fails `sap_bics_begin` naming the variable.
- A mandatory variable left empty is reported when the result is read (a variant may still fill it).

## Trial-system fixtures (ABAP Platform Trial, cube `0D_FC_C03`)

| Query | Variables |
|---|---|
| `0D_FC_AE_VAR_Q02` | `0D_FC_P02` (0CALYEAR, single, mandatory), `0D_FC_P21` (0D_FC_PH2, single, mandatory) |
| `0D_FC_AE_VAR_Q03` | `0D_FC_S01`, `0D_FC_S09` (selection option / interval, mandatory) |
| `0D_FC_AE_VAR_Q01` | `0D_FC_P07` (date), `0D_FC_H06` (hierarchy node), `0D_FC_N09` (multi-value, optional) |

`0D_FC_C03` holds data for 2004–2008.
