#pragma once

#include "duckdb.hpp"
#include "duckdb/main/extension/extension_loader.hpp"

namespace duckdb {

struct SapSecretInjectorInfo : public TableFunctionInfo {
	string secret_name;
	string read_table_function;
	string read_table_delimiter;
	table_function_bind_t orig_bind;

	SapSecretInjectorInfo(string secret, string fn, string delim, table_function_bind_t bind)
	    : secret_name(std::move(secret)), read_table_function(std::move(fn)),
	      read_table_delimiter(std::move(delim)), orig_bind(bind) {
	}
};

void RegisterSapStorageExtension(ExtensionLoader &loader);

} // namespace duckdb
