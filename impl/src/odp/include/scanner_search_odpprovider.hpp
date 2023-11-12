#pragma once

#include "duckdb.hpp"
#include "sapnwrfc.h"

#include "duckdb/parser/parsed_data/create_table_function_info.hpp"


namespace duckdb {
    CreateTableFunctionInfo CreateOdpSearchOdpProviderScanFunction();
} // namespace duckdb