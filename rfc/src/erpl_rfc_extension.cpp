#define DUCKDB_EXTENSION_MAIN

#include "duckdb.hpp"

#include "duckdb/catalog/catalog.hpp"
#include "duckdb/main/extension_util.hpp"

#include "erpl_rfc_extension.hpp"
#include "pragma_ping.hpp"
#include "pragma_set_trace.hpp"
#include "pragma_ini.hpp"
#include "scanner_invoke.hpp"
#include "scanner_show_groups.hpp"
#include "scanner_describe_function.hpp"
#include "scanner_show_functions.hpp"
#include "scanner_show_tables.hpp"
#include "scanner_describe_fields.hpp"
#include "scanner_read_table.hpp"

#include "telemetry.hpp"

namespace duckdb {


    static void OnTelemetryEnabled(ClientContext &context, SetScope scope, Value &parameter)
    {
        auto telemetry_enabled = parameter.GetValue<bool>();
        PostHogTelemetry::Instance().SetEnabled(telemetry_enabled);
    }

    static void OnAPIKey(ClientContext &context, SetScope scope, Value &parameter)
    {
        auto api_key = parameter.GetValue<std::string>();
        PostHogTelemetry::Instance().SetAPIKey(api_key);
    }

    static void RegisterConfiguration(DatabaseInstance &instance)
    {
        auto &config = DBConfig::GetConfig(instance);

        config.AddExtensionOption("sap_ashost", "The hostname SAP application server", LogicalType::VARCHAR);
        config.AddExtensionOption("sap_sysnr", "System number on the application server", LogicalType::VARCHAR);
        config.AddExtensionOption("sap_user", "Username to login", LogicalType::VARCHAR);
        config.AddExtensionOption("sap_password", "User password to login", LogicalType::VARCHAR);
        config.AddExtensionOption("sap_client", "Don't know yet", LogicalType::VARCHAR);
        config.AddExtensionOption("sap_lang", "Language of the user to connect with", LogicalType::VARCHAR, Value("EN"));
        config.AddExtensionOption("erpl_telemetry_enabled", "Enable ERPL telemetry, see https://erpl.io/telemetry for details.", 
                                  LogicalType::BOOLEAN, Value(true), OnTelemetryEnabled);
        config.AddExtensionOption("erpl_telemetry_key", "Telemetry key, see https://erpl.io/telemetry for details.", LogicalType::VARCHAR, 
                                  Value("phc_t3wwRLtpyEmLHYaZCSszG0MqVr74J6wnCrj9D41zk2t"), OnAPIKey);

        auto provider = make_uniq<RfcEnvironmentCredentialsProvider>(config);
        provider->SetAll();
    }

    static void RegisterRfcFunctions(DatabaseInstance &instance)
    {
        ExtensionUtil::RegisterFunction(instance, CreateRfcPingPragma());
        ExtensionUtil::RegisterFunction(instance, CreateRfcInvokeScanFunction());
        ExtensionUtil::RegisterFunction(instance, CreateRfcShowFunctionScanFunction());
        ExtensionUtil::RegisterFunction(instance, CreateRfcShowGroupScanFunction());
        ExtensionUtil::RegisterFunction(instance, CreateRfcDescribeFunctionScanFunction());
        ExtensionUtil::RegisterFunction(instance, CreateRfcShowTablesScanFunction());
        ExtensionUtil::RegisterFunction(instance, CreateRfcDescribeFieldsScanFunction());
        ExtensionUtil::RegisterFunction(instance, CreateRfcReadTableScanFunction());
        ExtensionUtil::RegisterFunction(instance, CreateRfcSetTraceLevelPragma());
        ExtensionUtil::RegisterFunction(instance, CreateRfcSetTraceDirPragma());
        ExtensionUtil::RegisterFunction(instance, CreateRfcSetMaximumTraceFileSizePragma());
        ExtensionUtil::RegisterFunction(instance, CreateRfcSetMaximumStoredTraceFilesPragma());
        ExtensionUtil::RegisterFunction(instance, CreateRfcSetIniPathPragma());
        ExtensionUtil::RegisterFunction(instance, CreateRfcReloadIniFilePragma());
    }
    
    void ErplRfcExtension::Load(DuckDB &db) 
    {
        PostHogTelemetry::Instance().CaptureExtensionLoad();
        
        RegisterConfiguration(*db.instance);
        RegisterRfcFunctions(*db.instance);
    }

    std::string ErplRfcExtension::Name() {
        return "erpl";
    }

} // namespace duckdb

extern "C" {
    DUCKDB_EXTENSION_API void erpl_rfc_init(duckdb::DatabaseInstance &db) 
    {
        duckdb::DuckDB db_wrapper(db);
	    db_wrapper.LoadExtension<duckdb::ErplRfcExtension>();
    }

    DUCKDB_EXTENSION_API const char *erpl_rfc_version() 
    {
        return duckdb::DuckDB::LibraryVersion();
    }
}

#ifndef DUCKDB_EXTENSION_MAIN
#error DUCKDB_EXTENSION_MAIN not defined
#endif
