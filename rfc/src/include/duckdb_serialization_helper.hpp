#pragma once

#include "duckdb.hpp"
#include "yyjson.hpp"

namespace duckdb 
{
    class ErplSerializer 
    {
        public:
            static std::string SerializeJson(const Value &value);
            static std::string SerializeJson(const Value &value, bool include_type);
            static Value DeserializeJson(const std::string &json);

            static std::string SerializeSQL(const Value &value, const bool pretty_print = false, const uint32_t indent = 0);
            static std::string SerializeSQLType(const LogicalType &type, const bool pretty_print = false, const uint32_t indent = 0);
        
        private:
            ErplSerializer() = delete;

            static yyjson_mut_val *SerializeJson(yyjson_mut_doc *doc, const Value &value, bool include_type);
            static yyjson_mut_val *SerializeJson(yyjson_mut_doc *doc, const std::string &value);
            static yyjson_mut_val *SerializeJson(yyjson_mut_doc *doc, const int8_t &value);
            static yyjson_mut_val *SerializeJson(yyjson_mut_doc *doc, const int16_t &value);
            static yyjson_mut_val *SerializeJson(yyjson_mut_doc *doc, const int32_t &value);
            static yyjson_mut_val *SerializeJson(yyjson_mut_doc *doc, const int64_t &value);
            static yyjson_mut_val *SerializeJson(yyjson_mut_doc *doc, const uint8_t &value);
            static yyjson_mut_val *SerializeJson(yyjson_mut_doc *doc, const uint16_t &value);
            static yyjson_mut_val *SerializeJson(yyjson_mut_doc *doc, const uint32_t &value);
            static yyjson_mut_val *SerializeJson(yyjson_mut_doc *doc, const uint64_t &value);
            static yyjson_mut_val *SerializeJson(yyjson_mut_doc *doc, const float &value);
            static yyjson_mut_val *SerializeJson(yyjson_mut_doc *doc, const double &value);
            static yyjson_mut_val *SerializeJson(yyjson_mut_doc *doc, const bool &value);
            static yyjson_mut_val *SerializeJson(yyjson_mut_doc *doc, const date_t &value);
            static yyjson_mut_val *SerializeJson(yyjson_mut_doc *doc, const dtime_t &value);
            static yyjson_mut_val *SerializeJson(yyjson_mut_doc *doc, const timestamp_t &value);
            static yyjson_mut_val *SerializeBlobJson(yyjson_mut_doc *doc, const Value &blob_value);
            static yyjson_mut_val *SerializeStructJson(yyjson_mut_doc *doc, const Value &struct_value, bool include_type);
            static yyjson_mut_val *SerializeListJson(yyjson_mut_doc *doc, const Value &list_value, bool include_type);

            static yyjson_mut_val *WrapType(yyjson_mut_doc *doc, const LogicalTypeId &type, yyjson_mut_val *value);

            static Value DeserializeJson(yyjson_val *val);
            static Value DeserializeJsonString(std::string &typ, yyjson_val *val);
            static Value DeserializeJsonInteger(std::string &typ, yyjson_val *val);
            static Value DeserializeJsonReal(std::string &typ, yyjson_val *val);
            static Value DeserializeJsonBoolean(std::string &typ, yyjson_val *val);
            static Value DeserializeJsonDate(std::string &typ, yyjson_val *val);
            static Value DeserializeJsonTime(std::string &typ, yyjson_val *val);
            static Value DeserializeJsonTimestamp(std::string &typ, yyjson_val *val);
            static Value DeserializeJsonBlob(std::string &typ, yyjson_val *val);
            static Value DeserializeJsonStruct(std::string &typ, yyjson_val *val);
            static Value DeserializeJsonList(std::string &typ, yyjson_val *val);

            static std::string SerializeStructSQL(const Value &value, const bool pretty_print, uint32_t indent);
            static std::string SerializeListSQL(const Value &value, const bool pretty_print, uint32_t indent);

            static std::string SerializeStructType(const LogicalType &type, const bool pretty_print, uint32_t indent);
            static std::string SerializeListType(const LogicalType &type, const bool pretty_print, uint32_t indent);
    };
}