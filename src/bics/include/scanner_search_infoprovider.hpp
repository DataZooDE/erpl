#pragma once

#include "duckdb.hpp"
#include "sapnwrfc.h"

#include "duckdb/parser/parsed_data/create_table_function_info.hpp"

#include "sap_connection.hpp"
#include "sap_function.hpp"

namespace duckdb {
    CreateTableFunctionInfo CreateBicsSearchInfoProviderScanFunction();
} // namespace duckdb