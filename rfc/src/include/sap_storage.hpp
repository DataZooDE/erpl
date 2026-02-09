#pragma once

#include "duckdb.hpp"
#include "duckdb/main/extension/extension_loader.hpp"

namespace duckdb {

void RegisterSapStorageExtension(ExtensionLoader &loader);

} // namespace duckdb
