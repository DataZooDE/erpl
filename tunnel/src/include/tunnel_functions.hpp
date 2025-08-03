#pragma once

#include "duckdb.hpp"

namespace duckdb {

class TunnelFunction {
public:
    static void Register(DatabaseInstance &db);
};

} // namespace duckdb
