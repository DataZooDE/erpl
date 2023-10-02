#define DUCKDB_EXTENSION_MAIN


#include "duckdb.hpp"

#include "duckdb/catalog/catalog.hpp"
#include "duckdb/parser/parsed_data/create_table_function_info.hpp"
#include "duckdb/parser/parsed_data/create_pragma_function_info.hpp"

#include "erpl_extension.hpp"
#include "pragma_ping.hpp"
#include "scanner_invoke.hpp"
#include "scanner_search_group.hpp"
#include "scanner_describe_function.hpp"
#include "scanner_search_function.hpp"
#include "scanner_show_tables.hpp"
#include "scanner_describe_fields.hpp"
#include "scanner_read_table.hpp"

#ifdef WITH_BICS
    #include "bics/include/scanner_search_infoprovider.hpp"
    #include "bics/include/scanner_describe_infoprovider.hpp"
#endif

namespace duckdb {

    static void RegisterConfiguration(DatabaseInstance &instance)
    {
        auto &config = DBConfig::GetConfig(instance);
        config.AddExtensionOption("sap_ashost", "The hostname SAP application server", LogicalType::VARCHAR);
        config.AddExtensionOption("sap_sysnr", "System number on the application server", LogicalType::VARCHAR);
        config.AddExtensionOption("sap_user", "Username to login", LogicalType::VARCHAR);
        config.AddExtensionOption("sap_password", "User password to login", LogicalType::VARCHAR);
        config.AddExtensionOption("sap_client", "Don't know yet", LogicalType::VARCHAR);
        config.AddExtensionOption("sap_lang", "Language of the user to connect with", LogicalType::VARCHAR, Value("EN"));

        auto provider = make_uniq<RfcEnvironmentCredentialsProvider>(config);
        provider->SetAll();
    }

    static void RegisterFunctions(DatabaseInstance &instance)
    {
        Connection con(instance);
        con.BeginTransaction();

        auto &context = *con.context;
        auto &catalog = Catalog::GetSystemCatalog(context);

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

        auto rfc_ping_pragma = PragmaFunction::PragmaStatement("sap_rfc_ping", RfcPing);
        CreatePragmaFunctionInfo rfc_ping_info(rfc_ping_pragma);
        catalog.CreatePragmaFunction(context, rfc_ping_info);

        auto rfc_function_desc_pragma = PragmaFunction::PragmaCall("sap_rfc_function_desc", RfcFunctionDesc, {LogicalType::VARCHAR});
        CreatePragmaFunctionInfo rfc_foo_info(rfc_function_desc_pragma);
        catalog.CreatePragmaFunction(context, rfc_foo_info);

        con.Commit();
    }

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

    static void LoadInternal(DatabaseInstance &instance) 
    {  
        RegisterConfiguration(instance);
        RegisterFunctions(instance);
        RegisterBicsFunctions(instance);
    }

    void ErplExtension::Load(DuckDB &db) {
        LoadInternal(*db.instance);
    }

    std::string ErplExtension::Name() {
        return "erpl";
    }

} // namespace duckdb

extern "C" {
    DUCKDB_EXTENSION_API void erpl_init(duckdb::DatabaseInstance &db) {
        LoadInternal(db);
    }

    DUCKDB_EXTENSION_API const char *erpl_version() {
        return duckdb::DuckDB::LibraryVersion();
    }
}

#ifndef DUCKDB_EXTENSION_MAIN
#error DUCKDB_EXTENSION_MAIN not defined
#endif