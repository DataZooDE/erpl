#pragma once

#include "duckdb.hpp"
#include "duckdb/function/pragma_function.hpp"

namespace duckdb {

string TunnelCloseAll(ClientContext &context, const FunctionParameters &parameters);
PragmaFunction CreateTunnelCloseAllPragma();

} // namespace duckdb 