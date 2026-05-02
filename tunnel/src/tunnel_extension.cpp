#define DUCKDB_EXTENSION_MAIN

#include "duckdb.hpp"
#include "duckdb/main/extension/extension_loader.hpp"
#include "duckdb/parser/parsed_data/create_table_function_info.hpp"
#include "tunnel_manager.hpp"
#include "tunnel_secret.hpp"
#include "erpl_tunnel_extension.hpp"
#include "pragma_tunnel_create.hpp"
#include "pragma_tunnel_close.hpp"
#include "pragma_tunnel_close_all.hpp"
#include "scanner_tunnels.hpp"
#include "telemetry.hpp"

// Needed for OPENSSL_init_ssl / OPENSSL_INIT_NO_ATEXIT
#include <openssl/ssl.h>

// Windows headers may redefine macros after our tracing.hpp include
#ifdef _WIN32
#ifdef DEBUG
    #undef DEBUG
#endif
#ifdef INFO
    #undef INFO
#endif
#ifdef WARN
    #undef WARN
#endif
#ifdef ERROR
    #undef ERROR
#endif
#ifdef TRACE
    #undef TRACE
#endif
#ifdef NONE
    #undef NONE
#endif
#ifdef CONSOLE
    #undef CONSOLE
#endif
#endif

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

static void RegisterConfiguration(ExtensionLoader &loader)
{
    auto &instance = loader.GetDatabaseInstance();
    auto &config = DBConfig::GetConfig(instance);
    config.AddExtensionOption("erpl_telemetry_enabled", "Enable ERPL telemetry, see https://erpl.io/telemetry for details.", 
                              LogicalType::BOOLEAN, Value(true), OnTelemetryEnabled);
    config.AddExtensionOption("erpl_telemetry_key", "Telemetry key, see https://erpl.io/telemetry for details.", LogicalType::VARCHAR, 
                              Value("phc_t3wwRLtpyEmLHYaZCSszG0MqVr74J6wnCrj9D41zk2t"), OnAPIKey);
}

static void RegisterTunnelFunctions(ExtensionLoader &loader) {
    // Register tunnel secret type
    RegisterTunnelSecretType(loader);

    // Initialize tunnel manager
    g_tunnel_manager = std::make_unique<TunnelManager>();

    // Register pragma functions
    loader.RegisterFunction(CreateTunnelCreatePragma());
    loader.RegisterFunction(CreateTunnelClosePragma());
    loader.RegisterFunction(CreateTunnelCloseAllPragma());

    {
        CreateTableFunctionInfo info(CreateTunnelsTableFunction());
        FunctionDescription desc;
        desc.description = "List all active SSH tunnels with their connection details and status.";
        desc.examples    = {"SELECT * FROM tunnels()"};
        desc.categories  = {"tunnel", "ssh"};
        info.descriptions.push_back(std::move(desc));
        loader.RegisterFunction(std::move(info));
    }
}

static void LoadInternal(ExtensionLoader &loader)
{
    // Suppress OpenSSL's atexit cleanup handler — see erpl_rfc_extension.cpp for full rationale.
#if OPENSSL_VERSION_NUMBER >= 0x10100000L
    OPENSSL_init_ssl(OPENSSL_INIT_NO_ATEXIT, nullptr);
#endif

    loader.SetDescription("SSH tunnel management for DuckDB — create and manage SSH tunnels to securely reach SAP systems behind firewalls.");

    PostHogTelemetry::Instance().CaptureExtensionLoad("erpl_tunnel");

    RegisterConfiguration(loader);
    RegisterTunnelFunctions(loader);
}

void ErplTunnelExtension::Load(ExtensionLoader &loader) {
    LoadInternal(loader);
}

std::string ErplTunnelExtension::Name() {
    return "erpl_tunnel";
}

} // namespace duckdb

extern "C" {
    DUCKDB_CPP_EXTENSION_ENTRY(erpl_tunnel, loader) {
        duckdb::LoadInternal(loader);
    }
}

#ifndef DUCKDB_EXTENSION_MAIN
#error DUCKDB_EXTENSION_MAIN not defined
#endif
