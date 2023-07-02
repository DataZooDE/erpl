#pragma once

#include "duckdb.hpp"

namespace duckdb {

    class ArgBuilder {
    public:
        ArgBuilder();
        ArgBuilder& Add(const std::string& name, const Value& value);
        ArgBuilder& Add(const std::string& name, Value& value);
        ArgBuilder& Add(const std::string& name, const ArgBuilder& builder);
        Value Build() const;
        std::vector<Value> BuildArgList() const;
    private:
        child_list_t<Value> _args;
    };


    bool HasParam(named_parameter_map_t& named_params, const std::string& name);
    Value ConvertBoolArgument(named_parameter_map_t& named_params, const std::string& name, const bool default_value); 

} // namespace duckdb