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

| Cols | Rows                       | Query                              | Wall      | Outcome     |
|-----:|---------------------------:|-----------------------------------:|----------:|------------:|
| 31   | 1.9M (DD03L)               | `SELECT * … LIMIT 10`              | <1s       | passes      |
| 156  | 403 (RSDCUBE)              | `SELECT * … LIMIT 10`              | 0.37s     | passes      |
| 361  | 0 (DDDDLQUANTYPES)         | `SELECT * … LIMIT 10`              | 0.50s     | passes      |
| 395  | 50 000 (simple types)      | `SELECT * … LIMIT 10`              | 0.56s     | passes      |
| 400  | 100 000 (23 type families) | `SELECT * … LIMIT 10`              | 0.56s     | passes      |
| 400  | 100 000 (23 type families) | `SELECT * … LIMIT 10000`           | 0.76s     | passes      |
| 400  | 100 000 (23 type families) | `SELECT COUNT(*)`                  | 1.93s     | passes      |
| **400** | **100 000 (23 type families)** | **`SELECT *` (no LIMIT)**     | **hang / "Could not complete read task after 5 attempts"** | **REPRODUCES** |

`LIMIT N` keeps the scan narrow (only as many state machines as needed
for the projected columns + budget), so the LIMIT variants stayed
healthy.  The full-table form unmasks the bug: ERPL spins up one RFC
state machine per active column for column-parallel scan.  At 400
columns each opening a persistent connection, the SAP SDK's
`MAX_CPIC_CONVERSATIONS=200` ceiling is exceeded; the SDK fails further
opens with `RFC_COMMUNICATION_FAILURE "max no of 200 conversations
exceeded"`; ERPL's retry loop (5×, exponential backoff) eventually
gives up with the reporter's exact message.

## Root cause

`ExecuteNextTableReadForColumn` retries on `RFC_COMMUNICATION_FAILURE`
because most comm failures are transient.  But CPIC-cap exhaustion
**is not transient** — every column state machine in the same process
shares the same 200-conversation budget, so the failure persists for
the full 5 × 10s/20s/40s/80s/160s = ~5min retry envelope.

`v2026.05.22` made it slightly worse: persistent-connection caching
holds connections open for the whole scan, so the cap is hit faster
and held longer.

## Fix

A second commit on this branch caps the persistent-connection pool to
**16** per scan via the new `erpl_rfc_max_persistent_connections`
extension option.  State machines past the cap fall back to per-batch
open/close — slower than caching but never starves the CPIC budget.
16 is chosen with headroom: intra-process effective concurrency
against the SAP gateway plateaus around 3-4 anyway (see
`project_rfc_intra_process_throughput_cap` in memory), so 16 leaves
4× headroom while staying well clear of any plausible SAP-side limit.

Result on the same query that previously hung:

| Query                                              | Before                                  | After     |
|---------------------------------------------------:|----------------------------------------:|----------:|
| `COPY (SELECT * FROM ZWIDE_BSEG) TO '/dev/null'`   | 5min hang → "Could not complete..."     | **111s** ✓ |
| `SELECT * FROM ZWIDE_BSEG LIMIT 10`                | 0.56s                                   | 0.58s     |

LIMIT-shaped scans are unaffected because they never exceed the cap;
narrow-table scans see the full v2026.05.22 caching benefit.

We attempted 1 000 000 rows on the populator but ABAP
`rdisp/max_wprun_time` caps the trial run at ~33 s of work-process
time, so the harness materialises 100 000 rows.  That's still 13 ×
the column count and 3 × the depth at which DD03L behaved healthily
in the reporter's testimony.

## Clean-up

To drop the Z-objects from the trial after investigation, delete in this
order via `erpl-adt object delete <uri>`:

- `/sap/bc/adt/oo/classes/zcl_wide_bseg`
- `/sap/bc/adt/ddic/tables/zwide_bseg`
- `/sap/bc/adt/ddic/tables/zwide_smoke`

All artefacts live in `$TMP` so there are no transport requests to release.
