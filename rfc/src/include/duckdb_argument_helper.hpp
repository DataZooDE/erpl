#pragma once

#include "duckdb.hpp"

namespace duckdb
{
class ArgBuilder
{
public:
    ArgBuilder();
    ArgBuilder &Add(const std::string &name, const Value &value);
    ArgBuilder &Add(const std::string &name, Value &value);
    ArgBuilder &Add(const std::string &name, const ArgBuilder &builder);
    ArgBuilder &Add(const std::string &name, duckdb::vector<Value> &values);
    ArgBuilder &Add(const std::string &name, std::vector<Value> &values);
    ArgBuilder &Add(const std::string &name, const std::initializer_list<Value> &values);

    Value Build() const;
    std::vector<Value> BuildArgList() const;
    Value &operator[](const std::string &name);

private:
    child_list_t<Value> _args;
};

// ------------------------------------------------------------------------------------------------

class ValueHelper
{
public:
    ValueHelper(Value &value);
    ValueHelper(Value &value, const std::string &root_path);
    ValueHelper(Value &value, std::vector<std::string> &root_path);
    ValueHelper(const ValueHelper &h) = default;
    Value operator[](const std::string &name);

    static std::vector<std::string> ParseJsonPointer(std::string path);
    static Value GetValueForPath(Value &value, std::vector<std::string> &tokens);
    
    static bool IsX(Value &value);
    static bool IsX(const Value &value);

    Value &Get();
    void Print();
    void Print(std::string path);

public:
    static Value CreateMutatedValue(Value &old_value, Value &new_value, std::vector<std::string> &tokens);
    static Value CreateMutatedValue(Value &old_value, Value &new_value, std::string path);
    static Value AddToList(Value &current_list, Value &new_value);
    static Value RemoveFromList(Value &current_list, Value &value_to_remove);
    static Value RemoveFromList(Value &current_list, unsigned int index_to_remove);

private:
    Value &_value;
    std::vector<std::string> _root_path;

    std::vector<std::string> GetPathWithRoot(std::string path);
};

bool HasParam(named_parameter_map_t &named_params, const std::string &name);
Value ConvertBoolArgument(named_parameter_map_t &named_params, const std::string &name, const bool default_value);

template<typename T>
std::vector<T> ConvertListValueToVector(Value &value) {
    std::vector<T> result;
    for (auto &v: ListValue::GetChildren(value)) {
        result.push_back(v.GetValue<T>());
    }
    return result;
}

// ------------------------------------------------------------------------------------------------

template <typename TRow>
class GenericTable 
{
public:
    GenericTable(std::shared_ptr<Value> value) 
        : _value(value) 
    {
        GenerateRowsFromVal();
    }

    GenericTable(std::shared_ptr<Value> value, std::string root_path) 
        : _value(value), _root_path(root_path)
    {
        GenerateRowsFromRootValWithPath();
    }

    unsigned int Count() const {
        return _items.size();
    }

    std::vector<TRow> Rows() const {
        return _items;
    }

    TRow operator[](idx_t index) const 
    {
        if (index >= _items.size()) {
            throw std::out_of_range("Index out of range for this table");
        }

        return _items[index];
    }

protected:
    std::shared_ptr<Value> _value;
    std::string _root_path;
    std::vector<TRow> _items;

    void GenerateRowsFromVal() 
    {
        _items.clear();
        auto row_vals = ListValue::GetChildren(*_value);
        for (auto &row_val : row_vals) {
            auto item = TRow(std::make_shared<Value>(row_val));
            _items.push_back(item);
        }
    }

    void GenerateRowsFromRootValWithPath()
    {
        _items.clear();
        auto list_val = ValueHelper(*_value)[_root_path];
        auto row_vals = ListValue::GetChildren(list_val);
        for (unsigned int i = 0; i < row_vals.size(); i++) {
            auto path = StringUtil::Format("%s/%d", _root_path, i);
            auto item = TRow(_value, path);
            _items.push_back(item);
        }   
    }
};

// ------------------------------------------------------------------------------------------------

class ValueWrapper 
{
public:
    ValueWrapper(std::shared_ptr<Value> value) 
        : _value(value) 
    { }

    ValueWrapper(std::shared_ptr<Value> value, std::string root_path) 
        : _value(value), _root_path(root_path) 
    { }

    inline ValueHelper Helper() const {
        return ValueHelper(*_value, _root_path);
    }

    bool IsMutable() const {
        return ! _root_path.empty();
    }

protected:
    std::shared_ptr<Value> _value;
    std::string _root_path;
};

// ------------------------------------------------------------------------------------------------

class TableWrapper
{
public:
    TableWrapper(std::shared_ptr<Value> value, std::string root_path = std::string());

    idx_t Size() const;
    duckdb::LogicalType ListOrientedRowType() const;

    duckdb::Value operator[](const idx_t index) const;
private:
    duckdb::LogicalType CreateListOrientedRowTypeFromStructOrientedValue(const duckdb::Value &value) const;
    duckdb::Value CreateListOrientedValueFromIndex(const idx_t index) const;

private:
    std::shared_ptr<Value> _struct_oriented_value;
    mutable duckdb::LogicalType _list_oriented_row_type;
    std::string _root_path;
};

} // namespace duckdb