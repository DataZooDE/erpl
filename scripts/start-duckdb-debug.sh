#!/bin/bash

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DUCKDB_BINARY="$SCRIPT_DIR/../build/debug/duckdb"

export LD_LIBRARY_PATH="$LD_LIBRARY_PATH:$SCRIPT_DIR/../nwrfcsdk/linux/lib"
export LSAN_OPTIONS="suppressions=$SCRIPT_DIR/lsan_suppress.txt"
export ASAN_OPTIONS="detect_odr_violation=0"

$DUCKDB_BINARY "$@"