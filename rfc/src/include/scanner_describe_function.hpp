#pragma once

#include "duckdb.hpp"
#include "sapnwrfc.h"

#include "duckdb/parser/parsed_data/create_table_function_info.hpp"

#include "sap_connection.hpp"
#include "sap_function.hpp"
#include "duckdb_argument_helper.hpp"

namespace duckdb 
{
class RfcDescribeFunctionBindData : public TableFunctionData
{
public:
    RfcDescribeFunctionBindData(std::shared_ptr<RfcFunction> function, std::shared_ptr<Value> RPY_FUNCTIONMODULE_READ)
        : _function(function), _RPY_FUNCTIONMODULE_READ(RPY_FUNCTIONMODULE_READ), _done(false)
    { }

    static std::unique_ptr<RfcDescribeFunctionBindData> FromFunctionHandleAndResultSet(std::shared_ptr<RfcFunction> function, 
                                                                                       std::shared_ptr<RfcResultSet> result_set) 
    {
        auto result_value = std::make_shared<Value>(result_set->GetResultValue());
        return std::make_unique<RfcDescribeFunctionBindData>(function, result_value);
    }

    std::vector<std::string> GetResultNames() 
    {
        std::vector<std::string> names = { 
            "name", 
            "text",
            "function_group",
            "remote_callable",
            "import", 
            "export", 
            "changing", 
            "tables", 
            "source" 
        };

        return names;
    }

    std::vector<LogicalType> GetResultTypes() 
    {
        auto param_struct = GetParamterResultType();

        std::vector<LogicalType> types = { 
            LogicalType::VARCHAR,            // name
            LogicalType::VARCHAR,            // text
            LogicalType::VARCHAR,            // function_group
            LogicalType::VARCHAR,            // remote_callable
            LogicalType::LIST(param_struct), // import
            LogicalType::LIST(param_struct), // export
            LogicalType::LIST(param_struct), // changing
            LogicalType::LIST(param_struct), // tables
            LogicalType::VARCHAR             // source
        };
       
        return types;
    }

    bool HasMoreResults() 
    {
        return _done == false;
    }

    void FetchNextResult(DataChunk &output) 
    {
        auto param_infos = _function->GetParameterInfos();
        std::vector<RfcFunctionParameterDesc> sel_infos;

        // name
        output.SetValue(0, 0, Value(_function->GetName()));

        // text
        output.SetValue(1, 0, ShortText());

        // function_group
        output.SetValue(2, 0, FunctionPool());

        // remote_callable
        output.SetValue(3, 0, RemoteCall());

        // import
        sel_infos.clear();
        std::copy_if(param_infos.begin(), param_infos.end(), std::back_inserter(sel_infos), 
                      [](RfcFunctionParameterDesc &x) { return x.GetDirection() == RFC_IMPORT; });
        output.SetValue(4, 0, ConvertParameterInfos(sel_infos));

        // export
        sel_infos.clear();
        std::copy_if(param_infos.begin(), param_infos.end(), std::back_inserter(sel_infos), 
                      [](RfcFunctionParameterDesc &x) { return x.GetDirection() == RFC_EXPORT; });
        output.SetValue(5, 0, ConvertParameterInfos(sel_infos));

        // changing
        sel_infos.clear();
        std::copy_if(param_infos.begin(), param_infos.end(), std::back_inserter(sel_infos), 
                      [](RfcFunctionParameterDesc &x) { return x.GetDirection() == RFC_CHANGING; });
        output.SetValue(6, 0, ConvertParameterInfos(sel_infos));

        // tables
        sel_infos.clear();
        std::copy_if(param_infos.begin(), param_infos.end(), std::back_inserter(sel_infos), 
                      [](RfcFunctionParameterDesc &x) { return x.GetDirection() == RFC_TABLES; });
        output.SetValue(7, 0, ConvertParameterInfos(sel_infos));

        // source
        output.SetValue(8, 0, Source());

        _done = true;
        output.SetCardinality(1);
    }

private:
    std::shared_ptr<RfcFunction> _function;
    std::shared_ptr<Value> _RPY_FUNCTIONMODULE_READ;
    bool _done;

    ValueHelper Helper() 
    {
        return ValueHelper(*_RPY_FUNCTIONMODULE_READ);
    }

    LogicalType GetParamterResultType() 
    {
        auto param_struct = LogicalType::STRUCT({ 
            { "name",           LogicalTypeId::VARCHAR },
            { "text",           LogicalTypeId::VARCHAR },
            { "abap_type",      LogicalTypeId::VARCHAR },
            { "duckdb_type",    LogicalTypeId::VARCHAR },
            { "direction",      LogicalTypeId::VARCHAR},
            { "length",         LogicalTypeId::INTEGER },
            { "decimals",       LogicalTypeId::INTEGER },
            { "default_value",  LogicalTypeId::VARCHAR },
            { "optional",       LogicalTypeId::BOOLEAN },
        });
        return param_struct;
    }

    Value ConvertParameterInfos(std::vector<RfcFunctionParameterDesc> &param_infos) 
    {
        vector<Value> param_values;
        for (auto &param_info : param_infos) {
            param_values.push_back(ConvertParameterInfo(param_info));
        }

        const auto param_type = GetParamterResultType();
        return Value::LIST(param_type, param_values);
    }

    Value ConvertParameterInfo(RfcFunctionParameterDesc &param_info) 
    {
        const auto param_type = GetParamterResultType();

        auto rfc_type = param_info.GetRfcType();
        auto duck_type = rfc_type->CreateDuckDbType();

        child_list_t<Value> param_value {
            make_pair("name", Value(param_info.GetName())),
            make_pair("text", Value(param_info.GetDescription())),
            make_pair("abap_type", Value(rfc_type->GetName())),
            make_pair("duckdb_type", Value(duck_type.ToString())),
            make_pair("direction", Value(param_info.GetDirectionAsString())),
            make_pair("length", Value::INTEGER(param_info.GetLength())),
            make_pair("decimals", Value::INTEGER(param_info.GetDecimals())),
            make_pair("default_value", Value(param_info.GetDefaultValue())),
            make_pair("optional", Value(param_info.IsOptional()))
        };
        
        return Value::STRUCT(param_value);
    }

    Value ShortText() { return Helper()["SHORT_TEXT"]; }
    Value RemoteCall() { return Helper()["REMOTE_CALL"]; }
    Value FunctionPool() { return Helper()["FUNCTION_POOL"]; }
    Value Source() 
    {
        std::stringstream ss;

        auto source_lines = ListValue::GetChildren(Helper()["SOURCE"]);
        for (auto &line : source_lines) 
        {
            ss << ValueHelper(line)["LINE"].ToString() << std::endl;
        }

        return Value(ss.str());
    }
};

TableFunction CreateRfcDescribeFunctionScanFunction();
} // namespace duckdb