#include "duckdb_argument_helper.hpp"
#include "duckdb_serialization_helper.hpp"

namespace duckdb 
{   
    ArgBuilder::ArgBuilder() {}

    ArgBuilder& ArgBuilder::Add(const std::string& name, const Value &value) 
    {
        _args.push_back(std::make_pair(name, value));
        return *this;
    }

    ArgBuilder& ArgBuilder::Add(const std::string& name, Value &value) 
    {
        _args.push_back(std::make_pair(name, value));
        return *this;
    }

    ArgBuilder& ArgBuilder::Add(const std::string& name, const ArgBuilder& builder) 
    {
        _args.push_back(std::make_pair(name, builder.Build()));
        return *this;
    }

    ArgBuilder& ArgBuilder::Add(const std::string& name, duckdb::vector<Value> &values) 
    {
        if (values.size() == 0) {
            return Add(name, Value::LIST(LogicalTypeId::ANY, vector<Value>()));
        }

        auto list_value = Value::LIST(values);
        return Add(name, list_value);
    }

    ArgBuilder& ArgBuilder::Add(const std::string& name, std::vector<Value> &std_vector) 
    {
        duckdb::vector<Value> duck_vector;
        for (auto &v: std_vector) {
            duck_vector.push_back(v);
        }
        return Add(name, duck_vector);
    }

    ArgBuilder& ArgBuilder::Add(const std::string& name, const std::initializer_list<Value> &values) 
    {
        auto list_value = Value::LIST(values);
        return Add(name, list_value);
    }

    Value ArgBuilder::Build() const {
        return Value::STRUCT(_args);
    }

    std::vector<Value> ArgBuilder::BuildArgList() const {
        return std::vector<Value>({ Build() });
    }

    // ArgBuilder ---------------------------------------------------------------

    ValueHelper::ValueHelper(Value &value) : _value(value) 
    { }

    ValueHelper::ValueHelper(Value &value, std::vector<std::string> &root_path) 
        : _value(value), _root_path(root_path) 
    { }

    ValueHelper::ValueHelper(Value &value, const std::string &root_path) 
        : _value(value), _root_path(ParseJsonPointer(root_path)) 
    { }

    Value ValueHelper::operator[](const std::string& name) 
    {
        auto full_path = GetPathWithRoot(name);
        return GetValueForPath(_value, full_path);
    }

    std::vector<std::string> ValueHelper::GetPathWithRoot(std::string path) 
    {
        auto json_path = ParseJsonPointer(path);
    
        std::vector<std::string> full_path;
        full_path.reserve(_root_path.size() + json_path.size());       
        full_path.insert(full_path.end(), _root_path.begin(), _root_path.end());
        full_path.insert(full_path.end(), json_path.begin(), json_path.end());

        return full_path;
    }

    /**
     * Gets a value from a struct by JSON Path tokens
     * 
     * The functionality depends on whether the value is a struct or a list.
     *  - If the value is a struct, the first token is used to find the child value with the matching name.
     *  - If the value is a list, the first token is used to find the child value with the matching index.
     * 
     * @param value The struct value to get the value from.
     * @param tokens A vector of string tokens representing the keys or indices in the JSON document.
    */
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
                for (std::size_t i = 0; i < child_values.size(); i++) {
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
                std::size_t child_index = std::stoi(*token_it);
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

    Value ValueHelper::CreateMutatedValue(Value &old_value, Value &new_value, std::string path) 
    {
        auto tokens = ParseJsonPointer(path);
        return CreateMutatedValue(old_value, new_value, tokens);
    }

    Value ValueHelper::CreateMutatedValue(Value &old_value, Value &new_value, std::vector<std::string> &tokens) 
    {
        if (tokens.size() == 0) {
            return new_value;
        }

        auto token_it = tokens.begin();
        auto remaining_tokens = std::vector<string>(token_it + 1, tokens.end());

        auto value_type = old_value.type();
        switch(value_type.id()) 
        {
            case LogicalTypeId::STRUCT:
            {
                auto child_values = StructValue::GetChildren(old_value);
                auto new_child_pairs = child_list_t<Value>();

                for (std::size_t i = 0; i < child_values.size(); i++) {
                    auto child_name = StructType::GetChildName(value_type, i);
                    auto new_child_value = child_name == *token_it 
                                            ? CreateMutatedValue(child_values[i], new_value, remaining_tokens) 
                                            : child_values[i];
                    new_child_pairs.push_back(std::make_pair(child_name, new_child_value));
                }
                return Value::STRUCT(new_child_pairs);
            }
            case LogicalTypeId::LIST:
            {
                auto child_values = ListValue::GetChildren(old_value);
                std::size_t child_index = std::stoi(*token_it);
                if (child_index >= child_values.size()) {
                    throw std::runtime_error(StringUtil::Format("Index '%s' out of bounds for list", *token_it));
                }
                child_values[child_index] = CreateMutatedValue(child_values[child_index], new_value, remaining_tokens);
                return Value::LIST(child_values);
            }
                
            default:
                throw std::runtime_error(StringUtil::Format("Field '%s' is not a container type", *token_it));
        };
    }

    Value ValueHelper::AddToList(Value &current_list, Value &new_value) 
    {
        if (current_list.type().id() != LogicalTypeId::LIST) {
            throw std::runtime_error("First argument is not a list");
        }

        auto list_values = ListValue::GetChildren(current_list);
        if (list_values.size() > 0)
        {
            auto current_type = list_values[0].type();
            auto casted_type = new_value.DefaultCastAs(current_type);
            list_values.push_back(new_value);
        }
        else
        {
            list_values.push_back(new_value);
        }

        return Value::LIST(list_values);
    }

    Value ValueHelper::RemoveFromList(Value &current_list, Value &remove_value) 
    {
        if (current_list.type().id() != LogicalTypeId::LIST) {
            throw std::runtime_error("First argument is not a list");
        }

        auto list_values = ListValue::GetChildren(current_list);
        auto new_list_values = duckdb::vector<Value>();

        for (auto &v: list_values) {
            if (v != remove_value) {
                new_list_values.push_back(v);
            }
        }

        return Value::LIST(remove_value.type(), new_list_values);
    }

    Value ValueHelper::RemoveFromList(Value &current_list, unsigned int index_to_remove) 
    {
        if (current_list.type().id() != LogicalTypeId::LIST) {
            throw std::runtime_error("First argument is not a list");
        }

        auto list_values = ListValue::GetChildren(current_list);
        auto new_list_values = duckdb::vector<Value>();

        for (std::size_t i = 0; i < list_values.size(); i++) {
            if (i != index_to_remove) {
                new_list_values.push_back(list_values[i]);
            }
        }

        return Value::LIST(new_list_values);
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

    bool ValueHelper::IsX(Value &value) {
        return value.ToString() == "X";
    }

    bool ValueHelper::IsX(const Value &value) {
        return value.ToString() == "X";
    }

    Value &ValueHelper::Get()
    {
        return _value;
    }

    void ValueHelper::Print() {
        _value.Print();
    }

    void ValueHelper::Print(std::string path) {
        auto full_path = GetPathWithRoot(path);
        auto val = GetValueForPath(_value, full_path);
        val.Print();
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

// TableWrapper ---------------------------------------------------------------

TableWrapper::TableWrapper(std::shared_ptr<Value> struct_oriented_value, std::string root_path)
    : _struct_oriented_value(struct_oriented_value), _list_oriented_row_type(LogicalType::INVALID), _root_path(root_path)
{ }

duckdb::LogicalType TableWrapper::ListOrientedRowType() const
{
    if (_list_oriented_row_type == LogicalType::INVALID && _struct_oriented_value != nullptr) {
        _list_oriented_row_type = CreateListOrientedRowTypeFromStructOrientedValue(*_struct_oriented_value);
    }
    return _list_oriented_row_type;
}

idx_t TableWrapper::Size() const
{
    auto list_oriented_row_type = ListOrientedRowType();
    auto struct_children = StructValue::GetChildren(*_struct_oriented_value);
    if (struct_children.size() < 1) {
        throw std::runtime_error("Struct oriented value table must have at least one column.");
    }

    auto rows_of_first_column = ListValue::GetChildren(struct_children[0]);
    idx_t size = rows_of_first_column.size();
    return size;
}

duckdb::Value TableWrapper::operator[](const idx_t index) const
{
    return CreateListOrientedValueFromIndex(index);
}

duckdb::LogicalType TableWrapper::CreateListOrientedRowTypeFromStructOrientedValue(const duckdb::Value &value) const
{
    // This is a struct of lists, we want to convert this to a
    // struct type which is not list oriented.
    child_list_t<LogicalType> list_oriented_child_types;

    for (auto [child_name, child_type]: StructType::GetChildTypes(value.type())) {
        if (child_type.id() != LogicalTypeId::LIST) {
            throw std::runtime_error("All children of a struct must be lists");
        }

        auto list_type = ListType::GetChildType(child_type);
        list_oriented_child_types.push_back(std::make_pair(child_name, list_type));
    }

    return LogicalType::STRUCT(list_oriented_child_types);
}

duckdb::Value TableWrapper::CreateListOrientedValueFromIndex(const idx_t index) const
{
    auto all_columns = StructValue::GetChildren(*_struct_oriented_value);
    vector<Value> new_record_values;

    for (auto &column: all_columns) {
        auto row_values = ListValue::GetChildren(column);
        new_record_values.push_back(row_values[index]);
    }

    return Value::STRUCT(ListOrientedRowType(), new_record_values);
}

} // namespace duckdb