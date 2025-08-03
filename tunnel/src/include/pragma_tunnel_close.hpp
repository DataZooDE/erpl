#pragma once

#include "duckdb.hpp"
#include "duckdb/function/pragma_function.hpp"

namespace duckdb {

string TunnelClose(ClientContext &context, const FunctionParameters &parameters);
PragmaFunction CreateTunnelClosePragma();

} // namespace duckdb 