#include <set>
#include <stdexcept>

#include "sapnwrfc.h"
#include "duckdb.hpp"
#include "duckdb_argument_helper.hpp"
#include "sap_function.hpp"

namespace duckdb {
RfcFieldDesc::RfcFieldDesc(const RFC_FIELD_DESC& sap_desc) : _desc_handle(sap_desc)
    { 
        _type = std::make_shared<RfcType>(sap_desc.type, sap_desc.typeDescHandle, GetLength(), GetDecimals());
    }
    
    RfcFieldDesc::~RfcFieldDesc()
    { }
    
    std::string RfcFieldDesc::GetName() const
    {
        return uc2std(_desc_handle.name);
    }

    std::shared_ptr<RfcType> RfcFieldDesc::GetRfcType() const
    {
        return _type;
    }

    std::string RfcFieldDesc::GetRfcTypeAsString() const
    {
        return rfctype2std(_desc_handle.type);
    }

    unsigned int RfcFieldDesc::GetLength() const
    {
        return _desc_handle.nucLength;
    }

    unsigned int RfcFieldDesc::GetDecimals() const
    {
        return _desc_handle.decimals;
    }

    std::string RfcFieldDesc::ToSqlLiteral() 
    {
        std::stringstream ss;

        ss << "{";
        ss << "'field_name': '" << GetName() << "', ";
        ss << "'field_type': '" << GetRfcTypeAsString() << "', ";
        ss << "'field_length': " << GetLength() << ", ";
        ss << "'field_decimals': " << GetDecimals() << ", ";
        ss << "'field_type_declaration': " << GetRfcType()->ToSqlLiteral();
        ss << "}";

        return ss.str();
    }

    // RfcFieldDesc -------------------------------------------------

    RfcType::RfcType(const RFCTYPE rfc_type, const RFC_TYPE_DESC_HANDLE handle, const unsigned int length, const unsigned int decimals) 
        : _rfc_type(rfc_type), _handle(handle), _length(length), _decimals(decimals)
    { }


    RfcType::~RfcType()
    {
        if (_handle == NULL)
            return;

        // CLARIFY: Type is explicitly not freed, because it is owned by the SAP NW RFC library.
        // Destroying it leads to wrong internal behaviour.
    }

    bool RfcType::IsComplexType() const
    {
        return _handle != NULL;
    }

    std::string RfcType::GetName() const
    {
        if (_handle == NULL) {
            return rfctype2std(_rfc_type);
        }

        RFC_ERROR_INFO error_info;
        const unsigned int name_len = 256;
        SAP_UC name_buffer[name_len];

        auto result = RfcGetTypeName(_handle, name_buffer, &error_info);
        if (result != RFC_OK) {
            throw std::runtime_error(StringUtil::Format("Failed to get type name: %s: %s", rfcrc2std(error_info.code), uc2std(error_info.message)));
        }

        return uc2std(name_buffer);
    }

    unsigned int RfcType::GetLength() const
    {
        return _length;
    }

    unsigned int RfcType::GetDecimals() const
    {
        return _decimals;
    }

    /**
     * @brief Returns the info for a single parameter.
    */
    std::vector<RfcFieldDesc> RfcType::GetFieldInfos()
    {
        if (!_field_infos.empty())
        {
            return _field_infos;
        }

        RFC_ERROR_INFO error_info;
        RFC_FIELD_DESC field_desc;

        if (_handle == NULL) {
            return std::vector<RfcFieldDesc>();
        }

        unsigned int field_count = 0;
        auto result = RfcGetFieldCount(_handle, &field_count, &error_info);
        if (result != RFC_OK) {
            throw std::runtime_error(StringUtil::Format("Failed to get field count: %s: %s", rfcrc2std(error_info.code), uc2std(error_info.message)));
        }

        for (unsigned int i = 0; i < field_count; i++) {
            result = RfcGetFieldDescByIndex(_handle, i, &field_desc, &error_info);
            if (result != RFC_OK) {
                throw std::runtime_error(StringUtil::Format("Failed to get field description: %s: %s", rfcrc2std(error_info.code), uc2std(error_info.message)));
            }

            _field_infos.push_back(RfcFieldDesc(field_desc));
        }

        return _field_infos;
    }

    /**
     * @brief Returns the info for a single field of a type.
     * @param name The name of the field.
     * @return A single field description.
    */
    RfcFieldDesc RfcType::GetFieldInfo(std::string name) 
    {
        for (auto &field_info : GetFieldInfos()) {
            if (field_info.GetName() == name) {
                return field_info;
            }
        }

        throw std::runtime_error(StringUtil::Format("Parameter '%s' not found", name.c_str()));
    }

    /**
     * @brief Returns the info for a single field of a type.
     * @param index The index of the column.
     * @return A single field description.
    */
    RfcFieldDesc RfcType::GetFieldInfo(unsigned int index) 
    {
        auto field_infos = GetFieldInfos();
        if (index >= field_infos.size()) {
            throw std::runtime_error(StringUtil::Format("Parameter index %d out of bounds", index));
        }

        return field_infos[index];
    }

    /// @brief Get the DuckDB type that is compatible with this RFC type
    /// @return A vector of DuckDB types that are compatible with this RFC type
    LogicalTypeId RfcType::GetCompatibleDuckDbType() 
    {
        return rfctype2logicaltype(_rfc_type);
    }

    LogicalType RfcType::CreateDuckDbType() 
    {
        switch(_rfc_type) 
        {
            case RFCTYPE_NULL: 
                return LogicalType::SQLNULL;
                break;
            // Numeric types
            case RFCTYPE_NUM: 
                return LogicalType::VARCHAR;
                break;
            case RFCTYPE_INT: 
                return LogicalType::BIGINT;
                break;
            case RFCTYPE_INT1: 
                return LogicalType::TINYINT;
                break;
            case RFCTYPE_INT2:
                return LogicalType::SMALLINT;
                break;
            case RFCTYPE_INT8: 
                return LogicalType::INTEGER;
                break;
            case RFCTYPE_FLOAT:
                return LogicalType::DOUBLE;
                break;
            case RFCTYPE_BCD:
            case RFCTYPE_DECF16:
            case RFCTYPE_DECF34:
                return LogicalType::DECIMAL(GetLength() * 2 - 1, GetDecimals());
                //return LogicalType::DECIMAL(GetLength(), GetDecimals());
                break;
            case RFCTYPE_CHAR:
            case RFCTYPE_STRING:
            case RFCTYPE_XSTRING:
            case RFCTYPE_XMLDATA:
                return LogicalType::VARCHAR;
                break;
            case RFCTYPE_BYTE:
                return LogicalType::BLOB;
                break;
            case RFCTYPE_TIME:
                return LogicalType::TIME;
                break;
            case RFCTYPE_DATE:
                return LogicalType::DATE;
                break;
            case RFCTYPE_UTCLONG:
            case RFCTYPE_UTCSECOND:
            case RFCTYPE_UTCMINUTE:
                return LogicalType::TIMESTAMP;
                break;
            case RFCTYPE_DTDAY:
            case RFCTYPE_DTWEEK:
            case RFCTYPE_DTMONTH:
                return LogicalType::INTEGER;
                break;
            case RFCTYPE_TSECOND:
            case RFCTYPE_TMINUTE:
                return LogicalType::INTEGER;
                break;
            case RFCTYPE_CDAY:
                return LogicalType::INTEGER;
                break;
            case RFCTYPE_ABAPOBJECT:
                return LogicalType::VARCHAR;
                break;
            case RFCTYPE_STRUCTURE: {
                auto child_types = CreateDuckDbTypesForRfcStruct();
                return LogicalType::STRUCT(child_types);
                break;
            }
            case RFCTYPE_TABLE: {
                auto child_type = CreateDuckDbTypeForRfcTable();
                return LogicalType::LIST(child_type);
                break;
            }
            default:
                throw std::runtime_error(StringUtil::Format("Unknown RFC type: %d", _rfc_type));
                break;
        }
    }

    child_list_t<LogicalType> RfcType::CreateDuckDbTypesForRfcStruct() 
    {
        child_list_t<LogicalType> child_types;
        for (auto &field_info : GetFieldInfos()) {
            auto rfc_type = field_info.GetRfcType();
            child_types.push_back(make_pair(field_info.GetName(), rfc_type->CreateDuckDbType()));
        }

        return child_types;
    }

    LogicalType RfcType::CreateDuckDbTypeForRfcTable() 
    {
        auto child_types = CreateDuckDbTypesForRfcStruct();
        // In the case that the table has only one column with no name, we don't create STRUCT.
        if (child_types.size() == 1 && child_types[0].first.empty()) {
            return child_types[0].second;
        }
        return LogicalType::STRUCT(child_types);
    }

    bool RfcType::IsCompatibleType(const LogicalTypeId type) 
    {
        auto compatible_type = GetCompatibleDuckDbType();
        return type == compatible_type;
    }

    void RfcType::AdaptValue(RfcInvocation &invocation, string &arg_name, Value &arg_value) 
    {
        auto function_handle = invocation.GetFunctionHandle();
        AdaptValue(function_handle, arg_name, arg_value);
    }

    void RfcType::AdaptValue(DATA_CONTAINER_HANDLE &container_handle, string &arg_name, Value &arg_value) 
    {
        RFC_RC rc = RFC_OK;
        RFC_ERROR_INFO error_info;
        auto sap_arg_name = std2uc(arg_name);

        switch(_rfc_type) 
        {
            // Numeric types
            case RFCTYPE_NUM:
            {
                auto string_value = std2uc(arg_value.GetValue<std::string>());
                rc = RfcSetNum(container_handle, sap_arg_name.get(), string_value.get(), strlenU(string_value.get()), &error_info);
                break;
            }
            case RFCTYPE_INT:
            {
                RFC_INT int_value;
                duck2rfc(arg_value, int_value);
                rc = RfcSetInt(container_handle, sap_arg_name.get(), int_value, &error_info);
                break;
            }
            case RFCTYPE_INT1:
            {
                RFC_INT1 int_value;
                duck2rfc(arg_value, int_value);
                rc = RfcSetInt1(container_handle, sap_arg_name.get(), int_value, &error_info);
                break;
            }
            case RFCTYPE_INT2:
            {
                RFC_INT2 int_value;
                duck2rfc(arg_value, int_value);
                rc = RfcSetInt2(container_handle, sap_arg_name.get(), int_value, &error_info);
                break;
            }
            case RFCTYPE_INT8:
            {
                RFC_INT8 int_value;
                duck2rfc(arg_value, int_value);
                rc = RfcSetInt8(container_handle, sap_arg_name.get(), int_value, &error_info);
                break;
            }
            case RFCTYPE_BCD:
            case RFCTYPE_DECF16:
            case RFCTYPE_DECF34:
            {
                auto string_value = arg_value.DefaultCastAs(LogicalType::VARCHAR).ToString();
                auto bcd_value = std2uc(string_value);
                rc = RfcSetString(container_handle, sap_arg_name.get(), bcd_value.get(), strlenU(bcd_value.get()), &error_info);
                break;
            }
            case RFCTYPE_FLOAT:
            {
                RFC_FLOAT float_value;
                duck2rfc(arg_value, float_value);
                rc = RfcSetFloat(container_handle, sap_arg_name.get(), float_value, &error_info);
                break;
            }
            // Character types
            case RFCTYPE_CHAR:
            case RFCTYPE_STRING:
            {
                if (! arg_value.IsNull()) {
                    auto string_value = duck2uc(arg_value);
                    rc = RfcSetString(container_handle, sap_arg_name.get(), string_value.get(), strlenU(string_value.get()), &error_info);
                }

                break;
            }
            case RFCTYPE_XSTRING:
            {
                throw std::logic_error("XSTRING type is not supported yet");
                break;
            }
            // Binary types
            case RFCTYPE_BYTE:
            {
                unsigned int byte_value_len = 0;
                SAP_RAW *byte_value = (SAP_RAW *)malloc(byte_value_len);

                rc = RfcSetBytes(container_handle, sap_arg_name.get(), byte_value, byte_value_len, &error_info);
                free(byte_value);
                break;
            }
            // Date types
            case RFCTYPE_TIME:
            {
                RFC_TIME time_value;
                duck2rfc(arg_value, time_value);
                rc = RfcSetTime(container_handle, sap_arg_name.get(), time_value, &error_info);
                if (rc != RFC_OK)
                {
                    throw std::runtime_error(StringUtil::Format("Error when putting DuckDB value %s into time field of type %s", arg_value.ToString().c_str(), rfctype2std(_rfc_type)));
                }
                break;
            }
            case RFCTYPE_DATE:
            {
                RFC_DATE date_value;
                duck2rfc(arg_value, date_value);
                rc = RfcSetDate(container_handle, sap_arg_name.get(), date_value, &error_info);
                if (rc != RFC_OK)
                {
                    throw std::runtime_error(StringUtil::Format("Error when putting DuckDB value %s into date field of type %s", arg_value.ToString().c_str(), rfctype2std(_rfc_type)));
                }
                break;
            }
            case RFCTYPE_UTCLONG:
            {
                throw std::logic_error("UTCLONG type is not supported yet");
                auto string_value = std2uc(arg_value.GetValue<std::string>());
                rc = RfcSetString(container_handle, sap_arg_name.get(), string_value.get(), strlenU(string_value.get()), &error_info);
                break;
            }
            // Complex types
            case RFCTYPE_STRUCTURE:
            {
                RFC_STRUCTURE_HANDLE struct_handle = NULL;
                rc = RfcGetStructure(container_handle, sap_arg_name.get(), &struct_handle, &error_info);
                if (rc != RFC_OK || struct_handle == NULL)
                {
                    throw std::runtime_error(StringUtil::Format("Failed to get structure %s: %s: %s", arg_name, rfcrc2std(error_info.code), uc2std(error_info.message)));
                }

                AdaptStructure(struct_handle, arg_name, arg_value);
                break;
            }
            case RFCTYPE_TABLE:
            {
                RFC_TABLE_HANDLE table_handle = NULL;
                rc = RfcGetTable(container_handle, sap_arg_name.get(), &table_handle, &error_info);
                if (rc != RFC_OK || table_handle == NULL)
                {
                    throw std::runtime_error(StringUtil::Format("Failed to get table '%s', got an invalid handle.", arg_name));
                }

                AdaptTable(table_handle, arg_name, arg_value);
                break;
            }
            default:
            {
                throw std::runtime_error(StringUtil::Format("RFC type %s for argument %s is not supported", rfctype2std(_rfc_type), arg_name));
            }

            if (rc != RFC_OK) {
                throw std::runtime_error(StringUtil::Format("Failed to set argument %s: %s: %s", arg_name, rfcrc2std(error_info.code), uc2std(error_info.message)));
            }
        }
    }

    void RfcType::AdaptStructure(RFC_STRUCTURE_HANDLE &struct_handle, string& arg_name, Value& struct_value)
    {
        auto &struct_values = StructValue::GetChildren(struct_value);
        auto &child_types = StructType::GetChildTypes(struct_value.type());

        for (idx_t i = 0; i < struct_values.size(); i++) {
			auto child_name = child_types[i].first;
            auto child_value = struct_values[i];
			
            try {
                auto rfc_type = GetFieldInfo(child_name).GetRfcType();
                rfc_type->AdaptValue(struct_handle, child_name, child_value);
            } catch (std::runtime_error &e) {
                throw std::runtime_error(StringUtil::Format("Failed to adapt field '%s' in structure '%s': %s", child_name, arg_name, e.what()));
            }
		}
    }

    void RfcType::AdaptTable(RFC_TABLE_HANDLE &table_handle, string& arg_name, Value& table_value)
    {
        RFC_ERROR_INFO error_info;

        for (auto child_value : ListValue::GetChildren(table_value)) {
            auto struct_handle = RfcAppendNewRow(table_handle, &error_info);
            if (error_info.code != RFC_OK) {
                throw std::runtime_error(StringUtil::Format("Failed to append new row to table %s: %s: %s", 
                                                            arg_name, rfcrc2std(error_info.code), uc2std(error_info.message)));
            }

            // TODO: Not sure, that this is correct.
            if (child_value.type().id() != LogicalTypeId::STRUCT) {
                auto rfc_field = GetFieldInfo(0);
                auto rfc_type = rfc_field.GetRfcType();
                auto rfc_name = rfc_field.GetName();
                rfc_type->AdaptValue(struct_handle, rfc_name, child_value);
            } else {
                AdaptStructure(struct_handle, arg_name, child_value);
            }
        }
    }

    Value RfcType::ConvertRfcValue(RfcInvocation &invocation, string &field_name) 
    {
        auto function_handle = invocation.GetFunctionHandle();
        return ConvertRfcValue(function_handle, field_name);
    }

    Value RfcType::ConvertRfcValue(std::shared_ptr<RfcInvocation> invocation, string &field_name) 
    {
        auto function_handle = invocation->GetFunctionHandle();
        return ConvertRfcValue(function_handle, field_name);
    }

    Value RfcType::ConvertRfcValue(RFC_FUNCTION_HANDLE &function_handle, string &field_name)
    {
        RFC_RC rc = RFC_OK;
        RFC_ERROR_INFO error_info;

        auto rfc_name = std2uc(field_name);

        switch(_rfc_type) 
        {
            // Numeric types
            case RFCTYPE_FLOAT:
            {
                RFC_FLOAT float_value;
                rc = RfcGetFloat(function_handle, rfc_name.get(), &float_value, &error_info);
                if (rc != RFC_OK)
                {
                    throw std::runtime_error(StringUtil::Format("Failed to get float %s: %s: %s", 
                                                                field_name, rfcrc2std(error_info.code), uc2std(error_info.message)));
                }
                return rfc2duck(float_value);
            }
            case RFCTYPE_INT:
            {
                RFC_INT int_value;
                rc = RfcGetInt(function_handle, rfc_name.get(), &int_value, &error_info);
                if (rc != RFC_OK)
                {
                    throw std::runtime_error(StringUtil::Format("Failed to get int %s: %s: %s", 
                                                                field_name, rfcrc2std(error_info.code), uc2std(error_info.message)));
                }
                return rfc2duck(int_value);
            }
            case RFCTYPE_INT1:
            {
                RFC_INT1 int1_value;
                rc = RfcGetInt1(function_handle, rfc_name.get(), &int1_value, &error_info);
                if (rc != RFC_OK)
                {
                    throw std::runtime_error(StringUtil::Format("Failed to get int1 %s: %s: %s", field_name, rfcrc2std(error_info.code), uc2std(error_info.message)));
                }
                return rfc2duck(int1_value);
            }
            case RFCTYPE_INT2:
            {
                RFC_INT2 int2_value;
                rc = RfcGetInt2(function_handle, rfc_name.get(), &int2_value, &error_info);
                if (rc != RFC_OK)
                {
                    throw std::runtime_error(StringUtil::Format("Failed to get int2 %s: %s: %s", 
                                                                field_name, rfcrc2std(error_info.code), uc2std(error_info.message)));
                }
                return rfc2duck(int2_value);
            }
            // Decimal types
            case RFCTYPE_BCD:
            {
                unsigned int result_len = 0, str_len = 2  * GetLength() + 1;
                SAP_UC *string_value = (RFC_CHAR *)mallocU(str_len + 1);
                rc = RfcGetString(function_handle, rfc_name.get(), string_value, str_len + 1, &result_len, &error_info);

                if (rc == RFC_BUFFER_TOO_SMALL)  // Buffer too small, we allocate to the returned result_len.
                {
                    free(string_value);
                    str_len = result_len;
                    string_value = (RFC_CHAR *)mallocU(str_len + 1);
                    rc = RfcGetString(function_handle, rfc_name.get(), string_value, str_len + 1, &result_len, &error_info);
                }
                if (rc != RFC_OK) {
                    free(string_value);
                    throw std::runtime_error(StringUtil::Format("Failed to get BCD %s: %s: %s", 
                                                                field_name, rfcrc2std(error_info.code), uc2std(error_info.message)));
                }

                auto bcd_str = uc2std(string_value, str_len);
                free(string_value);

                auto result_value = bcd2duck(bcd_str, GetLength() * 2 - 1, GetDecimals());
                return result_value;
            }
            // String types
            case RFCTYPE_CHAR:
            {
                RFC_CHAR *char_value = (RFC_CHAR *)mallocU(GetLength());
                rc = RfcGetChars(function_handle, rfc_name.get(), char_value, GetLength(), &error_info);
                if (rc != RFC_OK)
                {
                    free(char_value);
                    throw std::runtime_error(StringUtil::Format("Failed to get char %s: %s: %s", 
                                                                field_name, rfcrc2std(error_info.code), uc2std(error_info.message)));
                }
                auto result_value = rfc2duck(char_value, GetLength(), true);
                free(char_value);
                return result_value;
            }
            case RFCTYPE_STRING:
            {
                unsigned int result_len = 0, str_len = 0;
                RfcGetStringLength(function_handle, rfc_name.get(), &str_len, &error_info);
                SAP_UC *string_value = (RFC_CHAR *)mallocU(str_len + 1);
                rc = RfcGetString(function_handle, rfc_name.get(), string_value, str_len + 1, &result_len, &error_info);
                if (rc != RFC_OK)
                {
                    free(string_value);
                    throw std::runtime_error(StringUtil::Format("Failed to get string %s: %s: %s", field_name, 
                                                                rfcrc2std(error_info.code), uc2std(error_info.message)));
                }
                auto result_value = uc2duck(string_value, str_len, true);
                free(string_value);
                return result_value;
            }
            case RFCTYPE_XSTRING:
            {
                unsigned int result_len = 0, str_len = 0;
                rc = RfcGetStringLength(function_handle, rfc_name.get(), &str_len, &error_info);
                if (rc != RFC_OK)
                {
                    throw std::runtime_error(StringUtil::Format("Failed to get xstring %s length: %s: %s", field_name, 
                                                                rfcrc2std(error_info.code), uc2std(error_info.message)));
                }

                SAP_RAW *xstring_value = (SAP_RAW *)malloc(str_len + 1);
                xstring_value[str_len] = '\0'; // null terminate the string
                rc = RfcGetXString(function_handle, rfc_name.get(), xstring_value, str_len, &result_len, &error_info);
                if (rc != RFC_OK)
                {
                    free(xstring_value);
                    throw std::runtime_error(StringUtil::Format("Failed to get xstring %s: %s: %s", field_name, 
                                                                rfcrc2std(error_info.code), uc2std(error_info.message)));
                }
                auto result_value = rfc2duck(xstring_value, str_len);
                free(xstring_value);
                return result_value;
            }
            case RFCTYPE_NUM:
            {
                RFC_NUM *num_value = (RFC_CHAR *)mallocU(GetLength());
                rc = RfcGetNum(function_handle, rfc_name.get(), num_value, GetLength(), &error_info);
                if (rc != RFC_OK)
                {
                    free(num_value);
                    break;
                }
                auto result_value = rfc2duck(num_value, GetLength());
                free(num_value);
                return result_value;
            }
            // Binary types
            case RFCTYPE_BYTE:
            {
                SAP_RAW *byte_value = (SAP_RAW *)malloc(GetLength());

                rc = RfcGetBytes(function_handle, rfc_name.get(), byte_value, GetLength(), &error_info);
                if (rc != RFC_OK)
                {
                    free(byte_value);
                    break;
                }
                auto result_value = rfc2duck(byte_value, GetLength());
                free(byte_value);
                return result_value;
            }
            // Data & time types
            case RFCTYPE_DATE:
            {
                RFC_DATE date_value;
                rc = RfcGetDate(function_handle, rfc_name.get(), date_value, &error_info);
                if (rc != RFC_OK)
                {
                    break;
                }
                return rfc2duck(date_value);
            }
            case RFCTYPE_TIME:
            {
                RFC_TIME time_value;
                rc = RfcGetTime(function_handle, rfc_name.get(), time_value, &error_info);
                if (rc != RFC_OK)
                {
                    break;
                }
                return rfc2duck(time_value);
            }
            // Complex types
            case RFCTYPE_STRUCTURE:
            {
                RFC_STRUCTURE_HANDLE struct_handle;
                rc = RfcGetStructure(function_handle, std2uc(field_name).get(), &struct_handle, &error_info);
                if (rc != RFC_OK)
                {
                    throw std::runtime_error(StringUtil::Format("Failed to get structure %s: %s: %s", field_name, 
                                                                rfcrc2std(error_info.code), uc2std(error_info.message)));
                }

                return ConvertRfcStruct(struct_handle);
            }
            case RFCTYPE_TABLE:
            {
                RFC_TABLE_HANDLE table_handle;
                rc = RfcGetTable(function_handle, std2uc(field_name).get(), &table_handle, &error_info);
                if (rc != RFC_OK)
                {
                    throw std::runtime_error(StringUtil::Format("Failed to get table %s: %s: %s", 
                                                                rfcrc2std(error_info.code), uc2std(error_info.message)));
                }

                return ConvertRfcTable(table_handle);
            }
            default:
            {
                throw std::runtime_error(StringUtil::Format("Conversion of RFC type %s is not supported yet", rfctype2std(_rfc_type)));
            }
        }

        return Value::BOOLEAN(false);
    }

    Value RfcType::ConvertRfcStruct(RFC_STRUCTURE_HANDLE &struct_handle)
    {
        child_list_t<Value> struct_values;
        for (auto field_info : GetFieldInfos()) {
            auto struct_key = field_info.GetName();
            auto struct_value = field_info.GetRfcType()->ConvertRfcValue(struct_handle, struct_key);
            
            struct_values.emplace_back(make_pair(std::move(struct_key), std::move(struct_value)));
        }

        // In the case that the structure has only one column with no name, we don't create STRUCT.
        if (struct_values.size() == 1 && struct_values[0].first.empty()) {
            return struct_values[0].second;
        }

        return Value::STRUCT(std::move(struct_values));
    }

    Value RfcType::ConvertRfcTable(RFC_TABLE_HANDLE &table_handle) 
    {
        RFC_RC rc = RFC_OK;
        RFC_ERROR_INFO error_info;
        unsigned int row_count = 0;
        rc = RfcGetRowCount(table_handle, &row_count, &error_info);
        if (rc != RFC_OK)
        {
            throw std::runtime_error(StringUtil::Format("Failed to get row count: %s: %s", 
                                                        rfcrc2std(error_info.code), uc2std(error_info.message)));
        }

        auto child_type = CreateDuckDbTypeForRfcTable();
        if (row_count == 0)
        {
            return Value::LIST(child_type, {});
        }

        vector <Value> row_values;
        for (unsigned int i = 0; i < row_count; i++)
        {
            rc = RfcMoveTo(table_handle, i, &error_info);
            if (rc != RFC_OK)
            {
                throw std::runtime_error(StringUtil::Format("Failed to move to row %d: %s: %s", i, 
                                                            rfcrc2std(error_info.code), uc2std(error_info.message)));
            }

            auto row_handle = RfcGetCurrentRow(table_handle, &error_info);
            auto struct_value = ConvertRfcStruct(row_handle);

            row_values.emplace_back(std::move(struct_value));
        }

        return Value::LIST(child_type, row_values);
    }

    Value RfcType::ConvertCsvValue(Value &csv_value)
    {
        auto str_value = csv_value.GetValue<std::string>();
        switch(_rfc_type)
        {
            case RFCTYPE_DATE:
            {
                return dats2duck(str_value);
            }
            case RFCTYPE_TIME:
            {
                return tims2duck(str_value);
            }
            case RFCTYPE_BCD:
            {
                return bcd2duck(str_value, GetLength() * 2 - 1, GetDecimals());
            }
            default:
            {
                return Value(str_value);
            }
        }

        return Value(csv_value);
    }

    std::string RfcType::ToSqlLiteral() 
    {
        std::stringstream ss;

        if (_handle == NULL) {
            return "NULL";
        }

        ss << "{";
        ss << "'type_name': '" << GetName() << "', ";
        
        ss << "'fields': [";
        auto fields = GetFieldInfos();
        for (auto it = fields.begin(); it != fields.end(); ++it) {
            ss << it->ToSqlLiteral();
            if (std::next(it) != fields.end()) {
                ss << ", ";
            }
        }

        ss << "], ";
        ss << "}";

        return ss.str();
    }

    static const std::map<std::string, RFCTYPE> &GetTypeMap()
    {
        static const std::map<std::string, RFCTYPE> type_map = {
            {"ACCP",        RFCTYPE_NUM},
            {"CHAR",        RFCTYPE_CHAR},
            {"CLNT",        RFCTYPE_CHAR},
            {"CURR",        RFCTYPE_BCD},
            {"CUKY",        RFCTYPE_CHAR},
            {"DATS",        RFCTYPE_DATE},
            {"DEC",         RFCTYPE_BCD},
            {"FLTP",        RFCTYPE_FLOAT},
            {"INT1",        RFCTYPE_INT1},
            {"INT2",        RFCTYPE_INT2},
            {"INT4",        RFCTYPE_INT},
            {"LANG",        RFCTYPE_CHAR},
            {"LCHR",        RFCTYPE_STRING},
            {"LRAW",        RFCTYPE_BYTE},
            {"NUMC",        RFCTYPE_NUM},
            {"PREC",        RFCTYPE_INT2},
            {"QUAN",        RFCTYPE_BCD},
            {"RAW",         RFCTYPE_BYTE},
            {"RAWSTRING",   RFCTYPE_XSTRING},
            {"RSTR",        RFCTYPE_STRING},
            {"STRING",      RFCTYPE_STRING},
            {"STRG",        RFCTYPE_STRING},
            {"SSTR",        RFCTYPE_STRING},
            {"TIMS",        RFCTYPE_TIME},
            {"UNIT",        RFCTYPE_CHAR}
        };

        return type_map;
    }

    RfcType RfcType::FromTypeName(const std::string &type_name, const unsigned int length, const unsigned int decimals) 
    {
        // https://learn.microsoft.com/en-us/biztalk/adapters-and-accelerators/adapter-sap/basic-sap-data-types

        auto it = GetTypeMap().find(type_name);
        if (it != GetTypeMap().end()) {
            auto abap_internal_lenght = length;
            if (it->second == RFCTYPE_BCD) {
                abap_internal_lenght = (length + 1) / 2;
            }

            return RfcType(it->second, nullptr, abap_internal_lenght, decimals);
        } else {
            throw std::runtime_error(StringUtil::Format(
                "Unsupported SAP data type: %s. Use sap_describe_fields() to inspect available columns.", type_name));
        }
    }

    RfcType RfcType::FromTypeName(const std::string &type_name) {
        return FromTypeName(type_name, 0, 0);
    }

    bool RfcType::IsKnownDataType(const std::string &type_name) {
        return GetTypeMap().find(type_name) != GetTypeMap().end();
    }

    bool RfcType::IsStringType() const {
        return _rfc_type == RFCTYPE_STRING || _rfc_type == RFCTYPE_XSTRING;
    }

    // RfcType ------------------------------------------------------

    RfcFunctionParameterDesc::RfcFunctionParameterDesc(const RFC_PARAMETER_DESC& sap_desc) : _desc_handle(sap_desc)
    {
        _type = std::make_shared<RfcType>(sap_desc.type, sap_desc.typeDescHandle, GetLength(), GetDecimals());
    }

    /**
     * @brief Destroys the RFC_TYPE_DESC_HANDLE.
     */
    RfcFunctionParameterDesc::~RfcFunctionParameterDesc()
    {
    }

    /**
     * @brief Returns the name of the parameter.
     * @return The name of the parameter as a std::string.
     */
    std::string RfcFunctionParameterDesc::GetName() const
    {
        return uc2std(_desc_handle.name);
    }

    /**
     * @brief Returns the type of the parameter.
     * @return The type of the parameter as a std::shared_ptr<RfcType>.
     */
    std::shared_ptr<RfcType> RfcFunctionParameterDesc::GetRfcType() const
    {
        return _type;
    }

    /**
     * @brief Returns the type of the parameter as an RFC_TYPE enumeration value.
     * @return The type of the parameter as an RFC_TYPE enumeration value.
     */
    RFCTYPE RfcFunctionParameterDesc::GetRfcTypeAsEnum() const
    {
        return _desc_handle.type;
    }

    /**
     * @brief Returns the type of the parameter as a std::string.
     * @return The type of the parameter as a std::string. See rfcType2str() for possible values.
     */
    std::string RfcFunctionParameterDesc::GetRfcTypeAsString() const
    {
        return rfctype2std(_desc_handle.type);
    }

    /**
     * @brief Returns the direction of the parameter.
     * @return The RFC_DIRECTION enumeration value representing the parameter's direction (input, output, or bidirectional).
     */
    RFC_DIRECTION RfcFunctionParameterDesc::GetDirection() const {
        return _desc_handle.direction;
    }

    /**
     * @brief Returns the direction of the parameter as a std::string.
     * @return The direction of the parameter as a std::string. Possible values are "RFC_IMPORT", "RFC_EXPORT", and "RFC_CHANGING".
     */
    std::string RfcFunctionParameterDesc::GetDirectionAsString() const {
        return rfcdirection2std(_desc_handle.direction);
    }

    /**
     * @brief Returns the default value of the parameter.
     * @return The default value as defined in SE37 of the parameter as a std::string.
     */
    std::string RfcFunctionParameterDesc::GetDefaultValue() const {
        return uc2std(_desc_handle.defaultValue);
    }

    /**
     * @brief Returns the description of the parameter.
     * @return The description as defined in SE37 of the parameter as a std::string.
     */
    std::string RfcFunctionParameterDesc::GetDescription() const {
        auto desc = uc2std(_desc_handle.parameterText);
        if (desc.empty()) {
            return "NULL";
        }

        return desc;
    }

    /**
     * @brief Returns the length of the parameter.
     * @return The length of the parameter.
     */
    unsigned int RfcFunctionParameterDesc::GetLength() const {
        return _desc_handle.nucLength;
    }

    /**
     * @brief Returns the length of the parameter.
     * @return Gives the number of decimals in case of a packed number (BCD)
     */
    unsigned int RfcFunctionParameterDesc::GetDecimals() const {
        return _desc_handle.decimals;
    }

    /**
     * @brief Specifies whether this parameter is defined as optional in SE37
     * @return Returns true if the parameter is optional.
     */
    bool RfcFunctionParameterDesc::IsOptional() const {
        return _desc_handle.optional == 1;
    }

    // RfcFunctionParameterDesc -----------------------------------------------

    /**
     * @brief Constructs a RfcFunction object.
     * @param connection The connection to use. We assume that the connection is already open. We use a shared_ptr to the connection so that the connection is not closed when the RfcFunction object is destroyed.
     * @param function_name The name of the function to call.
     */
    RfcFunction::RfcFunction(std::shared_ptr<RfcConnection> &connection, std::string function_name) : 
        _function_name(function_name), _connection(connection)
    {
        RFC_ERROR_INFO error_info;
        _desc_handle = RfcGetFunctionDesc(connection->handle, std2uc(function_name).get(), &error_info);
        if (_desc_handle == NULL || error_info.code != RFC_OK) {
            throw std::runtime_error(StringUtil::Format("Error getting function description: %s: %s",
                                                        rfcrc2std(error_info.code), uc2std(error_info.message)));
        }
    }

    RfcFunction::~RfcFunction() noexcept(false)
    {
        if (_desc_handle == NULL)
            return;
        
        // CLARIFY: Function desc is explicitly not freed, because it is owned by the SAP NW RFC library.
        // Destroying it leads to wrong internal behaviour.
    }

    /**
     * @brief Returns the RFC_FUNCTION_DESC_HANDLE of the function.
     * @return The RFC_FUNCTION_DESC_HANDLE of the function.
    */
    RFC_FUNCTION_DESC_HANDLE RfcFunction::GetDescriptionHandle() const
    {
        return _desc_handle;
    }

    /**
     * @brief Returns the name of the function.
     * @return The name of the function as a std::string.
    */
    std::string RfcFunction::GetName()
    {
        return _function_name;
    }

    /**
     * @brief Returns all parameter descriptions of the function.
     * @return A std::vector of RfcFunctionParameterDesc objects.
    */
    std::vector<RfcFunctionParameterDesc> RfcFunction::GetParameterInfos() 
    {
        if (!_param_infos.empty()) {
            return _param_infos;
        }

        RFC_RC rc;
        RFC_ERROR_INFO error_info;
        RFC_PARAMETER_DESC sap_param_desc;
        unsigned int param_count = 0;

        rc = RfcGetParameterCount(_desc_handle, &param_count, &error_info);
        if (rc != RFC_OK) {
            throw std::runtime_error(StringUtil::Format("Failed to get parameter count: %s: %s", 
                                                        rfcrc2std(error_info.code), uc2std(error_info.message)));
        }

        for (unsigned int i = 0; i < param_count; ++i) {
            rc = RfcGetParameterDescByIndex(_desc_handle, i, &sap_param_desc, &error_info);
            if (rc != RFC_OK) {
                throw std::runtime_error(StringUtil::Format("Failed to get parameter description: %s: %s", 
                                                            rfcrc2std(error_info.code), uc2std(error_info.message)));
            }

            _param_infos.push_back(RfcFunctionParameterDesc(sap_param_desc));
        }

        return _param_infos;
    }

    /**
     * @brief Returns a single parameter description of a function parameter.
     * @param name The name of the parameter.
     * @return The RfcFunctionParameterDesc object.
    */
    RfcFunctionParameterDesc RfcFunction::GetParameterInfo(std::string name) 
    {
        for (auto &param : GetParameterInfos()) {
            if (param.GetName() == name) {
                return param;
            }
        }

        throw std::runtime_error(StringUtil::Format("Parameter '%s' not found", name.c_str()));
    }

    /**
     * @brief Returns the names of all export and changing parameters of the function.
     * @return A std::vector of the names.
    */
    std::vector<string> RfcFunction::GetResultNames() 
    {
        std::vector<string> ret;
        for (auto &param : GetResultInfos()) {
            ret.push_back(param.GetName());
        }
        return ret;
    }

    /**
     * @brief Returns the types of all export and changing parameters of the function.
     * @return A std::vector of duckdb LogicalTypes.
    */
    std::vector<LogicalType> RfcFunction::GetResultTypes() 
    {
        std::vector<LogicalType> ret;
        for (auto &param : GetResultInfos()) {
            auto rfc_type = param.GetRfcType();
            auto duckdb_type = rfc_type->CreateDuckDbType();

            ret.push_back(duckdb_type);
        }
        return ret;
    }

    /**
     * @brief Returns all result parameter descriptions of the function.
     * @return A std::vector of RfcFunctionParameterDesc objects.
    */
    std::vector<RfcFunctionParameterDesc> RfcFunction::GetResultInfos() 
    {
        std::vector<RfcFunctionParameterDesc> ret;
        for (auto &param : GetParameterInfos()) {
            if (param.GetDirection() == RFC_IMPORT) {
                continue;
            }

            ret.push_back(param);
        }
        return ret;
    }

    /**
     * @brief Returns the info for a single result parameter.
     * @param name The name of the parameter.
     * @return A single parameter description.
    */
    RfcFunctionParameterDesc RfcFunction::GetResultInfo(std::string name) 
    {
        for (auto &res : GetResultInfos()) {
            if (res.GetName() == name) {
                return res;
            }
        }

        throw std::runtime_error(StringUtil::Format("Parameter '%s' not found", name.c_str()));
    }

    /**
     * @brief Returns the info for a single result parameter.
     * @param index The index of the column.
     * @return A single parameter description.
    */
    RfcFunctionParameterDesc RfcFunction::GetResultInfo(unsigned int index) 
    {
        auto results = GetResultInfos();
        if (index >= results.size()) {
            throw std::runtime_error(StringUtil::Format("Parameter index %d out of bounds", index));
        }

        return results[index];
    }

    /** 
     * @brief Returns the connection associated with this function.
     * @return The connection associated with this function.
    */
    std::shared_ptr<RfcConnection> RfcFunction::GetConnection() const
    {
        return _connection;
    }


    std::shared_ptr<RfcInvocation> RfcFunction::BeginInvocation(std::vector<Value> &arguments) 
    {
        return std::make_shared<RfcInvocation>(shared_from_this(), arguments);
    }

    std::shared_ptr<RfcInvocation> RfcFunction::BeginInvocation() 
    {
        std::vector<Value> func_args;
        return std::make_shared<RfcInvocation>(shared_from_this(), func_args);
    }

    // RfcFunction --------------------------------------------------------

    RfcInvocationAdapter::RfcInvocationAdapter(RfcInvocation &invocation) 
        : _invocation(invocation)
    { 

    }

    RfcInvocationAdapter::~RfcInvocationAdapter() 
    { }

    RfcInvocationPositionalAdapter::RfcInvocationPositionalAdapter(RfcInvocation &invocation) 
        : RfcInvocationAdapter(invocation) 
    { }

    void RfcInvocationPositionalAdapter::Adapt(std::vector<RfcFunctionParameterDesc> parameter_infos, std::vector<Value> &arguments) 
    {
        if (arguments.size() == 0) {
            return;
        }

        throw std::logic_error("Positional adapter not implemented");
    }


    RfcInvocationNamedAdapter::RfcInvocationNamedAdapter(RfcInvocation &invocation) 
        : RfcInvocationAdapter(invocation) 
    { }

    void RfcInvocationNamedAdapter::Adapt(std::vector<RfcFunctionParameterDesc> parameter_infos, std::vector<Value> &arguments) 
    {
        if (arguments.size() != 1 || arguments[0].type().id() != LogicalTypeId::STRUCT) {
            throw std::runtime_error("Named invocation adapter expects a single struct argument");
        }

        auto &child_types = duckdb::StructType::GetChildTypes(arguments[0].type());
		auto &child_values = duckdb::StructValue::GetChildren(arguments[0]);

        for (std::size_t child_idx = 0; child_idx < child_values.size(); child_idx++) {
            auto child_value = child_values.at(child_idx);
			auto child_name = child_types.at(child_idx).first;
            
            try 
            {
                AdaptChildValue(parameter_infos, child_name, child_value);
            } catch (std::exception &ex) 
            {
                throw std::runtime_error(StringUtil::Format("Failed to adapt value for invocation argument '%s': %s", child_name, ex.what()));
            }
        }
    }

    void RfcInvocationNamedAdapter::AdaptChildValue(std::vector<RfcFunctionParameterDesc> parameter_infos, std::string &child_name, Value &child_value) 
    {
        for (auto &parameter_info : parameter_infos) {
            if (parameter_info.GetDirection() == RFC_EXPORT) {
                continue;
            }

            auto param_name = parameter_info.GetName();
            if (param_name != child_name) {
                continue;
            }

            auto child_type = child_value.type().id();
            auto param_type = parameter_info.GetRfcType();

            if (! param_type->IsCompatibleType(child_type)) {
                throw std::runtime_error(StringUtil::Format("Parameter '%s' is of type '%s' (RFC) but argument is of type '%s' (DuckDB)", 
                                                            child_name, param_type->GetName(), child_value.type().ToString()));
            }

            param_type->AdaptValue(_invocation, param_name, child_value);
            return;
        }

        throw std::runtime_error(StringUtil::Format("Parameter '%s' not found", child_name));
    }

    // RfcInvocationAdapter -----------------------------------------------

    RfcInvocation::RfcInvocation(std::shared_ptr<RfcFunction> function, std::vector<Value> &arguments) 
    {
        _function = function;
        _arguments = arguments;
        _handle = CreateRfcFunctionHandle(function);

        auto adapter = CreateRfcInvocationAdapter(arguments);
        adapter->Adapt(function->GetParameterInfos(), arguments);
    }

    RfcInvocation::~RfcInvocation() noexcept(false) 
    {
        if (_handle == NULL) {
            return;
        }

        RFC_RC rc = RFC_OK;
        RFC_ERROR_INFO error_info;
        rc = RfcDestroyFunction(_handle, &error_info);
        if (rc != RFC_OK) {
            throw std::runtime_error(StringUtil::Format("Error destroying function: %s: %s "  
                                                        "(This is indicates a serious error in the SAP NW RFC library.)",
                                                        rfcrc2std(error_info.code), uc2std(error_info.message)));
        }
        _handle = NULL;
    }

    RFC_FUNCTION_HANDLE RfcInvocation::CreateRfcFunctionHandle(std::shared_ptr<RfcFunction> function)
    {
        RFC_ERROR_INFO error_info;
        auto handle = RfcCreateFunction(function->GetDescriptionHandle(), &error_info);
        if (handle == nullptr) {
            throw std::runtime_error(StringUtil::Format("Failed to create function handle: %s: %s", 
                                                        rfcrc2std(error_info.code), uc2std(error_info.message)));
        }

        return handle;
    }

    bool RfcInvocation::IsAdaptByPosition(std::vector<Value> &arguments) 
    {
        if (arguments.empty()) {
            return true; // if we have no arguments, we assume we can adapt by position
        }
        
        if (arguments.size() > 1 || arguments[0].type().id() != LogicalTypeId::STRUCT) {
            return true;
        }

        return false;
    }

    std::unique_ptr<RfcInvocationAdapter> RfcInvocation::CreateRfcInvocationAdapter(std::vector<Value> &arguments) 
    {
        std::unique_ptr<RfcInvocationAdapter> adapter;
        
        if (IsAdaptByPosition(arguments)) {
            adapter = std::make_unique<RfcInvocationPositionalAdapter>(*this);
        } else {
            adapter = std::make_unique<RfcInvocationNamedAdapter>(*this);
        }
        
        return adapter;
    }


    std::shared_ptr<RfcFunction> RfcInvocation::GetFunction() const
    {
        return _function;
    }

    std::shared_ptr<RfcConnection> RfcInvocation::GetConnection() const
    {
        return GetFunction()->GetConnection();
    }

    std::shared_ptr<RfcResultSet> RfcInvocation::Invoke(std::string path) 
    {
        RFC_ERROR_INFO error_info;
        RFC_RC rc = RFC_OK;

        auto function = GetFunction();
        auto connection = function->GetConnection();

        rc = RfcInvoke(connection->handle, _handle, &error_info);
        if (rc != RFC_OK) {
            throw std::runtime_error(StringUtil::Format("Failed to invoke function:\n%s", rfcerrorinfo2std(error_info)));
        }

        return std::make_shared<RfcResultSet>(shared_from_this(), path);
    }
    
    std::shared_ptr<RfcResultSet> RfcInvocation::Invoke() 
    {
        return Invoke("");
    }

    void RfcInvocation::DeactivateResult(std::string &param_name)
    {
        RFC_ERROR_INFO error_info;
        RFC_RC rc = RFC_OK;

        auto function = GetFunction();
        int is_active = 0;
        rc = RfcSetParameterActive(_handle, std2uc(param_name).get(), is_active, &error_info);
        if (rc != RFC_OK) {
            throw std::runtime_error(StringUtil::Format("Failed to deactivate result parameter %s: %s: %s", 
                                                        param_name, rfcrc2std(error_info.code), uc2std(error_info.message)));
        }
    }

    void RfcInvocation::DeactivateResult(RfcFunctionParameterDesc &param_info)
    {
        auto param_name = param_info.GetName();
        DeactivateResult(param_name);
    }

    void RfcInvocation::DeactivateResult(unsigned int col_idx)
    {
        auto result_info = GetFunction()->GetResultInfo(col_idx);
        DeactivateResult(result_info);
    }

    void RfcInvocation::DeactivateResult(std::vector<std::string> &parameter_names)
    {
        for (auto &param_name : parameter_names) {
            DeactivateResult(param_name);
        }
    }

    void RfcInvocation::DeactivateResult(std::vector<unsigned int> &col_idxs)
    {
        for (auto &idx : col_idxs) {
            DeactivateResult(idx);
        }
    }

    
    void RfcInvocation::SelectResultAndDeactivateOthers(std::vector<std::string> &param_names)
    {
        auto function = GetFunction();
        auto result_infos = function->GetResultInfos();
        
        for (auto &result_info : result_infos) {
            auto param_name = result_info.GetName();
            if (std::find(param_names.begin(), param_names.end(), param_name) != param_names.end()) {
                continue;
            }
            DeactivateResult(result_info);
        }
    }

    void RfcInvocation::SelectResultAndDeactivateOthers(std::vector<unsigned int> &col_idxs)
    {
        auto param_names = std::vector<std::string>{};
        for (auto &idx : col_idxs) {
            auto result_info = GetFunction()->GetResultInfo(idx);
            param_names.push_back(result_info.GetName());
        }
        SelectResultAndDeactivateOthers(param_names);
    }

    void RfcInvocation::SelectResultAndDeactivateOthers(std::string &param_name)
    {
        auto params = std::vector<std::string>{param_name};
        SelectResultAndDeactivateOthers(params);
    }
    
    void RfcInvocation::SelectResultAndDeactivateOthers(RfcFunctionParameterDesc &param_desc)
    {
        auto param_name = param_desc.GetName();
        SelectResultAndDeactivateOthers(param_name);
    }

    void RfcInvocation::SelectResultAndDeactivateOthers(unsigned int col_idx)
    {
        auto result_info = GetFunction()->GetResultInfo(col_idx);
        SelectResultAndDeactivateOthers(result_info);
    }

    RFC_FUNCTION_HANDLE RfcInvocation::GetFunctionHandle() const
    {
        return _handle;
    }

    DATA_CONTAINER_HANDLE RfcInvocation::GetDataContainerHandle() const
    {
        auto func_handle = GetFunctionHandle();
        return (DATA_CONTAINER_HANDLE)func_handle;
    }

    // RfcInvocation ------------------------------------------------------------

    RfcResultSet::RfcResultSet(std::shared_ptr<RfcInvocation> invocation, std::string path) 
        : _invocation(invocation), _path(path), _total_rows(0), _current_row_idx(0)
    {
        std::tie(_result_names, _result_types) = InferResultSchema(path);
        _result_data = ConvertValuesAndSelectPath(path);
    }

    std::vector<LogicalType> RfcResultSet::GetResultTypes() 
    {
        return _result_types;   
    }

    std::vector<std::string> RfcResultSet::GetResultNames() 
    {
        return _result_names;   
    }
   
    std::pair<std::vector<std::string>, std::vector<LogicalType>> RfcResultSet::InferResultSchema(std::string path) 
    {
        auto tokens = ValueHelper::ParseJsonPointer(path);
        auto param_infos = _invocation->GetFunction()->GetResultInfos();

        if (tokens.empty()) {
            return std::make_pair(ExtractResultNames(param_infos), ExtractResultTypes(param_infos));
        }        

        // On a first level try to get the function parameter.
        auto token_it = tokens.begin();
        auto param = _invocation->GetFunction()->GetResultInfo(*token_it);
        auto field_infos = param.GetRfcType()->GetFieldInfos();

        for (token_it++; token_it != tokens.end(); token_it++) {
            auto field_it = std::find_if(field_infos.begin(), field_infos.end(), [&](auto &f) {
                return f.GetName() == *token_it;
            });

            if (field_it == field_infos.end()) {
                auto available_names = StringUtil::Join(ExtractResultNames(field_infos), ", ");
                throw std::runtime_error(StringUtil::Format("Field '%s' from path '%s' not found, available names are '%s'", 
                                                            *token_it, path, available_names));
            }

            field_infos = (*field_it).GetRfcType()->GetFieldInfos();
        }

        return std::make_pair(ExtractResultNames(field_infos), ExtractResultTypes(field_infos));
    }

    std::vector<std::string> RfcResultSet::ExtractResultNames(std::vector<RfcFunctionParameterDesc> &param_infos) 
    {
        std::vector<std::string> names;
        std::transform(param_infos.begin(), param_infos.end(), std::back_inserter(names), [&](auto &p) {
            return p.GetName();
        });
        return names;
    }

    std::vector<std::string> RfcResultSet::ExtractResultNames(std::vector<RfcFieldDesc> &field_infos) 
    {
        std::vector<std::string> names;
        std::transform(field_infos.begin(), field_infos.end(), std::back_inserter(names), [&](auto &f) {
            return f.GetName();
        });
        return names;
    }

    std::vector<LogicalType> RfcResultSet::ExtractResultTypes(std::vector<RfcFunctionParameterDesc> &param_infos) 
    {
        std::vector<LogicalType> types;
        std::transform(param_infos.begin(), param_infos.end(), std::back_inserter(types), [&](auto &p) {
            auto rfc_type = p.GetRfcType();
            return rfc_type->CreateDuckDbType();
        });
        return types;
    }

    std::vector<LogicalType> RfcResultSet::ExtractResultTypes(std::vector<RfcFieldDesc> &field_infos) 
    {
        std::vector<LogicalType> types;
        std::transform(field_infos.begin(), field_infos.end(), std::back_inserter(types), [&](auto &f) {
            auto rfc_type = f.GetRfcType();
            return rfc_type->CreateDuckDbType();
        });
        return types;
    }

    std::vector<Value> RfcResultSet::ConvertValuesAndSelectPath(std::string path) 
    {
        auto tokens = ValueHelper::ParseJsonPointer(path);
        auto param_infos = _invocation->GetFunction()->GetResultInfos();

        auto token_it = tokens.begin();
        if (token_it == tokens.end()) {
            return ConvertResultValues(param_infos);
        }

        auto param = _invocation->GetFunction()->GetResultInfo(*token_it);
        auto container_value = ConvertSingleResultValue(param);
        
        auto container_type = container_value.type().id();
        switch(container_type) 
        {
            case LogicalTypeId::STRUCT:
                return ConvertValuesFromStruct(container_value, std::vector<string>(token_it + 1, tokens.end()));
            case LogicalTypeId::LIST:
                return ConvertValuesFromList(container_value, std::vector<string>(token_it + 1, tokens.end()));
            default:
                throw std::runtime_error(StringUtil::Format("Field '%s' from path '%s' is not a container type", *token_it, path));
        };
    }

    std::vector<Value> RfcResultSet::ConvertResultValues(std::vector<RfcFunctionParameterDesc> &param_infos) 
    {
        std::vector<Value> values;
        std::transform(param_infos.begin(), param_infos.end(), std::back_inserter(values), [&](auto &p) {
            return ConvertSingleResultValue(p);
        });
        _total_rows = 1;
        return values;
    }

    Value RfcResultSet::ConvertSingleResultValue(RfcFunctionParameterDesc &param_info) 
    {
            auto rfc_type = param_info.GetRfcType();
            auto field_name = param_info.GetName();
            return rfc_type->ConvertRfcValue(_invocation, field_name);
    }

    std::vector<Value> RfcResultSet::ConvertValuesFromStruct(Value &container_value, std::vector<std::string> tokens) 
    {
        auto token_it = tokens.begin();
        auto child_values = StructValue::GetChildren(container_value);

        if (token_it != tokens.end()) {
            for (std::size_t i = 0; i < StructType::GetChildCount(container_value.type()); i++) {
                auto child_name = StructType::GetChildName(container_value.type(), i);
                if (child_name == *token_it) {
                    auto child_value = child_values[i];
                    auto child_type = child_value.type().id();
                    switch(child_type) 
                    {
                        case LogicalTypeId::STRUCT:
                            return ConvertValuesFromStruct(child_value, vector<string>(token_it + 1, tokens.end()));
                        case LogicalTypeId::LIST:
                            return ConvertValuesFromList(child_value, vector<string>(token_it + 1, tokens.end()));
                        default:
                            throw std::runtime_error(StringUtil::Format("Field '%s' is not a container type", *token_it));
                    };
                }
            }
        }
            
        _total_rows = 1;
        return child_values;
    }

    std::vector<Value> RfcResultSet::ConvertValuesFromList(Value &container_value, std::vector<std::string> tokens) 
    {
        auto token_it = tokens.begin();
        auto rows = ListValue::GetChildren(container_value);
        auto row_type = ListType::GetChildType(container_value.type());
        if (row_type.id() != LogicalTypeId::STRUCT) {
            throw std::runtime_error(StringUtil::Format("Field '%s' is not a container type", *token_it));
        }

        auto column_count = StructType::GetChildCount(row_type);
        vector<vector<Value>> pivot(column_count);
        for (Value &row : rows) {
            auto column_values = StructValue::GetChildren(row);
            for (std::size_t i = 0; i < column_count; i++) {
                pivot[i].push_back(column_values[i]);
            }
        }

        std::vector<Value> ret;
        for (std::size_t i = 0; i < column_count; i++) {
            auto column_type = StructType::GetChildType(row_type, i);
            auto column_values = pivot[i];
            auto column_list = Value::LIST(column_type, column_values);
            ret.push_back(column_list);
        }

        _total_rows = rows.size();
        return ret;
    }

    unsigned int RfcResultSet::FetchNextResult(DataChunk &output, std::vector<std::string> selected_fields) 
    {
        auto row_count = 0;

        for (std::size_t src_col_idx = 0; src_col_idx < _result_data.size(); src_col_idx++) {
            if (IsTabularResult()) {
                unsigned int tgt_col_idx = 0;
                if (MapColumnsByFieldSelection(selected_fields, src_col_idx, tgt_col_idx)) {
                    row_count = FetchNextTabularResult(output, src_col_idx, tgt_col_idx);
                }
            }
            else {
                auto &out_vec = output.data[src_col_idx];
                out_vec.SetValue(0, _result_data[src_col_idx]);
                output.SetCardinality(1);
                row_count++;
            }
        }
        _current_row_idx += row_count;
        return _current_row_idx;
    }

    unsigned int RfcResultSet::FetchNextResult(DataChunk &output) 
    {
        return FetchNextResult(output, _result_names);
    }

    bool RfcResultSet::MapColumnsByFieldSelection(std::vector<std::string> &selected_fields, unsigned int src_col_idx, unsigned int &tgt_col_idx) 
    {
        auto src_col_name = _result_names[src_col_idx];
        auto it = std::find(selected_fields.begin(), selected_fields.end(), src_col_name);
        if (it == selected_fields.end()) {
            return false;
        }
        tgt_col_idx = std::distance(selected_fields.begin(), it);
        return true;
    }

    unsigned int RfcResultSet::FetchNextTabularResult(DataChunk &output, unsigned int src_col_idx, unsigned int tgt_col_idx) 
    {
        unsigned int row_count = 0;
        auto result_vec = ListValue::GetChildren(_result_data[src_col_idx]);
        auto res_start = result_vec.begin() + _current_row_idx;
        auto res_end = res_start + STANDARD_VECTOR_SIZE > result_vec.end() 
                            ? result_vec.end() : res_start + STANDARD_VECTOR_SIZE;
        auto res_chunk = std::vector<Value>(res_start, res_end);
        for (std::size_t j = 0; j < res_chunk.size(); j++) {
            auto cell_value = res_chunk[j];
            output.SetValue(tgt_col_idx, j, cell_value);
        }
        output.SetCardinality(res_chunk.size());
        row_count = res_chunk.size();

        return row_count;
    }

    Value RfcResultSet::GetResultValue() 
    {
        child_list_t<Value> value_list;
        for (idx_t i=0; i < _result_data.size(); i++) {
            value_list.push_back(std::make_pair(_result_names[i], _result_data[i]));
        }

        return Value::STRUCT(value_list);
    }

    Value RfcResultSet::GetResultValue(unsigned int col_idx) 
    {
        return _result_data[col_idx];
    }

    Value RfcResultSet::GetResultValue(std::string col_path) 
    {
        auto tokens = ValueHelper::ParseJsonPointer(col_path);
        auto tokens_it = tokens.begin();

        auto it = std::find(_result_names.begin(), _result_names.end(), *tokens_it);
        if (it == _result_names.end()) {
            throw std::runtime_error(StringUtil::Format("Column name '%s' not found.", *tokens_it));
        }
        auto col_idx = std::distance(_result_names.begin(), it);
        auto result = GetResultValue(col_idx);

        auto remaining_tokens = std::vector<string>(tokens_it + 1, tokens.end());
        return ValueHelper::GetValueForPath(result, remaining_tokens);
    }

    unsigned int RfcResultSet::TotalRows() {
        return _total_rows;
    }

    bool RfcResultSet::HasMoreResults() 
    {
        return _current_row_idx < _total_rows;
    }

    bool RfcResultSet::IsTabularResult() {
        auto all_lists = std::all_of(_result_data.begin(), _result_data.end(), [](Value &v) {
            return v.type().id() == LogicalTypeId::LIST;
        });
        return all_lists;
    }

    std::string RfcResultSet::ToSQLString() 
    {
        std::stringstream ss;
        
        for (auto &name : GetResultNames()) {
            ss << name << ": ";
            ss << GetResultValue(name).ToSQLString() << std::endl;
        }
    
        return ss.str();
    }

    std::shared_ptr<RfcResultSet> RfcResultSet::InvokeFunction(std::shared_ptr<RfcConnection> connection,
                                                               std::string function_name,
                                                               std::vector<Value> &function_arguments,
                                                               std::string result_path)
    {
        auto func = std::make_shared<RfcFunction>(connection, function_name);
        auto invocation = function_arguments.size() > 0 
                            ? func->BeginInvocation(function_arguments) 
                            : func->BeginInvocation();
    
        auto result_set = result_path.empty() == false 
                            ? invocation->Invoke(result_path)
                            : invocation->Invoke();
        return result_set;
    }

    std::shared_ptr<RfcResultSet> RfcResultSet::InvokeFunction(std::shared_ptr<RfcConnection> connection,
                                                               std::string function_name,
                                                               std::vector<Value> &function_arguments)
    {
        return InvokeFunction(connection, function_name, function_arguments, "");
    }

    std::shared_ptr<RfcResultSet> RfcResultSet::InvokeFunction(std::shared_ptr<RfcConnection> connection,
                                                               std::string function_name)
    {
        std::vector<Value> empty_args;
        return InvokeFunction(connection, function_name, empty_args, "");
    }

    // RfcResultSet ------------------------------------------------------------

    RfcFunctionBindData::RfcFunctionBindData() 
        : _selected_fields_with_alias({})
    { }

    RfcFunctionBindData::RfcFunctionBindData(std::vector<std::string> selected_fields)
        : _selected_fields_with_alias(WithDefaultAlias(selected_fields))
    { }

    RfcFunctionBindData::RfcFunctionBindData(std::vector<std::tuple<std::string, std::string>> selected_fields_with_alias)
        : _selected_fields_with_alias(selected_fields_with_alias)
    { }

    std::vector<std::tuple<std::string, std::string>> RfcFunctionBindData::WithDefaultAlias(std::vector<std::string> &selected_fields)
    {
        std::vector<std::tuple<std::string, std::string>> selected_fields_with_alias;
        for (auto field : selected_fields) {
            selected_fields_with_alias.push_back(std::make_tuple(field, field));
        }
        return selected_fields_with_alias;
    }

    std::vector<std::string> RfcFunctionBindData::GetFieldNames()
    {
        std::vector<std::string> selected_fields;
        std::transform(_selected_fields_with_alias.begin(), _selected_fields_with_alias.end(), std::back_inserter(selected_fields), [&](auto &f) {
            return std::get<0>(f);
        });
        return selected_fields;
    }

    std::vector<std::string> RfcFunctionBindData::GetResultNames()
    {
        if (_selected_fields_with_alias.empty()) {
            result_set->GetResultNames();
        }

        std::vector<std::string> selected_fields;
        std::transform(_selected_fields_with_alias.begin(), _selected_fields_with_alias.end(), std::back_inserter(selected_fields), [&](auto &f) {
            return std::get<1>(f);
        });
        return selected_fields;
    }

    std::vector<LogicalType> RfcFunctionBindData::GetResultTypes()
    {
        auto result_types = result_set->GetResultTypes();
        if (_selected_fields_with_alias.empty()) {
            return result_types;
        }

        auto result_names = result_set->GetResultNames();
        D_ASSERT(result_names.size() == result_types.size());

        std::vector<LogicalType> selected_types;
        for (idx_t i = 0; i < result_types.size(); i++) {
            auto it = std::find_if(_selected_fields_with_alias.begin(), _selected_fields_with_alias.end(), [&](auto &f) {
                return std::get<0>(f) == result_names[i];
            });
            if (it == _selected_fields_with_alias.end()) {
                continue;
            }

            selected_types.push_back(result_types[i]);
        }
        return selected_types;
    }

    bool RfcFunctionBindData::HasMoreResults()
    {
        return result_set->HasMoreResults();
    }

    unsigned int RfcFunctionBindData::FetchNextResult(DataChunk &output)
    {
        if (_selected_fields_with_alias.empty()) {
            return result_set->FetchNextResult(output);
        }
        
        return result_set->FetchNextResult(output, GetFieldNames());
    }

} // namespace duckdb
