#pragma once

#include "duckdb.hpp"

#include "duckdb/parser/parsed_data/create_table_function_info.hpp"

namespace duckdb {
    // sap_rfc_authorizations(): a static, connection-free reference of which SAP
    // RFC function modules each ERPL DuckDB function invokes, so admins can scope
    // the S_RFC authorization object for the ERPL service user (issue #71).
    TableFunction CreateRfcAuthorizationsScanFunction();
} // namespace duckdb
