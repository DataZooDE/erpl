PROJ_DIR := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))

EXT_CONFIG=${PROJ_DIR}extension_config.cmake

.PHONY: all clean format debug release pull update wasm_mvp wasm_eh wasm_threads sql_tests_rfc sql_tests_bics sql_tests_odp sql_tests_tunnel

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
VCPKG_TOOLCHAIN_PATH?=$(PROJ_DIR)vcpkg/scripts/buildsystems/vcpkg.cmake
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
	cd ./duckdb && make clean-python
endif

configure_ci:
	@echo "configure_ci step is skipped for this extension build..."

#### SQL Test targets

sql_tests_rfc: debug
	cd ./rfc && RFC_TRACE=0 LSAN_OPTIONS=suppressions=../scripts/lsan_suppress.txt ASAN_OPTIONS=detect_odr_violation=0 ERPL_SAP_ASHOST=localhost ERPL_SAP_SYSNR=00 ERPL_SAP_CLIENT=001 ERPL_SAP_LANG=EN ERPL_SAP_USER=DEVELOPER ERPL_SAP_PASSWORD=ABAPtr2023#00 LD_LIBRARY_PATH=$${LD_LIBRARY_PATH}:../nwrfcsdk/linux/lib bash -c 'if [ -n "$(TEST_FILE)" ]; then \
		echo "Running test: test/sql/$(TEST_FILE)" && \
		../build/debug/test/unittest --test-dir . "test/sql/$(TEST_FILE)"; \
	else \
		for test_file in test/sql/*.test; do \
			echo "Running test: $$test_file"; \
			../build/debug/test/unittest --test-dir . "$$test_file"; \
		done; \
	fi'

sql_tests_bics: debug
	cd ./bics && RFC_TRACE=0 LSAN_OPTIONS=suppressions=../scripts/lsan_suppress.txt ASAN_OPTIONS=detect_odr_violation=0 ERPL_SAP_ASHOST=localhost ERPL_SAP_SYSNR=00 ERPL_SAP_CLIENT=001 ERPL_SAP_LANG=EN ERPL_SAP_USER=DEVELOPER ERPL_SAP_PASSWORD=ABAPtr2023#00 LD_LIBRARY_PATH=$${LD_LIBRARY_PATH}:../nwrfcsdk/linux/lib bash -c 'if [ -n "$(TEST_FILE)" ]; then \
		echo "Running test: test/sql/$(TEST_FILE)" && \
		../build/debug/test/unittest --test-dir . "test/sql/$(TEST_FILE)"; \
	else \
		for test_file in test/sql/*.test; do \
			echo "Running test: $$test_file"; \
			../build/debug/test/unittest --test-dir . "$$test_file"; \
		done; \
	fi'

sql_tests_odp: debug
	cd ./odp && RFC_TRACE=0 LSAN_OPTIONS=suppressions=../scripts/lsan_suppress.txt ASAN_OPTIONS=detect_odr_violation=0 ERPL_SAP_ASHOST=localhost ERPL_SAP_SYSNR=00 ERPL_SAP_CLIENT=001 ERPL_SAP_LANG=EN ERPL_SAP_USER=DEVELOPER ERPL_SAP_PASSWORD=ABAPtr2022#00 LD_LIBRARY_PATH=$${LD_LIBRARY_PATH}:../nwrfcsdk/linux/lib bash -c 'if [ -n "$(TEST_FILE)" ]; then \
		echo "Running test: test/sql/$(TEST_FILE)" && \
		../build/debug/test/unittest --test-dir . "test/sql/$(TEST_FILE)"; \
	else \
		for test_file in test/sql/*.test; do \
			echo "Running test: $$test_file"; \
			../build/debug/test/unittest --test-dir . "$$test_file"; \
		done; \
	fi'

sql_tests_tunnel: debug
	@echo "Starting SSH mock server via docker-compose..."
	cd ./tunnel/test/integration && docker-compose up -d
	@echo "Waiting for SSH server to be ready..."
	@sleep 15
	@echo "Using static SSH keypairs for testing..."
	@cd ./tunnel/test/integration && echo "Static keys are already generated and mounted"
	@echo "Static SSH public keys are already mounted in the container..."
	@echo "Waiting for SSH key setup..."
	@sleep 5
	@echo "Running tunnel SQL tests..."
	-cd ./tunnel && ERPL_SSH_HOST=localhost ERPL_SSH_PORT=2222 ERPL_SSH_USER=root ERPL_SSH_PASSWORD=testpass ERPL_SSH_PRIVATE_KEY_PATH=test/integration/test_key LSAN_OPTIONS=suppressions=../scripts/lsan_suppress.txt ASAN_OPTIONS=detect_odr_violation=0 bash -c 'if [ -n "$(TEST_FILE)" ]; then \
		echo "Running test: test/sql/$(TEST_FILE)" && \
		../build/debug/test/unittest --test-dir . "test/sql/$(TEST_FILE)"; \
	else \
		for test_file in test/sql/*.test; do \
			echo "Running test: $$test_file"; \
			../build/debug/test/unittest --test-dir . "$$test_file"; \
		done; \
	fi'
	@echo "Stopping SSH mock server..."
	cd ./tunnel/test/integration && docker-compose down
	@echo "Static SSH keys are preserved for future tests..."

# Usage examples:
#   make sql_tests_bics                    # Run all BICS tests
#   make sql_tests_bics TEST_FILE=sap_bics_hierarchy.test  # Run only hierarchy test
#   make sql_tests_rfc TEST_FILE=sap_rfc_invoke.test       # Run only RFC invoke test
#   make sql_tests_odp TEST_FILE=sap_odp_describe.test     # Run only ODP describe test
#   make sql_tests_tunnel                  # Run all tunnel tests
#   make sql_tests_tunnel TEST_FILE=sap_tunnel_basic.test  # Run only basic tunnel test
