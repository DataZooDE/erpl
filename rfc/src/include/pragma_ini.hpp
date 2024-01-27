#pragma once

#include "duckdb.hpp"
#include "sapnwrfc.h"
#include "sap_rfc.hpp"

namespace duckdb {
    string PragmaSetIniPath(ClientContext &context, const FunctionParameters &parameters);
    PragmaFunction CreateRfcSetIniPathPragma();

    string PragmaReloadIniFile(ClientContext &context, const FunctionParameters &parameters);
    PragmaFunction CreateRfcReloadIniFilePragma();
}