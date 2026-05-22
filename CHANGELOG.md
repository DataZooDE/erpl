# Changelog

All notable changes to ERPL are documented here. Versions follow `vYYYY.MM.DD`
(the date the binary set was cut). Same-day re-cuts append `.N`.

Each bullet is tagged with the affected sub-extension(s):

- **[rfc]** — `erpl_rfc`, the SAP RFC scan path
- **[bics]** — `erpl_bics`, BW BICS queries (lives in a git submodule)
- **[odp]** — `erpl_odp`, ODP data replication (lives in a git submodule)
- **[tunnel]** — `erpl_tunnel`, SSH tunneling
- **[trampoline]** — `erpl`, the umbrella extension that bundles the SAP SDK
- **[all]** — cross-cutting work that touches every sub-extension

Binaries are self-distributed via [get.erpl.io](http://get.erpl.io) for the
matrix `{linux_amd64, linux_amd64_musl, osx_amd64, osx_arm64, windows_amd64} ×
{DuckDB v1.4.4, v1.5.3}`. Install with:

```sql
INSTALL erpl FROM 'http://get.erpl.io';
LOAD erpl;
```

---

## v2026.05.22 — DuckDB 1.5.3 support, wide-table memory fix, NULL date handling

First tagged release. Aggregates work landed on `master` over the past month,
including the DuckDB 1.5.3 build matrix bump and two reported correctness
fixes against the SAP RFC scan path.

### Scan path

- **[rfc]** **Memory regression on wide-table `LIMIT N` queries fixed (issue
  #63, PR #65).** `SELECT * FROM <attached>.<wide_table> LIMIT 10` against a
  350-column SAP table previously consumed > 16 GB of RAM and OOMed; it now
  lands in a few hundred MB. Three layers:
  - `RfcReadColumnStateMachine` now uses **divisibility-preserving warm-up
    batching**: each scan starts at `STANDARD_VECTOR_SIZE = 2048` rows and
    doubles only at moments when the running offset is divisible by the new
    size, preserving the ABAP server's `ROWSKIPS % ROWCOUNT == 0` invariant
    while keeping the first batch tiny so `LIMIT N` queries terminate cheaply.
  - **Persistent per-state-machine RFC connection + `RfcGetFunctionDesc`
    cache**, behind the new `erpl_rfc_persistent_connections` toggle
    (default `true`). Cuts the number of `RfcOpenConnection` round-trips per
    scan by ~80 %. Thread-affine so the SAP NW RFC SDK's "one connection
    per thread" rule stays satisfied when DuckDB's task scheduler routes a
    later batch to a different worker.
  - **LOAD-path data-copy elimination.** The hot loop that hands batches to
    DuckDB used to duplicate the full `LIST<VARCHAR>` children vector on
    every chunk delivery. Allocations in `LoadNextBatchToDuckDBColumn`
    dropped 95.6 % and temporary allocations 99 % (heaptrack).
- **[rfc]** **NULL DATS / TIMS values now round-trip as SQL NULL (issue #64,
  PR #66).** Empty / all-zero / all-spaces SAP date and time fields
  previously surfaced as `1970-01-01` or as wild values like
  `1439716-08-13`; they now produce proper NULL with type `DATE` / `TIME`.
- **[rfc]** **`MAX_ROWS = N` no longer rejects with `OPTION_NOT_VALID`** when
  N is between `STANDARD_VECTOR_SIZE` and `MAX_BATCH_SIZE` (Codex review
  follow-up to #65).
- **[rfc]** **SAP tables exposed as real `TableCatalogEntry` instead of
  view-wrapping** (PR #65). Unlocks projection pushdown into the underlying
  RFC scan when using ATTACH; earlier the view body hid the table function
  from the optimizer.

### Configuration

- **[rfc]** New option `erpl_rfc_persistent_connections` (default `true`) —
  toggle the RFC connection + function descriptor cache.

### Tests

- C++ unit suite: 297 assertions across 81 cases (full `erpl_rfc_tests` run).
- SQL suite against the ABAP Platform Trial: `sap_read_table.test` 55/55,
  `sap_rfc_attach.test` 37/37, `sap_rfc_attach_limit.test` 6/6 (new — covers
  the ATTACH + `LIMIT N` path end-to-end), `sap_rfc_invoke.test` 87/87.

### Known residuals

- Inside `libsapnwrfc.so`, `RfcTable::resetWithCapacity` still allocates the
  per-call SDK row buffer sized to `ROWCOUNT`. The warm-up keeps `ROWCOUNT`
  small for `LIMIT N`; for bulk scans this cost is unavoidable from the
  client side.
- `RfcResultSet` still materialises every cell as `duckdb::Value`. Writing
  directly into `FlatVector` would shave another ~30 % off the conversion
  path. Tracked as follow-up.

---

## v2026.05.20 — DuckDB v1.5.3 support

- **[all]** **DuckDB v1.5.3 added to the build matrix** (PR #61). Every
  subsequent tagged release ships binaries for both DuckDB v1.4.4 (LTS) and
  v1.5.3.
- `extension-ci-tools` submodule intentionally not bumped — same precedent
  as the v1.5.0 → v1.5.2 bump.

---

## v2026.05.17 — `sap_read_table` parallelism, `sap_rfc_invoke` hardening, and BICS AfO-style char properties

A big week — RFC robustness improvements landed alongside a meaty BICS
release pulled in via submodule bumps.

### RFC

- **[rfc]** **Five-round fuzz/verification pass against the ABAP Cloud
  Developer Trial** (PR #60). Twelve distinct bugs surfaced and fixed
  across the RFC scanner and `sap_rfc_invoke` marshalling, plus two
  previously-stubbed code paths implemented and one missing fallback added.
- **[rfc]** **`/SAPDS/RFC_READ_TABLE2` TBLOUT auto-discovery**: the scan
  now walks `/TBLOUT128 → /TBLOUT512 → /TBLOUT2048 → /TBLOUT8192 →
  /TBLOUT30000` on the same RFC invocation when the bind-time-chosen output
  table is empty for the projected row width.
- **[rfc]** **`sap_rfc_invoke` correctness**: empty function signatures,
  NULL, BLOB, path-to-scalar, positional arguments, and missing BYTE /
  UTCLONG I/O all handled correctly.
- **[rfc]** **`sap_rfc_describe_function` SDK-only fallback** when RPY
  trips an FL180 short-dump on older ABAP stacks.
- **[rfc]** 95 new test assertions across new and existing test files
  (57 fuzz + 14 + 7 + 17).

### BICS

- **[bics]** **`sap_bics_set_char_prop(state, char, prop, value)`** — new
  function for AfO-style characteristic properties: display KEY / TEXT /
  BOTH, sort ASC / DESC / NONE, totals SHOW / HIDE.
- **[bics]** **AfO-style hierarchy workflow** test coverage (query Q0010).
- **[bics]** **DisplayMode-aware result rendering** — TEXT for `0CALMONTH`,
  BOTH for `0D_NW_DIV` and similar pre-configured InfoObject properties
  are now honoured by the result parser.
- **[bics]** **Variable-less queries supported** — `E_VARIABLE_CONTAINER_HANDLE
  = '0000'` is now treated as legitimate rather than an error.
- **[bics]** **Multi-structure characteristic lookup** + **dynamic
  key-figure fallback** in result rendering.
- **[bics]** **`/E_T_COLUMNS` column-index map fixed** after
  `sap_bics_filter` injects structural header entries.
- **[bics]** **`bics-tui`** — Terminal UI example with Background Filter +
  Properties panel + SQL recorder, plus screenshots, capture script, and
  `__main__.py` entrypoint.
- **[bics]** New AfO workflow + property-regression test coverage.

### Docs

- `API_REFERENCE.md` updated with the new `sap_bics_set_char_prop` function,
  its prop/value matrix, and a sample end-to-end workflow combining filter
  + set_char_prop + result.

---

## v2026.05.14 — Type-system robustness, catalog-qualified function calls, ODP signature fix

### RFC

- **[rfc]** **VARCHAR fallback for unsupported SAP RFC types (issue #53,
  PR #59)**. Fields whose RFC type ERPL cannot natively map are surfaced as
  `VARCHAR` instead of failing the bind. Strict behaviour available via
  `SET erpl_rfc_strict_type_check = true`.
- **[rfc]** **`RFCTYPE_INT8` mapping fixed**: previously mapped to
  `LogicalType::INTEGER` (32-bit), now correctly `LogicalType::BIGINT`
  (64-bit) — matching `rfc2duck(RFC_INT8)` which already returned
  `Value::BIGINT()`.
- **[rfc]** 15 missing `ConvertRfcValue` cases filled in the schema switch.
- **[rfc]** **Catalog-qualified SAP function calls (issue #55, PR #58)**.
  After `ATTACH '' AS sap_s4 (TYPE sap_rfc, SECRET 'my_secret')` the seven
  SAP table functions become accessible as `sap_s4.sap_rfc_invoke(...)`,
  `sap_s4.sap_read_table(...)`, etc. The attachment secret is injected
  automatically. Multiple ATTACHes to different SAP systems coexist in one
  session, each routing through its own secret.

### BICS

- **[bics]** Submodule bumped — BICS metadata scanners now use real RFC
  table schemas; `RfcReadTableHelper` fixes from the rfc-side type work
  cascade through.

### ODP

- **[odp]** Consistent `SECRET` named parameter across `sap_odp_*`
  functions.
- **[odp]** Named parameters enumerated in function descriptions.
- **[odp]** `sap_odp_drop` documented with its real signature —
  `PRAGMA sap_odp_drop(odp_context, subscriber_name, subscriber_process,
  odp_name)`. The stale `PRAGMA odp_drop(subscription_id)` placeholder is
  removed from `API_REFERENCE.md`.

### Configuration

- **[rfc]** New option `erpl_rfc_strict_type_check` (default `false`).

---

## v2026.05.10 — Process stability fixes

- **[all]** **No more SIGSEGV when ERPL is loaded twice in the same
  process (issue #52, PR #56)**. Touches the trampoline plus every
  sub-extension. Three root causes addressed: trampoline self-pinning via
  `dlopen(RTLD_NODELETE)` so `dlclose` cannot drop the SAP SDK's
  `RTLD_GLOBAL` entries; ordering fixes around sub-extension teardown;
  protection against double initialisation of the telemetry singleton.
- **[all]** **`SapDefaultGenerator` no longer disabled after the first
  scan (issue #54, PR #57)**. Subsequent SAP table lookups in the same
  session keep working when other extensions (e.g. Ducklake) are also
  attached. Root cause was DuckDB's `CatalogSet::CreateDefaultEntries()`
  setting `created_all_entries = true` on the empty loop, after which our
  generator was never consulted again. Fix touches `sap_storage` in all
  sub-extensions that register a generator.

---

## v2026.05.02 — Build pipeline hardening, OpenSSL/Windows fixes

- **[rfc]** **`erpl_rfc` function metadata** (PR #51). `description`,
  `examples`, `categories`, and `parameter_names` registered on all
  `erpl_rfc` table functions so they appear in `duckdb_functions()` with
  rich documentation. (The same metadata for `erpl_bics`, `erpl_odp`, and
  `erpl_tunnel` landed earlier in v2026.04.09.)
- **[all]** **Post-build smoke tests across the full matrix**
  (`scripts/smoke-test.sh`, `smoke-test-musl.sh`, `smoke-test.ps1`).
  Download the matching DuckDB CLI, install the built artifact, and verify
  the SAP functions register correctly. Integrated into CI for all 5
  platforms × 3 DuckDB versions.
- **[all]** **OpenSSL `atexit` shutdown crash fixed** in *every*
  sub-extension. `openssl/ssl.h` registers an `atexit` cleanup hook on
  first use; combined with the SAP SDK's `RTLD_GLOBAL` symbol layout it
  triggered a SIGSEGV on extension unload. Suppression applied in
  `erpl_rfc`, `erpl_bics`, `erpl_odp`, `erpl_tunnel`, and the trampoline.
- **[all]** **Windows macros (`ERROR`, `WARN`, `INFO`, …) undef'd after
  the openssl include** in all sub-extensions — wingdi/windows.h macros
  were shadowing log-level identifiers.
- **[bics, odp]** **`parameter_types` removed from `FunctionDescription`**
  (root-cause fix) and then **restored** after the removal introduced a
  new v1.5.1 crash. Net result: both `erpl_bics` and `erpl_odp` keep the
  field, with the right type matrix.

---

## v2026.04.16 — DuckDB v1.5.2

- **[all]** **DuckDB v1.5.2 added** (PR #50). v1.4.4 LTS build unchanged.

---

## v2026.04.09 — `duckdb_functions()` metadata across all sub-extensions

- **[bics, odp, tunnel, trampoline]** **All 47 public table functions
  wrapped with `CreateTableFunctionInfo`** (PR #48). `description`,
  `examples`, `categories`, and `parameter_types` now populated in
  `duckdb_functions()`. `loader.SetDescription()` wired into each
  `LoadInternal()` so `duckdb_extensions()` output is complete too.
  (The corresponding `erpl_rfc` metadata work landed in v2026.05.02.)
- **[all]** Removes the deprecated `beads/bd` issue tracker, the
  pre-commit / post-merge hooks, and tracked `.beads/` files. `CLAUDE.md`
  updated to forbid AI attribution in commit messages.

---

## v2026.03.28 — DuckDB v1.5.1

- **[all]** **DuckDB v1.5.1 added** to the build matrix. v1.4.4 LTS and
  v1.5.0 builds unchanged at this point. (DuckDB v1.5.1 introduced a
  parameter-type-validation crash that was later worked around in
  v2026.05.02; v1.5.0 was dropped some weeks later when v1.5.2 landed.)

---

## v2026.03.11 — DuckDB v1.5.0 support

- **[all]** **DuckDB v1.5.0 added** (PR #47); v1.4.3 dropped; v1.4.4
  retained as LTS.
- **[all]** `extension-ci-tools` submodule moved to its v1.5.0 branch.
- **[rfc]** `rfc/src/sap_storage.cpp`: uses new
  `StorageExtension::Register()` static method — `config.storage_extensions[]`
  map was removed in v1.5.
- **[rfc, bics, odp]** Test CMakes link `dummy_static_extension_loader` —
  required by v1.5 because `LoadAllExtensions` moved out of
  `libduckdb_static`.

---

## v2026.02.11 — ATTACH syntax for SAP RFC, CDS view fix, BICS/ODP hardening

### RFC

- **[rfc]** **`ATTACH '' AS sap (TYPE sap_rfc, SECRET 'my_secret')` syntax
  (PR #45)**. Implements `StorageExtension` for the `sap_rfc` type so SAP
  systems can be ATTACHed as catalogs. A `DefaultGenerator` lazily creates
  views wrapping `sap_read_table()` for on-demand table resolution.
  - `SECRET` named parameter on `sap_read_table` for explicit secret
    selection.
  - Optional `TABLES` option (comma-separated) to whitelist accessible
    tables.
- **[rfc]** **CDS view scan no longer crashes (issue #42, PR #43)**.
  `sap_read_table` previously errored with `"Unsupported SAP table type
  name: NODE"` when reading CDS views like `/DMO/R_Booking_D`. New
  `RfcType::IsKnownDataType()` whitelist filters non-data DFIES entries
  (NODE, STRU, etc.) in `GetTableFieldMetas()` before they reach type
  mapping. `sap_describe_fields()` continues to surface NODE entries — it
  is metadata-only.

### BICS

- **[bics]** **Four unguarded `std::stoi` calls in metadata scanners
  hardened** — they no longer crash the process when SAP returns a
  non-numeric value where ERPL expected a digit string.
- **[bics]** 17 new test assertions across 6 test files cover the
  crash-prone metadata-scanner paths.

### ODP

- **[odp]** **Crash fixes and expanded test coverage** in the ODP scanner
  (submodule bump).

---

## v2026.01.28 — DuckDB v1.4.4 support (modern build matrix baseline)

- **[all]** **DuckDB v1.4.4 added** (PR #41). Marks the start of the
  current release/build matrix used by ERPL.
- **[all]** v1.4.2 and v1.4.3 support retained at this point (later
  removed in successive releases).

---

## Earlier history (pre-2026, not tagged)

Older work lives in the git log and is not retroactively tagged. Highlights:

- **2025-03**: maintenance update to DuckDB v0.10.3 build (#39). [all]
- **2024-06**: DuckDB v1.0.0 build (#34); OSX Apple Silicon support (#37).
  [all]
- **2024-04**: DuckDB v0.10.2 build (#26 – #29). [all]
- **2024-03**: DuckDB v0.10.1 build, Docker integration, ABAP trial
  update, initial DECIMAL parsing fix (#17, #19, #20, #23). [rfc]
- **2024-02**: project bootstrap — DuckDB v0.10.0 support (#10),
  BCD/decimal type conversion (#8, #14), first working
  `sap_odp_read_full` (#4). [rfc, odp]

For commit-level history, run `git log --oneline --no-merges` in the
repository.
