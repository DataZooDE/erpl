# BEx Variable Submission for ERPL BICS — Implementation Plan

Issue: [#96](https://github.com/DataZooDE/erpl/issues/96) — support BEx query variable
submission (`BICS_PROV_VAR_SET_VARIABLES`).

Status: **implemented** on branch `feat/issue-96-bex-variables` in the `bics` submodule.
See section 6 for what the live system did and did not allow us to assert.
Phase 0 (protocol discovery) is **done** — findings below are
verified live against the `a4h` ABAP trial on 2026-07-29 via `erpl-adt object run`.

---

## 1. Phase 0 findings (verified, not assumed)

### 1.1 The clean path is `BICS_PROV_OPEN`, not `BICS_PROV_VAR_SET_VARIABLES`

`BICS_PROV_OPEN` already accepts variable values at open time:

```
I I_T_VIEW_VARIABLE_VALUES     BICS_PROV_STATE_T_INIT_VARIABLES
I I_T_VIEW_SELECTION_STATE     BICS_PROV_STATE_T_INIT_VARIABLES
I I_T_VIEW_CHARACTERISTIC_HRY  BICS_PROV_STATE_T_INIT_VARIABLES
I I_VARIANT                    RSRVARI     " BEx variant name
```

Line type `BICS_PROV_STATE_INIT_VARIABLES`:

| Field | Type | Meaning |
|---|---|---|
| `NAME` | CHAR40 | variable technical name |
| `SIGN` | CHAR1 | `I` / `E` |
| `OPT` | CHAR2 | `EQ`, `BT`, `GE`, `LE`, `CP`, … |
| `LOW` | SSTR1333 | low value |
| `HIGH` | SSTR1333 | high value (interval) |
| `HIERARCHY_ACTIVE` | CHAR1 | hierarchy-node variable |
| `HIERARCHY_NAME` | CHAR40 | |
| `HIERARCHY_VERSION` | CHAR3 | |
| `HIERARCHY_DUE_DATE` | DATS8 | |
| `LOW_IS_EXIT_VARIABLE` / `HIGH_IS_EXIT_VARIABLE` | CHAR1 | |

This is **exactly** the `{SIGN, OPERATOR, LOW, HIGH}` shape the issue asked for, plus
hierarchy support. Multiple values = multiple rows with the same `NAME`.

**Consequence:** the core feature is *one additional table parameter on an RFC call we
already make*. No new handle lifecycle, no new call ordering, and it survives session
re-open for free because `OpenDataProvider()` is what re-runs on state restore.

`BICS_PROV_VAR_SET_VARIABLES` is the *interactive* path (fill the prompt on an already
open container). Its payload is **not** what the issue assumed: `BICS_PROV_STATE_VARIABLE`
is only `ID / VALUE_NAME / VALUE_TEXT / VALUE_NUMBER`; the selection itself travels
through `I_TSX_SELECTION` keyed by the variable's `BASE_SELECTOR_HANDLE`. Considerably
more marshalling for no user-visible gain. Recommend: **do not implement it in v1.**

Also present and relevant:
- `BICS_PROV_SUBMIT_VARIABLES` (`I_VARIABLE_CONTAINER_HANDLE` only) — finalizes the
  variable screen. Needed only if we adopt the interactive path.
- `BICS_PROV_CANCEL_VARIABLES`, `BICS_PROV_VAR_SET_HIERARCHY`, `BICS_PROV_VARIANT_*`.

### 1.2 `BICS_PROV_VAR_GET_VARIABLES` gives us everything for validation

`E_T_META_VARIABLE` (`BICS_PROV_META_VARIABLE`) carries `TECHNICAL_NAME`, `TEXT`,
**`MANDATORY`**, **`INPUT_ENABLED`**, `VARTYP`, `VPARSEL`, `ENTRYTP`,
`BASE_SELECTOR_HANDLE`, `REFERENCE_CHAR`, `IS_EXIT_VARIABLE`. The FM also exports
`E_INPUT_NECESSARY`. That is enough to produce the "clear error naming the variable"
the issue asks for, entirely client-side.

### 1.3 Test fixtures exist on the a4h trial — nothing has to be created

Probing all 90 `0D_*` demo queries for `INPUT_ENABLED='X' OR MANDATORY='X'`:

| Query | Cube | Variable | Mandatory | Kind (`vartyp`/`vparsel`) |
|---|---|---|---|---|
| `0D_FC_AE_VAR_Q02` | `0D_FC_C03` | `0D_FC_P02` | **X** | single value, `0CALYEAR` (1/P) |
| `0D_FC_AE_VAR_Q02` | `0D_FC_C03` | `0D_FC_P21` | **X** | single value, `0D_FC_PH2` (1/P) |
| `0D_FC_AE_VAR_Q03` | `0D_FC_C03` | `0D_FC_S01` | **X** | **selection option / interval** (1/S) |
| `0D_FC_AE_VAR_Q03` | `0D_FC_C03` | `0D_FC_S09` | **X** | selection option (1/S) |
| `0D_FC_AE_VAR_Q01` | `0D_FC_C03` | `0D_FC_P07` | **X** | single value, `0DATE` (1/P) |
| `0D_FC_AE_VAR_Q01` | `0D_FC_C03` | `0D_FC_H06` | **X** | **hierarchy node** (5/P) |
| `0D_FC_AE_VAR_Q01` | `0D_FC_C03` | `0D_FC_N09` | – | **multiple values** (2/M) |
| `0D_FC_AE_CONDITION_Q0015` | `0D_FC_C01` | `0D_FC_F01` | – | formula variable (4/P) |

All four variable classes the issue names — single, interval, multiple, hierarchy node —
are covered by real BW content. `E_INPUT_NECESSARY = X` on Q01/Q02/Q03.

The existing `sap_bics_variables_workflow.test` targets `0D_FC_NW_C01_Q0007/Q0009`, whose
only variable (`0D_NW_ACTCMON`) is an **exit** variable — `INPUT_ENABLED` blank,
`MANDATORY` blank. That is why those tests pass today and why they cannot detect this
feature. The new tests must use the `0D_FC_C03` queries.

### 1.4 One behavioural caveat

On this trial, `BICS_PROV_OPEN` with `I_STATE_VARIABLE_MODE = 'U'` **succeeds** for
mandatory-variable queries (returns valid data-provider and variable-container handles);
BW signals the unfilled prompt via `E_INPUT_NECESSARY = X` rather than refusing the open.
The reporter's system refuses at open. Both are handled by the same fix — we supply values
at open — but it means the *reporter's exact symptom* is not reproducible here. We
reproduce the equivalent failure one step later (unrestricted / failing result).

### 1.5 Discovery artifact

`ZCL_BICS_DISCO` (package `$TMP`, class, `IF_OO_ADT_CLASSRUN`) is currently on the trial
and reruns the whole probe. Delete when done, or keep as the phase-0 harness.

---

## 2. Design

### 2.1 Public API — mirrors the existing ODP select-option convention

ERPL already has an idiomatic shape for "a SAP range/select-option passed from SQL":
`sap_odp_read(filters => …)` takes `LIST<STRUCT(FIELDNAME, SIGN, OP, LOW, HIGH)>` mapped
onto `RODPS_REPL_S_SELECTION` (`odp/src/odp_fetch.cpp:24`, documented at
`API_REFERENCE.md:786`). Uppercase field names, plain `VARCHAR` members, every field
written out explicitly including empty `HIGH`.

BEx variables are the same animal, so they get the same shape — `NAME` instead of
`FIELDNAME` (it names a variable, not a field), mapped onto
`BICS_PROV_STATE_INIT_VARIABLES`:

```sql
SELECT * FROM sap_bics_begin(
    '0D_FC_C03', '0D_FC_AE_VAR_Q02',
    variables => [
        {'NAME':'0D_FC_P02','SIGN':'I','OP':'EQ','LOW':'2001','HIGH':''},
        {'NAME':'0D_FC_S01','SIGN':'I','OP':'BT','LOW':'20010101','HIGH':'20011231'},
        {'NAME':'0D_FC_N09','SIGN':'I','OP':'EQ','LOW':'A','HIGH':''},
        {'NAME':'0D_FC_N09','SIGN':'I','OP':'EQ','LOW':'B','HIGH':''}
    ]
);
```

`LIST` not `MAP`: BW variables are legitimately multi-valued, and a name-keyed map cannot
express two values for `0D_FC_N09`. Repeating `NAME` is how `vparsel='M'` works on the
wire, so the list mirrors the RFC table 1:1.

DuckDB struct casts require the exact field set, so all five members are mandatory —
consistent with how `filters` is already written throughout the ODP tests and docs.

**Hierarchy-node variables get their own parameter.** `BICS_PROV_STATE_INIT_VARIABLES`
carries four extra hierarchy fields, but folding them into the struct above would force
every caller to spell out nine members for the common case. Instead:

```sql
hierarchy_variables => [
    {'NAME':'0D_FC_H06','LOW':'<node>','HIERARCHY_NAME':'…','HIERARCHY_VERSION':'…',
     'HIERARCHY_DUE_DATE':'99991231'}
]
```

Both parameters marshal into the same `I_T_VIEW_VARIABLE_VALUES` table; the hierarchy rows
simply set `HIERARCHY_ACTIVE = 'X'`. (If hierarchy variables turn out to need
`BICS_PROV_VAR_SET_HIERARCHY` instead — see cycle 8 — only this parameter's backend
changes, not `variables`.)

Plus:
- `variant => 'MYVARIANT'` — passes `I_VARIANT`, one line, zero marshalling.
- `sap_bics_variables('0D_FC_C03', '0D_FC_AE_VAR_Q02')` — table function over
  `E_T_META_VARIABLE` so users can discover what to fill (name, text, mandatory,
  input_enabled, type, reference_char). Without it, `variables =>` is guesswork.

### 2.2 Internals

| File | Change |
|---|---|
| `bics/src/include/bics_variables.hpp` *(new)* | `BicsVariableSpec` struct + `BicsVariableSpecs` collection: parse from `duckdb::Value`, apply defaults, validate, marshal to the RFC table `Value`. |
| `bics/src/bics_variables.cpp` *(new)* | Implementation. Pure — no RFC, no `ClientContext` — so it is unit-testable offline. |
| `bics/src/bics.cpp` | `OpenDataProvider()` gains `I_T_VIEW_VARIABLE_VALUES` (+ `I_VARIANT`); `BicsSession` holds `_variables`; post-open `CheckVariableInput()` calls `VAR_GET_VARIABLES` and raises a named error when a mandatory variable is unfilled. |
| `bics/src/bics_state.cpp` / `bics_repository.cpp` | Persist the variable specs so `FromPersistedState()` replays them on re-open. |
| `bics/src/scanner_bics_query.cpp` | `variables` / `variant` named params on `sap_bics_begin`; plumb into the session. |
| `bics/src/scanner_bics_variables.cpp` *(new, optional)* | `sap_bics_variables()` metadata function. |
| `bics/src/erpl_bics_extension.cpp` | Register the new function + description/examples. |

**Persistence — decided: new column on `bics.bics_state`.** The table is
`(id, version, state)` (`bics_repository.cpp:291`); it gains a `variables` column holding
the serialized spec list. Because the table is created with `CREATE TABLE IF NOT EXISTS`,
a database that already has the old shape must be migrated — add an idempotent
`ALTER TABLE … ADD COLUMN IF NOT EXISTS variables …` to
`CreateBicsStateSchemaQuery()`, and make `FetchBicsState()` tolerate a NULL/absent value
(= no variables, current behaviour). This keeps BW-shaped state and ERPL-shaped state
separate.

---

## 3. Red/green TDD sequence

Every cycle is: write the failing test, run it, **see it fail for the stated reason**,
implement the minimum, see it pass, refactor. Two harnesses:

- offline C++ — `./build/debug/extension/erpl_bics/test/cpp/erpl_bics_tests "[bics_variables]"`
- live SQL — `make sql_tests_bics TEST_FILE=sap_bics_variables_set.test`

Never run these concurrently (corrupts the test binary), and never run the bare C++ bics
binary without cwd + SAP env — it locks `DEVELOPER`.

### Cycle 0 — reproduce (red, stays red until cycle 4)

New `bics/test/sql/sap_bics_variables_set.test`, marked with the current expectation:

```sql
statement error
SELECT * FROM sap_bics_begin('0D_FC_C03', '0D_FC_AE_VAR_Q02',
    variables => [{'NAME':'0D_FC_P02','SIGN':'I','OP':'EQ','LOW':'2001','HIGH':''}]);
----
Invalid named parameter "variables"
```

Red reason today: binder rejects `variables`. This is the executable statement of the bug.

### Cycle 1 — spec parsing (offline)

`bics/test/cpp/test_bics_variables.cpp`, tag `[bics_variables]`:
- `{'NAME','SIGN','OP','LOW','HIGH'}` → one row, values passed through verbatim
- empty `HIGH` with `OP='BT'` → `InvalidInputException` naming the variable
- duplicate `NAME` → two rows, order preserved
- empty `NAME` → `InvalidInputException` naming the offending list element
- wrong struct shape (missing/extra member) → DuckDB's own cast error, asserted so the
  message stays comprehensible

Green: `bics_variables.{hpp,cpp}` — parsing + defaults only.

### Cycle 2 — RFC marshalling (offline)

Same tag: spec list → `Value` matching `BICS_PROV_STATE_INIT_VARIABLES` — all 11 fields,
correct names and order, unset fields empty (not NULL), `HIERARCHY_ACTIVE='X'` iff the row
came from `hierarchy_variables`, `HIERARCHY_DUE_DATE` formatted `YYYYMMDD`.

Green: `ToRfcTable()`.

### Cycle 3 — open passes them through (live, single value)

```sql
query I
SELECT count(*) > 0 FROM sap_bics_begin('0D_FC_C03', '0D_FC_AE_VAR_Q02', id='v_q02',
    variables => [{'NAME':'0D_FC_P02','SIGN':'I','OP':'EQ','LOW':'2001','HIGH':''},
                  {'NAME':'0D_FC_P21','SIGN':'I','OP':'EQ','LOW':'...','HIGH':''}]);
```

Green: `OpenDataProvider()` gains the table param; `sap_bics_begin` gains the named param.
Cycle 0's test flips from "invalid parameter" to a real open.

### Cycle 4 — the restriction is actually applied (live) ← *the test that proves the feature*

Same query twice with different `0D_FC_P02` values; assert the result sets differ and that
each is confined to the requested year. Anything short of this passes even if we silently
drop the values on the floor.

```sql
query I
SELECT DISTINCT <year column> FROM sap_bics_result('v_q02_2001');
----
2001
```

Exact column/values to be pinned during the cycle by running the query once.

### Cycle 5 — diagnostics, without changing when we fail (live)

**Decided: ERPL does not invent its own mandatory-variable error.** A query that opens on
defaults today keeps opening on defaults. We only make BW's own refusal legible.

Two sub-cases, and the first step of this cycle is to find out empirically which one the
trial exhibits — open `0D_FC_AE_VAR_Q02` with no variables and run through to
`sap_bics_result`:

- **BW refuses** (the reporter's symptom) → the existing "Failed to open data provider…"
  path fires. Green: enrich `BicsResultDiagnostics()` so that when
  `E_INPUT_NECESSARY = 'X'`, the message appends the unfilled mandatory variables with
  their texts, e.g. `… unfilled mandatory BEx variables: 0D_FC_P02 (Year),
  0D_FC_P21 (Product hierarchy)`. Test asserts the variable name appears in the error.
- **BW runs on defaults** (what the a4h trial does at open) → no error to enrich. Then
  this cycle only asserts the non-regression: opening without `variables` still succeeds
  and returns the same rows as before the feature.

Independently, and cheap: a supplied `NAME` that BW does not report as `INPUT_ENABLED` is
a caller typo that BW ignores silently, so **that** we do reject with a named error. Test
asserts `variables => [{'NAME':'0D_FC_NOPE',…}]` errors listing the valid variable names.

Because we never invent a new failure, `sap_bics_variables_workflow.test` and the
`sap_bics_ao_workflow_*` tests must pass unchanged — run them as the regression gate.

### Cycle 6 — interval / select-option (live)

`0D_FC_AE_VAR_Q03`, `0D_FC_S01` with `low`+`high` → `OPT='BT'`; assert the result is
bounded by the interval. This is the reporter's date-interval case.

### Cycle 7 — multiple values (live)

`0D_FC_AE_VAR_Q01`, `0D_FC_N09` given twice → assert both members present, a third absent.

### Cycle 8 — hierarchy node (live)

`0D_FC_AE_VAR_Q01`, `0D_FC_H06` (`REFERENCE_CHAR = 0D_FC_SEMP`) via the
`hierarchy_variables` parameter. Needs a valid hierarchy from
`sap_bics_show_hierarchies('0D_FC_SEMP')` — pin it at the start of the cycle.

In scope for v1. This is the one type where the open-time table may prove insufficient and
`BICS_PROV_VAR_SET_HIERARCHY` (post-open, `I_VARIABLE_CONTAINER_HANDLE`) is required
instead. If the red test cannot be made green through `I_T_VIEW_VARIABLE_VALUES`, that
call is the fallback — it is the only place in v1 where a second RFC call may appear, and
it stays behind `hierarchy_variables` so `variables` is unaffected either way.

### Cycle 9 — persistence across statements (live) ← *the regression that will bite*

Split across separate SQL statements so the session is torn down and restored:

```sql
CREATE OR REPLACE TEMP TABLE s AS SELECT * FROM sap_bics_begin(..., variables => [...]);
SELECT * FROM sap_bics_rows('v_q02', '0D_FC_PH2', op='SET');
SELECT * FROM sap_bics_result('v_q02');   -- must still be restricted to 2001
```

Green: persist + replay in `FromPersistedState()`. Without this the feature works in a
one-liner and silently loses the restriction in exactly the chained workflow the reporter
posted.

### Cycle 10 — variant + metadata function (live)

`variant => …`, and `sap_bics_variables()` returning one row per variable with the
`MANDATORY`/`INPUT_ENABLED` flags. Add the C++ shape test offline first.

---

## 4. Delivery

- Work lands in the **`bics` submodule** on `feat/issue-96-bex-variables`; erpl-bics has no
  CI of its own, so it reaches CI via a parent `erpl` PR that bumps only the `bics`
  gitlink. Leave unrelated `duckdb`/`odp` gitlink moves unstaged.
- Docs: `API_REFERENCE.md` (new named params + new function), erpl.io `bics.md` — the
  "variable filling is not implemented" paragraph the reporter quoted must go.
- `CHANGELOG.md` entry.
- Reply on #96: correct the `BICS_PROV_VAR_SET_VARIABLES` assumption (the values ride on
  `BICS_PROV_OPEN`), confirm the API shape, and offer `RRW3_GET_QUERY_VIEW_DATA` via
  `sap_rfc_invoke` as the interim unblock.

## 5. Scope and effort

**v1 = one PR, everything in it**: single / interval / multiple values,
hierarchy-node variables, `variant =>`, and `sap_bics_variables()`.

Cycles 1–7 + 9: ~2–3 days. Cycle 8 (hierarchy): +1 day. Cycle 10: +0.5 day. Call it a
week including docs and review.

Uncertainty is concentrated in cycle 4 (pinning expected result values from live BW data),
cycle 8 (whether hierarchy variables need the post-open FM) and cycle 9 (state
persistence) — not in the RFC marshalling, which Phase 0 has already de-risked.


---

## 6. Implementation outcome

Built as planned, with three deviations forced by what the trial system actually does.

### What shipped

- `variables => LIST<STRUCT(NAME, SIGN, OP, LOW, HIGH)>` and
  `hierarchy_variables => …` and `variant => …` on `sap_bics_begin`, marshalled into
  `BICS_PROV_OPEN`'s `I_T_VIEW_VARIABLE_VALUES` (`bics_variables.{hpp,cpp}`).
- `sap_bics_variables(info_provider [, query])` over `E_T_META_VARIABLE`.
- Values persisted with the session state and replayed on re-open.
- 13 offline unit tests (`[bics_variables]`), 31 live SQL assertions.

### Deviation 1 — the restore path had to be fixed too

`BICS_PROV_GET_INITIAL_STATE` returns empty `QUERY_PROPERTIES` for query-based
sessions, so `FromPersistedState` re-opened with an empty InfoProvider and *every*
second statement against a query session failed with
`Failed to open data provider … for info provider ''`. This was pre-existing and
independent of variables — `sap_bics_begin('0D_FC_NW_C01_Q0009')` followed by
`sap_bics_result` reproduced it on master. The chained `begin → result` workflow is
exactly the reporter's SQL, so the fix is in scope: the InfoProvider and query names
are now persisted alongside the state (`info_provider` / `data_provider` columns) and
preferred over the state on restore.

### Deviation 2 — cycle 4 was blocked, then unblocked by fixing #99

Initially the decisive test — same query, two different values, assert the results differ
— was not achievable: every query with input-ready variables hit a result
materialization bug (`Could not convert string 'FC008' to DOUBLE`), which was filed as
[#99](https://github.com/DataZooDE/erpl/issues/99) and has since been **fixed in this
same PR**.

Root cause: the result schema was sized from the persisted state's axis tables while the
data was written using the result's own row-element count. `BICS_PROV_GET_INITIAL_STATE`
returns empty `E_T_STATE_ROWS_CHARS` for BEx query sessions, so the schema collapsed to
the column-axis leaves alone. Fixed by making the result set the single source of truth
for the axis width (`GetRowAxisNames`), with state-derived names retained whenever the
state agrees on the count.

Cycle 4 is therefore now asserted live, on cube data (2004..2008):

| `0D_FC_P02` | `0D_FC_P21` | `dyn_kf_1` | `dyn_kf_2` |
|---|---|---|---|
| 2005 | FC001 | 2.0 | NULL |
| 2004 | FC002 | NULL | 2.0 |
| 2004 | FC001 | NULL | NULL |

Three combinations, three different answers — the variables demonstrably reach BW.

### Deviation 3 — hierarchy variables are plumbed but not proven

`sap_bics_show_hierarchies(info_object='0D_FC_SEMP')` returns nothing on the trial: no
hierarchy is loaded for the characteristic that `0D_FC_H06` references. The parameter,
its marshalling and its validation are tested; an end-to-end hierarchy-node restriction
is not. `BICS_PROV_VAR_SET_HIERARCHY` remains the fallback if the open-time table proves
insufficient against a real BW.

### Cycle 5, as decided

No client-side mandatory-variable error was invented. BW's own refusal — a result with
no structure, which DuckDB surfaced as the internal error *"Table function must return
at least one column"* — now reads:

```
The BW server returned no result structure for this query. Unfilled mandatory BEx
variables: 0D_FC_P02 (…), 0D_FC_P21 (…). Supply them with sap_bics_begin(...).
```
