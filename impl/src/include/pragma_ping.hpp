#pragma once

#include "duckdb.hpp"
#include "sapnwrfc.h"
#include "sap_rfc.hpp"

namespace duckdb {
    string RfcPing(ClientContext &context, const FunctionParameters &parameters);
    CreatePragmaFunctionInfo CreateRfcPingPragma();
}