#pragma once

#include "duckdb.hpp"
#include "sapnwrfc.h"
#include "sap_function.hpp"

#include "duckdb/common/types/time.hpp"

namespace duckdb 
{
    std::string uc2std(SAP_UC *str, unsigned int len, bool rtrim);
    std::string uc2std(SAP_UC *str, unsigned int len);
    std::string uc2std(SAP_UC *str);
    std::string uc2std(const SAP_UC *str);

    Value uc2duck(SAP_UC *uc_str, unsigned int uc_str_len, bool rtrim);
    Value uc2duck(SAP_UC *uc_str, unsigned int uc_str_len);
    Value uc2duck(SAP_UC *uc_str);
    
    void std2uc(string &std_str, SAP_UC *uc_str, unsigned int uc_str_size);
    unique_ptr<SAP_UC, void (*)(void*)> std2uc(string &std_str);
    unique_ptr<SAP_UC, void (*)(void*)> std2uc(const string &std_str);
    unique_ptr<SAP_UC, void (*)(void*)> duck2uc(Value &value);
    Value dats2duck(std::string &dats_str);
    Value tims2duck(std::string &tims_str);
    Value bcd2duck(std::string &bcd_str, unsigned int length, unsigned int decimals);

    Value rfc2duck(RFC_DATE &rfc_date);
    Value rfc2duck(const RFC_DATE &rfc_date);
    Value rfc2duck(RFC_TIME &rfc_time);
    Value rfc2duck(const RFC_TIME &rfc_time);
    Value rfc2duck(RFC_FLOAT &rfc_float);
    Value rfc2duck(const RFC_FLOAT &rfc_float);
    Value rfc2duck(RFC_INT &rfc_int);
    Value rfc2duck(const RFC_INT &rfc_int);
    Value rfc2duck(RFC_INT1 &rfc_char);
    Value rfc2duck(const RFC_INT1 &rfc_char);
    Value rfc2duck(RFC_INT2 &rfc_short);
    Value rfc2duck(const RFC_INT2 &rfc_short);
    Value rfc2duck(RFC_INT8 &rfc_long);
    Value rfc2duck(const RFC_INT8 &rfc_long);
    Value rfc2duck(RFC_NUM *rfc_num, unsigned int len, bool rtrim);
    Value rfc2duck(const RFC_NUM *rfc_num, const unsigned int len, const bool rtrim);
    Value rfc2duck(RFC_NUM *rfc_num, unsigned int len);
    Value rfc2duck(const RFC_NUM *rfc_num, const unsigned int len);
    Value rfc2duck(RFC_BYTE *rfc_byte, unsigned int len);
    Value rfc2duck(const RFC_BYTE *rfc_byte, const unsigned int len);

    void duck2rfc(Value &duck_value, RFC_DATE &rfc_date);
    void duck2rfc(Value &duck_value, RFC_TIME &rfc_time);
    void duck2rfc(Value &duck_value, RFC_FLOAT &rfc_float);
    void duck2rfc(Value &duck_value, RFC_INT &rfc_int);
    void duck2rfc(Value &duck_value, RFC_INT1 &rfc_char);
    void duck2rfc(Value &duck_value, RFC_INT2 &rfc_short);
    void duck2rfc(Value &duck_value, RFC_INT8 &rfc_long);
    void duck2rfc(Value &duck_value, RFC_NUM *rfc_num, unsigned int len);
    //void duck2rfc(Value &duck_value, RFC_BCD &rfc_bcd);
    void duck2rfc(Value &duck_value, RFC_DECF16 &rfc_dec16);
    void duck2rfc(Value &duck_value, RFC_DECF34 &rfc_dec34);
    
    std::string rfcrc2std(RFC_RC &rc);

    std::string rfctype2std(RFCTYPE &type, bool short_name = false);
    const std::string rfctype2std(const RFCTYPE &type, const bool short_name = false);
    LogicalTypeId rfctype2logicaltype(RFCTYPE rfc_type);

    std::string rfcdirection2std(RFC_DIRECTION &direction, bool short_name = false);
    const std::string rfcdirection2std(const RFC_DIRECTION &direction, const bool short_name = false);

    std::string rfcerrorgroup2std(RFC_ERROR_GROUP &group);
    std::string rfcerrorinfo2std(RFC_ERROR_INFO &error_info);

} // namespace duckdb
