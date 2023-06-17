#include "duckdb_argument_helper.hpp"

namespace duckdb 
{   
    ArgBuilder::ArgBuilder() {}

    ArgBuilder& ArgBuilder::Add(const std::string& name, const Value &value) {
        _args.push_back(std::make_pair(name, value));
        return *this;
    }

    ArgBuilder& ArgBuilder::Add(const std::string& name, Value &value) {
        _args.push_back(std::make_pair(name, value));
        return *this;
    }

    ArgBuilder& ArgBuilder::Add(const std::string& name, const ArgBuilder& builder) {
        _args.push_back(std::make_pair(name, builder.Build()));
        return *this;
    }

    Value ArgBuilder::Build() const {
        return Value::STRUCT(_args);
    }

    bool HasParam(named_parameter_map_t& named_params, const std::string& name) {
        return named_params.find(name) != named_params.end();
    };

    Value ConvertBoolArgument(named_parameter_map_t& named_params, const std::string& name, const bool default_value) {
        auto value = HasParam(named_params, name) ? BooleanValue::Get(named_params[name]) : default_value;

        if (value == true) {
            return Value::CreateValue("X");
        } else {
            return Value::CreateValue("");
        }
    }
} // namespace duckdb