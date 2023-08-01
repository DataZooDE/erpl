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
            ValueHelper(const ValueHelper &h) = default;
            Value operator[](const std::string& name);

            static std::vector<std::string> ParseJsonPointer(std::string path);
            static Value GetValueForPath(Value &value, std::vector<std::string> &tokens);
            static bool IsX(Value &value);
            static bool IsX(const Value &value);

            Value &Get() const;
            void Print() const;
            void Print(std::string path) const;
        private:
            Value &_value;
    };

    bool HasParam(named_parameter_map_t& named_params, const std::string& name);
    Value ConvertBoolArgument(named_parameter_map_t& named_params, const std::string& name, const bool default_value); 

} // namespace duckdb