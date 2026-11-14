# Developing erpl

This page is for people changing erpl itself. To *use* erpl, start with the [README](./README.md),
[API_REFERENCE.md](./API_REFERENCE.md) and the Claude Code plugin in [`plugins/erpl/`](./plugins/erpl/).

## Build

```sh
GEN=ninja make debug      # statically linked debug build: build/debug/duckdb
GEN=ninja make release    # loadable extensions + trampoline: build/release/extension/erpl/erpl.duckdb_extension
```

Prerequisites: `VCPKG_ROOT` pointing at a vcpkg checkout (manifest `rfc/vcpkg.json`), and the SAP NetWeaver
RFC SDK under `nwrfcsdk/<platform>/` (`linux`, `win`, `osx_arm`, `osx_amd64`; proprietary, not committed).
`bics/`, `odp/` and `ape/` are private submodules; without them only `erpl_rfc` is built.

The debug build links every extension into `build/debug/duckdb`; start it with
`./scripts/start-duckdb-debug.sh`, which puts the SDK on `LD_LIBRARY_PATH`.

Windows builds (MSVC, `windows_amd64`) run in CI (`.github/workflows/_extension_build.yml`); that workflow is the
reference for the toolchain and steps.

## Test

```sh
make sql_tests_rfc TEST_FILE=sap_read_table.test   # SQL tests need the ABAP Platform Trial (a4h)
make cpp_tests_rfc                                 # C++ unit tests, no SAP needed
make smoke_test                                    # release artifact installs, loads, registers functions
make check_skill_names                             # plugin + reference docs vs. the registered surface
make check_skills                                  # plugin SQL against a4h
```

`CLAUDE.md` has the full set of commands, the architecture and the conventions.

## Releasing

Releases are date-tagged (`vYYYY.MM.DD`); CI builds every platform on the tag and publishes to
`http://get.erpl.io`. Before tagging: add the release to `CHANGELOG.md`, set `plugins/erpl/.claude-plugin/plugin.json`
`version` to the tag's date, and run `make check_skills` against a4h.

## Looking at the SAP side

| Transaction | Shows |
|---|---|
| SM04 | Logged-on users and their RFC sessions |
| SM50 / SM66 | Work processes — what erpl's connections are doing |
| ST22 | ABAP short dumps |
| SM21 | System log |
| ST03 | Workload statistics |
| ODQMON | ODP queues, subscriptions and requests |
| DHCDC_MON | APE / change data capture subscriptions |
| RSRT | Run a BW query as BW does (compare with `sap_bics_result`) |
