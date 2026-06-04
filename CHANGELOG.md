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

## v2026.06.04 — BICS: actionable open errors and cross-release result fetch

Two SAP BW (BICS) reliability fixes, both reported against live customer systems.

### Clearer errors when opening a BW data provider

Fixes [#80](https://github.com/DataZooDE/erpl/issues/80): `sap_bics_describe` / `sap_bics_begin`
occasionally failed with the opaque `Failed to open data provider on the BW server, giving up!`,
which gave no way to tell *why* the open was rejected (authorization, query not active in the
requested version, a transient server condition, …).

- **[bics]** BICS RFC modules report failures through `E_MAX_MESSAGE_TYPE` (the highest message
  severity) and an `E_T_MESSAGE` table (the individual `BICS_PROV_MESSAGE` rows). ERPL ignored both
  and only noticed the sentinel handle `0000`. `BICS_PROV_OPEN` and `BICS_CONS_CREATE_DATA_AREA`
  now surface the info provider, the query name, the message severity and the actual SAP message
  text in the thrown error, e.g. `… for info provider '0D_NW_C01' (query '0D_FC_NW_C01_Q0008'),
  giving up! (max message type 'E'): [E BRAIN 299] Query … is not available in version A`. A new
  `FormatBicsMessages()` helper renders the message table and is covered by offline unit tests.
  Diagnostics gathering is best-effort and never throws while building the exception.

### Result fetch works across BW releases

Fixes [#81](https://github.com/DataZooDE/erpl/issues/81): `sap_bics_result` failed on some BW
systems with `Failed to adapt value for invocation argument 'I_CONFIRM_AUTORETRY': Parameter
'I_CONFIRM_AUTORETRY' not found`. ERPL passed the optional import parameter `I_CONFIRM_AUTORETRY`
to `BICS_PROV_GET_RESULT_SET` unconditionally, but it only exists on newer BW releases — older
systems reject the call during argument adaptation.

- **[bics]** `FetchResultSet` now looks up the target function's actual parameter set and drops
  `I_CONFIRM_AUTORETRY` only when the release does not define it (the dropped argument is traced).
  Systems that do define it are unaffected.
- **[rfc]** New reusable, version-tolerant primitives backing the fix: `SelectSupportedNamedArgs()`
  removes version-specific optional fields from a named-argument struct — a field is dropped only
  when it is **both** absent from the target signature **and** in an explicit droppable allowlist,
  so a genuinely misspelled required parameter still raises a clear `Parameter not found` error
  rather than being silently swallowed. `RfcFunction::GetParameterNames()` introspects the target
  system's signature. Both are covered by offline unit tests (`[select_supported_args]`).

### Build & CI

- **[bics]** Relaxed three `GenericTable::Rows()` bindings in `bics_result.cpp` from `auto&` to
  `const auto&` (`Rows()` returns by value; binding the temporary to a non-const reference is
  rejected by newer compilers).

## v2026.06.01 — RFC connection teardown no longer crashes the host process

Fixes [#78](https://github.com/DataZooDE/erpl/issues/78): a long-running process (reported in a
container) crashed with `terminate called after throwing an instance of 'duckdb::IOException'` and
`Error during SAP RFC connection closing: RFC_INVALID_HANDLE`. This is a hard `std::terminate`
abort — not an out-of-memory kill — so it takes down the **entire host process**, not just the
query.

### SAP scan path

- **[rfc]** `RfcConnection`'s destructor no longer throws. Root cause: `RfcConnection::Close()`
  raised a `duckdb::IOException` whenever `RfcCloseConnection` returned anything other than
  `RFC_OK`, and the destructor called it with no guard. A C++ destructor is implicitly `noexcept`,
  so an escaping exception goes straight to `std::terminate`. On **long-running sessions** the
  cached connection handle can become invalid — the SAP gateway drops an idle/long-lived
  connection, or a failed first close left the handle non-NULL and the destructor closed it a
  second time — and the resulting `RFC_INVALID_HANDLE` aborted the process.
- **[rfc]** `Close()` now clears the handle immediately after `RfcCloseConnection` regardless of
  outcome (eliminating the double-close path) and treats `RFC_INVALID_HANDLE` as benign — the
  handle is already gone, so there is nothing left to close. Genuinely unexpected RFC return codes
  still raise for explicit callers.
- **[rfc]** The same hardening was applied to the `RfcInvocation` / `RfcFunction` destructors,
  which carried the identical latent `std::terminate` risk (`RfcDestroyFunction` failures are now
  traced via `ERPL_TRACE_ERROR` instead of thrown), and both are now correctly marked `noexcept`.
- Note: this stops the crash, but the underlying connection drop (most often an idle gateway
  timeout) still occurs — it is now handled gracefully instead of aborting. Keepalive/reconnect on
  cached connections is a possible follow-up.

### Build & CI

- **[rfc]** Added a regression test (`rfc/test/cpp/test_connection_close.cpp`) that reproduces the
  exact precondition against the live trial system — open a connection, invalidate its handle
  out-of-band, then assert close/teardown no longer throw.

## v2026.05.30 — RFC authorization reference: `sap_rfc_authorizations()`

Addresses [#71](https://github.com/DataZooDE/erpl/issues/71): there was no documentation of which
SAP RFC function modules ERPL invokes, which an admin needs to grant least-privilege `S_RFC`
authorizations to the ERPL service user.

### Discovery & metadata

- **[rfc]** New table function `sap_rfc_authorizations()` — a static, **connection-free** reference
  (no secret required, makes no RFC calls) listing which RFC function modules each ERPL function
  invokes, across `erpl_rfc`, `erpl_bics` and `erpl_odp`. Columns: `extension`, `duckdb_function`,
  `rfc_function_module`, `invocation` (`always` / `fallback` / `optional` / `metadata` /
  `user-specified`), and `purpose`. Use it to scope `S_RFC`, e.g.
  `SELECT DISTINCT rfc_function_module FROM sap_rfc_authorizations() WHERE rfc_function_module NOT LIKE '<%>'`.
  The mapping accounts for the runtime nuances — `sap_read_table`'s capability-dependent
  `RFC_READ_TABLE` fallback chain, the optional `RPY_FUNCTIONMODULE_READ`, shared
  `DDIF_FIELDINFO_GET`, and `sap_rfc_invoke`'s caller-supplied module. (DuckDB's function `comment`
  and `tags` fields can't be set for C++ functions in v1.5.3, so a queryable table is used — which
  also documents pragmas uniformly.)

## v2026.05.29 — BICS result: NULL date marshalling fix

Fixes [#72](https://github.com/DataZooDE/erpl/issues/72): `sap_bics_result` failed with
`Failed to adapt field 'HIERARCHY_DUEDATE' in structure 'I_TH_CHARACTERISTICS': Calling
GetValueInternal on a value that is NULL`. A recent SAP BW addition put a `HIERARCHY_DUEDATE`
(DATE) field into the characteristics structure that `sap_bics_result` round-trips back to SAP via
`BICS_PROV_SET_STATE`; it is NULL for non-hierarchy characteristics.

### SAP scan path

- **[rfc]** Harden the RFC argument marshaller (`RfcType::AdaptValue`): a NULL DuckDB value for a
  scalar field is now left at the SDK's initial value, instead of crashing. The `DATE`, `TIME`,
  `NUM`, `INT`/`INT1`/`INT2`/`INT8`, `BCD`/`DECF16`/`DECF34` and `FLOAT` branches previously called
  the value converter on the NULL without a guard (throwing `Calling GetValueInternal on a value
  that is NULL`), whereas `CHAR`/`STRING`/`XSTRING`/`BYTE`/`UTCLONG` already skipped NULLs — this
  closes that gap with a single guard. Because the fix is in the shared `erpl_rfc` marshalling
  layer, it also hardens **[bics]** `sap_bics_result` (the reported case) and any `sap_rfc_invoke`
  / **[odp]** call that passes a NULL date/time/number inside a structure or table parameter.
  Covered by an offline regression test (no live SAP needed) plus the full RFC and BICS SQL suites.

## v2026.05.28 — Wide-scan memory: stream RFC results, bound the SDK buffer

Continuation of [#63](https://github.com/DataZooDE/erpl/issues/63), addressing
[#69](https://github.com/DataZooDE/erpl/issues/69): a full `SELECT *` over a wide
table (BSEG, ACDOCA — hundreds of columns, ~75k+ rows) climbed to **>8 GB**, ran
slowly, and the memory was never released after the connection closed. This is
**not** a regression of the #63 `LIMIT`-path fix — that work (warm-up batching,
`ROWCOUNT` cap, persistent connections, LOAD copy-elision) stays intact. #69 was a
distinct, never-addressed layer. heaptrack on a 100k-row × 153-column scan
attributed the 8.2 GB peak to two separate causes, each fixed below.

### SAP scan path

- **[rfc]** The `sap_read_table` scan now **streams rows straight from the SAP SDK
  result-table handle into the output vector**, instead of first materializing each
  batch as a tree of `duckdb::Value` objects. That intermediate representation was
  responsible for ~95% of all heap allocations (285 M allocations on the test scan)
  — the real driver of the high CPU and of the heap fragmentation that kept RSS
  elevated after the query. The conversion reuses the exact same per-cell logic as
  before, so results are byte-for-byte identical; `sap_rfc_invoke` and other callers
  of the materializing path are unchanged. A `malloc_trim` at scan teardown returns
  freed arenas to the OS on glibc.

- **[rfc]** New `erpl_rfc_read_table_batch_budget` extension option (UINTEGER,
  default `1310720`) bounds the **SAP SDK's own result buffer**, which dominated the
  remaining peak (~6.7 GB): RFC_READ_TABLE materializes every row's fixed-width work
  area inside `libsapnwrfc`, and every projected column reads in parallel. The option
  is a *concurrent-row* budget — a cap on `columns × per-column batch size` — so it
  self-adapts to table width: wider tables automatically use smaller batches, while
  narrow tables keep the full batch for throughput. Lower it to cap memory harder (at
  the cost of more RFC round-trips); raise it for fewer round-trips; `0` disables the
  cap. The `ROWSKIPS % ROWCOUNT == 0` invariant from #63 is preserved (power-of-two
  cap). On the 100k × 153-col scan, peak RSS dropped **8.2 GB → 1.78 GB** at the
  default (≈0.9 GB on a 340-column BSEG; 0.55 GB at a 256k budget), with the memory
  ↔ round-trip tradeoff now under user control.

## v2026.05.27 — `SHOW TABLES` on attached catalogs

Fixes [#70](https://github.com/DataZooDE/erpl/issues/70): `SHOW TABLES FROM <attached
sap catalog>` returned no rows. A SAP system exposes tens of thousands of tables
(~55k on the ABAP trial), and each catalog entry needs a per-table dictionary
roundtrip to discover its schema, so the catalog cannot eagerly enumerate everything.

### SAP scan path

- **[rfc]** The `TABLES` ATTACH option now accepts glob patterns (`*`, `?`) in
  addition to exact names, e.g. `ATTACH '' AS sap (TYPE sap_rfc, TABLES '/DMO/*,Z*')`.
  Patterns are resolved once, at ATTACH time, against `DD02V` (the same dictionary
  view `sap_show_tables()` uses). `SHOW TABLES` and `information_schema.tables` then
  list the resolved, bounded set, and tables outside it are not exposed through the
  catalog. A safety cap (5000 tables) rejects unbounded patterns with an actionable
  hint. Without `TABLES`, tables remain resolvable on demand by name and `SHOW TABLES`
  stays empty by design — use `sap_show_tables()` to browse the full catalog.

## v2026.05.24 — Wide-table CPIC exhaustion fix

Targeted follow-up to [#67](https://github.com/DataZooDE/erpl/issues/67), reported
two days after v2026.05.22 against BSEG (~401 cols) and ACDOCA (~511 cols) on a
real S/4 system. `SELECT * FROM <attached> LIMIT 10` hung ~5 min then failed with
`Could not complete read task after 5 attempts`.

### SAP scan path

- **[rfc]** Cap the persistent-connection pool to 16 per scan (configurable
  via the new `erpl_rfc_max_persistent_connections` extension option, default
  16, set to 0 to disable persistent caching entirely). Wide-table scans
  previously opened one persistent RFC connection per column state machine
  and exhausted the SAP SDK's `MAX_CPIC_CONVERSATIONS=200` ceiling; the SDK
  then returned `RFC_COMMUNICATION_FAILURE "max no of 200 conversations
  exceeded"` and ERPL's retry loop (5×, exponential backoff) bailed out.
  State machines beyond the cap now fall back to per-batch open/close —
  slower than caching but never starves the CPIC budget. 16 leaves ~4×
  headroom over the ~3-4 effective intra-process concurrency the SAP gateway
  can sustain, while staying well clear of any plausible SAP-side limit
  (gateway `max_conn` ~500, DWP pool, HANA worker limits). Verified on a
  400-col × 100k-row synthetic wide table on the `a4h` trial: full
  `COPY (SELECT *) TO '/dev/null'` went from a 5-min hang to 111s clean.

### Errors and diagnostics

- **[rfc]** Cleaner error message when an `ATTACH ... TYPE sap_rfc` reference
  cannot be resolved — actionable hints for the four most common causes
  (no secret, bad secret, table missing from DDIC, server-side timeout) in
  place of the previous bare INTERNAL Error stack.

### Tooling

- **[rfc]** New reproduction harness under `tools/issue-67-repro/` (CDS DDL
  generator, populator ABAP class, end-to-end recipe via `erpl-adt`). 400
  columns × 23 type families × 100 000 rows on the `a4h` trial. Reusable for
  any future wide-table regression coverage.

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
