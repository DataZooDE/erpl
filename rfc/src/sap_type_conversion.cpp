#include <codecvt>
#include <regex>
#include <sstream>

#include "duckdb.hpp"
#include "sap_type_conversion.hpp"


namespace duckdb 
{
    /**
     * @brief Converts a SAP_UC string to a std::string.
     * 
     * @param uc_str The SAP_UC string to convert.
     * @param uc_str_len The length of the SAP_UC string.
     * @param rtrim If true, trims trailing spaces from the resulting string.
     * @return The SAP_UC string as a std::string.
     */
    std::string uc2std(SAP_UC *uc_str, unsigned int uc_str_len, bool rtrim) 
    {
        if (uc_str == NULL || uc_str_len == 0) {
            return std::string();
        }

        RFC_RC rc = RFC_OK;
        RFC_ERROR_INFO error_info;
        unsigned int utf8_size;
        unsigned int result_len = 0;
        char *utf8;

        // try with 5 bytes per unicode character (also 3 bytes is possible, but this will also fit.)
        utf8_size = uc_str_len * 5 + 1;
        utf8 = (char *)malloc(utf8_size);
        utf8[0] = '\0';

        rc = RfcSAPUCToUTF8(uc_str, uc_str_len, (RFC_BYTE *)utf8, &utf8_size, &result_len, &error_info);
        if (rc != RFC_OK || result_len <= 0) {
            free(utf8);
            return std::string();
        }

        // trim null bytes or trailing spaces if requested
        if (result_len > 0) {
            int i = result_len - 1;

            while (i >= 0 && (utf8[i] == '\0' || (rtrim && isspace(utf8[i])))) {
                i--;
            }

            utf8[i+1] = '\0';
            result_len = i+1;
        }

        auto ret = std::string(utf8, result_len);
        free(utf8);
        
        return ret;
    }

    /**
     * @brief Converts a SAP_UC string with a given length to a std::string.
     * 
     * @param uc_str The SAP_UC string to convert.
     * @param uc_str_len The length of the SAP_UC string.
     * @return The SAP_UC string as a std::string.
     */
    std::string uc2std(SAP_UC *uc_str, unsigned int uc_str_len) 
    {
        if (uc_str == NULL || uc_str_len == 0) {
            return std::string();
        }
        return uc2std(uc_str, uc_str_len, false);
    }

    /**
     * @brief Converts a SAP_UC string to a std::string.
     * 
     * @param uc_str The SAP_UC string to convert.
     * @return The SAP_UC string as a std::string.
     */
    std::string uc2std(SAP_UC *uc_str) 
    {
        return uc2std(uc_str, strlenU(uc_str), false);
    }

    /**
     * @brief Converts a SAP_UC string to a std::string.
     * 
     * @param uc_str The SAP_UC string to convert.
     * @return The SAP_UC string as a std::string.
     */
    std::string uc2std(const SAP_UC *uc_str) 
    {
        return uc2std((SAP_UC *)uc_str);
    }

    /**
     * @brief Converts a RFCTYPE to a LogicalTypeId.
     * 
     * @param sap_type The RFCTYPE to convert.
     * @return The RFCTYPE as a LogicalTypeId.
     */
    LogicalTypeId rfctype2logicaltype(RFCTYPE sap_type) 
    {
        switch (sap_type) {
            case RFCTYPE_NULL: return LogicalTypeId::SQLNULL;
            // Numeric types
            case RFCTYPE_NUM: return LogicalTypeId::VARCHAR;
            case RFCTYPE_INT: return LogicalTypeId::INTEGER;
            case RFCTYPE_INT1: return LogicalTypeId::TINYINT;
            case RFCTYPE_INT2: return LogicalTypeId::SMALLINT;
            case RFCTYPE_INT8: return LogicalTypeId::INTEGER;
            case RFCTYPE_FLOAT: return LogicalTypeId::DOUBLE;
            case RFCTYPE_BCD: return LogicalTypeId::DECIMAL;
            case RFCTYPE_DECF16: return LogicalTypeId::DECIMAL;
            case RFCTYPE_DECF34: return LogicalTypeId::DECIMAL;
            // Character types
            case RFCTYPE_CHAR: return LogicalTypeId::VARCHAR;
            case RFCTYPE_STRING: return LogicalTypeId::VARCHAR;
            case RFCTYPE_XSTRING: return LogicalTypeId::VARCHAR;
            case RFCTYPE_XMLDATA: return LogicalTypeId::VARCHAR;
            // Binary types
            case RFCTYPE_BYTE: return LogicalTypeId::BLOB;
            // Date types
            case RFCTYPE_TIME: return LogicalTypeId::TIME;
            case RFCTYPE_DATE: return LogicalTypeId::DATE;
            case RFCTYPE_UTCLONG: return LogicalTypeId::TIMESTAMP;
            case RFCTYPE_UTCSECOND: return LogicalTypeId::TIMESTAMP;
            case RFCTYPE_UTCMINUTE: return LogicalTypeId::TIMESTAMP;
            case RFCTYPE_DTDAY: return LogicalTypeId::INTEGER;
            case RFCTYPE_DTWEEK: return LogicalTypeId::INTEGER;
            case RFCTYPE_DTMONTH: return LogicalTypeId::INTEGER;
            case RFCTYPE_TSECOND: return LogicalTypeId::INTEGER;
            case RFCTYPE_TMINUTE: return LogicalTypeId::INTEGER;
            case RFCTYPE_CDAY: return LogicalTypeId::INTEGER;
            // Complex types
            case RFCTYPE_ABAPOBJECT: return LogicalTypeId::VARCHAR;
            case RFCTYPE_STRUCTURE: return LogicalTypeId::STRUCT;
            case RFCTYPE_TABLE: return LogicalTypeId::LIST;
            default: return LogicalTypeId::SQLNULL;
        }
    }

    /**
     * @brief Converts a SAP_UC string to a duckdb Value.
     * 
     * @param uc_str The SAP_UC string to convert.
     * @param uc_str_len The length of the SAP_UC string.
     * @param rtrim If true, trims trailing spaces from the resulting string.
     * @return The SAP_UC string as a duckdb Value.
     */
    Value uc2duck(SAP_UC *uc_str, unsigned int uc_str_len, bool rtrim) 
    {
        auto str = uc2std(uc_str, uc_str_len, rtrim);
        if (str.empty()) {
            return Value();
        }
        return Value::CreateValue(str);
    }

    /**
     * @brief Converts a SAP_UC string to a duckdb Value.
     * 
     * @param uc_str The SAP_UC string to convert.
     * @param uc_str_len The length of the SAP_UC string.
     * @return The SAP_UC string as a duckdb Value.
     */
    Value uc2duck(SAP_UC *uc_str, unsigned int uc_str_len) 
    {
        return uc2duck(uc_str, uc_str_len, false);
    }

    /**
     * @brief Converts a SAP_UC string to a duckdb Value.
     * 
     * @param uc_str The SAP_UC string to convert.
     * @return The SAP_UC string as a duckdb Value.
     */
    Value uc2duck(SAP_UC *uc_str) 
    {
        if (uc_str == NULL) {
            return Value();
        }
        return uc2duck(uc_str, strlenU(uc_str), false);
    }
    

    /**
     * @brief Converts a std::string to a SAP_UC string.
     * 
     * @param std_str The std::string to convert.
     * @param uc_str The resulting SAP_UC string.
     * @param uc_str_size The size of the resulting SAP_UC string.
     * @throws std::runtime_error if an error occurs during conversion.
     * @note The resulting SAP_UC string must be freed using free().
     */
    void std2uc(string &std_str, SAP_UC *uc_str, unsigned int uc_str_size) 
    {
        RFC_RC rc = RFC_OK;
        RFC_ERROR_INFO error_info;
        unsigned int result_len = 0;

        rc = RfcUTF8ToSAPUC((RFC_BYTE *)std_str.c_str(), std_str.length(), uc_str, &uc_str_size, &result_len, &error_info);
        if (rc != RFC_OK) {
            throw std::runtime_error("Error converting string to SAP_UC");
        }
    }

    /**
     * @brief Converts a std::string to a SAP_UC string.
     * 
     * @param std_str The std::string to convert.
     * @return A unique_ptr to the resulting SAP_UC string.
     * @note The resulting SAP_UC string must be freed using free().
     * @note If an error occurs during conversion, a null unique_ptr is returned.
     */
    unique_ptr<SAP_UC, void (*)(void*)> std2uc(string &std_str) 
    {
        unsigned int uc_str_size = std_str.length() + 1;
        unique_ptr<SAP_UC, void (*)(void*)> uc_str((SAP_UC *)mallocU(uc_str_size), free);
        uc_str.get()[0] = '\0';

        try {
            std2uc(std_str, uc_str.get(), uc_str_size);
        } catch (std::exception &e) {
            return unique_ptr<SAP_UC, void (*)(void*)>(nullptr, free);
        }

        return uc_str;
    }

    /**
     * @brief Converts a std::string to a SAP_UC string.
     * 
     * @param std_str The std::string to convert.
     * @return A unique_ptr to the resulting SAP_UC string.
     * @note The resulting SAP_UC string must not be freed using free(), it is managed by the smart pointer.
     * @note If an error occurs during conversion, a null is returned.
     */
    unique_ptr<SAP_UC, void (*)(void*)> std2uc(const string &std_str) 
    {
        return std2uc((string &)std_str);
    }

    /**
     * @brief Converts a duckdb Value to a SAP_UC string.
     * 
     * @param value The duckdb String Value to convert.
     * @return A unique_ptr to the resulting SAP_UC string.
     * @note The resulting SAP_UC string must not be freed using free(), it is managed by the smart pointer.
     * @note If the input value is null, a null unique_ptr is returned.
     */
    unique_ptr<SAP_UC, void (*)(void*)> duck2uc(Value &value) 
    {
        if (value.IsNull()) {
            return unique_ptr<SAP_UC, void (*)(void*)>(nullptr, free);
        }

        auto str = StringValue::Get(value);
        return std2uc(str);
    }

    /**
     * @brief Converts a SAP date to a DuckDB date.
     * 
     * @param rfc_date The SAP date to convert.
     * @return The SAP date as a DuckDB DATETIME.
     * @note The SAP date is expected to be in the format YYYYMMDD.
     * @note If the date is invalid a default DuckDB date is returned.
     */
    Value rfc2duck(RFC_DATE &rfc_date) 
    {
        auto date_str = uc2std(rfc_date, SAP_DATE_LN);
        return dats2duck(date_str);
    }

    /** 
     * @brief Converts a SAP date '20230901' to a DuckDB date.
     * 
     * @param dats_str The SAP date to convert.
     * @return The SAP date as a DuckDB DATETIME.
     * @note The SAP date is expected to be in the format YYYYMMDD.
     * @note If the date is invalid a default DuckDB date is returned.
    */
    Value dats2duck(std::string &dats_str) 
    {
        if (dats_str.empty()) {
            return Value();
        }

        int32_t year, month, day;

        std::istringstream(dats_str.substr(0, 4)) >> year;
        std::istringstream(dats_str.substr(4, 2)) >> month;
        std::istringstream(dats_str.substr(6, 2)) >> day;

        date_t duck_date;
        Date::TryFromDate(year, month, day, duck_date);
        return Value::CreateValue(duck_date);
    }

    /** 
     * @brief Converts a SAP time '235959' to a DuckDB time.
     * 
     * @param tims_str The SAP time to convert.
     * @return The SAP time as a DuckDB DATETIME.
     * @note The SAP time is expected to be in the format HHMMSS.
     * @note If the time is invalid a default DuckDB time is returned.
    */
    Value tims2duck(std::string &tims_str)
    {
        if (tims_str.empty()) {
            return Value();
        }

        int32_t hour, minute, second;

        std::istringstream(tims_str.substr(0, 2)) >> hour;
        std::istringstream(tims_str.substr(2, 2)) >> minute;
        std::istringstream(tims_str.substr(4, 2)) >> second;

        dtime_t duck_time = Time::FromTime(hour, minute, second, 0);
        return Value::CreateValue(duck_time);
    }

    Value bcd2duck(std::string &bcd_str, unsigned int length, unsigned int decimals)
    {
        if (bcd_str.empty()) {
            return Value();
        }

        // Remove after terminator from the string
        size_t pos = bcd_str.find('\0');
        if (pos != std::string::npos) {
            bcd_str = bcd_str.substr(0, pos);
        }

        // Check if it is a negative bcd and the last character 
        // is of the decimal is '-'
        if (!bcd_str.empty() && bcd_str.back() == '-') {
            bcd_str.pop_back();
            bcd_str.insert(bcd_str.begin(), '-');
        }

        auto str_val = Value(bcd_str);
        auto dec_typ = LogicalType::DECIMAL(length, decimals);
        auto dec_val = str_val.DefaultCastAs(dec_typ);

        return dec_val;
    }

    /**
     * @brief Converts a SAP date to a DuckDB date.
     * 
     * @param rfc_date The SAP date to convert.
     * @return The SAP date as a DuckDB DATETIME.
     * @note The SAP date is expected to be in the format YYYYMMDD.
     * @note If the date is invalid a default DuckDB date is returned.
     */
    Value rfc2duck(const RFC_DATE &rfc_date) 
    {
        return rfc2duck((RFC_DATE &)rfc_date);
    }

    /**
     * @brief Checks if a given time is valid.
     * 
     * @param hour The hour component of the time.
     * @param minute The minute component of the time.
     * @param second The second component of the time.
     * @return true if the time is valid, false otherwise.
     */
    bool IsValidTime(const int32_t hour, const int32_t minute, const int32_t second) 
    {
        return hour >= 0 && hour <= 23 && minute >= 0 && minute <= 59 && second >= 0 && second <= 59;
    }

    /**
     * @brief Converts a SAP time to a DuckDB time.
     * 
     * @param rfc_time The SAP time to convert.
     * @return The SAP time as a DuckDB DATETIME.
     * @note The SAP time is expected to be in the format HHMMSS.
     * @note If the input time is invalid, a default time value is returned.
     */
    Value rfc2duck(RFC_TIME &rfc_time) 
    {
        auto time_str = uc2std(rfc_time, SAP_TIME_LN);
        int32_t hour, minute, second;

        std::istringstream(time_str.substr(0, 2)) >> hour;
        std::istringstream(time_str.substr(2, 2)) >> minute;
        std::istringstream(time_str.substr(4, 2)) >> second;

        if (!IsValidTime(hour, minute, second)) {
            return Value::CreateValue((dtime_t()));
        }

        dtime_t duck_time = Time::FromTime(hour, minute, second, 0);
        return Value::CreateValue(duck_time);
    }

    /**
     * @brief Converts a SAP time to a DuckDB time.
     * 
     * @param rfc_time The SAP time to convert.
     * @return The SAP time as a DuckDB DATETIME.
     * @note The SAP time is expected to be in the format HHMMSS.
     * @note If the input time is invalid, a default time value is returned.
     */
    Value rfc2duck(const RFC_TIME &rfc_time) 
    {
        return rfc2duck((RFC_TIME &)rfc_time);
    }

    /**
     * @brief Converts a SAP float to a DuckDB double.
     * 
     * @param rfc_float The SAP float to convert.
     * @return The SAP float as a DuckDB DOUBLE.
     */
    Value rfc2duck(RFC_FLOAT &rfc_float) 
    {
        return Value::DOUBLE((double)rfc_float);
    }

    /**
     * @brief Converts a SAP float to a DuckDB double.
     * 
     * @param rfc_float The SAP float to convert.
     * @return The SAP float as a DuckDB DOUBLE.
     */
    Value rfc2duck(const RFC_FLOAT &rfc_float) 
    {
        return rfc2duck((RFC_FLOAT &)rfc_float);
    }

    /**
     * @brief Converts a SAP integer to a DuckDB integer.
     * 
     * @param rfc_int The SAP integer to convert.
     * @return The SAP integer as a DuckDB INTEGER.
     */
    Value rfc2duck(RFC_INT &rfc_int) 
    {
        return Value::INTEGER((int)rfc_int);
    }

    /**
     * @brief Converts a SAP integer to a DuckDB integer.
     * 
     * @param rfc_int The SAP integer to convert.
     * @return The SAP integer as a DuckDB INTEGER.
     */
    Value rfc2duck(const RFC_INT &rfc_int) 
    {
        return rfc2duck((RFC_INT &)rfc_int);
    }

    /**
     * @brief Converts a SAP integer (1-byte) to a DuckDB integer.
     * 
     * @param rfc_int1 The SAP integer to convert.
     * @return The SAP integer as a DuckDB TINYINT.
     */
    Value rfc2duck(RFC_INT1 &rfc_int1) 
    {
        return Value::TINYINT((int8_t)rfc_int1);
    }

    /**
     * @brief Converts a SAP integer (1-byte) to a DuckDB integer.
     * 
     * @param rfc_int1 The SAP integer to convert.
     * @return The SAP integer as a DuckDB TINYINT.
     */
    Value rfc2duck(const RFC_INT1 &rfc_int1) 
    {
        return rfc2duck((RFC_INT1 &)rfc_int1);
    }

    /**
     * @brief Converts a SAP integer (2-byte) to a DuckDB integer.
     * 
     * @param rfc_int2 The SAP integer to convert.
     * @return The SAP integer as a DuckDB SMALLINT.
     */
    Value rfc2duck(RFC_INT2 &rfc_int2) 
    {
        return Value::SMALLINT((int16_t)rfc_int2);
    }

    /**
     * @brief Converts a SAP integer (2-byte) to a DuckDB integer.
     * 
     * @param rfc_int2 The SAP integer to convert.
     * @return The SAP integer as a DuckDB SMALLINT.
     */
    Value rfc2duck(const RFC_INT2 &rfc_int2) 
    {
        return rfc2duck((RFC_INT2 &)rfc_int2);
    }

    /**
     * @brief Converts a SAP integer (8-byte) to a DuckDB integer.
     * 
     * @param rfc_int8 The SAP integer to convert.
     * @return The SAP integer as a DuckDB BITGINT.
     */
    Value rfc2duck(RFC_INT8 &rfc_int8) 
    {
        return Value::BIGINT((int64_t)rfc_int8);
    }

    /**
     * @brief Converts a SAP integer (2-byte) to a DuckDB integer.
     * 
     * @param rfc_int2 The SAP integer to convert.
     * @return The SAP integer as a DuckDB BIGINT.
     */
    Value rfc2duck(const RFC_INT8 &rfc_int8) 
    {
        return rfc2duck((RFC_INT8 &)rfc_int8);
    }

    /**
     * @brief Converts a SAP numeric value to a DuckDB value.
     * 
     * @param rfc_num Pointer to the SAP numeric value.
     * @param rfc_num_len Length of the SAP numeric value.
     * @param rtrim If true, the returned string is right-trimmed.
     * @return The SAP numeric value as a DuckDB STRING.
     */
    Value rfc2duck(RFC_NUM *rfc_num, unsigned int rfc_num_len, bool rtrim) 
    {
        auto num_str = uc2std(rfc_num, rfc_num_len, rtrim);
        return Value(num_str);
    }

    /**
     * @brief Converts a SAP NUM (or CHAR) value to a DuckDB value.
     * 
     * @param rfc_num Pointer to the SAP NUM (or CHAR) array.
     * @param rfc_num_len Length of the SAP value array.
     * @param rtrim If true, the returned string is right-trimmed.
     * @return The SAP numeric value as a DuckDB STRING.
     */
    Value rfc2duck(const RFC_NUM *rfc_num, const unsigned int rfc_num_len, const bool rtrim) 
    {
        return rfc2duck((RFC_NUM *)rfc_num, rfc_num_len, rtrim);
    }

    /**
     * @brief Converts a SAP numeric value to a DuckDB value.
     * 
     * @param rfc_num Pointer to the SAP numeric value.
     * @param rfc_num_len Length of the SAP numeric value.
     * @param rtrim If true, the returned string is right-trimmed.
     * @return The SAP numeric value as a DuckDB STRING.
     */
    Value rfc2duck(RFC_NUM *rfc_num, unsigned int rfc_num_len) 
    {
        auto num_str = uc2std(rfc_num, rfc_num_len, false);
        return Value(num_str);
    }

    /**
     * @brief Converts a SAP NUM (or CHAR) value to a DuckDB value.
     * 
     * @param rfc_num Pointer to the SAP NUM (or CHAR) array.
     * @param rfc_num_len Length of the SAP value array.
     * @param rtrim If true, the returned string is right-trimmed.
     * @return The SAP numeric value as a DuckDB STRING.
     */
    Value rfc2duck(const RFC_NUM *rfc_num, const unsigned int rfc_num_len) 
    {
        return rfc2duck((RFC_NUM *)rfc_num, rfc_num_len, false);
    }


    /**
     * @brief Converts a SAP byte array to a DuckDB BLOB value.
     * 
     * @param rfc_byte Pointer to the SAP byte array.
     * @param rfc_byte_len Length of the SAP byte array.
     * @return The SAP byte array as a DuckDB BLOB.
     */
    Value rfc2duck(RFC_BYTE *rfc_byte, unsigned int rfc_byte_len) 
    {
        return Value::BLOB(rfc_byte, rfc_byte_len);
    }

    /**
     * @brief Converts a SAP byte array to a DuckDB BLOB value.
     * 
     * @param rfc_byte Pointer to the SAP byte array.
     * @param rfc_byte_len Length of the SAP byte array.
     * @return The SAP byte array as a DuckDB BLOB.
     */
    Value rfc2duck(const RFC_BYTE *rfc_byte, const unsigned int rfc_byte_len) 
    {
        return rfc2duck((RFC_BYTE *)rfc_byte, rfc_byte_len);
    }

    /**
     * @brief Converts a DuckDB DATE value to a SAP date value.
     * 
     * @param value The DuckDB DATE value to convert.
     * @param rfc_date The SAP date value to store the converted value.
     * @return void
     * 
     * @note The SAP date value is stored in the format YYYYMMDD.
     * @note The DuckDB DATE value is stored in the format YYYY-MM-DD.
     */
    void duck2rfc(Value &value, RFC_DATE &rfc_date) 
    {
        int32_t year, month, day;
        Date::Convert(value.GetValue<date_t>(), year, month, day);

        auto uc_date = std2uc(StringUtil::Format("%04d%02d%02d", year, month, day));
        memcpy((char *)rfc_date, (char *)uc_date.get(), SAP_DATE_LN * 2);
    }

    /**
     * @brief Converts a DuckDB TIME value to a SAP time value.
     * 
     * @param value The DuckDB TIME value to convert.
     * @param rfc_time The SAP time value to store the converted value.
     * @return void
     * 
     * @note The SAP time value is stored in the format HHMMSS.
     * @note The DuckDB TIME value is stored in the format HH:MM:SS:MS.
     */
    void duck2rfc(Value &value, RFC_TIME &rfc_time) 
    {
        int32_t hour, minute, second, ms;
        Time::Convert(value.GetValue<dtime_t>(), hour, minute, second, ms);

        auto uc_time = std2uc(StringUtil::Format("%02d%02d%02d", hour, minute, second));
        memcpy((char *)rfc_time, (char *)uc_time.get(), SAP_TIME_LN * 2);
    }

    /**
     * @brief Converts a DuckDB DOUBLE value to a SAP float value.
     * 
     * @param value The DuckDB DOUBLE value to convert.
     * @param rfc_float The SAP float value to store the converted value.
     * @return void
     */
    void duck2rfc(Value &value, RFC_FLOAT &rfc_float) 
    {
        auto duck_value = value.GetValue<double>();
        rfc_float = (RFC_FLOAT)duck_value;
    }

    /**
     * @brief Converts a DuckDB INTEGER value to a SAP integer value.
     * 
     * @param value The DuckDB INTEGER value to convert.
     * @param rfc_int The SAP integer value to store the converted value.
     * @return void
     */
    void duck2rfc(Value &value, RFC_INT &rfc_int) 
    {
        auto duck_value = value.GetValue<int>();
        rfc_int = (RFC_INT)duck_value;
    }

    /**
     * @brief Converts a DuckDB TINYINT value to a SAP integer 1-byte value.
     * 
     * @param value The DuckDB TINYINT value to convert.
     * @param rfc_int1 The SAP integer 1-byte value to store the converted value.
     * @return void
     */
    void duck2rfc(Value &value, RFC_INT1 &rfc_int1) 
    {
        auto duck_value = value.GetValue<int8_t>();
        rfc_int1 = (RFC_INT1)duck_value;
    }

    /**
     * @brief Converts a DuckDB SMALLINT value to a SAP integer 2-byte value.
     * 
     * @param value The DuckDB SMALLINT value to convert.
     * @param rfc_int1 The SAP integer 2-byte value to store the converted value.
     * @return void
     */
    void duck2rfc(Value &value, RFC_INT2 &rfc_int2) 
    {
        auto duck_value = value.GetValue<int16_t>();
        rfc_int2 = (RFC_INT2)duck_value;
    }

    /**
     * @brief Converts a DuckDB BIGINT value to a SAP integer 8-byte value.
     * 
     * @param value The DuckDB BIGINT value to convert.
     * @param rfc_int1 The SAP integer 8-byte value to store the converted value.
     * @return void
     */
    void duck2rfc(Value &value, RFC_INT8 &rfc_int8) 
    {
        auto duck_value = value.GetValue<int64_t>();
        rfc_int8 = (RFC_INT8)duck_value;
    }

    /**
     * @brief Converts a DuckDB String value to a SAP NUMC value of a given size.
     * 
     * @param value The DuckDB STRING value to convert.
     * @param rfc_num The SAP NUMC value to store the converted value.
     * @param rfc_num_len The length of the SAP NUMC field.
     * @return void
     */
    void duck2rfc(Value &value, RFC_NUM *rfc_num, unsigned int rfc_num_len) 
    {
        auto uc_num = std2uc(value.ToString());
        memcpy((char *)rfc_num, (char *)uc_num.get(), rfc_num_len * 2);
    }

    void duck2rfc(Value &value, RFC_DECF16 &rfc_dec16) 
    {
    }

    void duck2rfc(Value &value, RFC_DECF34 &rfc_dec34) 
    {
    }

    

    /**
     * @brief Converts a SAP RFCTYPE to a std::string.
     * 
     * @param type The SAP RFCTYPE to convert.
     * @param short_name Whether to return the short name of the type (without the RFCTYPE_ prefix).
     * @return std::string The converted std::string.
     */
    std::string rfctype2std(RFCTYPE &type, bool short_name) 
    {
        auto type_uc = RfcGetTypeAsString(type);
        auto type_str = uc2std(type_uc);
        if (short_name) {
            // remove RFCTYPE_ prefix
            return type_str.substr(8);
        }

        return type_str;
    }

    /**
     * @brief Converts a SAP RFCTYPE to a std::string.
     * 
     * @param type The SAP RFCTYPE to convert.
     * @param short_name Whether to return the short name of the type (without the RFCTYPE_ prefix).
     * @return std::string The converted std::string.
     */
    const std::string rfctype2std(const RFCTYPE &type, const bool short_name) 
    {
        return rfctype2std((RFCTYPE &)type, short_name);
    }

    /**
     * @brief Converts a SAP RFC_DIRECTION to a std::string.
     * 
     * @param direction The SAP RFC_DIRECTION to convert.
     * @param short_name Whether to return the short name of the direction (without the RFC_ prefix).
     * @return std::string The converted std::string.
     */
    std::string rfcdirection2std(RFC_DIRECTION &direction, bool short_name) 
    {
        auto direction_uc = RfcGetDirectionAsString(direction);
        auto direction_str = uc2std(direction_uc);
        if (short_name) {
            // remove RFC_ prefix
            return direction_str.substr(4);
        }

        return direction_str;
    }

    /**
     * @brief Converts a SAP RFC_DIRECTION to a std::string.
     * 
     * @param direction The SAP RFC_DIRECTION to convert.
     * @param short_name Whether to return the short name of the direction (without the RFC_ prefix).
     * @return std::string The converted std::string.
     */
    const std::string rfcdirection2std(const RFC_DIRECTION &direction, const bool short_name) 
    {
        return rfcdirection2std((RFC_DIRECTION &)direction, short_name);
    }

    /**
     * @brief Converts a SAP RFC_RC to a std::string.
     */
    std::string rfcrc2std(RFC_RC &rc) 
    {
        auto rc_uc = RfcGetRcAsString(rc);
        return uc2std(rc_uc);
    }

    std::string rfcerrorgroup2std(RFC_ERROR_GROUP &group) 
    {
        switch (group) 
        {
            case OK: return "OK";
            case ABAP_APPLICATION_FAILURE: return "ABAP_APPLICATION_FAILURE - ABAP Exception raised in ABAP function modules";
            case ABAP_RUNTIME_FAILURE: return "ABAP_RUNTIME_FAILURE - ABAP Message raised in ABAP function modules or in ABAP runtime of the backend (e.g Kernel)";
            case LOGON_FAILURE: return "LOGON_FAILURE - Error message raised when logon fails";
            case COMMUNICATION_FAILURE: return "COMMUNICATION_FAILURE - Problems with the network connection (or backend broke down and killed the connection)";
            case EXTERNAL_RUNTIME_FAILURE: return "EXTERNAL_RUNTIME_FAILURE- Problems in the RFC runtime of the external program (i.e \"this\" library)";
            case EXTERNAL_APPLICATION_FAILURE: return "EXTERNAL_APPLICATION_FAILURE - Problems in the external program (e.g in the external server implementation)";
            case EXTERNAL_AUTHORIZATION_FAILURE: return "EXTERNAL_AUTHORIZATION_FAILURE - Problems raised in the authorization check handler provided by the external server implementation";
            case EXTERNAL_AUTHENTICATION_FAILURE: return "EXTERNAL_AUTHENTICATION_FAILURE - Problems raised by the authentication handler (RFC_ON_AUTHENTICATION_CHECK)";
            case CRYPTOLIB_FAILURE: return "CRYPTOLIB_FAILURE - Problems when dealing with functions provided by the cryptolibrary";
            case LOCKING_FAILURE: return "LOCKING_FAILURE - Requesting or freeing critical sections or mutex failed";
            default: return "UNKNOWN";
        }
    }

    std::string rfcerrorinfo2std(RFC_ERROR_INFO &error_info) 
    {
        auto error_code = rfcrc2std(error_info.code);
        auto error_message =  uc2std(error_info.message);

        std::ostringstream oss;

        oss << "--- RFC error info: " << rfcrc2std(error_info.code) << " ---" << std::endl;
        if (strlenU(error_info.message) > 0) {
            oss << "\tMessage: " << error_message << std::endl;
        }
        
        if (strlenU(error_info.key) > 0) {
            oss << "\tKey: " << uc2std(error_info.key) << std::endl;
        }
        
        if (strlenU(error_info.abapMsgClass) > 0) {
            oss << "\tAbapMsgClass: " << uc2std(error_info.abapMsgClass) << std::endl;
        }
        
        if (strlenU(error_info.abapMsgType) > 0) {
            oss << "\tAbapMsgType: " << uc2std(error_info.abapMsgType) << std::endl;
        }
    
        if (strlenU(error_info.abapMsgNumber) > 0) {
            oss << "\tAbapMsgNumber: " << uc2std(error_info.abapMsgNumber) << std::endl;
        }
    
        if (strlenU(error_info.abapMsgV1) > 0) {
            oss << "\tAbapMsgV1: " << uc2std(error_info.abapMsgV1) << std::endl;
        }

        if (strlenU(error_info.abapMsgV2) > 0) {
            oss << "\tAbapMsgV2: " << uc2std(error_info.abapMsgV2) << std::endl;
        }
        
        if (strlenU(error_info.abapMsgV3) > 0) {
            oss << "\tAbapMsgV3: " << uc2std(error_info.abapMsgV3) << std::endl;
        }

        if (strlenU(error_info.abapMsgV4) > 0) {
            oss << "\tAbapMsgV4: " << uc2std(error_info.abapMsgV4) << std::endl;
        }

        oss << "--------------------------" << std::endl;

        return oss.str();
    }

} // namespace duckdb


