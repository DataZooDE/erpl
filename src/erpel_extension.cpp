#define DUCKDB_EXTENSION_MAIN


#include "duckdb.hpp"

#include "duckdb/catalog/catalog.hpp"
#include "duckdb/parser/parsed_data/create_table_function_info.hpp"
#include "duckdb/parser/parsed_data/create_pragma_function_info.hpp"

#include "erpel_extension.hpp"
#include "pragma_ping.hpp"
#include "scanner_invoke.hpp"
#include "scanner_describe_function.hpp"
#include "scanner_search_function.hpp"

#ifdef WITH_BICS
    #include "bics/include/scanner_search_infoprovider.hpp"
#endif

namespace duckdb {

    inline void QuackScalarFun(DataChunk &args, ExpressionState &state, Vector &result) 
    {
        auto &name_vector = args.data[0];
        UnaryExecutor::Execute<string_t, string_t>(
            name_vector, result, args.size(),
            [&](string_t name) { 
                return StringVector::AddString(result, "Quack "+name.GetString()+" 🐥");;
            });
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

        auto rfc_search_function_info = CreateRfcSearchFunctionScanFunction();
        catalog.CreateTableFunction(context, &rfc_search_function_info);

        auto rfc_describe_function_info = CreateRfcDescribeFunctionScanFunction();
        catalog.CreateTableFunction(context, &rfc_describe_function_info);

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

        auto rfc_search_infoprovider_info = CreateBicsSearchInfoProviderScanFunction();
        catalog.CreateTableFunction(context, &rfc_search_infoprovider_info);

        con.Commit();
    }

    static void LoadInternal(DatabaseInstance &instance) 
    {  
        RegisterConfiguration(instance);
        RegisterFunctions(instance);
        RegisterBicsFunctions(instance);
    }

    void ErpelExtension::Load(DuckDB &db) {
        LoadInternal(*db.instance);
    }

    std::string ErpelExtension::Name() {
        return "erpel";
    }

} // namespace duckdb

extern "C" {
    DUCKDB_EXTENSION_API void erpel_init(duckdb::DatabaseInstance &db) {
        LoadInternal(db);
    }

    DUCKDB_EXTENSION_API const char *erpel_version() {
        return duckdb::DuckDB::LibraryVersion();
    }
}

#ifndef DUCKDB_EXTENSION_MAIN
#error DUCKDB_EXTENSION_MAIN not defined
#endif
