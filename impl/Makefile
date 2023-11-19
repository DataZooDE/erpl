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
	TOOLCHAIN_FLAGS:=${TOOLCHAIN_FLAGS} -DVCPKG_MANIFEST_DIR='${PROJ_DIR}' -DVCPKG_BUILD=1 -DCMAKE_TOOLCHAIN_FILE='${VCPKG_TOOLCHAIN_PATH}'
endif
ifneq ("${VCPKG_TARGET_TRIPLET}", "")
	TOOLCHAIN_FLAGS:=${TOOLCHAIN_FLAGS} -DVCPKG_TARGET_TRIPLET='${VCPKG_TARGET_TRIPLET}'
endif

#### Enable Ninja as generator
ifeq ($(GEN),ninja)
	GENERATOR=-G "Ninja" -DFORCE_COLORED_OUTPUT=1
endif

#### Configuration for this extension
EXTENSION_NAME=ERPL_IMPL
EXTENSION_FLAGS=\
-DDUCKDB_EXTENSION_NAMES="erpl_impl" \
-DDUCKDB_EXTENSION_${EXTENSION_NAME}_PATH="$(PROJ_DIR)" \
-DDUCKDB_EXTENSION_${EXTENSION_NAME}_LOAD_TESTS=1 \
-DDUCKDB_EXTENSION_${EXTENSION_NAME}_INCLUDE_PATH="$(PROJ_DIR)src/include" \
-DDUCKDB_EXTENSION_${EXTENSION_NAME}_TEST_PATH="$(PROJ_DIR)test/sql"

#### Add more of the DuckDB in-tree extensions here that you need (also feel free to remove them when not needed)
EXTRA_EXTENSIONS_FLAG=-DBUILD_EXTENSIONS=""

# EXTENSION_STATIC_BUILD=1 means that duckdb is linked into the extension for compatibility reasons, 
# do not change this
BUILD_FLAGS=-DEXTENSION_STATIC_BUILD=1 $(EXTENSION_FLAGS) ${EXTRA_EXTENSIONS_FLAG} $(OSX_BUILD_FLAG) $(TOOLCHAIN_FLAGS)
CLIENT_FLAGS:=

#### Main build
CLIENT_FLAGS=-DDUCKDB_EXTENSION_${EXTENSION_NAME}_SHOULD_LINK=0 -D_GLIBCXX_USE_CXX11_ABI=0

debug:
ifeq ($(OS),Windows_NT)
	powershell.exe -Command "if (!(Test-Path -Path ../build/debug/$(EXTENSION_NAME))) { New-Item -ItemType Directory -Path ../build/debug/$(EXTENSION_NAME) }"
else
	mkdir -p ../build/debug/$(EXTENSION_NAME)
endif
	cmake $(GENERATOR) $(BUILD_FLAGS) $(CLIENT_FLAGS) -DCMAKE_BUILD_TYPE=Debug -S ../duckdb/ -B ../build/debug/$(EXTENSION_NAME)
	cmake --build ../build/debug/$(EXTENSION_NAME) --config Debug

release:
ifeq ($(OS),Windows_NT)
	powershell.exe -Command "if (!(Test-Path -Path ../build/release/$(EXTENSION_NAME))) { New-Item -ItemType Directory -Path ../build/release/$(EXTENSION_NAME) }"
else
	mkdir -p ../build/release/$(EXTENSION_NAME)
endif
	cmake $(GENERATOR) $(BUILD_FLAGS) $(CLIENT_FLAGS) -DCMAKE_BUILD_TYPE=Release -S ../duckdb/ -B ../build/release/$(EXTENSION_NAME)
	cmake --build ../build/release/$(EXTENSION_NAME) --config Release

#### Misc
update:
	git submodule update --remote --merge
pull:
	git submodule init
	git submodule update --recursive --remote

clean:
ifeq ($(OS),Windows_NT)
	powershell.exe -Command "if (Test-Path -Path ../build) { Remove-Item -Recurse -Force ../build }"
	powershell.exe -Command "if (Test-Path -Path ../testext) { Remove-Item -Recurse -Force ../testext }"
	powershell.exe -Command "if (Test-Path -Path ../duckdb/build) { Remove-Item -Recurse -Force ../duckdb/build }"
else
	rm -rf ../build
	rm -rf ../testext
	cd ../duckdb && make clean
	cd ../duckdb && make clean-python
endif

# Main tests
test: test_release

test_release: release
	../build/release/ERPL_IMPL/test/unittest --test-dir . "[sql]"

test_debug: debug
	../build/debug/ERPL_IMPL/test/unittest --test-dir . "[sql]"