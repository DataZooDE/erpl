#pragma once

#include "duckdb.hpp"

namespace duckdb 
{
    class ArgBuilder 
    {
        public:
            ArgBuilder();
            ArgBuilder& Add(const std::string& name, const Value& value);
            ArgBuilder& Add(const std::string& name, Value& value);
            ArgBuilder& Add(const std::string& name, const ArgBuilder& builder);
            Value Build() const;
            std::vector<Value> BuildArgList() const;
            Value& operator[](const std::string& name);
        private:
            child_list_t<Value> _args;
    };

    class ValueHelper
    {
        public:
            ValueHelper(Value &value);
            Value operator[](const std::string& name);

            static std::vector<std::string> ParseJsonPointer(std::string path);
            static Value GetValueForPath(Value &value, std::vector<std::string> &tokens);
        private:
            Value &_value;
    };

    bool HasParam(named_parameter_map_t& named_params, const std::string& name);
    Value ConvertBoolArgument(named_parameter_map_t& named_params, const std::string& name, const bool default_value); 

} // namespace duckdb