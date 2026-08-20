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

Measured against this tree, `rfc/`, `bics/` and `odp/` together call **54 SDK entry
points**, and the shim implements all 54:

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
altogether and resolves the 54 entry points through a function-pointer table filled by
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

### Phase 1 — build plumbing, no behaviour change

- Add the `proto/` submodule.
- CMake: when `proto/Cargo.toml` exists, a custom target runs
  `cargo build --release -p erpl-proto-nwrfc` and copies the artifact to
  `build/<cfg>/liberpl_proto_nwrfc.so` — **renamed**, so it can never be found by SONAME or
  by a stray `-lsapnwrfc`.
- Nothing links or loads it yet. All suites stay green, unchanged.

### Phase 2 — the dispatch layer (riskiest step; its own PR)

- New `rfc/src/sap_rfc_api.{hpp,cpp}`: an X-macro list of the 54 entry points generating a
  `RfcApi` function-pointer struct, populated on first use.
- The header includes `sapnwrfc.h` for types, then `#define`s each entry point to the
  dispatch call. Every consumer includes this instead of `sapnwrfc.h` directly — that is
  53 headers and sources across `rfc/`, `bics/` and `odp/`.
- Drop `${SAPNWRFC_LIB_FILES}` from `target_link_libraries`; add `${CMAKE_DL_LIBS}`.
- **Backend is still nwrfc**, now reached by dlopen. Full RFC + BICS + ODP suites must be
  green before merge.
- `bics/` and `odp/` changes ship as submodule commits plus a parent pointer-bump PR.

### Phase 3 — the toggle

- Extension option `erpl_rfc_backend`: `'nwrfc'` (default) or `'proto'`. Env override
  `ERPL_RFC_BACKEND`.
- `erpl_rfc_backend_path` / `ERPL_RFC_PROTO_LIB` for an explicit library path.
- Resolved once and frozen at first RFC use. Changing it afterwards raises a clear error
  rather than silently mixing backends within a process.
- Search order: explicit path → alongside the extension → build dir → loader path. A
  missing library when `proto` was requested is a hard error — **never** a silent fallback
  to nwrfc, which would make a green proto test leg meaningless.

### Phase 4 — tests on both backends

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

### Phase 5 — CI

Add a proto leg to `.github/workflows/_extension_build.yml`, gated on submodule
availability so fork PRs skip it instead of failing.

### Phase 6 — close the gaps

Work `proto_known_failures.txt` down: deep tables (`STFC_DEEP_TABLE`), `RSTR` columns typed
`VARCHAR` where they should be `BLOB`, and whatever the BICS and ODP suites surface. These
are fixes in erpl-proto, reaching erpl by submodule pointer bump.

### Phase 7 — release packaging

The payoff. `trampoline/` currently embeds `libsapnwrfc` + `libsapucum` + three ICU
libraries, roughly 30 MB. A proto build embeds one ~1 MB Rust `.so` and needs no
post-install extraction of the SDK. This is also where a vendored `erpl_sapnwrfc.h` lands in
erpl-proto, removing the SDK from the build entirely.

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
