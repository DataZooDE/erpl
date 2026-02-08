# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

**ERPL** is a multi-extension DuckDB project that provides SAP data integration directly from SQL. It connects to SAP systems via RFC (Remote Function Call) and BICS (Business Intelligence Consumer Services) protocols, using the proprietary SAP NetWeaver RFC SDK.

The project is a **mono-repo** containing multiple DuckDB extensions that are built together:
- **erpl_rfc** (`rfc/`) - Core SAP RFC connectivity: table reading, function invocation, metadata discovery
- **erpl_bics** (`bics/`) - SAP BW (Business Warehouse) queries via BICS protocol, metadata views, lineage
- **erpl_odp** (`odp/`) - SAP ODP (Operational Data Provisioning) for data replication
- **erpl_tunnel** (`tunnel/`) - SSH tunneling for SAP connections via libssh2
- **erpl** (`trampoline/`) - Trampoline extension that bundles and extracts SAP SDK dependencies at install time

## Build & Development Commands

### Building

```bash
GEN=ninja make debug      # Debug build with Ninja
GEN=ninja make release    # Release build with Ninja
```

**Always use `GEN=ninja`** — never run `make debug` or `make release` without it. Plain make is significantly slower.

Use `make clean` sparingly — it leads to very long rebuild times and is rarely necessary.

**Prerequisites:**
- `VCPKG_ROOT` environment variable pointing to your vcpkg installation. Manifest is at `rfc/vcpkg.json` (dependencies: openssl, libssh2).
- SAP NetWeaver RFC SDK in `nwrfcsdk/` (proprietary, not committed). Platform subdirs: `linux/`, `win/`, `osx_arm/`, `osx_amd64/`.

### Running DuckDB with Extensions

In debug builds, all extensions are **statically linked** into the DuckDB binary. No `LOAD` needed. Use the helper script which sets `LD_LIBRARY_PATH` for SAP libraries:

```bash
./scripts/start-duckdb-debug.sh              # Interactive shell
./scripts/start-duckdb-debug.sh -s 'SELECT 42'  # Run SQL directly
```

### Testing

SQL tests require a running SAP system (ABAP Platform Trial on Docker) and use SQLLogicTest format.

```bash
make sql_tests_rfc                                    # All RFC tests
make sql_tests_bics                                   # All BICS tests
make sql_tests_odp                                    # All ODP tests
make sql_tests_tunnel                                 # All tunnel tests (starts Docker SSH mock)
make sql_tests_rfc TEST_FILE=sap_read_table.test      # Single test file
make sql_tests_bics TEST_FILE=sap_bics_hierarchy.test # Single BICS test
```

C++ unit tests (no SAP system needed):
```bash
./build/debug/test/unittest "[erpl_rfc]"    # Run RFC C++ tests
```

SQL test files: `{rfc,bics,odp,tunnel}/test/sql/*.test`. C++ tests: `rfc/test/cpp/`.

### Development Cycle

1. Edit source in `rfc/src/`, `bics/src/`, etc.
2. Rebuild: `GEN=ninja make debug`
3. Quick test: `./scripts/start-duckdb-debug.sh -s "SELECT * FROM sap_read_table('SFLIGHT')"`
4. Run test suite: `make sql_tests_rfc TEST_FILE=sap_read_table.test`

### Tracing for Debugging

Enable runtime tracing to diagnose SAP communication issues:
```sql
SET erpl_trace_enabled = TRUE;
SET erpl_trace_level = 'DEBUG';   -- TRACE, DEBUG, INFO, WARN, ERROR
SET erpl_trace_output = 'console'; -- console, file, both
```

Trace files go to `./trace/` directory. In code, use the `ERPL_TRACE_*` macros:
```cpp
ERPL_TRACE_DEBUG("ComponentName", "message");
ERPL_TRACE_INFO_DATA("ComponentName", "message", data_string);
```

## Architecture

### Multi-Extension Build System

`extension_config.cmake` loads all sub-extensions via `duckdb_extension_load()`. In **debug** mode they are statically linked; in **release** mode they use `DONT_LINK` (dynamically loadable). Sub-extensions with private repos (`bics/`, `odp/`) are conditionally loaded only if their `CMakeLists.txt` exists.

Each sub-extension has its own `CMakeLists.txt`, `src/`, and `test/` directories. Shared CMake helpers in `scripts/functions.cmake`:
- `find_sap_libraries()` - Discovers SAP SDK libs per platform
- `default_{linux,win32,osx}_libraries()` / `default_{linux,win32,osx}_definitions()` - Platform config
- `enable_mold_linker()` - Uses mold for faster linking when available
- `add_yyjson_from_duckdb()` - Reuses DuckDB's bundled yyjson for JSON
- `add_duckdb_version_definition()` - Exposes `DUCKDB_MAJOR/MINOR/PATCH_VERSION` as compile defines

### Submodules

- `duckdb/` - DuckDB core (CMake build root, `-S ./duckdb/`)
- `bics/` - erpl-bics (private: `DataZooDE/erpl-bics`)
- `odp/` - erpl-odp (private: `DataZooDE/erpl-odp`)
- `extension-ci-tools/` - DuckDB shared CI tooling
- `third_party/posthog-telemetry/` - Telemetry library

### Extension Entry Point Pattern

Extensions use the **modern DuckDB API** (`DUCKDB_CPP_EXTENSION_ENTRY` + `ExtensionLoader`):

```cpp
// rfc/src/erpl_rfc_extension.cpp
static void LoadInternal(ExtensionLoader &loader) {
    RegisterConfiguration(loader);  // config options via AddExtensionOption
    RegisterRfcFunctions(loader);   // loader.RegisterFunction(...)
}

extern "C" {
    DUCKDB_CPP_EXTENSION_ENTRY(erpl_rfc, loader) {
        duckdb::LoadInternal(loader);
    }
}
```

Each sub-extension registers:
- **Table functions** (scanners) via `loader.RegisterFunction(CreateRfc*ScanFunction())`
- **Pragma functions** for configuration commands
- **Secret types** via `RegisterSapSecretType(loader)` for `CREATE SECRET ... TYPE sap_rfc`
- **Extension options** via `config.AddExtensionOption()` with change callbacks

### Adding a New Function

1. Create `scanner_new_thing.hpp` with `TableFunction CreateNewThingScanFunction();`
2. Implement bind/init/execute in `scanner_new_thing.cpp` following existing scanner patterns
3. Register in the extension's `LoadInternal()` via `loader.RegisterFunction(...)`
4. Add SQL test in `test/sql/new_thing.test` (SQLLogicTest format with `require` directive)
5. Use `ERPL_TRACE_*` macros for diagnostic output

### SAP Connection & Type System

- `sap_connection.cpp` - Manages RFC connections using `RFC_CONNECTION_HANDLE`, resolved from DuckDB secrets
- `sap_type_conversion.cpp` - Bidirectional conversion between SAP and DuckDB types:
  - `rfc2duck()` overloads: `RFC_DATE/TIME/FLOAT/INT/NUM/BYTE` -> `duckdb::Value`
  - `duck2rfc()` overloads: `duckdb::Value` -> `RFC_DATE/TIME/FLOAT/INT/NUM`
  - `uc2std()`/`std2uc()`: SAP Unicode (`SAP_UC*`) <-> `std::string`
  - `rfctype2logicaltype()`: Maps `RFCTYPE` enum to `LogicalTypeId`
- `sap_function.cpp` - Wraps RFC function calls with parameter marshalling

### Trampoline Pattern

The `trampoline/` extension (`erpl`) embeds actual extension binaries and SAP SDK shared libraries as binary objects (via `objcopy` on Linux, `xxd` on macOS). On first load, it extracts them to `~/.duckdb/extensions/`. See `scripts/functions.cmake` for `embed_binary_to_object()` and `convert_dylib_to_object()`.

## C++ Conventions

Follow the erpl-web conventions (see `../erpl-web/CLAUDE.md` for full details). Key points:
- C++17, match DuckDB naming: CamelCase functions/classes, snake_case variables
- Use `duckdb::unique_ptr`/`duckdb::shared_ptr`, never raw `new`/`delete`; allocate with `make_uniq<T>()`
- Use `idx_t` for indices, fixed-width integer types (`int32_t`, `int64_t`)
- Tabs for indentation, 120 char line limit
- Use DuckDB exception types (`InvalidInputException`, `BinderException`, etc.) for errors
- Use `StringVector::AddString()` for string results, `FlatVector::GetData<T>()` for typed vector access
- Follow `.clang-format` configuration

## Git Commit Style

Use conventional commits without AI attribution:
```
feat: add BICS hierarchy support
fix: resolve type conversion for CURR fields
test: add coverage for ODP delta extraction
```
