PROJ_DIR := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))

EXT_CONFIG=${PROJ_DIR}extension_config.cmake

.PHONY: all clean format debug release pull update wasm_mvp wasm_eh wasm_threads

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
