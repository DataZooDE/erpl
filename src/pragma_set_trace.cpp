#include "pragma_set_trace.hpp"
#include "duckdb/parser/parsed_data/create_pragma_function_info.hpp"
#include "sapnwrfc.h"

namespace duckdb 
{
    std::tuple<unsigned int, std::string> GetTraceLevelFromEnum(const Value &trace_level_enum) 
    {
        auto trace_level = trace_level_enum.GetValue<unsigned int>();
        auto trace_level_str = trace_level_enum.ToString();
        return std::make_tuple(trace_level, trace_level_str);
    }

    string PragmaSetTraceLevel(ClientContext &context, const FunctionParameters &parameters) 
    {
        RFC_RC rc = RFC_OK;
        RFC_ERROR_INFO error_info;

        auto [trace_level, trace_level_str] = GetTraceLevelFromEnum(parameters.values[0]);
        
        rc = RfcSetTraceLevel(NULL, NULL, trace_level, &error_info);
        if (rc != RFC_OK) {
            throw Exception(StringUtil::Format("Failed to set trace level: %s", uc2std(error_info.message)));
        }

        auto tl_query = StringUtil::Format(StringUtil::Format("SELECT '%s (%d)' as trace_level", trace_level_str, trace_level));
	    return tl_query;
    }

    string PragmaSetTraceDir(ClientContext &context, const FunctionParameters &parameters) 
    {
        RFC_RC rc = RFC_OK;
        RFC_ERROR_INFO error_info;

        auto trace_dir = std2uc(parameters.values[0].ToString());
        rc = RfcSetTraceDir(trace_dir.get(), &error_info);
        if (rc != RFC_OK) {
            throw Exception(StringUtil::Format("Failed to set trace directory: %s", uc2std(error_info.message)));
        }

        auto td_query = StringUtil::Format(StringUtil::Format("SELECT '%s' as trace_dir", uc2std(trace_dir.get())));
        return td_query;
    }

    string PragmaSetMaximumTraceFileSize(ClientContext &context, const FunctionParameters &parameters) 
    {
        RFC_RC rc = RFC_OK;
        RFC_ERROR_INFO error_info;

        auto trace_file_size = parameters.values[0].GetValue<unsigned int>();
        SAP_UC trace_size_unit = parameters.values[1].ToString()[0];

        rc = RfcSetMaximumTraceFileSize(trace_file_size, trace_size_unit, &error_info);
        if (rc != RFC_OK) {
            throw Exception(StringUtil::Format("Failed to set trace file size: %s", uc2std(error_info.message)));
        }

        auto tfs_query = StringUtil::Format(StringUtil::Format("SELECT '%d' as trace_file_size", trace_file_size));
        return tfs_query;
    }

    string PragmaSetMaximumStoredTraceFiles(ClientContext &context, const FunctionParameters &parameters)
    {
        RFC_RC rc = RFC_OK;
        RFC_ERROR_INFO error_info;

        auto max_stored_trace_files = parameters.values[0].GetValue<unsigned int>();

        rc = RfcSetMaximumStoredTraceFiles(max_stored_trace_files, &error_info);
        if (rc != RFC_OK) {
            throw Exception(StringUtil::Format("Failed to set maximum stored trace files: %s", uc2std(error_info.message)));
        }

        auto mstf_query = StringUtil::Format(StringUtil::Format("SELECT '%d' as max_stored_trace_files", max_stored_trace_files));
        return mstf_query;
    }

    CreatePragmaFunctionInfo CreateRfcSetTraceLevelPragma() 
    {
        auto trace_level_enum = Vector(LogicalType::VARCHAR, 2);
        trace_level_enum.SetValue(0, Value("Off"));
        trace_level_enum.SetValue(1, Value("Brief"));
        trace_level_enum.SetValue(2, Value("Verbose"));
        trace_level_enum.SetValue(3, Value("Detailed"));
        trace_level_enum.SetValue(4, Value("Full"));

        auto rfc_set_trace_level = PragmaFunction::PragmaCall("sap_rfc_set_trace_level", 
                                                              PragmaSetTraceLevel, 
                                                              { LogicalType::ENUM("TRACE_LEVEL", trace_level_enum, 5) });

        CreatePragmaFunctionInfo rfc_set_trace_level_info(rfc_set_trace_level);
        return rfc_set_trace_level_info;
    }

    CreatePragmaFunctionInfo CreateRfcSetTraceDirPragma() 
    {
        auto rfc_set_trace_dir = PragmaFunction::PragmaCall("sap_rfc_set_trace_dir", 
                                                            PragmaSetTraceDir, 
                                                            { LogicalType::VARCHAR });

        CreatePragmaFunctionInfo rfc_set_trace_dir_info(rfc_set_trace_dir);
        return rfc_set_trace_dir_info;
    }

    CreatePragmaFunctionInfo CreateRfcSetMaximumTraceFileSizePragma() 
    {
        auto trace_size_unit_enum = Vector(LogicalType::VARCHAR, 2);
        trace_size_unit_enum.SetValue(0, Value("M"));
        trace_size_unit_enum.SetValue(1, Value("G"));

        auto rfc_set_maximum_trace_file_size = PragmaFunction::PragmaCall("sap_rfc_set_maximum_trace_file_size", 
                                                                          PragmaSetMaximumTraceFileSize, 
                                                                          { LogicalType::INTEGER, 
                                                                            LogicalType::ENUM("SIZE_UNIT", trace_size_unit_enum, 2) });

        CreatePragmaFunctionInfo rfc_set_maximum_trace_file_size_info(rfc_set_maximum_trace_file_size);
        return rfc_set_maximum_trace_file_size_info;
    }

    CreatePragmaFunctionInfo CreateRfcSetMaximumStoredTraceFilesPragma() 
    {
        auto rfc_set_maximum_stored_trace_files = PragmaFunction::PragmaCall("sap_rfc_set_maximum_stored_trace_files", 
                                                                             PragmaSetMaximumStoredTraceFiles, 
                                                                             { LogicalType::INTEGER });

        CreatePragmaFunctionInfo rfc_set_maximum_stored_trace_files_info(rfc_set_maximum_stored_trace_files);
        return rfc_set_maximum_stored_trace_files_info;
    }

} // namespace duckdb