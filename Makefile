PROJ_DIR := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))

EXT_CONFIG=${PROJ_DIR}extension_config.cmake

.PHONY: all clean format debug debug_tests release pull update wasm_mvp wasm_eh wasm_threads sql_tests_rfc sql_tests_bics sql_tests_odp sql_tests_rfc_proto sql_tests_bics_proto sql_tests_odp_proto sql_tests_all_backends delta_fixtures_odp delta_tests_odp smoke_test smoke_test_musl

# Test file argument - if provided, run only that specific test
TEST_FILE ?=

all: release

TEST_PATH="/test/unittest"
DUCKDB_PATH="/duckdb"

# For non-MinGW windows the path is slightly different
ifeq ($(OS),Windows_NT)
ifneq ($(CXX),g++)
	TEST_PATH="/test/Release/unittest.exe"
	DUCKDB_PATH="/Release/duckdb.exe"
endif
endif

#### OSX config
OSX_BUILD_FLAG=
ifneq (${OSX_BUILD_ARCH}, "")
	OSX_BUILD_FLAG=-DOSX_BUILD_ARCH=${OSX_BUILD_ARCH}
endif

#### VCPKG config
VCPKG_TOOLCHAIN_PATH?=$(VCPKG_ROOT)/scripts/buildsystems/vcpkg.cmake
ifneq ("${VCPKG_TOOLCHAIN_PATH}", "")
	TOOLCHAIN_FLAGS:=${TOOLCHAIN_FLAGS} -DVCPKG_MANIFEST_DIR='${PROJ_DIR}rfc' -DVCPKG_BUILD=1 -DCMAKE_TOOLCHAIN_FILE='${VCPKG_TOOLCHAIN_PATH}'
endif
ifneq ("${VCPKG_TARGET_TRIPLET}", "")
	TOOLCHAIN_FLAGS:=${TOOLCHAIN_FLAGS} -DVCPKG_TARGET_TRIPLET='${VCPKG_TARGET_TRIPLET}' -DVCPKG_HOST_TRIPLET='${VCPKG_TARGET_TRIPLET}'
endif

#### Enable Ninja as generator
ifeq ($(GEN),ninja)
	GENERATOR=-G "Ninja" -DFORCE_COLORED_OUTPUT=1
endif

#### Configuration for the extensions
EXTENSION_FLAGS=-DDUCKDB_EXTENSION_CONFIGS='${EXT_CONFIG}'
BUILD_FLAGS=-DEXTENSION_STATIC_BUILD=1 $(EXTENSION_FLAGS) ${EXT_FLAGS} $(OSX_BUILD_FLAG) $(TOOLCHAIN_FLAGS) -DDUCKDB_EXPLICIT_PLATFORM='${DUCKDB_PLATFORM}'

#### Windows-specific optimizations
ifeq ($(OS),Windows_NT)
	# Reduce parallelism to avoid memory issues on Windows
	CMAKE_BUILD_PARALLEL_LEVEL ?= 2
	# Add Windows-specific build flags
	BUILD_FLAGS += -DCMAKE_BUILD_PARALLEL_LEVEL=$(CMAKE_BUILD_PARALLEL_LEVEL)
	BUILD_FLAGS += -DCMAKE_GENERATOR_PLATFORM=x64
endif

#### Main build
debug:
ifeq ($(OS),Windows_NT)
	powershell.exe -Command "if (!(Test-Path -Path ./build/debug/)) { New-Item -ItemType Directory -Path ./build/debug/ }"
else
	mkdir -p ./build/debug/
endif
	cmake $(GENERATOR) $(BUILD_FLAGS) -DBUILD_UNITTESTS=ON -DCMAKE_BUILD_TYPE=Debug -S ./duckdb/ -B ./build/debug/
	cmake --build ./build/debug/ --config Debug

release:
ifeq ($(OS),Windows_NT)
	powershell.exe -Command "if (!(Test-Path -Path ./build/release/)) { New-Item -ItemType Directory -Path ./build/release/$(EXTENSION_NAME) }"
else
	mkdir -p ./build/release/
endif
	cmake $(GENERATOR) $(BUILD_FLAGS) -DBUILD_UNITTESTS=OFF -DCMAKE_BUILD_TYPE=Release -S ./duckdb/ -B ./build/release/
	cmake --build ./build/release/ --config Release

clean:
ifeq ($(OS),Windows_NT)
	powershell.exe -Command "if (Test-Path -Path ./build) { Remove-Item -Recurse -Force ./build }"
	powershell.exe -Command "if (Test-Path -Path ./testext) { Remove-Item -Recurse -Force ./testext }"
	powershell.exe -Command "if (Test-Path -Path ./duckdb/build) { Remove-Item -Recurse -Force ./duckdb/build }"
else
	rm -rf ./build
	rm -rf ./testext
	cd ./duckdb && make clean
endif

configure_ci:
	@echo "configure_ci step is skipped for this extension build..."

#### Build target for the SQL suites
# Builds only the unittest binary (and, through it, the extensions and the erpl-proto
# shim).  The full `debug` target additionally links tools/plan_serializer, which fails
# under GNU ld for reasons unrelated to erpl -- depending on it would make every SQL
# suite unrunnable.
debug_tests:
	mkdir -p ./build/debug/
	cmake $(GENERATOR) $(BUILD_FLAGS) -DBUILD_UNITTESTS=ON -DCMAKE_BUILD_TYPE=Debug -S ./duckdb/ -B ./build/debug/
	cmake --build ./build/debug/ --config Debug --target unittest

#### SQL Test Configuration
# Common environment variables for all test targets
COMMON_TEST_ENV = RFC_TRACE=0 \
                  LSAN_OPTIONS=suppressions=../scripts/lsan_suppress.txt \
                  ASAN_OPTIONS=detect_odr_violation=0 \
                  LD_LIBRARY_PATH=$${LD_LIBRARY_PATH}:../nwrfcsdk/linux/lib

# Common SAP connection variables
SAP_PASSWORD = ABAPtr2023\#00
SAP_COMMON_VARS = ERPL_SAP_ASHOST=localhost \
                  ERPL_SAP_SYSNR=00 \
                  ERPL_SAP_CLIENT=001 \
                  ERPL_SAP_LANG=EN \
                  ERPL_SAP_USER=DEVELOPER \
                  ERPL_SAP_PASSWORD=$(SAP_PASSWORD)

#### RFC backend selection
# The suites run twice: once on SAP's SDK, once on the pure-Rust erpl-proto shim.  nwrfc
# is the default and needs no variables at all -- the proto legs point the extension at
# the library the build staged.  See ERPL_PROTO_INTEGRATION_PLAN.md.
PROTO_BACKEND_VARS = ERPL_RFC_BACKEND=proto \
                     ERPL_RFC_BACKEND_PATH=$(PROJ_DIR)build/debug/liberpl_proto_nwrfc.so

# Tests known to fail on the proto backend.  A listed test that fails is reported as a
# known gap; a listed test that PASSES fails the run, so the list cannot silently rot.
# Override with ALLOW_UNEXPECTED_PASS=1 while working through the list.
ALLOW_UNEXPECTED_PASS ?= 0


# Common test execution logic lives in scripts/run_sql_tests.sh -- it grew past what is
# readable inlined in a make recipe once it had to handle two backends and per-backend
# expected failures.  See that script for the rules it applies.
define RUN_SQL_TESTS
	ALLOW_UNEXPECTED_PASS=$(ALLOW_UNEXPECTED_PASS) $(COMMON_TEST_ENV) $(2) $(3) \
		$(PROJ_DIR)scripts/run_sql_tests.sh $(1) \
		$(if $(4),--backend $(4)) \
		$(if $(5),--known-failures $(PROJ_DIR)$(5)) \
		$(if $(TEST_FILE),--test-file $(TEST_FILE))
endef

#### SQL Test targets
#
# Each suite runs on both RFC backends.  The bare targets are the nwrfc (SAP SDK) legs and
# keep their previous meaning; the _proto targets run the same files against erpl-proto.

sql_tests_rfc: debug_tests
	$(call RUN_SQL_TESTS,rfc,$(SAP_COMMON_VARS),,nwrfc,)

sql_tests_bics: debug_tests
	$(call RUN_SQL_TESTS,bics,$(SAP_COMMON_VARS),,nwrfc,)

sql_tests_odp: debug_tests
	$(call RUN_SQL_TESTS,odp,$(SAP_COMMON_VARS),,nwrfc,)

sql_tests_rfc_proto: debug_tests
	$(call RUN_SQL_TESTS,rfc,$(SAP_COMMON_VARS),$(PROTO_BACKEND_VARS),proto,rfc/test/proto_known_failures.txt)

sql_tests_bics_proto: debug_tests
	$(call RUN_SQL_TESTS,bics,$(SAP_COMMON_VARS),$(PROTO_BACKEND_VARS),proto,bics/test/proto_known_failures.txt)

sql_tests_odp_proto: debug_tests
	$(call RUN_SQL_TESTS,odp,$(SAP_COMMON_VARS),$(PROTO_BACKEND_VARS),proto,odp/test/proto_known_failures.txt)

# Real-change delta tests for sap_odp_read_delta.
#
# Deliberately NOT part of sql_tests_odp.  Every delta test in odp/test/sql runs against
# 0D_FC_C01$F, a static fact cube that never changes -- so they can only ever assert
# "a second call returns nothing".  This harness is the only thing in the repo that
# proves the protocol streams actual inserts, updates and deletes, and it asserts real
# values (VAL, REV, ODQ_CHANGEMODE), not row counts.
#
# It is separate because it MUTATES the SAP system: it writes rows to a Z table through
# a CDS view, so it cannot run unattended alongside other suites on a shared trial.
# Run delta_fixtures_odp once per fresh system, then delta_tests_odp as often as needed.
delta_fixtures_odp:
	./odp/test/harness/setup.sh

delta_tests_odp: debug_tests
	./odp/test/harness/run_delta_tests.sh

# Every suite on every backend.  Serial on purpose: the suites share one SAP system and
# one unittest binary, and running them concurrently corrupts both.
sql_tests_all_backends:
	$(MAKE) sql_tests_rfc
	$(MAKE) sql_tests_bics
	$(MAKE) sql_tests_odp
	$(MAKE) sql_tests_rfc_proto
	$(MAKE) sql_tests_bics_proto
	$(MAKE) sql_tests_odp_proto


# Usage examples:
#   make sql_tests_bics                    # Run all BICS tests
#   make sql_tests_bics TEST_FILE=sap_bics_hierarchy.test  # Run only hierarchy test
#   make sql_tests_rfc TEST_FILE=sap_rfc_invoke.test       # Run only RFC invoke test
#   make sql_tests_odp TEST_FILE=sap_odp_describe.test     # Run only ODP describe test
#   make sql_tests_rfc_proto               # Same RFC files, on the erpl-proto backend
#   make sql_tests_all_backends            # Every suite on both backends

#### Smoke test — verifies the release extension installs and loads correctly
# Downloads the official DuckDB CLI for the built version; no SAP connection required.
# Usage:
#   make smoke_test                         # Linux (glibc) or macOS
#   make smoke_test_musl                    # linux_amd64_musl (requires Docker)
#   DUCKDB_GIT_VERSION=v1.5.1 make smoke_test  # Override version if not on a tagged commit

SMOKE_EXT ?= build/release/extension/erpl/erpl.duckdb_extension

smoke_test: release
ifeq ($(OS),Windows_NT)
	powershell.exe -ExecutionPolicy Bypass -File scripts\smoke-test.ps1 -ExtensionPath "$(PROJ_DIR)$(SMOKE_EXT)"
else
	./scripts/smoke-test.sh "$(PROJ_DIR)$(SMOKE_EXT)"
endif

smoke_test_musl: release
	./scripts/smoke-test-musl.sh "$(PROJ_DIR)$(SMOKE_EXT)"
