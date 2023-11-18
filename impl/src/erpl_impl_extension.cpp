#define DUCKDB_EXTENSION_MAIN

#include "duckdb.hpp"

#include "duckdb/catalog/catalog.hpp"
#include "duckdb/parser/parsed_data/create_pragma_function_info.hpp"

#include "erpl_impl_extension.hpp"
#include "pragma_ping.hpp"
#include "pragma_set_trace.hpp"
#include "pragma_ini.hpp"
#include "scanner_invoke.hpp"
#include "scanner_search_group.hpp"
#include "scanner_describe_function.hpp"
#include "scanner_search_function.hpp"
#include "scanner_show_tables.hpp"
#include "scanner_describe_fields.hpp"
#include "scanner_read_table.hpp"
#include "telemetry.hpp"

#ifdef WITH_BICS
    #include "bics/include/scanner_search_infoprovider.hpp"
    #include "bics/include/scanner_describe_infoprovider.hpp"
#endif



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

    static void RegisterFunctions(DatabaseInstance &instance)
    {
        Connection con(instance);
        con.BeginTransaction();

        auto &context = *con.context;
        auto &catalog = Catalog::GetSystemCatalog(context);

        auto rfc_ping_info = CreateRfcPingPragma();
        catalog.CreatePragmaFunction(context, rfc_ping_info);

        auto rfc_invoke_info = CreateRfcInvokeScanFunction();
	    catalog.CreateTableFunction(context, &rfc_invoke_info);

        auto rfc_search_group_info = CreateRfcSearchGroupScanFunction();
        catalog.CreateTableFunction(context, &rfc_search_group_info);

        auto rfc_search_function_info = CreateRfcSearchFunctionScanFunction();
        catalog.CreateTableFunction(context, &rfc_search_function_info);

        auto rfc_describe_function_info = CreateRfcDescribeFunctionScanFunction();
        catalog.CreateTableFunction(context, &rfc_describe_function_info);

        auto rfc_show_tables_info = CreateRfcShowTablesScanFunction();
        catalog.CreateTableFunction(context, &rfc_show_tables_info);

        auto rfc_describe_fields_info = CreateRfcDescribeFieldsScanFunction();
        catalog.CreateTableFunction(context, &rfc_describe_fields_info);

        auto rfc_read_table_info = CreateRfcReadTableScanFunction();
        catalog.CreateTableFunction(context, &rfc_read_table_info);

        auto rfc_function_desc_pragma = PragmaFunction::PragmaCall("sap_rfc_function_desc", RfcFunctionDesc, {LogicalType::VARCHAR});
        CreatePragmaFunctionInfo rfc_foo_info(rfc_function_desc_pragma);
        catalog.CreatePragmaFunction(context, rfc_foo_info);

        auto rfc_set_trace_level_info = CreateRfcSetTraceLevelPragma();
        catalog.CreatePragmaFunction(context, rfc_set_trace_level_info);

        auto rfc_set_trace_dir_info = CreateRfcSetTraceDirPragma();
        catalog.CreatePragmaFunction(context, rfc_set_trace_dir_info);

        auto rfc_set_maximum_trace_file_size_info = CreateRfcSetMaximumTraceFileSizePragma();
        catalog.CreatePragmaFunction(context, rfc_set_maximum_trace_file_size_info);

        auto rfc_set_maximum_stored_trace_files_info = CreateRfcSetMaximumStoredTraceFilesPragma();
        catalog.CreatePragmaFunction(context, rfc_set_maximum_stored_trace_files_info);

        auto rfc_set_ini_path_info = CreateRfcSetIniPathPragma();
        catalog.CreatePragmaFunction(context, rfc_set_ini_path_info);

        auto rfc_reload_ini_file_info = CreateRfcReloadIniFilePragma();
        catalog.CreatePragmaFunction(context, rfc_reload_ini_file_info);
        
        con.Commit();
    }

    /*
    static void RegisterBicsFunctions(DatabaseInstance &instance)
    {
        #ifndef WITH_BICS
            return;
        #endif

        Connection con(instance);
        con.BeginTransaction();

        auto &context = *con.context;
        auto &catalog = Catalog::GetSystemCatalog(context);

        auto bics_search_infoprovider_info = CreateBicsSearchInfoProviderScanFunction();
        catalog.CreateTableFunction(context, &bics_search_infoprovider_info);

        auto bics_describe_infoprovider_info = CreateBicsDescribeInfoProviderScanFunction();
        catalog.CreateTableFunction(context, &bics_describe_infoprovider_info);

        con.Commit();
    }
    */

  
    void ErplImplExtension::Load(DuckDB &db) 
    {
        PostHogTelemetry::Instance().CaptureExtensionLoad();
        
        RegisterConfiguration(*db.instance);
        RegisterFunctions(*db.instance);
        //RegisterBicsFunctions(instance);
    }

    std::string ErplImplExtension::Name() {
        return "erpl";
    }

} // namespace duckdb

extern "C" {
    DUCKDB_EXTENSION_API void erpl_impl_init(duckdb::DatabaseInstance &db) 
    {
        duckdb::DuckDB db_wrapper(db);
	    db_wrapper.LoadExtension<duckdb::ErplImplExtension>();
    }

    DUCKDB_EXTENSION_API const char *erpl_impl_version() 
    {
        return duckdb::DuckDB::LibraryVersion();
    }
}

#ifndef DUCKDB_EXTENSION_MAIN
#error DUCKDB_EXTENSION_MAIN not defined
#endif
