#pragma once

#include "duckdb.hpp"
#include "duckdb/main/secret/secret.hpp"
#include "duckdb/main/secret/secret_manager.hpp"

namespace duckdb {

class RfcAuthParams; // Forward declaration

static constexpr const char *SAP_SECRET_PROVIDER = "config";
static constexpr const char *SAP_SECRET_TYPE_NAME = "sap_rfc";
static constexpr const char *SAP_SECRET_DEFAULT_PATH = "*";

// Convert a DuckDB secret to an RfcAuthParams
RfcAuthParams ConvertSecretToAuthParams(const KeyValueSecret &duck_secret);

// Register the SAP secret type with DuckDB
void RegisterSapSecretType(DatabaseInstance &instance);

// Get the secret name from the named parameters
std::string GetSecretNameFromParams(const TableFunctionBindInput &parameters);
std::string GetSecretNameFromParams(const FunctionParameters &parameters);
std::string GetSecretNameFromParams(const named_parameter_map_t &named_params);

// Get the secret from the context
RfcAuthParams GetAuthParamsFromContext(ClientContext &context, const TableFunctionBindInput &parameters);
RfcAuthParams GetAuthParamsFromContext(ClientContext &context, const FunctionParameters &parameters);
RfcAuthParams GetAuthParamsFromContext(ClientContext &context, const std::string &secret_name);

} // namespace duckdb 