.PHONY: all clean debug release pull update

all: release

MKFILE_PATH := $(abspath $(lastword $(MAKEFILE_LIST)))
PROJ_DIR := $(dir $(MKFILE_PATH))

ifeq ($(OS),Windows_NT)
	TEST_PATH="/test/Release/unittest.exe"
else
	TEST_PATH="/test/unittest"
endif

#### OSX config
OSX_BUILD_FLAG=
ifneq (${OSX_BUILD_ARCH}, "")
	OSX_BUILD_FLAG=-DOSX_BUILD_ARCH=${OSX_BUILD_ARCH}
endif

#### VCPKG config
VCPKG_TOOLCHAIN_PATH?=../vcpkg/scripts/buildsystems/vcpkg.cmake
ifneq ("${VCPKG_TOOLCHAIN_PATH}", "")
	TOOLCHAIN_FLAGS:=${TOOLCHAIN_FLAGS} -DVCPKG_MANIFEST_DIR='${PROJ_DIR}/rfc' -DVCPKG_BUILD=1 -DCMAKE_TOOLCHAIN_FILE='${VCPKG_TOOLCHAIN_PATH}'
endif
ifneq ("${VCPKG_TARGET_TRIPLET}", "")
	TOOLCHAIN_FLAGS:=${TOOLCHAIN_FLAGS} -DVCPKG_TARGET_TRIPLET='${VCPKG_TARGET_TRIPLET}'
endif

#### Enable Ninja as generator
ifeq ($(GEN),ninja)
	GENERATOR=-G "Ninja" -DFORCE_COLORED_OUTPUT=1
endif

#### Configuration for the extensions
 
EXTENSION_FLAGS :=

# rfc is always included
EXTENSION_NAMES := erpl_rfc
EXTENSION_FLAGS := \
	-DDUCKDB_EXTENSION_ERPL_RFC_PATH="$(PROJ_DIR)rfc" \
	-DDUCKDB_EXTENSION_ERPL_RFC_LOAD_TESTS=1 \
	-DDUCKDB_EXTENSION_ERPL_RFC_INCLUDE_PATH="$(PROJ_DIR)rfc/src/include" \
	-DDUCKDB_EXTENSION_ERPL_RFC_TEST_PATH="$(PROJ_DIR)rfc/test/sql" \
	-DDUCKDB_EXTENSION_ERPL_RFC_SHOULD_LINK=0

# Check if files exist in bics directory
ifneq ($(wildcard $(PROJ_DIR)bics/CMakeLists.txt),)
    EXTENSION_NAMES := $(strip $(EXTENSION_NAMES);erpl_bics)
    EXTENSION_FLAGS += \
		-DDUCKDB_EXTENSION_ERPL_BICS_PATH="$(PROJ_DIR)bics" \
		-DDUCKDB_EXTENSION_ERPL_BICS_LOAD_TESTS=1 \
		-DDUCKDB_EXTENSION_ERPL_BICS_INCLUDE_PATH="$(PROJ_DIR)bics/src/include" \
		-DDUCKDB_EXTENSION_ERPL_BICS_TEST_PATH="$(PROJ_DIR)bics/test/sql" \
		-DDUCKDB_EXTENSION_ERPL_BICS_SHOULD_LINK=0
endif

# Check if files exist in odp directory
ifneq ($(wildcard $(PROJ_DIR)odp/CMakeLists.txt),)
    EXTENSION_NAMES := $(strip $(EXTENSION_NAMES);erpl_odp)
    EXTENSION_FLAGS += \
		-DDUCKDB_EXTENSION_ERPL_ODP_PATH="$(PROJ_DIR)odp" \
		-DDUCKDB_EXTENSION_ERPL_OPD_LOAD_TESTS=1 \
		-DDUCKDB_EXTENSION_ERPL_ODP_INCLUDE_PATH="$(PROJ_DIR)odp/src/include" \
		-DDUCKDB_EXTENSION_ERPL_ODP_TEST_PATH="$(PROJ_DIR)/odp/test/sql" \
		-DDUCKDB_EXTENSION_ERPL_ODP_SHOULD_LINK=0
endif

$(info **** $(EXTENSION_NAMES))

# If extension names are not empty, prepend the option
ifneq ($(EXTENSION_NAMES),)
    EXTENSION_FLAGS += -DDUCKDB_EXTENSION_NAMES="$(EXTENSION_NAMES)"
endif


#### Add more of the DuckDB in-tree extensions here that you need (also feel free to remove them when not needed)
EXTRA_EXTENSIONS_FLAG=-DBUILD_EXTENSIONS=""

# EXTENSION_STATIC_BUILD=1 means that duckdb is linked into the extension for compatibility reasons, 
# do not change this
BUILD_FLAGS=-DEXTENSION_STATIC_BUILD=1 $(EXTENSION_FLAGS) ${EXTRA_EXTENSIONS_FLAG} $(OSX_BUILD_FLAG) $(TOOLCHAIN_FLAGS)
CLIENT_FLAGS:=

#### Main build
CLIENT_FLAGS= -D_GLIBCXX_USE_CXX11_ABI=0

debug:
ifeq ($(OS),Windows_NT)
	powershell.exe -Command "if (!(Test-Path -Path ./build/debug/)) { New-Item -ItemType Directory -Path ./build/debug/ }"
else
	mkdir -p ./build/debug/
endif
	cmake $(GENERATOR) $(BUILD_FLAGS) $(CLIENT_FLAGS) -DBUILD_UNITTESTS=ON -DCMAKE_BUILD_TYPE=Debug -S ./duckdb/ -B ./build/debug/
	cmake --build ./build/debug/ --config Debug

release:
ifeq ($(OS),Windows_NT)
	powershell.exe -Command "if (!(Test-Path -Path ./build/release/)) { New-Item -ItemType Directory -Path ./build/release/$(EXTENSION_NAME) }"
else
	mkdir -p ./build/release/
endif
	cmake $(GENERATOR) $(BUILD_FLAGS) $(CLIENT_FLAGS) -DBUILD_UNITTESTS=OFF -DCMAKE_BUILD_TYPE=Release -S ./duckdb/ -B ./build/release/
	cmake --build ./build/release/ --config Release

#### Misc
update:
	git submodule update --remote --merge
pull:
	git submodule init
	git submodule update --recursive --remote

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

# Main tests
test: test_release

test_release: release
	./build/release/test/unittest --test-dir . "[sql]"

test_debug: debug
	./build/debug/test/unittest --test-dir . "[sql]"