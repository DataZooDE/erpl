#pragma once

#include "duckdb.hpp"
#include "duckdb/main/secret/secret.hpp"
#include "duckdb/main/secret/secret_manager.hpp"

namespace duckdb {

class TunnelAuthParams; // Forward declaration

static constexpr const char *TUNNEL_SECRET_PROVIDER = "config";
static constexpr const char *TUNNEL_SECRET_TYPE_NAME = "ssh_tunnel";
static constexpr const char *TUNNEL_SECRET_DEFAULT_PATH = "*";

// Convert a DuckDB secret to an TunnelAuthParams
TunnelAuthParams ConvertTunnelSecretToAuthParams(const KeyValueSecret &duck_secret);

// Register the tunnel secret type with DuckDB
void RegisterTunnelSecretType(ExtensionLoader &loader);

// Get the secret name from the named parameters
std::string GetTunnelSecretNameFromParams(const TableFunctionBindInput &parameters);
std::string GetTunnelSecretNameFromParams(const FunctionParameters &parameters);
std::string GetTunnelSecretNameFromParams(const named_parameter_map_t &named_params);

// Get the secret from the context
TunnelAuthParams GetTunnelAuthParamsFromContext(ClientContext &context, const TableFunctionBindInput &parameters);
TunnelAuthParams GetTunnelAuthParamsFromContext(ClientContext &context, const FunctionParameters &parameters);
TunnelAuthParams GetTunnelAuthParamsFromContext(ClientContext &context, const std::string &secret_name);

} // namespace duckdb 