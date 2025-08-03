#define DUCKDB_EXTENSION_MAIN

#include "duckdb.hpp"
#include "duckdb/main/extension_util.hpp"
#include "tunnel_manager.hpp"
#include "tunnel_secret.hpp"
#include "erpl_tunnel_extension.hpp"
#include "pragma_tunnel_create.hpp"
#include "pragma_tunnel_close.hpp"
#include "pragma_tunnel_close_all.hpp"
#include "scanner_tunnels.hpp"
#include "telemetry.hpp"

namespace duckdb {

// Global tunnel manager instance
std::unique_ptr<TunnelManager> g_tunnel_manager;

static void OnTelemetryEnabled(ClientContext &context, SetScope scope, Value &parameter)
{
    PostHogTelemetry::Instance().SetEnabled(parameter.GetValue<bool>());
}

static void OnAPIKey(ClientContext &context, SetScope scope, Value &parameter)
{
    PostHogTelemetry::Instance().SetAPIKey(parameter.GetValue<string>());
}

static void RegisterConfiguration(DatabaseInstance &instance)
{
    auto &config = DBConfig::GetConfig(instance);
    config.AddExtensionOption("erpl_telemetry_enabled", "Enable ERPL telemetry, see https://erpl.io/telemetry for details.", 
                              LogicalType::BOOLEAN, Value(true), OnTelemetryEnabled);
    config.AddExtensionOption("erpl_telemetry_key", "Telemetry key, see https://erpl.io/telemetry for details.", LogicalType::VARCHAR, 
                              Value("phc_t3wwRLtpyEmLHYaZCSszG0MqVr74J6wnCrj9D41zk2t"), OnAPIKey);
}

static void RegisterTunnelFunctions(DatabaseInstance &instance) {
    // Register tunnel secret type
    RegisterTunnelSecretType(instance);
    
    // Initialize tunnel manager
    g_tunnel_manager = std::make_unique<TunnelManager>();
    
    // Register pragma functions
    ExtensionUtil::RegisterFunction(instance, CreateTunnelCreatePragma());
    ExtensionUtil::RegisterFunction(instance, CreateTunnelClosePragma());
    ExtensionUtil::RegisterFunction(instance, CreateTunnelCloseAllPragma());

    // Register table function for listing tunnels
    ExtensionUtil::RegisterFunction(instance, CreateTunnelsTableFunction());
}

void ErplTunnelExtension::Load(DuckDB &db) {
    PostHogTelemetry::Instance().CaptureExtensionLoad();
    
    RegisterConfiguration(*db.instance);
    RegisterTunnelFunctions(*db.instance);
}

std::string ErplTunnelExtension::Name() {
    return "erpl_tunnel";
}

} // namespace duckdb

extern "C" {
    DUCKDB_EXTENSION_API void erpl_tunnel_init(duckdb::DatabaseInstance &db) {
        duckdb::DuckDB db_wrapper(db);
        db_wrapper.LoadExtension<duckdb::ErplTunnelExtension>();
    }

    DUCKDB_EXTENSION_API const char *erpl_tunnel_version() {
        return duckdb::DuckDB::LibraryVersion();
    }
}

#ifndef DUCKDB_EXTENSION_MAIN
#error DUCKDB_EXTENSION_MAIN not defined
#endif
