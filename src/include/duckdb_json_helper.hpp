#pragma once

#include "duckdb.hpp"
#include "yyjson.hpp"

namespace duckdb 
{
    class ErpelSerializer 
    {
        public:
            static std::string Serialize(const Value &value);
            static std::string Serialize(const Value &value, bool include_type);
            static Value Deserialize(const std::string &json);
        
        private:
            ErpelSerializer() = delete;

            static yyjson_mut_val *Serialize(yyjson_mut_doc *doc, const Value &value, bool include_type);
            static yyjson_mut_val *Serialize(yyjson_mut_doc *doc, const std::string &value);
            static yyjson_mut_val *Serialize(yyjson_mut_doc *doc, const int8_t &value);
            static yyjson_mut_val *Serialize(yyjson_mut_doc *doc, const int16_t &value);
            static yyjson_mut_val *Serialize(yyjson_mut_doc *doc, const int32_t &value);
            static yyjson_mut_val *Serialize(yyjson_mut_doc *doc, const int64_t &value);
            static yyjson_mut_val *Serialize(yyjson_mut_doc *doc, const uint8_t &value);
            static yyjson_mut_val *Serialize(yyjson_mut_doc *doc, const uint16_t &value);
            static yyjson_mut_val *Serialize(yyjson_mut_doc *doc, const uint32_t &value);
            static yyjson_mut_val *Serialize(yyjson_mut_doc *doc, const uint64_t &value);
            static yyjson_mut_val *Serialize(yyjson_mut_doc *doc, const float &value);
            static yyjson_mut_val *Serialize(yyjson_mut_doc *doc, const double &value);
            static yyjson_mut_val *Serialize(yyjson_mut_doc *doc, const bool &value);
            static yyjson_mut_val *Serialize(yyjson_mut_doc *doc, const date_t &value);
            static yyjson_mut_val *Serialize(yyjson_mut_doc *doc, const dtime_t &value);
            static yyjson_mut_val *Serialize(yyjson_mut_doc *doc, const timestamp_t &value);
            static yyjson_mut_val *SerializeBlob(yyjson_mut_doc *doc, const Value &blob_value);
            static yyjson_mut_val *SerializeStruct(yyjson_mut_doc *doc, const Value &struct_value, bool include_type);
            static yyjson_mut_val *SerializeList(yyjson_mut_doc *doc, const Value &list_value, bool include_type);

            static yyjson_mut_val *WrapType(yyjson_mut_doc *doc, const LogicalTypeId &type, yyjson_mut_val *value);

            static Value Deserialize(yyjson_val *val);
            static Value DeserializeString(std::string &typ, yyjson_val *val);
            static Value DeserializeInteger(std::string &typ, yyjson_val *val);
            static Value DeserializeReal(std::string &typ, yyjson_val *val);
            static Value DeserializeBoolean(std::string &typ, yyjson_val *val);
            static Value DeserializeDate(std::string &typ, yyjson_val *val);
            static Value DeserializeTime(std::string &typ, yyjson_val *val);
            static Value DeserializeTimestamp(std::string &typ, yyjson_val *val);
            static Value DeserializeBlob(std::string &typ, yyjson_val *val);
            static Value DeserializeStruct(std::string &typ, yyjson_val *val);
            static Value DeserializeList(std::string &typ, yyjson_val *val);

    };
}