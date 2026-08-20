#pragma once

#include "duckdb.hpp"
#include "sap_rfc_api.hpp"
#include "sap_rfc.hpp"

namespace duckdb {
    string RfcPingPragma(ClientContext &context, const FunctionParameters &parameters);
    PragmaFunction CreateRfcPingPragma();
}