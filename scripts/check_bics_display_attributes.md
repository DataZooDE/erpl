# BICS display-attribute regression (#179)

An acceptance check for
[issue #179](https://github.com/DataZooDE/erpl/issues/179), run against *installed*
extensions, so it checks the binaries users get rather than a debug build. The fix
lives in the private `DataZooDE/erpl-bics` submodule; its own regression test is
`bics/test/sql/sap_bics_display_attributes.test`.

## On the a4h trial

The defaults target the `ZERPL_Q179_ATTR` fixture that
`scripts/sap/provision.d/41-bics-q179-query.sh` saves on the a4h trial: `0D_NW_PROD` on
rows with display attribute `0D_NW_PRDGP` active in the query definition, so member
`CN0F17` must come back with `0D_NW_PROD__0D_NW_PRDGP = 'MON1'`. With the `ERPL_SAP_*`
variables the SQL test suites use:

```sh
./scripts/sap/provision.sh --only 41
uv run --no-project --with duckdb==1.5.6 python -I scripts/check_bics_display_attributes.py \
  --init-from-env --report /tmp/bics-display-attributes.json
```

`--init-from-env` installs `erpl` from get.erpl.io (`--repository` to change) into the
connection and creates the secret from `ERPL_SAP_*`. The trampoline extracts its
sub-extensions into `~/.duckdb/extensions`; if a local build left differently built
`erpl_*` binaries there, loading fails with "Metadata mismatch", so run with a fresh
`HOME` or clear them. Releases up to v2026.10.28 fail with
`Missing result column '0D_NW_PROD__0D_NW_PRDGP'`; v2026.11.05 passes.

`--report` writes the JSON report to a file: the trampoline prints its load banner to
stdout.

## On another BW system

## Fixture prerequisite

Pass `--cube`, `--query`, `--member-column`, `--member`, `--attribute-column` and
`--expected` for your own fixture. For the reported query, a small disposable BW query
with no key figures (`ZMAT_Q002`):

- Row characteristic `ZMATERIAL`, with member `M1` / text `Material1`.
- Time-independent display attribute `ZMAT_GRP`, with value `GRP1` for `M1`.
- Another attribute `ZMAT_TYPE`, with value `T01`, left inactive.
- **Save `ZMAT_GRP` as active in the query definition**, then open a fresh session
  in RSRT and confirm it appears without manually activating it there. Record
  this confirmation with the report. The issue explicitly leaves this unverified.

`sap_bics_describe(...).characteristics[].attributes` establishes availability,
not activation. Reading `/BIC/PZMATERIAL` establishes master-data values, not
whether BICS selected them. Neither alone establishes this fixture prerequisite.
Do not activate all discoverable attributes to make the test pass.

## Run

Use a Python environment with `duckdb` matching the installed ERPL binaries.
Create an **untracked local** initialization file containing the normal `LOAD`
statements and `CREATE SECRET ... (TYPE sap_rfc, ...)` for the test system. It is
executed in the same in-memory connection as the check. Extension installation
and credentials are deliberately left to the operator; the script does not
install or upgrade anything. The query must open without mandatory variable
prompts and use the default SAP secret selected by the BICS functions.

```sh
python scripts/check_bics_display_attributes.py \
  --init-sql /private/path/bw-init.sql --cube '<CUBE>' --query ZMAT_Q002 \
  --member-column ZMATERIAL --member 'M1 Material1' \
  --attribute-column ZMATERIAL__ZMAT_GRP --expected GRP1 \
  --report /private/path/bics-display-attributes.json
```

A display attribute column is named `<char>__<attr>` (see `sap_bics_attributes` in
`API_REFERENCE.md`), e.g. `--attribute-column ZMATERIAL__ZMAT_GRP` for the reported query.
Use `--member` and `--expected` for the exact formatted values produced by the
query's display settings. The check does not change display/axis state, so it
exercises the saved query definition, including the no-key-figures case.

Success exits zero; a missing column, absent member, or incorrect/null attribute
exits nonzero. Connection, metadata and RFC errors also exit nonzero and must
not be interpreted as a reproduction of the missing-column bug. The script
reads the full result: use only the small fixture above, not a production query.

If a diagnostic stage fails, the script emits the partial report with a
`failed_stage` field, then re-raises the original error. Failures before the
diagnostic run (import, connection or initialization SQL) have no report.

Output contains extension versions, design/session metadata, result statistics,
the bound schema, row count, and rows for the fixture member. Treat this report
as private until inspected: metadata and statistics may identify the system/user,
and matching rows contain data. Credentials are not deliberately printed, but
SDK/SQL errors and any tracing enabled by the initialization file may contain
sensitive details. Do not commit the initialization file or raw traces.

For warning output, optionally add to the initialization file:

```sql
SET erpl_trace_enabled = true;
SET erpl_trace_level = 'WARN';
```

Trace output can mix with stdout, so disable console tracing when collecting a
machine-readable JSON report, or capture diagnostics separately.

## Maintainer investigation and acceptance criteria

Trace the same characteristic and attribute identities through these boundaries;
these are investigation targets, not a claim about private source symbols:

1. Compare design-time metadata with the state returned when opening the saved
   query. Establish whether the active display-attribute selection survives.
2. Inspect the raw BICS response for attribute descriptors and member values.
   If absent, investigate state restoration/request selection. If present,
   investigate decoding, schema binding and row materialization separately.
3. Resolve attributes by parent characteristic **and** attribute identity. Avoid
   positional matching or inferring attribute values from formatted member text.
4. Emit stable, collision-safe names and consistent schema/value order. Preserve
   characteristic and hierarchy-level columns, including zero-key-figure queries.
5. Preserve active selections across session operations. If explicit activation
   is needed, define/document a public API separately; do not silently treat
   every available metadata attribute as active.

The companion script checks the original missing-column/value symptom. Inside
`erpl-bics`, add native regression fixtures for inactive attributes staying absent,
multiple attributes/parents (including repeated attribute names), null values,
empty results, totals/hierarchy rows, and state round trips. Check both streamed
and non-streamed result paths where applicable. Empty results must preserve the
same bound schema even though this live fixture intentionally requires `M1`.
Also verify ordinary queries without attributes keep their existing output.

Do not substitute client-side master-data joins: those would need to reproduce
BW key-date, compound-key, authorization and formatting semantics. A public API
or result-shape change should update `API_REFERENCE.md` alongside the private
implementation and its submodule pin. This contribution leaves both unchanged.

## Offline verification

```sh
python3 -m unittest discover -s scripts/tests -v
```

These tests validate the acceptance oracle against missing columns, incorrect or
null values, empty results and wrong members. Connection doubles also exercise
report generation/filtering, partial output on failures, session-ID validation,
exception propagation and CLI success/failure reporting. They do **not** validate SAP/BICS
behavior. No live BW verification is claimed by this PR.
