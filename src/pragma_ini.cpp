#include "pragma_ini.hpp"
#include "duckdb/parser/parsed_data/create_pragma_function_info.hpp"
#include "sapnwrfc.h"

namespace duckdb 
{
    string PragmaSetIniPath(ClientContext &context, const FunctionParameters &parameters)
    {
        RFC_RC rc = RFC_OK;
        RFC_ERROR_INFO error_info;

        auto ini_path = std2uc(parameters.values[0].ToString());
        rc = RfcSetIniPath(ini_path.get(), &error_info);
        if (rc != RFC_OK) {
            throw Exception(StringUtil::Format("Failed to set ini path: %s", uc2std(error_info.message)));
        }

        auto ip_query = StringUtil::Format(StringUtil::Format("SELECT '%s' as ini_path", uc2std(ini_path.get())));
        return ip_query;
    }

    string PragmaReloadIniFile(ClientContext &context, const FunctionParameters &parameters)
    {
        RFC_RC rc = RFC_OK;
        RFC_ERROR_INFO error_info;

        rc = RfcReloadIniFile(&error_info);
        if (rc != RFC_OK) {
            throw Exception(StringUtil::Format("Failed to reload ini file: %s", uc2std(error_info.message)));
        }

        auto rif_query = StringUtil::Format(StringUtil::Format("SELECT 'Reloaded ini file' as reload_ini_file"));
        return rif_query;
    }

    CreatePragmaFunctionInfo CreateRfcSetIniPathPragma()
    {
        auto pragma_function = PragmaFunction::PragmaCall("sap_rfc_set_ini_path", PragmaSetIniPath, { LogicalType::VARCHAR });
        CreatePragmaFunctionInfo info(pragma_function);

        return info;
    }

    CreatePragmaFunctionInfo CreateRfcReloadIniFilePragma()
    {
        auto pragma_function = PragmaFunction::PragmaStatement("sap_rfc_reload_ini_file", PragmaReloadIniFile);
        CreatePragmaFunctionInfo info(pragma_function);

        return info;
    }
}