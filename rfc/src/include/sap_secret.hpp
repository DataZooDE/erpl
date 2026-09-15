#pragma once

#include "duckdb.hpp"
#include "duckdb/main/secret/secret.hpp"
#include "duckdb/main/secret/secret_manager.hpp"

namespace duckdb {

struct RfcAuthParams; // Forward declaration

static constexpr const char *SAP_SECRET_PROVIDER = "config";
static constexpr const char *SAP_SECRET_TYPE_NAME = "sap_rfc";
static constexpr const char *SAP_SECRET_DEFAULT_PATH = "*";

// The parameter names accepted by a `sap_rfc` secret. Derived from
// RfcAuthParamDefinitions() in sap_connection.hpp, which is the single source
// of truth for the supported RfcOpenConnection parameters.
const vector<string> &SapSecretParameterNames();

// Convert a DuckDB secret to an RfcAuthParams
RfcAuthParams ConvertSecretToAuthParams(const KeyValueSecret &duck_secret);

// Non-connection secret options (e.g. read_table_function)
struct RfcSecretOptionDefinition {
	const char *name;
};

const vector<RfcSecretOptionDefinition> &RfcSecretOptionDefinitions();

// Look up a non-connection option on a named secret
std::string LookupSecretOption(ClientContext &context, const std::string &secret_name, const std::string &key);

// Register the SAP secret type with DuckDB
void RegisterSapSecretType(ExtensionLoader &loader);

// Get the secret name from the named parameters
std::string GetSecretNameFromParams(const TableFunctionBindInput &parameters);
std::string GetSecretNameFromParams(const FunctionParameters &parameters);
std::string GetSecretNameFromParams(const named_parameter_map_t &named_params);

// Get the secret from the context
RfcAuthParams GetAuthParamsFromContext(ClientContext &context, const TableFunctionBindInput &parameters);
RfcAuthParams GetAuthParamsFromContext(ClientContext &context, const FunctionParameters &parameters);
RfcAuthParams GetAuthParamsFromContext(ClientContext &context, const std::string &secret_name);

} // namespace duckdb 