#!/bin/bash

RELEASE_URL="https://github.com/duckdb/duckdb/releases/download/v0.9.0/duckdb_cli-linux-amd64.zip"
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd )"
DUCK="$SCRIPT_DIR/duckdb"

if [ ! -f "$DUCK" ]; then
    echo "Downloading DuckDB CLI"
    wget "$RELEASE_URL" -O /tmp/duckdb.zip
    unzip /tmp/duckdb.zip -d "$SCRIPT_DIR"
    rm /tmp/duckdb.zip
fi

BUILD="debug" # or release
ERPL_EXTENSION=`realpath "$SCRIPT_DIR/../build/$BUILD/extension/erpl/erpl.duckdb_extension"`
LSAN_SUPRESS_OPTIONS="suppressions=$SCRIPT_DIR/lsan_suppress.txt"
NWRFC_LIB_PATH="$SCRIPT_DIR/../nwrfcsdk/lib"
TRACE_DIR="./trace"

rm -rf "$HOME/.duckdb/extensions/"
rm $TRACE_DIR/*

#PRAGMA sap_rfc_set_trace_dir('$TRACE_DIR');
#PRAGMA sap_rfc_set_trace_level(2);
#PRAGMA sap_rfc_set_trace_level(0);

SQL_CMDS=$(cat <<EOF
INSTALL '$ERPL_EXTENSION';
LOAD 'erpl';

SELECT * FROM duckdb_extensions();

SET memory_limit='10GB';
SET enable_progress_bar=true;
SET sap_ashost = 'localhost';
SET sap_sysnr = '00';
SET sap_user = 'DEVELOPER';
SET sap_password = 'Htods70334';
SET sap_client = '001';
SET sap_lang = 'EN';

SELECT * FROM duckdb_settings() WHERE name like 'sap_%';
SELECT * FROM duckdb_functions() WHERE function_name like 'sap_%';
SELECT * FROM sap_show_tables(TABLENAME="DD0*") WHERE class = 'TRANSP' ORDER BY 1;

DESCRIBE SELECT * FROM sap_read_table('REPOSRC', MAX_ROWS=10);

EOF
)

LSAN_OPTIONS=$LSAN_SUPRESS_OPTIONS LD_LIBRARY_PATH="$LD_LIBRARY_PATH:$NWRFC_LIB_PATH" $DUCK -unsigned -s "$SQL_CMDS"
