#pragma once

#include "duckdb.hpp"
#include "sapnwrfc.h"
#include "sap_rfc.hpp"

namespace duckdb {
    string PragmaSetTraceLevel(ClientContext &context, const FunctionParameters &parameters);
    CreatePragmaFunctionInfo CreateRfcSetTraceLevelPragma();

    string PragmaSetTraceDir(ClientContext &context, const FunctionParameters &parameters);
    CreatePragmaFunctionInfo CreateRfcSetTraceDirPragma();

    string PragmaSetMaximumTraceFileSize(ClientContext &context, const FunctionParameters &parameters);
    CreatePragmaFunctionInfo CreateRfcSetMaximumTraceFileSizePragma();

    string PragmaSetMaximumStoredTraceFiles(ClientContext &context, const FunctionParameters &parameters);
    CreatePragmaFunctionInfo CreateRfcSetMaximumStoredTraceFilesPragma();
}