#!/bin/bash

DUCKDB_BINARY="./build/debug/duckdb"

export LD_LIBRARY_PATH="$LD_LIBRARY_PATH:$(pwd)/nwrfcsdk/linux/lib"
export ERPL_SSH_HOST="localhost"
export ERPL_SSH_PORT=2222
export ERPL_SSH_USER="root"
export ERPL_SSH_PASSWORD="testpass"
export LSAN_OPTIONS="suppressions=../scripts/lsan_suppress.txt"
export ASAN_OPTIONS="detect_odr_violation=0"

$DUCKDB_BINARY