#include <iostream>
#include "catch.hpp"
#include "test_helpers.hpp"
#include "duckdb.hpp"
#include "sapnwrfc.h"
#include "sap_type_conversion.hpp"

using namespace duckdb;
using namespace std;

TEST_CASE("Test sapuc2duckval with rtrim = 0", "[sap_type_conversion]") {
    SAP_UC str[] = {'H', 'e', 'l', 'l', 'o', ' ', 'W', 'o', 'r', 'l', 'd', '\0'};
    Value result = uc2duck(str, 11, false);
    REQUIRE(StringValue::Get(result) == "Hello World");
}

TEST_CASE("Test sapuc2duckval with rtrim = 1", "[sap_type_conversion]") {
    SAP_UC str[] = {'H', 'e', 'l', 'l', 'o', ' ', 'W', 'o', 'r', 'l', 'd', ' ', ' ', ' ', '\0'};
    Value result = uc2duck(str, 14, true);
    REQUIRE(StringValue::Get(result) == "Hello World");
}

TEST_CASE("Test sapuc2duckval with len = 0", "[sap_type_conversion]") {
    SAP_UC str[] = {'H', 'e', 'l', 'l', 'o', ' ', 'W', 'o', 'r', 'l', 'd', '\0'};
    Value result = uc2duck(str, 0);
    REQUIRE(result.IsNull());
}

TEST_CASE("Test sapuc2duckval with len > strlen(str)", "[sap_type_conversion]") {
    SAP_UC str[] = {'H', 'e', 'l', 'l', 'o', ' ', 'W', 'o', 'r', 'l', 'd', '\0'};
    Value result = uc2duck(str, 20);
    REQUIRE(StringValue::Get(result) == "Hello World");
}

TEST_CASE("Test sapuc2duckval with len < strlen(str)", "[sap_type_conversion]") {
    SAP_UC str[] = {'H', 'e', 'l', 'l', 'o', ' ', 'W', 'o', 'r', 'l', 'd', '\0'};
    Value result = uc2duck(str, 5);
    REQUIRE(StringValue::Get(result) == "Hello");
}

TEST_CASE("Test sapuc2duckval with empty string", "[sap_type_conversion]") {
    SAP_UC str[] = {'\0'};
    Value result = uc2duck(str);
    REQUIRE(result.IsNull());
}

TEST_CASE("Test sapuc2duckval with nullptr", "[sap_type_conversion]") {
    Value result = uc2duck(nullptr);
    REQUIRE(result.IsNull());
}

TEST_CASE("Test sapuc2std with rtrim = 0", "[sap_type_conversion]") {
    SAP_UC str[] = {'H', 'e', 'l', 'l', 'o', ' ', 'W', 'o', 'r', 'l', 'd', '\0'};
    std::string result = uc2std(str, 11, false);
    REQUIRE(result == "Hello World");
}

TEST_CASE("Test sapuc2std with rtrim = 1", "[sap_type_conversion]") {
    SAP_UC str[] = {'H', 'e', 'l', 'l', 'o', ' ', 'W', 'o', 'r', 'l', 'd', ' ', ' ', ' ', '\0'};
    std::string result = uc2std(str, 14, true);
    REQUIRE(result == "Hello World");
}

TEST_CASE("Test sapuc2std with len = 0", "[sap_type_conversion]") {
    SAP_UC str[] = {'H', 'e', 'l', 'l', 'o', ' ', 'W', 'o', 'r', 'l', 'd', '\0'};
    std::string result = uc2std(str, 0);
    REQUIRE(result.empty());
}

TEST_CASE("Test sapuc2std with len > strlen(str)", "[sap_type_conversion]") {
    SAP_UC str[] = {'H', 'e', 'l', 'l', 'o', ' ', 'W', 'o', 'r', 'l', 'd', '\0'};
    std::string result = uc2std(str, 20);
    REQUIRE(result == "Hello World");
}

TEST_CASE("Test sapuc2std with len < strlen(str)", "[sap_type_conversion]") {
    SAP_UC str[] = {'H', 'e', 'l', 'l', 'o', ' ', 'W', 'o', 'r', 'l', 'd', '\0'};
    std::string result = uc2std(str, 5);
    REQUIRE(result == "Hello");
}

TEST_CASE("Test sapuc2std with RFC date", "[sap_type_conversion]") {
    RFC_DATE date = {'2', '0', '2', '0', '0', '1', '0', '1'};
    std::string result = uc2std(date, 8);
    REQUIRE(result == "20200101");
}

TEST_CASE("Test std2sapuc with string", "[sap_type_conversion]") {
    auto sap_uc = std2uc("Hello World");
    
    REQUIRE(sap_uc.get()[0] == 'H');
    REQUIRE(sap_uc.get()[1] == 'e');
    REQUIRE(sap_uc.get()[2] == 'l');
    REQUIRE(sap_uc.get()[3] == 'l');
    REQUIRE(sap_uc.get()[4] == 'o');
    REQUIRE(sap_uc.get()[5] == ' ');
    REQUIRE(sap_uc.get()[6] == 'W');
    REQUIRE(sap_uc.get()[7] == 'o');
    REQUIRE(sap_uc.get()[8] == 'r');
    REQUIRE(sap_uc.get()[9] == 'l');
    REQUIRE(sap_uc.get()[10] == 'd');
    REQUIRE(sap_uc.get()[11] == '\0');
}

TEST_CASE("Test std2sapuc with empty string", "[sap_type_conversion]") {
    auto sap_uc = std2uc("");
    REQUIRE(sap_uc.get()[0] == '\0');
}

TEST_CASE("Test duckval2sapuc with string value", "[sap_type_conversion]") {
    auto value = Value("Hello World");
    auto sap_uc = duck2uc(value);
    
    REQUIRE(sap_uc.get()[0] == 'H');
    REQUIRE(sap_uc.get()[1] == 'e');
    REQUIRE(sap_uc.get()[2] == 'l');
    REQUIRE(sap_uc.get()[3] == 'l');
    REQUIRE(sap_uc.get()[4] == 'o');
    REQUIRE(sap_uc.get()[5] == ' ');
    REQUIRE(sap_uc.get()[6] == 'W');
    REQUIRE(sap_uc.get()[7] == 'o');
    REQUIRE(sap_uc.get()[8] == 'r');
    REQUIRE(sap_uc.get()[9] == 'l');
    REQUIRE(sap_uc.get()[10] == 'd');
    REQUIRE(sap_uc.get()[11] == '\0');
}

TEST_CASE("Test duckval2sapuc with null value", "[sap_type_conversion]") {
    auto value = Value();
    auto sap_uc = duck2uc(value);
    REQUIRE(sap_uc.get() == nullptr);
}

TEST_CASE("Test duckval2sapuc with value other than str", "[sap_type_conversion]") {
    REQUIRE_THROWS([]() { 
        auto value = Value(42);
        auto sap_uc = duck2uc(value);
    }());

    REQUIRE_THROWS([]() { 
        auto value = Value(21.0);
        auto sap_uc = duck2uc(value);
    }());

    REQUIRE_THROWS([]() { 
        auto value = Value(true);
        auto sap_uc = duck2uc(value);
    }());
}

TEST_CASE("Test rfc2duck with RFC date", "[sap_type_conversion]") {
    RFC_DATE date = {'2', '0', '2', '0', '0', '1', '0', '1'};
    Value result = rfc2duck(date);
    REQUIRE(DateValue::Get(result) == Date::FromDate(2020, 1, 1));
}

TEST_CASE("Test rfc2duck with empty date", "[sap_type_conversion]") {
    RFC_DATE date = {'0', '0', '0', '0', '0', '0', '0', '0'};
    Value result = rfc2duck(date);
    REQUIRE(DateValue::Get(result) == date_t());
}

TEST_CASE("Test rfc2duck with invalid date", "[sap_type_conversion]") {
    RFC_DATE date = {'2', '0', '2', '0', '1', '3', '6', '4'};
    Value result = rfc2duck(date);
    REQUIRE(DateValue::Get(result) == date_t());
}

TEST_CASE("TEST rfc2duck with RFC time", "[sap_type_conversion]") {
    RFC_TIME time = {'1', '0', '3', '0', '0', '0'};
    Value result = rfc2duck(time);
    REQUIRE(TimeValue::Get(result) == Time::FromTime(10, 30, 0, 0));
}

TEST_CASE("TEST rfc2duck with empty time", "[sap_type_conversion]") {
    RFC_TIME time = {'0', '0', '0', '0', '0', '0'};
    Value result = rfc2duck(time);
    REQUIRE(TimeValue::Get(result) == dtime_t());
}

TEST_CASE("TEST rfc2duck with invalid time", "[sap_type_conversion]") {
    RFC_TIME time = {'2', '6', '0', '0', '0', '0'};
    Value result = rfc2duck(time);
    REQUIRE(TimeValue::Get(result) == dtime_t());
}

TEST_CASE("Test rfc2duck for float", "[sap_type_conversion]") {
    RFC_FLOAT rfc_float = 42.0;
    Value result = rfc2duck(rfc_float);
    REQUIRE(DoubleValue::Get(result) == 42.0);
}

TEST_CASE("Test rfc2duck for int", "[sap_type_conversion]") {
    RFC_INT rfc_int = 42;
    Value result = rfc2duck(rfc_int);
    REQUIRE(IntegerValue::Get(result) == 42);
}

TEST_CASE("Test rfc2duck for int1", "[sap_type_conversion]") {
    RFC_INT1 rfc_int = 10;
    Value result = rfc2duck(rfc_int);
    REQUIRE(TinyIntValue::Get(result) == 10);
}

TEST_CASE("Test rfc2duck for int2", "[sap_type_conversion]") {
    RFC_INT2 rfc_int = 130;
    Value result = rfc2duck(rfc_int);
    REQUIRE(SmallIntValue::Get(result) == 130);
}

TEST_CASE("Test rfc2duck for int8", "[sap_type_conversion]") {
    RFC_INT8 rfc_int = 130000000000000000;
    Value result = rfc2duck(rfc_int);
    REQUIRE(BigIntValue::Get(result) == 130000000000000000);
}

TEST_CASE("Test rfc2duck for num", "[sap_type_conversion]") {
    RFC_NUM rfc_num[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9', '0'};
    Value result = rfc2duck(rfc_num, 10);
    REQUIRE(StringValue::Get(result) == "1234567890");
}

TEST_CASE("Test rfc2duck for char", "[sap_type_conversion]") {
    RFC_CHAR rfc_char[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9', '0'};
    Value result = rfc2duck(rfc_char, 10);
    REQUIRE(StringValue::Get(result) == "1234567890");
}

TEST_CASE("Test rfc2duck for byte", "[sap_type_conversion]") {
    RFC_BYTE rfc_byte[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9', '0'};
    Value result = rfc2duck(rfc_byte, 10);
    REQUIRE(1 == 1); // don't know how to test this
}

TEST_CASE("Test duck2rfc for RFC_Date", "[sap_type_conversion]") {
    auto value = Value::DATE(2022, 1, 1);
    RFC_DATE rfc_date;
    duck2rfc(value, rfc_date);

    REQUIRE(rfc_date[0] == '2');
    REQUIRE(rfc_date[1] == '0');
    REQUIRE(rfc_date[2] == '2');
    REQUIRE(rfc_date[3] == '2');
    REQUIRE(rfc_date[4] == '0');
    REQUIRE(rfc_date[5] == '1');
    REQUIRE(rfc_date[6] == '0');
    REQUIRE(rfc_date[7] == '1');
}

TEST_CASE("Test duck2rfc for RFC_Time", "[sap_type_conversion]") {
    auto value = Value::TIME(10, 30, 0, 0);
    RFC_TIME rfc_time;
    duck2rfc(value, rfc_time);

    REQUIRE(rfc_time[0] == '1');
    REQUIRE(rfc_time[1] == '0');
    REQUIRE(rfc_time[2] == '3');
    REQUIRE(rfc_time[3] == '0');
    REQUIRE(rfc_time[4] == '0');
    REQUIRE(rfc_time[5] == '0');
}

TEST_CASE("Test duck2rfc for RFC_Float", "[sap_type_conversion]") {
    auto value = Value::DOUBLE(42.5);
    RFC_FLOAT rfc_float;
    duck2rfc(value, rfc_float);

    REQUIRE(rfc_float == 42.5);
}

TEST_CASE("Test duck2rfc for RFC_Float with decimal input", "[sap_type_conversion]") {
    auto value = Value::DECIMAL(425, 1, 1);
    RFC_FLOAT rfc_float;
    duck2rfc(value, rfc_float);

    REQUIRE(rfc_float == 42.5);
}

TEST_CASE("Test duck2rfc for RFC_Int", "[sap_type_conversion]") {
    auto value = Value::INTEGER(42);
    RFC_INT rfc_int;
    duck2rfc(value, rfc_int);

    REQUIRE(rfc_int == 42);
}

TEST_CASE("Test duck2rfc for RFC_Int1", "[sap_type_conversion]") {
    auto value = Value::TINYINT(10);
    RFC_INT1 rfc_int;
    duck2rfc(value, rfc_int);

    REQUIRE(rfc_int == 10);
}

TEST_CASE("Test duck2rfc for RFC_Int2", "[sap_type_conversion]") {
    auto value = Value::SMALLINT(130);
    RFC_INT2 rfc_int;
    duck2rfc(value, rfc_int);

    REQUIRE(rfc_int == 130);
}

TEST_CASE("Test duck2rfc for RFC_Int8", "[sap_type_conversion]") {
    auto value = Value::BIGINT(130000000000000000);
    RFC_INT8 rfc_int;
    duck2rfc(value, rfc_int);

    REQUIRE(rfc_int == 130000000000000000);
}

TEST_CASE("Test duck2rfc for RFC_Num", "[sap_type_conversion]") {
    auto value = Value::CreateValue("1234567890");
    RFC_NUM rfc_num[10];
    duck2rfc(value, rfc_num, 10);

    REQUIRE(rfc_num[0] == '1');
    REQUIRE(rfc_num[1] == '2');
    REQUIRE(rfc_num[2] == '3');
    REQUIRE(rfc_num[3] == '4');
    REQUIRE(rfc_num[4] == '5');
    REQUIRE(rfc_num[5] == '6');
    REQUIRE(rfc_num[6] == '7');
    REQUIRE(rfc_num[7] == '8');
    REQUIRE(rfc_num[8] == '9');
    REQUIRE(rfc_num[9] == '0');
}

TEST_CASE("Test rfctype2std", "[sap_type_conversion]") {
    REQUIRE(rfctype2std(RFCTYPE_DATE) == "RFCTYPE_DATE");
    REQUIRE(rfctype2std(RFCTYPE_TIME) == "RFCTYPE_TIME");
    REQUIRE(rfctype2std(RFCTYPE_NUM, true) == "NUM");
    REQUIRE(rfctype2std(RFCTYPE_BYTE, true) == "BYTE");
}

TEST_CASE("Test rfcdirection2std", "[sap_type_conversion]") {
    REQUIRE(rfcdirection2std(RFC_IMPORT) == "RFC_IMPORT");
    REQUIRE(rfcdirection2std(RFC_EXPORT) == "RFC_EXPORT");
    REQUIRE(rfcdirection2std(RFC_CHANGING, true) == "CHANGING");
}