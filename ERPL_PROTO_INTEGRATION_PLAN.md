# Integrating erpl-proto into erpl

Plan for putting [`erpl-proto`](https://github.com/DataZooDE/erpl-proto) — the pure-Rust
implementation of SAP's classic RFC protocol — underneath `erpl`, selectable at runtime
against the stock SAP NW RFC SDK.

**`nwrfc` stays the default.** `proto` is opt-in until its test legs are green.

## The seam: the nwrfc C ABI

`erpl-proto-cxx` exposes a narrow, high-level API (`invoke` / `describe` / `read_table`).
That is nowhere near erpl's surface — `bics/` and `odp/` reach deep into containers, rows
and field descriptors. `erpl-proto-nwrfc`, by contrast, builds a `libsapnwrfc.so` that
exports the full SDK symbol set.

Measured against this tree, `rfc/`, `bics/` and `odp/` together call **55 SDK entry
points**, and the shim implements all 55:

```
RfcAppendNewRow RfcCloseConnection RfcCreateFunction RfcDestroyFunction RfcGetBytes
RfcGetChars RfcGetConnectionAttributes RfcGetCurrentRow RfcGetDate RfcGetDirectionAsString
RfcGetFieldCount RfcGetFieldDescByIndex RfcGetFloat RfcGetFunctionDesc RfcGetInt RfcGetInt1
RfcGetInt2 RfcGetInt8 RfcGetNum RfcGetParameterCount RfcGetParameterDescByIndex
RfcGetRcAsString RfcGetRowCount RfcGetString RfcGetStringLength RfcGetStructure RfcGetTable
RfcGetTime RfcGetTypeAsString RfcGetTypeName RfcGetXString RfcInvoke RfcMoveTo
RfcOpenConnection RfcPing RfcReloadIniFile RfcSAPUCToUTF8 RfcSetBytes RfcSetDate RfcSetFloat
RfcSetIniPath RfcSetInt RfcSetInt1 RfcSetInt2 RfcSetInt8 RfcSetNum RfcSetParameterActive
RfcSetString RfcSetTime RfcSetTraceDir RfcSetTraceLevel RfcSetMaximumStoredTraceFiles
RfcSetMaximumTraceFileSize RfcSetXString
```

So this is a link/load change, not a source port.

## The selection mechanism: runtime dispatch

Both libraries export the same symbols, so only one can be *linked* — whichever the loader
resolves first wins for the whole process. erpl therefore stops linking `libsapnwrfc`
altogether and resolves the 55 entry points through a function-pointer table filled by
`dlopen` + `dlsym` (`LoadLibrary` / `GetProcAddress` on Windows) at first RFC use.

Two facts make this safe:

- The Rust cdylib carries **no SONAME** (verified with `objdump -p`), so it can be renamed
  and loaded by absolute path with no chance of SONAME-based resolution picking the wrong one.
- Loading with `RTLD_LOCAL` keeps its symbols out of the global table, so the two libraries
  cannot interpose on each other even if both are ever resident.

`sapnwrfc.h` remains a **compile-time** dependency for types, structs and enums. Phase 7
removes even that.

### Why not a build-time switch

`-DERPL_RFC_BACKEND=proto` would mean two full build trees to test both backends: slow
locally, doubled CI, and impossible to flip for a single test file. Runtime dispatch gives
one binary, both backends, and a dual-leg test matrix off a single build.

## Repository layout

`erpl-proto` stays private. It is added as a submodule at `proto/`, following the exact
pattern `bics/` and `odp/` already use — `extension_config.cmake` and the RFC build guard on
`if(EXISTS ...)`, so a clone without access builds precisely as it does today, with the
nwrfc backend and no Rust toolchain required.

The submodule uses an HTTPS remote (like `duckdb/`) rather than the `git@` form `bics/` and
`odp/` use, because HTTPS + the `gh` credential helper is what authenticates in practice.

## Phases

Each phase is a self-contained PR.

### Phase 0 — decisions

- [x] Seam is the nwrfc C ABI, not the cxx bridge.
- [x] Selection is runtime `dlopen` dispatch, not a configure-time switch.
- [x] `proto/` is a private submodule, conditionally built.
- [x] SDK headers stay a compile-time dependency for now; vendoring is Phase 7.
- [x] Rust becomes a build prerequisite *only* when `proto/` is present.

### Phase 1 — build plumbing, no behaviour change *(done)*

- Add the `proto/` submodule.
- CMake: when `proto/Cargo.toml` exists, a custom target runs
  `cargo build --release -p erpl-proto-nwrfc` and copies the artifact to
  `build/<cfg>/liberpl_proto_nwrfc.so` — **renamed**, so it can never be found by SONAME or
  by a stray `-lsapnwrfc`.
- Nothing links or loads it yet. All suites stay green, unchanged.

### Phase 2 — the dispatch layer *(done, landed with Phase 3)*

- New `rfc/src/sap_rfc_api.{hpp,cpp}`: an X-macro list of the 55 entry points generating a
  `RfcApi` function-pointer struct, populated on first use.
- The header includes `sapnwrfc.h` for types, then `#define`s each entry point to the
  dispatch call. Every consumer includes this instead of `sapnwrfc.h` directly — that is
  48 headers and sources across `rfc/`, `bics/` and `odp/`.

  One collision had to be resolved first: `duckdb::RfcPing`, the `sap_rfc_ping` pragma
  handler, shares its name with the SDK's `RfcPing` and is renamed `RfcPingPragma`.

  `strlenU16` turned out to be the only symbol erpl needed from `libsapucum`. It is a
  four-line loop, now supplied in the dispatch header, which lets that library leave the
  link line too — so the SDK is gone from the link entirely, not merely reduced.
- Drop `${SAPNWRFC_LIB_FILES}` from `target_link_libraries`; add `${CMAKE_DL_LIBS}`.
- **Backend is still nwrfc**, now reached by dlopen. Full RFC + BICS + ODP suites must be
  green before merge.
- `bics/` and `odp/` changes ship as submodule commits plus a parent pointer-bump PR.

### Phase 3 — the toggle *(done)*

Landed together with Phase 2: separating them would have meant committing a dispatch
layer hardcoded to one backend and then immediately rewriting it.


- Extension option `erpl_rfc_backend`: `'nwrfc'` (default) or `'proto'`. Env override
  `ERPL_RFC_BACKEND`.
- `erpl_rfc_backend_path` / `ERPL_RFC_BACKEND_PATH` for an explicit library path.
- Resolved once and frozen at first RFC use. Changing it afterwards raises a clear error
  rather than silently mixing backends within a process.
- Search order: explicit path → alongside the extension → loader path. A
  missing library when `proto` was requested is a hard error — **never** a silent fallback
  to nwrfc, which would make a green proto test leg meaningless.

### Phase 4 — tests on both backends *(done)*

- Parameterise `RUN_SQL_TESTS` in the `Makefile` with a backend argument.
- New targets: `sql_tests_rfc_proto`, `sql_tests_bics_proto`, `sql_tests_odp_proto`, and
  `sql_tests_all_backends`. Existing targets keep their current meaning (nwrfc).
- `test/proto_known_failures.txt`, checked in: the proto leg fails on anything not listed,
  and warns loudly when a listed test unexpectedly passes. This keeps the proto leg
  meaningfully green while gaps close, rather than permanently red and ignored.
  **Seed it from a measured run** — erpl-proto's own sources disagree about the current
  state (`README.md` says 13 of 20, `docs/spec/15-nwrfc-abi.md` ABI-4 says 18 of 20 with
  `sap_rfc_invoke_fuzz` and `sap_rfc_type_coverage` failing), so trust neither.
- C++ unit tests: `test_type_conversion` is backend-independent; `test_connection_close`
  and `test_sap_secret` run per backend.
- The SAP SDK exit-abort tolerance (erpl#112) is gated to the nwrfc leg. Under proto there
  is no SDK static destructor, so an abort there is a real failure.

### Phase 5 — CI *(done)*

Added an `erpl_proto_backend` job: it builds the shim, runs erpl-proto's sans-IO codec
tests, and gates the entry-point contract with `scripts/check_proto_backend_symbols.sh`.
Gated on submodule availability so fork PRs skip it instead of failing.

The SQL suites cannot run in CI — they need a live ABAP system — so both backends are
covered locally through `make sql_tests_all_backends`.

`erpl-proto` was added to the scoped token used by the matrix job. The platform build jobs
use an *unscoped* app token, so the GitHub App must additionally be installed on
`DataZooDE/erpl-proto` before their submodule checkout will succeed. That is a repository
admin action, not a code change.

### Phase 6 — close the gaps

Work `proto_known_failures.txt` down: deep tables (`STFC_DEEP_TABLE`), `RSTR` columns typed
`VARCHAR` where they should be `BLOB`, and whatever the BICS and ODP suites surface. These
are fixes in erpl-proto, reaching erpl by submodule pointer bump.

### Phase 7 — release packaging *(partly done)*

The trampoline now also embeds the proto shim and extracts it next to the other
extensions, where the dispatch layer finds it without any search path or environment
variable. It is shipped **alongside** the SDK, not instead of it: nwrfc stays the default,
and carrying both means a released build can be flipped with `SET erpl_rfc_backend =
'proto'` and flipped straight back. That costs about 1 MB against the SDK + ICU's ~30 MB.

It is extracted but deliberately **not** loaded. The trampoline loads its libraries
`RTLD_GLOBAL`; doing that to a library exporting the same `Rfc*` symbols as the SDK would
have the two interposing on each other. erpl_rfc opens it on demand, `RTLD_LOCAL`, and only
if the backend is actually selected.

Still outstanding, and only worth doing once proto has proven itself in the field:

- Drop the SDK + ICU from the trampoline in a proto-only build. That is what actually makes
  the artifact small; shipping both cannot.
- Vendor a minimal `erpl_sapnwrfc.h` into erpl-proto, removing the SDK from the build
  entirely. Today `sapnwrfc.h` is still needed at compile time for its types.

## Measured state

Both backends run the full suites against the a4h trial. Measured on this branch, not
assumed:

| Suite | nwrfc | proto |
|---|---|---|
| `rfc` (27 files) | 27 pass | 27 pass |
| `odp` (24 files) | 24 pass | 24 pass |
| `bics` (43 files) | *measuring* | *measuring* |

The proto leg was additionally run with the SAP SDK **removed from the library path
entirely**, and stayed green — which is what rules out a silent fallback. The same run on
the nwrfc backend fails immediately with "Could not load the SAP NW RFC SDK", so the check
is not vacuous.

This is well ahead of what erpl-proto's own documentation claims: its `README.md` says 13
of erpl's 20 tests pass and `docs/spec/15-nwrfc-abi.md` (ABI-4) says 18 of 20, naming
`sap_rfc_invoke_fuzz` and `sap_rfc_type_coverage` as failures. Both are stale — the suite
is 27 files now, and both named tests pass. Worth correcting in erpl-proto.

## Known risks

- **Phase 2 spans three repositories.** `bics/` and `odp/` include `sapnwrfc.h` from 31 of
  their headers between them; all must move to the dispatch header in lockstep.
- **Windows and macOS** need their own dispatch back end (`LoadLibrary`, and dlopen with
  `.dylib` naming). Linux lands first.
- **`scripts/lsan_suppress.txt`** and the `LD_LIBRARY_PATH` plumbing in
  `scripts/start-duckdb-debug.sh` and the `Makefile`'s `COMMON_TEST_ENV` are SDK-specific
  and need proto variants.
- **The OpenSSL/telemetry conflict** (SAP SDK's `RTLD_GLOBAL` shadowing OpenSSL, worked
  around with `DATAZOO_DISABLE_TELEMETRY=1` in smoke tests) may behave differently — or
  disappear — under `RTLD_LOCAL` and no SDK. Re-measure rather than assume.
- **Debug builds statically link the extensions** into the DuckDB binary; the SDK is
  currently linked in with them. Dispatch removes that link, which changes what
  `build/debug/duckdb` depends on at load time.
