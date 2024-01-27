#pragma once

#include "duckdb.hpp"
#include "sapnwrfc.h"
#include "sap_rfc.hpp"

namespace duckdb {
    string PragmaSetTraceLevel(ClientContext &context, const FunctionParameters &parameters);
    PragmaFunction CreateRfcSetTraceLevelPragma();

    string PragmaSetTraceDir(ClientContext &context, const FunctionParameters &parameters);
    PragmaFunction CreateRfcSetTraceDirPragma();

    string PragmaSetMaximumTraceFileSize(ClientContext &context, const FunctionParameters &parameters);
    PragmaFunction CreateRfcSetMaximumTraceFileSizePragma();

    string PragmaSetMaximumStoredTraceFiles(ClientContext &context, const FunctionParameters &parameters);
    PragmaFunction CreateRfcSetMaximumStoredTraceFilesPragma();
}