#include "duckdb_serialization_helper.hpp"
#include "duckdb/common/types/blob.hpp"
#include "yyjson.hpp"

namespace duckdb 
{
    std::string ErplSerializer::SerializeJson(const Value &value) 
    {
        return SerializeJson(value, false);
    } 

    std::string ErplSerializer::SerializeJson(const Value &value, bool include_type) 
    {
        auto doc = yyjson_mut_doc_new(NULL);
        
        yyjson_mut_val *root = SerializeJson(doc, value, include_type);
        yyjson_mut_doc_set_root(doc, root);
        auto flags = YYJSON_WRITE_PRETTY | YYJSON_WRITE_ESCAPE_UNICODE;
        const char *json = yyjson_mut_write(doc, flags, NULL);
        std::string result(json);
        free((void *)json);
        yyjson_mut_doc_free(doc);

        return result;
    }

    yyjson_mut_val *ErplSerializer::SerializeJson(yyjson_mut_doc *doc, const Value &value, bool include_type)
    {
        auto type = value.type().id();
        yyjson_mut_val *ret = nullptr;
        switch (type) 
        {
            case LogicalTypeId::VARCHAR:
                ret = SerializeJson(doc, value.ToString());
                break;
            case LogicalTypeId::TINYINT:
                ret =  SerializeJson(doc, value.GetValue<int8_t>());
                break;
            case LogicalTypeId::SMALLINT:
                ret =  SerializeJson(doc, value.GetValue<int16_t>());
                break;
            case LogicalTypeId::INTEGER:
                ret =  SerializeJson(doc, value.GetValue<int32_t>());
                break;
            case LogicalTypeId::BIGINT:
                ret =  SerializeJson(doc, value.GetValue<int64_t>());
                break;
            case LogicalTypeId::DOUBLE:
                ret =  SerializeJson(doc, value.GetValue<double>());
                break;
            case LogicalTypeId::BOOLEAN:
                ret =  SerializeJson(doc, value.GetValue<bool>());
                break;
            case LogicalTypeId::SQLNULL:
                ret =  yyjson_mut_null(doc);
                break;
            case LogicalTypeId::DATE:
                ret =  SerializeJson(doc, value.GetValue<date_t>());
                break;
            case LogicalTypeId::TIME:
                ret =  SerializeJson(doc, value.GetValue<dtime_t>());
                break;
            case LogicalTypeId::TIMESTAMP:
                ret =  SerializeJson(doc, value.GetValue<timestamp_t>());
                break;
            case LogicalTypeId::BLOB:
                ret =  SerializeBlobJson(doc, value);
                break;
            case LogicalTypeId::STRUCT:
                ret =  SerializeStructJson(doc, value, include_type);
                break;
            case LogicalTypeId::LIST:
                ret =  SerializeListJson(doc, value, include_type);
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

    yyjson_mut_val *ErplSerializer::SerializeJson(yyjson_mut_doc *doc, const std::string &value) 
    {
        return yyjson_mut_strcpy(doc, value.c_str());
    }

    yyjson_mut_val *ErplSerializer::SerializeJson(yyjson_mut_doc *doc, const int8_t &value) 
    {
        return yyjson_mut_sint(doc, value);
    }

    yyjson_mut_val *ErplSerializer::SerializeJson(yyjson_mut_doc *doc, const int16_t &value) 
    {
        return yyjson_mut_sint(doc, value);
    }

    yyjson_mut_val *ErplSerializer::SerializeJson(yyjson_mut_doc *doc, const int32_t &value) 
    {
        return yyjson_mut_int(doc, value);
    }

    yyjson_mut_val *ErplSerializer::SerializeJson(yyjson_mut_doc *doc, const int64_t &value) 
    {
        return yyjson_mut_int(doc, value);
    }

    yyjson_mut_val *ErplSerializer::SerializeJson(yyjson_mut_doc *doc, const uint8_t &value) 
    {
        return yyjson_mut_uint(doc, value);
    }

    yyjson_mut_val *ErplSerializer::SerializeJson(yyjson_mut_doc *doc, const uint16_t &value) 
    {
        return yyjson_mut_uint(doc, value);
    }

    yyjson_mut_val *ErplSerializer::SerializeJson(yyjson_mut_doc *doc, const uint32_t &value) 
    {
        return yyjson_mut_uint(doc, value);
    }

    yyjson_mut_val *ErplSerializer::SerializeJson(yyjson_mut_doc *doc, const uint64_t &value) 
    {
        return yyjson_mut_uint(doc, value);
    }

    yyjson_mut_val *ErplSerializer::SerializeJson(yyjson_mut_doc *doc, const float &value) 
    {
        return yyjson_mut_real(doc, value);
    }   

    yyjson_mut_val *ErplSerializer::SerializeJson(yyjson_mut_doc *doc, const double &value) 
    {
        return yyjson_mut_real(doc, value);
    }

    yyjson_mut_val *ErplSerializer::SerializeJson(yyjson_mut_doc *doc, const bool &value) 
    {
        return yyjson_mut_bool(doc, value);
    }

    yyjson_mut_val *ErplSerializer::SerializeJson(yyjson_mut_doc *doc, const date_t &value) 
    {
        auto str = Date::ToString(value);
        return yyjson_mut_strcpy(doc, str.c_str());
    }

    yyjson_mut_val *ErplSerializer::SerializeJson(yyjson_mut_doc *doc, const dtime_t &value) 
    {
        return SerializeJson(doc, value.micros);
    }

    yyjson_mut_val *ErplSerializer::SerializeJson(yyjson_mut_doc *doc, const timestamp_t &value) 
    {
        auto str = Timestamp::ToString(value);
        return yyjson_mut_strcpy(doc, str.c_str());
    }

    yyjson_mut_val *ErplSerializer::SerializeBlobJson(yyjson_mut_doc *doc, const Value &blob_value) 
    {
        auto result_size = Blob::ToBase64Size(blob_value.ToString());
        auto result_str = std::string(result_size, ' ');
		Blob::ToBase64(blob_value.ToString(), result_str.data());
		
        return SerializeJson(doc, result_str);
    }

    yyjson_mut_val *ErplSerializer::SerializeStructJson(yyjson_mut_doc *doc, const Value &struct_value, bool include_type) 
    {
        auto child_values = StructValue::GetChildren(struct_value);
        auto struct_type = struct_value.type();

        auto ret = yyjson_mut_obj(doc);
        for (std::size_t i = 0; i < child_values.size(); i++) {
            auto child_name = SerializeJson(doc, StructType::GetChildName(struct_type, i));
            auto child_value = SerializeJson(doc, child_values[i], include_type);

            yyjson_mut_obj_add(ret, child_name, child_value);
        }

        return ret;
    }

    yyjson_mut_val *ErplSerializer::SerializeListJson(yyjson_mut_doc *doc, const Value &list_value, bool include_type) 
    {
        auto child_values = ListValue::GetChildren(list_value);
        auto ret = yyjson_mut_arr(doc);

        for (std::size_t i = 0; i < child_values.size(); i++) {
            auto child_value = SerializeJson(doc, child_values[i], include_type);
            yyjson_mut_arr_append(ret, child_value);
        }

        return ret;
    }

    yyjson_mut_val *ErplSerializer::WrapType(yyjson_mut_doc *doc, const LogicalTypeId &type, yyjson_mut_val *value) 
    {
        auto type_str = LogicalType(type).ToString();
        auto wrapper = yyjson_mut_obj(doc);

        yyjson_mut_obj_add_val(doc, wrapper, "$type", yyjson_mut_strcpy(doc, type_str.c_str()));
        yyjson_mut_obj_add_val(doc, wrapper, "$value", value);

        return wrapper;
    }

    // ----------------------------------------------------------------------------------

    Value ErplSerializer::DeserializeJson(const std::string &json) 
    {
        yyjson_doc *doc = yyjson_read(json.c_str(), json.size(), 0);
        yyjson_val *root = yyjson_doc_get_root(doc);

        auto ret = DeserializeJson(root);
        
        yyjson_doc_free(doc);
        return ret;
    }

    Value ErplSerializer::DeserializeJson(yyjson_val *val) 
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
            ret = DeserializeJsonString(type_str, value);
        }
        else if (type_str == "TINYINT") {
            ret =  DeserializeJsonInteger(type_str, value);
        }
        else if (type_str == "SMALLINT") {
            ret =  DeserializeJsonInteger(type_str, value);
        }
        else if (type_str == "INTEGER") {
            ret =  DeserializeJsonInteger(type_str, value);
        }
        else if (type_str == "BIGINT") {
            ret =  DeserializeJsonInteger(type_str, value);
        }
        else if (type_str == "DOUBLE") {
            ret =  DeserializeJsonReal(type_str, value);
        }
        else if (type_str == "BOOLEAN") {
            ret =  DeserializeJsonBoolean(type_str, value);
        }
        else if (type_str == "NULL") {
            ret =  Value();
        }
        else if (type_str == "DATE") {
            ret =  DeserializeJsonDate(type_str, value);
        }
        else if (type_str == "TIME") {
            ret =  DeserializeJsonTime(type_str, value);
        }
        else if (type_str == "TIMESTAMP") {
            ret =  DeserializeJsonTimestamp(type_str, value);
        }
        else if (type_str == "BLOB") {
            ret =  DeserializeJsonBlob(type_str, value);
        }
        else if (type_str == "STRUCT") {
            ret =  DeserializeJsonStruct(type_str, value);
        }
        else if (type_str == "LIST") {
            ret =  DeserializeJsonList(type_str, value);
        }
        else {
            throw std::invalid_argument(StringUtil::Format("Invalid JSON format, $type must be a valid type, got %s", type_str));
        }

        return ret;
    }

    Value ErplSerializer::DeserializeJsonString(std::string &typ, yyjson_val *val) 
    {
        auto str = std::string(yyjson_get_str(val));
        return Value(str);
    }

    Value ErplSerializer::DeserializeJsonInteger(std::string &typ, yyjson_val *val) 
    {
        auto int_val = yyjson_get_int(val);
        return Value::CreateValue(int_val);
    }

    Value ErplSerializer::DeserializeJsonReal(std::string &typ, yyjson_val *val) 
    {
        auto real_val = yyjson_get_real(val);
        return Value::CreateValue(real_val);
    }

    Value ErplSerializer::DeserializeJsonBoolean(std::string &typ, yyjson_val *val) 
    {
        auto bool_val = yyjson_get_bool(val);
        return Value::CreateValue(bool_val);
    }

    Value ErplSerializer::DeserializeJsonDate(std::string &typ, yyjson_val *val) 
    {
        auto str = std::string(yyjson_get_str(val));
        return Value::CreateValue(Date::FromString(str));
    }

    Value ErplSerializer::DeserializeJsonTime(std::string &typ, yyjson_val *val) 
    {
        auto int_val = yyjson_get_int(val);
        return Value::CreateValue(dtime_t(int_val));
    }

    Value ErplSerializer::DeserializeJsonTimestamp(std::string &typ, yyjson_val *val) 
    {
        auto str = std::string(yyjson_get_str(val));
        return Value::CreateValue(Timestamp::FromString(str));
    }

    Value ErplSerializer::DeserializeJsonBlob(std::string &typ, yyjson_val *val) 
    {
        auto base64 = std::string(yyjson_get_str(val));
        auto size = Blob::FromBase64Size(base64);
        auto data = std::string(size, ' ');
        
        Blob::FromBase64(base64, data_ptr_cast(data.data()), size);
        return Value::BLOB(data);
    }

    Value ErplSerializer::DeserializeJsonStruct(std::string &typ, yyjson_val *val) 
    {
        child_list_t<Value> child_values;
        
        yyjson_obj_iter iter;
        yyjson_obj_iter_init(val, &iter);
        for (yyjson_val *key_obj; (key_obj = yyjson_obj_iter_next(&iter));) 
        {
            auto key = std::string(yyjson_get_str(key_obj));
            auto val = yyjson_obj_iter_get_val(key_obj);
            child_values.push_back(std::pair(key, DeserializeJson(val)));
        }

        return Value::STRUCT(child_values);
    }

    Value ErplSerializer::DeserializeJsonList(std::string &typ, yyjson_val *val) 
    {
        vector<Value> child_values;
        yyjson_arr_iter iter;
        yyjson_arr_iter_init(val, &iter);

        auto size = yyjson_arr_size(val);
        if (size == 0) {
            return Value::LIST(LogicalType::SQLNULL, child_values);
        }

        for(yyjson_val *val; (val = yyjson_arr_iter_next(&iter));) 
        {
            child_values.push_back(DeserializeJson(val));
        }

        auto child_type = child_values[0].type();
        for (auto &val : child_values) {
		    val = val.DefaultCastAs(child_type, false);
	    }

        return Value::LIST(child_type, child_values);
    }

    std::string ErplSerializer::SerializeSQL(const Value &value, const bool pretty_print, const uint32_t indent) 
    {
        auto type = value.type().id();
        switch(type) {
            case LogicalTypeId::STRUCT:
                return SerializeStructSQL(value, pretty_print, indent);
            case LogicalTypeId::LIST:
                return SerializeListSQL(value, pretty_print, indent);
            default:
                return value.ToSQLString();
        }
    }

    std::string ErplSerializer::SerializeStructSQL(const Value &value, const bool pretty_print, uint32_t indent)
    {
        if (pretty_print == false) {
            indent = 0;
        }

        auto typ = value.type();
		auto &child_types = StructType::GetChildTypes(typ);
		auto &struct_values = StructValue::GetChildren(value);
        
        if (struct_values.size() == 1 && child_types[0].first.empty()) {
            return SerializeSQL(struct_values[0]);
        }

        string ret = "STRUCT_PACK(";
        if (pretty_print && ! struct_values.empty()) {
            ret += "\n";
        }
		for (size_t i = 0; i < struct_values.size(); i++) {
			auto &name = child_types[i].first;
			auto &child = struct_values[i];
            if (pretty_print) {
                ret += StringUtil::Format("%s\"%s\" := %s", StringUtil::Repeat(" ", indent + 2), name, SerializeSQL(child, pretty_print, indent + 4));
            }
            else {
                ret += StringUtil::Format("\"%s\" := %s", name, SerializeSQL(child, pretty_print, indent + 4));
            }
			if (i < struct_values.size() - 1) {
				ret += ", ";
			}
            if (pretty_print && ! struct_values.empty()) {
                ret += "\n";
            }
		}
		ret += StringUtil::Format("%s)", StringUtil::Repeat(" ", struct_values.empty() ? 0 : indent));
		return ret;
    }

    std::string ErplSerializer::SerializeListSQL(const Value &value, const bool pretty_print, uint32_t indent)
    {
        if (pretty_print == false) {
            indent = 0;
        }

        auto &list_values = ListValue::GetChildren(value);

        string ret = "LIST_VALUE(";
        if (pretty_print && ! list_values.empty()) {
            ret += "\n";
        }
		
		for (size_t i = 0; i < list_values.size(); i++) {
			auto &child = list_values[i];
            if (pretty_print) {
                ret += StringUtil::Format("%s%s", StringUtil::Repeat(" ", indent + 2), SerializeSQL(child, pretty_print, indent + 4));
            }
            else {
                ret += SerializeSQL(child, pretty_print, indent + 4);
            }
			if (i < list_values.size() - 1) {
				ret += ", ";
			}
            if (pretty_print && ! list_values.empty()) {
                ret += "\n";
            }
		}

		ret += StringUtil::Format("%s)", StringUtil::Repeat(" ", list_values.empty() ? 0 : indent));
		return ret;
    }

    std::string ErplSerializer::SerializeSQLType(const LogicalType &type, const bool pretty_print, const uint32_t indent)
    {
        auto alias = type.GetAlias();
        if (!alias.empty()) {
            return alias;
        }

        auto id = type.id();
        switch (id) {
            case LogicalTypeId::STRUCT:
                return SerializeStructType(type, pretty_print, indent);
            case LogicalTypeId::LIST:
                return SerializeListType(type, pretty_print, indent);
            default:
                return type.ToString();
        }
    }

    std::string ErplSerializer::SerializeStructType(const LogicalType &type, const bool pretty_print, uint32_t indent)
    {
        if (pretty_print == false) {
            indent = 0;
        }

		auto &child_types = StructType::GetChildTypes(type);

        string ret = "STRUCT(";
        if (pretty_print && ! child_types.empty()) {
            ret += "\n";
        }

		for (size_t i = 0; i < child_types.size(); i++) {
			auto &name = child_types[i].first;
			auto &child_type = child_types[i].second;
            if (pretty_print) {
                ret += StringUtil::Format("%s\"%s\" %s", StringUtil::Repeat(" ", indent + 2), name, SerializeSQLType(child_type, pretty_print, indent + 4));
            }
            else {
                ret += StringUtil::Format("\"%s\" %s", name, SerializeSQLType(child_type, pretty_print, indent + 4));
            }
			if (i < child_types.size() - 1) {
				ret += ", ";
			}
            if (pretty_print && ! child_types.empty()) {
                ret += "\n";
            }
		}
		ret += StringUtil::Format("%s)", StringUtil::Repeat(" ", child_types.empty() ? 0 : indent));
		return ret;
    }

    std::string ErplSerializer::SerializeListType(const LogicalType &type, const bool pretty_print, uint32_t indent)
    {
        if (pretty_print == false) {
            indent = 0;
        }

        auto &child_type = ListType::GetChildType(type);
        string ret = "";
        //if (pretty_print) {
        //    ret += StringUtil::Format("%s%s", StringUtil::Repeat(" ", indent + 2), SerializeSQLType(child_type, pretty_print, indent + 4));
        //}
        //else {
            ret += SerializeSQLType(child_type, pretty_print, indent + 4);
        //}
			
		ret += "[]";
		return ret;
    }
}