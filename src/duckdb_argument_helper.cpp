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

    std::vector<Value> ArgBuilder::BuildArgList() const {
        return std::vector<Value>({ Build() });
    }

    // ArgBuilder ---------------------------------------------------------------

    ValueHelper::ValueHelper(Value &value) : _value(value) 
    {}

    Value ValueHelper::operator[](const std::string& name) {
        auto tokens = ParseJsonPointer(name);
        return GetValueForPath(_value, tokens);
    }

    Value ValueHelper::GetValueForPath(Value &value, std::vector<string> &tokens) 
    {
        if (tokens.size() == 0) {
            return value;
        }

        auto token_it = tokens.begin();
        auto remaining_tokens = std::vector<string>(token_it + 1, tokens.end());
        
        auto value_type = value.type();
        switch(value_type.id()) 
        {
            case LogicalTypeId::STRUCT:
            {
                auto child_values = StructValue::GetChildren(value);
                for (auto i = 0; i < child_values.size(); i++) {
                    auto child_name = StructType::GetChildName(value_type, i);
                    if (child_name == *token_it) {
                        auto child_value = child_values[i];
                        
                        return GetValueForPath(child_value, remaining_tokens);
                    }
                }
                throw std::runtime_error(StringUtil::Format("Field '%s' not found in struct", *token_it));
            }
            case LogicalTypeId::LIST:
            {
                auto child_values = ListValue::GetChildren(value);
                auto child_index = std::stoi(*token_it);
                if (child_index >= child_values.size()) {
                    throw std::runtime_error(StringUtil::Format("Index '%s' out of bounds for list", *token_it));
                }
                auto child_value = child_values[child_index];
                return GetValueForPath(child_value, remaining_tokens);
            }
                
            default:
                throw std::runtime_error(StringUtil::Format("Field '%s' is not a container type", *token_it));
        };
    }

    /**
     * Parses a JSON Pointer string into a vector of string tokens.
     *
     * A JSON Pointer is a string syntax for identifying a specific value within a JSON document.
     * This function takes a JSON Pointer string and returns a vector of string tokens, where each token
     * represents a key or index in the JSON document.
     *
     * @param path The JSON Pointer string to parse.
     * @return A vector of string tokens representing the keys or indices in the JSON document.
     *
     * @example
     * std::string path = "/foo/0/bar";
     * std::vector<std::string> tokens = ParseJsonPointer(path);
     * // tokens = {"foo", "0", "bar"}
     *
     * @example
     * std::string path = "/a~1b/c~0d";
     * std::vector<std::string> tokens = ParseJsonPointer(path);
     * // tokens = {"a/b", "c~d"}
     */
    std::vector<std::string> ValueHelper::ParseJsonPointer(std::string path) 
    {
        std::vector<std::string> tokens;
        std::string token;
        std::istringstream token_stream(path);

        while (std::getline(token_stream, token, '/')) {
            // Unescape ~1 and ~0 sequences.
            size_t pos = 0;
            while ((pos = token.find("~1", pos)) != std::string::npos) {
                token.replace(pos, 2, "/");
                pos += 1;
            }
            pos = 0;
            while ((pos = token.find("~0", pos)) != std::string::npos) {
                token.replace(pos, 2, "~");
                pos += 1;
            }

            if (! token.empty()) {
                tokens.push_back(token);
            }
        }

        return tokens;
    }

    // ValueHelper ---------------------------------------------------------------

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