#include "duckdb_json_helper.hpp"
#include "duckdb/common/types/blob.hpp"
#include "yyjson.hpp"


namespace duckdb 
{
    std::string ErpelSerializer::Serialize(const Value &value) 
    {
        return Serialize(value, false);
    } 

    std::string ErpelSerializer::Serialize(const Value &value, bool include_type) 
    {
        auto doc = yyjson_mut_doc_new(NULL);
        
        yyjson_mut_val *root = Serialize(doc, value, include_type);
        yyjson_mut_doc_set_root(doc, root);
        auto flags = YYJSON_WRITE_PRETTY | YYJSON_WRITE_ESCAPE_UNICODE;
        const char *json = yyjson_mut_write(doc, flags, NULL);
        std::string result(json);
        free((void *)json);
        yyjson_mut_doc_free(doc);

        return result;
    }

    yyjson_mut_val *ErpelSerializer::Serialize(yyjson_mut_doc *doc, const Value &value, bool include_type)
    {
        auto type = value.type().id();
        yyjson_mut_val *ret = nullptr;
        switch (type) 
        {
            case LogicalTypeId::VARCHAR:
                ret = Serialize(doc, value.ToString());
                break;
            case LogicalTypeId::TINYINT:
                ret =  Serialize(doc, value.GetValue<int8_t>());
                break;
            case LogicalTypeId::SMALLINT:
                ret =  Serialize(doc, value.GetValue<int16_t>());
                break;
            case LogicalTypeId::INTEGER:
                ret =  Serialize(doc, value.GetValue<int32_t>());
                break;
            case LogicalTypeId::BIGINT:
                ret =  Serialize(doc, value.GetValue<int64_t>());
                break;
            case LogicalTypeId::DOUBLE:
                ret =  Serialize(doc, value.GetValue<double>());
                break;
            case LogicalTypeId::BOOLEAN:
                ret =  Serialize(doc, value.GetValue<bool>());
                break;
            case LogicalTypeId::SQLNULL:
                ret =  yyjson_mut_null(doc);
                break;
            case LogicalTypeId::DATE:
                ret =  Serialize(doc, value.GetValue<date_t>());
                break;
            case LogicalTypeId::TIME:
                ret =  Serialize(doc, value.GetValue<dtime_t>());
                break;
            case LogicalTypeId::TIMESTAMP:
                ret =  Serialize(doc, value.GetValue<timestamp_t>());
                break;
            case LogicalTypeId::BLOB:
                ret =  SerializeBlob(doc, value);
                break;
            case LogicalTypeId::STRUCT:
                ret =  SerializeStruct(doc, value, include_type);
                break;
            case LogicalTypeId::LIST:
                ret =  SerializeList(doc, value, include_type);
                break;
            default:
                throw NotImplementedException(StringUtil::Format("Type %s not implemented yet", value.type().ToString()));
        }

        if (include_type == true) {
            return WrapType(doc, type, ret);
        }
        else {
            return ret;
        }
    }

    yyjson_mut_val *ErpelSerializer::Serialize(yyjson_mut_doc *doc, const std::string &value) 
    {
        return yyjson_mut_strcpy(doc, value.c_str());
    }

    yyjson_mut_val *ErpelSerializer::Serialize(yyjson_mut_doc *doc, const int8_t &value) 
    {
        return yyjson_mut_sint(doc, value);
    }

    yyjson_mut_val *ErpelSerializer::Serialize(yyjson_mut_doc *doc, const int16_t &value) 
    {
        return yyjson_mut_sint(doc, value);
    }

    yyjson_mut_val *ErpelSerializer::Serialize(yyjson_mut_doc *doc, const int32_t &value) 
    {
        return yyjson_mut_int(doc, value);
    }

    yyjson_mut_val *ErpelSerializer::Serialize(yyjson_mut_doc *doc, const int64_t &value) 
    {
        return yyjson_mut_int(doc, value);
    }

    yyjson_mut_val *ErpelSerializer::Serialize(yyjson_mut_doc *doc, const uint8_t &value) 
    {
        return yyjson_mut_uint(doc, value);
    }

    yyjson_mut_val *ErpelSerializer::Serialize(yyjson_mut_doc *doc, const uint16_t &value) 
    {
        return yyjson_mut_uint(doc, value);
    }

    yyjson_mut_val *ErpelSerializer::Serialize(yyjson_mut_doc *doc, const uint32_t &value) 
    {
        return yyjson_mut_uint(doc, value);
    }

    yyjson_mut_val *ErpelSerializer::Serialize(yyjson_mut_doc *doc, const uint64_t &value) 
    {
        return yyjson_mut_uint(doc, value);
    }

    yyjson_mut_val *ErpelSerializer::Serialize(yyjson_mut_doc *doc, const float &value) 
    {
        return yyjson_mut_real(doc, value);
    }   

    yyjson_mut_val *ErpelSerializer::Serialize(yyjson_mut_doc *doc, const double &value) 
    {
        return yyjson_mut_real(doc, value);
    }

    yyjson_mut_val *ErpelSerializer::Serialize(yyjson_mut_doc *doc, const bool &value) 
    {
        return yyjson_mut_bool(doc, value);
    }

    yyjson_mut_val *ErpelSerializer::Serialize(yyjson_mut_doc *doc, const date_t &value) 
    {
        auto str = Date::ToString(value);
        return yyjson_mut_strcpy(doc, str.c_str());
    }

    yyjson_mut_val *ErpelSerializer::Serialize(yyjson_mut_doc *doc, const dtime_t &value) 
    {
        return Serialize(doc, value.micros);
    }

    yyjson_mut_val *ErpelSerializer::Serialize(yyjson_mut_doc *doc, const timestamp_t &value) 
    {
        auto str = Timestamp::ToString(value);
        return yyjson_mut_strcpy(doc, str.c_str());
    }

    yyjson_mut_val *ErpelSerializer::SerializeBlob(yyjson_mut_doc *doc, const Value &blob_value) 
    {
        auto result_size = Blob::ToBase64Size(blob_value.ToString());
        auto result_str = std::string(result_size, ' ');
		Blob::ToBase64(blob_value.ToString(), result_str.data());
		
        return Serialize(doc, result_str);
    }

    yyjson_mut_val *ErpelSerializer::SerializeStruct(yyjson_mut_doc *doc, const Value &struct_value, bool include_type) 
    {
        auto child_values = StructValue::GetChildren(struct_value);
        auto struct_type = struct_value.type();

        auto ret = yyjson_mut_obj(doc);
        for (auto i = 0; i < child_values.size(); i++) {
            auto child_name = Serialize(doc, StructType::GetChildName(struct_type, i));
            auto child_value = Serialize(doc, child_values[i], include_type);

            yyjson_mut_obj_add(ret, child_name, child_value);
        }

        return ret;
    }

    yyjson_mut_val *ErpelSerializer::SerializeList(yyjson_mut_doc *doc, const Value &list_value, bool include_type) 
    {
        auto child_values = ListValue::GetChildren(list_value);
        auto ret = yyjson_mut_arr(doc);

        for (auto i = 0; i < child_values.size(); i++) {
            auto child_value = Serialize(doc, child_values[i], include_type);
            yyjson_mut_arr_append(ret, child_value);
        }

        return ret;
    }

    yyjson_mut_val *ErpelSerializer::WrapType(yyjson_mut_doc *doc, const LogicalTypeId &type, yyjson_mut_val *value) 
    {
        auto type_str = LogicalType(type).ToString();
        auto wrapper = yyjson_mut_obj(doc);

        yyjson_mut_obj_add_val(doc, wrapper, "$type", yyjson_mut_strcpy(doc, type_str.c_str()));
        yyjson_mut_obj_add_val(doc, wrapper, "$value", value);

        return wrapper;
    }

    // ----------------------------------------------------------------------------------

    Value ErpelSerializer::Deserialize(const std::string &json) 
    {
        yyjson_doc *doc = yyjson_read(json.c_str(), json.size(), 0);
        yyjson_val *root = yyjson_doc_get_root(doc);

        auto ret = Deserialize(root);
        
        yyjson_doc_free(doc);
        return ret;
    }

    Value ErpelSerializer::Deserialize(yyjson_val *val) 
    {
        auto type_obj = yyjson_obj_get(val, "$type");
        auto value = yyjson_obj_get(val, "$value");

        if (type_obj == nullptr || value == nullptr) {
            throw std::invalid_argument("Invalid JSON format, you have to serialize with type information ($type and $value)");
        }

        auto type_str = std::string(yyjson_get_str(type_obj));
        Value ret = nullptr;
        if (type_str.empty()) {
            throw std::invalid_argument("Invalid JSON format, $type must be a string containing a DuckDB type");
        }
        else if (type_str == "VARCHAR") {
            ret = DeserializeString(type_str, value);
        }
        else if (type_str == "INTEGER") {
            ret =  DeserializeInteger(type_str, value);
        }
        else if (type_str == "DOUBLE") {
            ret =  DeserializeReal(type_str, value);
        }
        else if (type_str == "BOOLEAN") {
            ret =  DeserializeBoolean(type_str, value);
        }
        else if (type_str == "NULL") {
            ret =  Value();
        }
        else if (type_str == "DATE") {
            ret =  DeserializeDate(type_str, value);
        }
        else if (type_str == "TIME") {
            ret =  DeserializeTime(type_str, value);
        }
        else if (type_str == "TIMESTAMP") {
            ret =  DeserializeTimestamp(type_str, value);
        }
        else if (type_str == "BLOB") {
            ret =  DeserializeBlob(type_str, value);
        }
        else if (type_str == "STRUCT") {
            ret =  DeserializeStruct(type_str, value);
        }
        else if (type_str == "LIST") {
            ret =  DeserializeList(type_str, value);
        }
        else {
            throw std::invalid_argument(StringUtil::Format("Invalid JSON format, $type must be a valid type, got %s", type_str));
        }

        return ret;
    }

    Value ErpelSerializer::DeserializeString(std::string &typ, yyjson_val *val) 
    {
        auto str = std::string(yyjson_get_str(val));
        return Value(str);
    }

    Value ErpelSerializer::DeserializeInteger(std::string &typ, yyjson_val *val) 
    {
        auto int_val = yyjson_get_int(val);
        return Value::CreateValue(int_val);
    }

    Value ErpelSerializer::DeserializeReal(std::string &typ, yyjson_val *val) 
    {
        auto real_val = yyjson_get_real(val);
        return Value::CreateValue(real_val);
    }

    Value ErpelSerializer::DeserializeBoolean(std::string &typ, yyjson_val *val) 
    {
        auto bool_val = yyjson_get_bool(val);
        return Value::CreateValue(bool_val);
    }

    Value ErpelSerializer::DeserializeDate(std::string &typ, yyjson_val *val) 
    {
        auto str = std::string(yyjson_get_str(val));
        return Value::CreateValue(Date::FromString(str));
    }

    Value ErpelSerializer::DeserializeTime(std::string &typ, yyjson_val *val) 
    {
        auto int_val = yyjson_get_int(val);
        return Value::CreateValue(dtime_t(int_val));
    }

    Value ErpelSerializer::DeserializeTimestamp(std::string &typ, yyjson_val *val) 
    {
        auto str = std::string(yyjson_get_str(val));
        return Value::CreateValue(Timestamp::FromString(str));
    }

    Value ErpelSerializer::DeserializeBlob(std::string &typ, yyjson_val *val) 
    {
        auto base64 = std::string(yyjson_get_str(val));
        auto size = Blob::FromBase64Size(base64);
        auto data = std::string(size, ' ');
        
        Blob::FromBase64(base64, data_ptr_cast(data.data()), size);
        return Value::BLOB(data);
    }

    Value ErpelSerializer::DeserializeStruct(std::string &typ, yyjson_val *val) 
    {
        child_list_t<Value> child_values;
        
        yyjson_obj_iter iter;
        yyjson_obj_iter_init(val, &iter);
        for (yyjson_val *key_obj; (key_obj = yyjson_obj_iter_next(&iter));) 
        {
            auto key = std::string(yyjson_get_str(key_obj));
            auto val = yyjson_obj_iter_get_val(key_obj);
            child_values.push_back(std::pair(key, Deserialize(val)));
        }

        return Value::STRUCT(child_values);
    }

    Value ErpelSerializer::DeserializeList(std::string &typ, yyjson_val *val) 
    {
        vector<Value> child_values;
        yyjson_arr_iter iter;
        yyjson_arr_iter_init(val, &iter);

        for(yyjson_val *val; (val = yyjson_arr_iter_next(&iter));) 
        {
            child_values.push_back(Deserialize(val));
        }

        return Value::LIST(child_values);
    }
}