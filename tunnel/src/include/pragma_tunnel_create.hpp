#pragma once

#include "duckdb.hpp"
#include "duckdb/function/pragma_function.hpp"

namespace duckdb {

string TunnelCreate(ClientContext &context, const FunctionParameters &parameters);
PragmaFunction CreateTunnelCreatePragma();

} // namespace duckdb 