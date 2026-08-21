#pragma once

#include "duckdb.hpp"
#include "sap_rfc_api.hpp"

#include "duckdb/parser/parsed_data/create_table_function_info.hpp"

#include "sap_connection.hpp"
#include "sap_function.hpp"

namespace duckdb {
    TableFunction CreateRfcShowGroupScanFunction();
} // namespace duckdb