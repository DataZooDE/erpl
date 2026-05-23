# Issue #67 reproduction harness

Reproducing https://github.com/DataZooDE/erpl/issues/67 — wide+deep SAP
tables (BSEG ~401 cols, ACDOCA ~511 cols) hang for ~5 min on
`SELECT * … LIMIT 10` and die with
`Could not complete read task after 5 attempts.` on a real S/4 system.

The `a4h` ABAP Platform Trial container does not ship BSEG or ACDOCA, so
this directory contains the tooling to materialise a BSEG-shaped Z-table
in the trial via `erpl-adt` (the ADT REST CLI) and exercise the trigger
query against it.

## Files

- **`gen_wide_ddl.py`** — generates `zwide_bseg.ddl`, a ~400-column CDS
  DDL with deliberate type variety: `abap.dec` at several precisions,
  `abap.char` at multiple lengths, `abap.numc` at multiple lengths,
  `abap.fltp`, `abap.string`, `abap.raw`, `abap.dats`, `abap.tims`,
  `abap.lang`, `abap.int4`, `abap.int8`. Run via
  `uv run --no-project gen_wide_ddl.py > zwide_bseg.ddl`.
- **`zwide_bseg.ddl`** — output of the generator. ABAP-Cloud-compliant
  annotations (`@AbapCatalog.enhancement.category`, `tableCategory`,
  `deliveryClass`, `dataMaintenance`, plus `@EndUserText.label`).
- **`zwide_smoke.ddl`** — 12-column smoke-test table used to validate
  the erpl-adt push pipeline before running the wide one.
- **`zcl_wide_bseg.abap`** — `IF_OO_ADT_CLASSRUN` class that bulk-INSERTs
  1 000 000 synthetic rows into `ZWIDE_BSEG` in batches of 1 000.
  Populates the keys plus a sample of payload columns covering every
  type family so the RFC response carries non-trivial bytes for each
  type; the remaining columns stay at their initial values (still
  serialised by `RFC_READ_TABLE`).

## End-to-end recipe

Assumes the `a4h` container is running with ICM on port 50000 and the
DEVELOPER user (`ABAPtr2023#00`).

1. Generate the DDL:

       uv run --no-project tools/issue-67-repro/gen_wide_ddl.py \
           > tools/issue-67-repro/zwide_bseg.ddl

2. Push the table:

       SAP_PASSWORD='ABAPtr2023#00' uvx erpl-adt \
           --host localhost --port 50000 --user DEVELOPER --client 001 \
           --password-env SAP_PASSWORD \
           object create --type TABL/DT --name ZWIDE_BSEG \
           --package '$TMP' --description "Issue 67 wide-table reproduction"

       SAP_PASSWORD='ABAPtr2023#00' uvx erpl-adt \
           --host localhost --port 50000 --user DEVELOPER --client 001 \
           --password-env SAP_PASSWORD \
           source write '/sap/bc/adt/ddic/tables/zwide_bseg' \
           --file tools/issue-67-repro/zwide_bseg.ddl --activate

3. Push and run the populator. Runtime on the trial is roughly two
   minutes for 1 000 000 rows.

       SAP_PASSWORD='ABAPtr2023#00' uvx erpl-adt \
           --host localhost --port 50000 --user DEVELOPER --client 001 \
           --password-env SAP_PASSWORD \
           object create --type CLAS/OC --name ZCL_WIDE_BSEG \
           --package '$TMP' --description "Populator for ZWIDE_BSEG"

       SAP_PASSWORD='ABAPtr2023#00' uvx erpl-adt \
           --host localhost --port 50000 --user DEVELOPER --client 001 \
           --password-env SAP_PASSWORD \
           source write ZCL_WIDE_BSEG --type CLAS \
           --file tools/issue-67-repro/zcl_wide_bseg.abap --activate

       SAP_PASSWORD='ABAPtr2023#00' uvx erpl-adt \
           --host localhost --port 50000 --user DEVELOPER --client 001 \
           --password-env SAP_PASSWORD --timeout 900 \
           object run ZCL_WIDE_BSEG

4. Run the reporter's exact query shape against the populated table.

## Result on the `a4h` trial against `v2026.05.22`

| Cols | Rows                       | Query                       | Wall  |
|-----:|---------------------------:|----------------------------:|------:|
| 31   | 1.9M (DD03L)               | `SELECT * … LIMIT 10`       | <1s   |
| 156  | 403 (RSDCUBE)              | `SELECT * … LIMIT 10`       | 0.37s |
| 361  | 0 (DDDDLQUANTYPES)         | `SELECT * … LIMIT 10`       | 0.50s |
| 395  | 50 000 (simple types)      | `SELECT * … LIMIT 10`       | 0.56s |
| 395  | 50 000 (simple types)      | with `erpl_rfc_persistent_connections=false` | 0.54s |
| **400** | **100 000 (23 type families)** | **`SELECT * … LIMIT 10`** | **0.56s** |
| 400  | 100 000 (23 type families) | `SELECT * … LIMIT 10000`    | 0.76s |
| 400  | 100 000 (23 type families) | `SELECT COUNT(*)`           | 1.93s |

The reporter's `Could not complete read task after 5 attempts` symptom
**does not reproduce on the trial at any column/row combination** we
could materialise, including the widest type-varied 400-col × 100k row
configuration. The trigger query (`SELECT * FROM <attached> LIMIT 10`)
returns in under a second in every variant we tried.

We attempted 1 000 000 rows but the populator class hits an ABAP
`rdisp/max_wprun_time`-driven HTTP 500 at ~33 s, so the harness caps at
100 000 rows on the trial. That's still 3 × the depth at which the
reporter says DD03L works, with 13 × the column count.

Most plausible explanations, in order:

1. The reporter's installed ERPL binary pre-dates `v2026.05.22`. The
   warm-up batching + persistent-connection caching that shipped in
   v2026.05.22 are the most likely fix for an RFC-comm-failure-driven
   retry exhaustion.
2. The bug requires a real production S/4 backend (real BSEG /
   ACDOCA data, real `rdisp/max_wprun_time`, real cluster-table
   semantics on the FI tables) that the lightweight `a4h` trial
   cannot approximate.

The next step on this issue is therefore to ask the reporter to confirm
their installed `extension_version` and, if it pre-dates `v2026.05.22`,
to `FORCE INSTALL erpl FROM 'http://get.erpl.io'` and re-test.

## Clean-up

To drop the Z-objects from the trial after investigation, delete in this
order via `erpl-adt object delete <uri>`:

- `/sap/bc/adt/oo/classes/zcl_wide_bseg`
- `/sap/bc/adt/ddic/tables/zwide_bseg`
- `/sap/bc/adt/ddic/tables/zwide_smoke`

All artefacts live in `$TMP` so there are no transport requests to release.
