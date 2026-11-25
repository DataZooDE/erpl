#include <iostream>
#include "catch.hpp"
#include "test_helpers.hpp"
#include "duckdb.hpp"
#include "sap_rfc_api.hpp"
#include "sap_type_conversion.hpp"
#include "sap_function.hpp"

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

// RFCTYPE_STRING is character data.  Value::CreateValue<std::string>() builds
// a BLOB, which re-interprets the bytes as a blob literal: non-ASCII throws
// and a literal "\x.." sequence would be silently decoded.
TEST_CASE("Test sapuc2duckval yields VARCHAR, not BLOB", "[sap_type_conversion]") {
    SAP_UC str[] = {'H', 'e', 'l', 'l', 'o', '\0'};
    Value result = uc2duck(str, 5, false);
    REQUIRE(result.type().id() == LogicalTypeId::VARCHAR);
}

TEST_CASE("Test sapuc2duckval with non-ASCII characters", "[sap_type_conversion]") {
    SAP_UC str[] = {'s', 't', 'r', 'a', 0x00DF, 'e', '\0'};
    Value result = uc2duck(str, 6, false);
    REQUIRE(StringValue::Get(result) == "stra\xC3\x9F" "e");
}

TEST_CASE("Test sapuc2duckval keeps a literal backslash-x sequence", "[sap_type_conversion]") {
    SAP_UC str[] = {'\\', 'x', '4', '1', '\0'};
    Value result = uc2duck(str, 4, false);
    REQUIRE(StringValue::Get(result) == "\\x41");
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
    REQUIRE(result.IsNull());
    REQUIRE(result.type().id() == LogicalTypeId::DATE);
}

TEST_CASE("Test dats2duck with all-spaces date returns NULL", "[sap_type_conversion]") {
    std::string dats_str = "        ";
    Value result = dats2duck(dats_str);
    REQUIRE(result.IsNull());
    REQUIRE(result.type().id() == LogicalTypeId::DATE);
}

TEST_CASE("Test dats2duck with empty string returns NULL", "[sap_type_conversion]") {
    std::string dats_str = "";
    Value result = dats2duck(dats_str);
    REQUIRE(result.IsNull());
    REQUIRE(result.type().id() == LogicalTypeId::DATE);
}

TEST_CASE("Test rfc2duck with invalid date returns NULL", "[sap_type_conversion]") {
    RFC_DATE date = {'2', '0', '2', '0', '1', '3', '6', '4'};
    Value result = rfc2duck(date);
    REQUIRE(result.IsNull());
    REQUIRE(result.type().id() == LogicalTypeId::DATE);
}

TEST_CASE("TEST rfc2duck with RFC time", "[sap_type_conversion]") {
    RFC_TIME time = {'1', '0', '3', '0', '0', '0'};
    Value result = rfc2duck(time);
    REQUIRE(TimeValue::Get(result) == Time::FromTime(10, 30, 0, 0));
}

TEST_CASE("TEST rfc2duck with initial time is midnight", "[sap_type_conversion]") {
    // 000000 is midnight, a real time; this test used to pin NULL (issue #255
    // audit, decided).
    RFC_TIME time = {'0', '0', '0', '0', '0', '0'};
    Value result = rfc2duck(time);
    REQUIRE(!result.IsNull());
    REQUIRE(TimeValue::Get(result) == Time::FromTime(0, 0, 0, 0));
}

TEST_CASE("Test tims2duck with all-spaces time returns NULL", "[sap_type_conversion]") {
    std::string tims_str = "      ";
    Value result = tims2duck(tims_str);
    REQUIRE(result.IsNull());
    REQUIRE(result.type().id() == LogicalTypeId::TIME);
}

TEST_CASE("Test tims2duck with empty string returns NULL", "[sap_type_conversion]") {
    std::string tims_str = "";
    Value result = tims2duck(tims_str);
    REQUIRE(result.IsNull());
    REQUIRE(result.type().id() == LogicalTypeId::TIME);
}

TEST_CASE("TEST rfc2duck with invalid time", "[sap_type_conversion]") {
    RFC_TIME time = {'2', '6', '0', '0', '0', '0'};
    Value result = rfc2duck(time);
    REQUIRE(result.IsNull());
    REQUIRE(result.type().id() == LogicalTypeId::TIME);
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
    // INT1 is unsigned (issue #255 audit); this test used to pin the signed
    // TINYINT, under which 255 read back as -1.
    RFC_INT1 rfc_int = 10;
    Value result = rfc2duck(rfc_int);
    REQUIRE(UTinyIntValue::Get(result) == 10);
    RFC_INT1 top = 255;
    REQUIRE(UTinyIntValue::Get(rfc2duck(top)) == 255);
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

TEST_CASE("AdaptValue skips a NULL scalar instead of crashing (issue #72)", "[sap_type_conversion]") {
    // Regression for #72: a NULL HIERARCHY_DUEDATE (DATE) in the BICS
    // I_TH_CHARACTERISTICS table crashed sap_bics_result with
    // "Calling GetValueInternal on a value that is NULL", because AdaptValue's
    // DATE/TIME/numeric branches called duck2rfc()/GetValue<>() without a NULL
    // guard (unlike CHAR/STRING/UTC). A NULL scalar must be left at the SDK's
    // initial value. For a NULL scalar AdaptValue returns before touching the
    // container handle, so a nullptr handle is safe here (no live SAP needed).
    DATA_CONTAINER_HANDLE container = nullptr;
    std::string field = "HIERARCHY_DUEDATE";

    RfcType date_type(RFCTYPE_DATE, nullptr, 8, 0);
    Value null_date(LogicalType::DATE);
    REQUIRE_NOTHROW(date_type.AdaptValue(container, field, null_date));

    RfcType time_type(RFCTYPE_TIME, nullptr, 6, 0);
    Value null_time(LogicalType::TIME);
    REQUIRE_NOTHROW(time_type.AdaptValue(container, field, null_time));

    RfcType int_type(RFCTYPE_INT, nullptr, 4, 0);
    Value null_int(LogicalType::INTEGER);
    REQUIRE_NOTHROW(int_type.AdaptValue(container, field, null_int));
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

TEST_CASE("Test IsKnownDataType with known ABAP types", "[sap_function]") {
    REQUIRE(RfcType::IsKnownDataType("CHAR") == true);
    REQUIRE(RfcType::IsKnownDataType("INT4") == true);
    REQUIRE(RfcType::IsKnownDataType("DATS") == true);
    REQUIRE(RfcType::IsKnownDataType("TIMS") == true);
    REQUIRE(RfcType::IsKnownDataType("FLTP") == true);
    REQUIRE(RfcType::IsKnownDataType("STRING") == true);
    REQUIRE(RfcType::IsKnownDataType("DEC") == true);
    REQUIRE(RfcType::IsKnownDataType("NUMC") == true);
    REQUIRE(RfcType::IsKnownDataType("RAW") == true);
}

TEST_CASE("Test IsKnownDataType with unsupported types", "[sap_function]") {
    REQUIRE(RfcType::IsKnownDataType("NODE") == false);
    REQUIRE(RfcType::IsKnownDataType("STRU") == false);
    REQUIRE(RfcType::IsKnownDataType("") == false);
    REQUIRE(RfcType::IsKnownDataType("UNKNOWN") == false);
}

TEST_CASE("Test IsStringType identifies string ABAP types", "[sap_function]") {
    REQUIRE(RfcType::FromTypeName("SSTR", 256, 0).IsStringType() == true);
    REQUIRE(RfcType::FromTypeName("STRG", 0, 0).IsStringType() == true);
    REQUIRE(RfcType::FromTypeName("RSTR", 0, 0).IsStringType() == true);
    REQUIRE(RfcType::FromTypeName("STRING", 0, 0).IsStringType() == true);
    REQUIRE(RfcType::FromTypeName("LCHR", 100, 0).IsStringType() == true);
    REQUIRE(RfcType::FromTypeName("CHAR", 10, 0).IsStringType() == false);
    REQUIRE(RfcType::FromTypeName("NUMC", 10, 0).IsStringType() == false);
    REQUIRE(RfcType::FromTypeName("INT4", 4, 0).IsStringType() == false);
}
// --- issue #109: RAW columns arrive as hex text and must be decoded -----------

TEST_CASE("hex2blob decodes RFC_READ_TABLE hex into bytes", "[sap_type_conversion]") {
    // "Cr\xC3\xA9dit Agricole SA\0" — the 0xC3 0xA9 pair makes this non-ASCII, so
    // it also pins that the decoded bytes go in raw rather than through the
    // escaped STRING -> BLOB path (issue #107).
    auto result = hex2blob("4372C3A96469742041677269636F6C6520534100");
    REQUIRE(result.type().id() == LogicalTypeId::BLOB);

    const string expected = string("Cr\xC3\xA9""dit Agricole SA", 19) + string(1, '\0');
    REQUIRE(StringValue::Get(result) == expected);
    // The whole point: 20 bytes in, 20 bytes out — not the 40 hex characters.
    REQUIRE(StringValue::Get(result).size() == 20);
}

TEST_CASE("hex2blob accepts lower case and trailing blanks", "[sap_type_conversion]") {
    // The DATA line is fixed width, so cells come back blank padded.
    REQUIRE(StringValue::Get(hex2blob("00ff10  ")) == string("\x00\xFF\x10", 3));
    REQUIRE(StringValue::Get(hex2blob("00FF10")) == string("\x00\xFF\x10", 3));
}

TEST_CASE("hex2blob maps non-hex and empty cells to NULL", "[sap_type_conversion]") {
    // SAP emits the field delimiter for an empty RAW; storing that literal '~'
    // as blob content was the old behaviour.
    REQUIRE(hex2blob("~").IsNull());
    REQUIRE(hex2blob("").IsNull());
    REQUIRE(hex2blob("   ").IsNull());
    REQUIRE(hex2blob("ABC").IsNull());      // odd length
    REQUIRE(hex2blob("ZZ").IsNull());       // not hex
    REQUIRE(hex2blob("00GG").IsNull());
}

TEST_CASE("ConvertCsvValue decodes RAW and RAWSTRING columns", "[sap_function]") {
    for (auto &type_name : {"RAW", "LRAW", "RAWSTRING", "RSTR"}) {
        auto rfc_type = RfcType::FromTypeName(type_name, 16, 0);
        REQUIRE(rfc_type.CreateDuckDbType().id() == LogicalTypeId::BLOB);

        auto converted = rfc_type.ConvertCsvValue(Value("48656C6C6F"));
        REQUIRE(converted.type().id() == LogicalTypeId::BLOB);
        REQUIRE(StringValue::Get(converted) == "Hello");
    }
}

TEST_CASE("ConvertCsvValue yields the column's declared type for every DDIC type",
          "[sap_function]") {
    // Whatever ConvertCsvValue leaves as VARCHAR is implicitly cast when it is
    // written into the output vector — and that cast throws for a blank cell on
    // any non-VARCHAR column, aborting the whole scan. Reading a UTCLONG column
    // failed exactly that way ("invalid timestamp field format").
    //
    // This covers every DDIC name RfcType::FromTypeName maps, so a newly added
    // mapping without a matching ConvertCsvValue branch fails here.
    struct Case { const char *name; unsigned int len; unsigned int dec; };
    const Case cases[] = {
        {"ACCP", 6, 0},   {"CHAR", 10, 0},  {"CLNT", 3, 0},   {"CUKY", 5, 0},
        {"CURR", 15, 2},  {"DATS", 8, 0},   {"DEC", 31, 2},   {"D16D", 16, 2},
        {"D16N", 16, 2},  {"D16R", 16, 2},  {"D16S", 16, 2},  {"DECF16", 16, 2},
        {"D34D", 34, 2},  {"D34N", 34, 2},  {"D34R", 34, 2},  {"D34S", 34, 2},
        {"DECF34", 34, 2},{"FLTP", 16, 0},  {"INT1", 1, 0},   {"INT2", 2, 0},
        {"INT4", 4, 0},   {"INT8", 8, 0},   {"LANG", 1, 0},   {"LCHR", 100, 0},
        {"LRAW", 32, 0},  {"NUMC", 10, 0},  {"PREC", 2, 0},   {"QUAN", 13, 3},
        {"RAW", 16, 0},   {"RAWSTRING", 0, 0}, {"RSTR", 0, 0},{"STRING", 0, 0},
        {"STRG", 0, 0},   {"SSTR", 256, 0}, {"TIMS", 6, 0},   {"UTCL", 27, 7},
        {"UTCLONG", 27, 7}, {"UTCS", 21, 0},{"UTCM", 19, 0},  {"UNIT", 3, 0},
    };

    for (auto &c : cases) {
        auto rfc_type = RfcType::FromTypeName(c.name, c.len, c.dec);
        auto declared = rfc_type.CreateDuckDbType();
        INFO("DDIC type " << c.name);

        // A blank cell is the one input every column type has to tolerate: SAP
        // emits it, and it is what used to abort the scan.
        Value blank;
        REQUIRE_NOTHROW(blank = rfc_type.ConvertCsvValue(Value("   ")));
        REQUIRE((blank.IsNull() || blank.type() == declared));
    }
}

TEST_CASE("ConvertCsvValue parses SAP UTC timestamps rather than casting them",
          "[sap_function]") {
    // SAP's compact form is not an ISO timestamp, so the implicit VARCHAR ->
    // TIMESTAMP cast could not have parsed it even when the cell was populated.
    auto utc = RfcType::FromTypeName("UTCL", 27, 7);
    REQUIRE(utc.CreateDuckDbType().id() == LogicalTypeId::TIMESTAMP);

    auto converted = utc.ConvertCsvValue(Value("20240115143000,0000000"));
    REQUIRE(converted.type().id() == LogicalTypeId::TIMESTAMP);
    REQUIRE(converted.ToString() == "2024-01-15 14:30:00");

    // The ABAP initial value is NULL, not year 0.
    REQUIRE(utc.ConvertCsvValue(Value("00000000000000")).IsNull());
}

TEST_CASE("ConvertCsvValue parses integer and float columns", "[sap_function]") {
    auto i4 = RfcType::FromTypeName("INT4", 4, 0);
    REQUIRE(i4.ConvertCsvValue(Value("42")).type() == i4.CreateDuckDbType());
    REQUIRE(i4.ConvertCsvValue(Value("42")).GetValue<int64_t>() == 42);
    REQUIRE(i4.ConvertCsvValue(Value("  ")).IsNull());

    auto f = RfcType::FromTypeName("FLTP", 16, 0);
    REQUIRE(f.ConvertCsvValue(Value("1.5")).type() == f.CreateDuckDbType());
    REQUIRE(f.ConvertCsvValue(Value("1.5")).GetValue<double>() == 1.5);
}

TEST_CASE("ConvertCsvValue decimal precision matches the declared column type",
          "[sap_function]") {
    // bcd2duck used to be called without CreateDuckDbType's min(..., 38) cap, so
    // the produced DECIMAL could disagree with the column it is written into.
    for (auto &type_name : {"CURR", "DEC", "QUAN"}) {
        auto rfc_type = RfcType::FromTypeName(type_name, 31, 2);
        auto converted = rfc_type.ConvertCsvValue(Value("123.45"));
        REQUIRE(converted.type() == rfc_type.CreateDuckDbType());
    }

    auto d16 = RfcType::FromTypeName("D16D", 16, 2);
    REQUIRE(d16.ConvertCsvValue(Value("123.45")).type() == d16.CreateDuckDbType());

    auto d34 = RfcType::FromTypeName("D34D", 34, 2);
    REQUIRE(d34.ConvertCsvValue(Value("123.45")).type() == d34.CreateDuckDbType());
}

// uc2std(..., rtrim=true) trims on the UTF-16 units before converting. The
// reference below is the algorithm it replaced, kept verbatim: convert every
// unit, then trim '\0' and isspace() bytes off the UTF-8. Both run against the
// loaded backend's RfcSAPUCToUTF8, so the comparison covers either library.
static std::string ReferenceUc2stdRtrim(SAP_UC *uc_str, unsigned int uc_str_len) {
    if (uc_str == NULL || uc_str_len == 0) {
        return std::string();
    }
    RFC_ERROR_INFO error_info;
    unsigned int utf8_size = uc_str_len * 5 + 1;
    unsigned int result_len = 0;
    std::vector<char> utf8(utf8_size);
    auto rc = RfcSAPUCToUTF8(uc_str, uc_str_len, (RFC_BYTE *)utf8.data(), &utf8_size, &result_len, &error_info);
    if (rc != RFC_OK || result_len <= 0) {
        return std::string();
    }
    int i = result_len - 1;
    while (i >= 0 && (utf8[i] == '\0' || isspace(utf8[i]))) {
        i--;
    }
    return std::string(utf8.data(), i + 1);
}

static void RequireSameAsReference(std::vector<SAP_UC> units, const std::string &label) {
    auto expected = ReferenceUc2stdRtrim(units.data(), (unsigned int)units.size());
    auto actual = uc2std(units.data(), (unsigned int)units.size(), true);
    INFO(label);
    REQUIRE(actual == expected);
}

static std::vector<SAP_UC> Units(const std::u16string &text, size_t pad_to = 0, SAP_UC pad = ' ') {
    std::vector<SAP_UC> units(text.begin(), text.end());
    while (units.size() < pad_to) {
        units.push_back(pad);
    }
    return units;
}

TEST_CASE("uc2std rtrim trims on UTF-16 exactly as it did on UTF-8", "[sap_type_conversion]") {
    RequireSameAsReference(Units(u"", 512), "all blanks");
    RequireSameAsReference(Units(u"PROG000001", 512), "CHAR(512) WA cell");
    RequireSameAsReference(Units(u"NOBLANKS"), "no blanks");
    RequireSameAsReference(Units(u"AB  CD", 40), "inner blanks stay");
    RequireSameAsReference(Units(u" LEAD", 10), "leading blanks stay");
    RequireSameAsReference(Units(u"TAB\t\t"), "trailing tabs");
    RequireSameAsReference(Units(u"LINE\r\n\v\f \t"), "every isspace character");
    RequireSameAsReference(Units(u"NUL", 8, 0), "trailing NUL units");
    RequireSameAsReference(Units(u"MIX \t", 9, 0), "blanks, a tab and NULs");
    RequireSameAsReference(Units(u"straße €", 30), "non-ASCII then blanks");
    RequireSameAsReference(Units(u" NBSP ", 12), "U+00A0 is not trimmed");
    RequireSameAsReference(Units(u"　IDEO　"), "U+3000 is not trimmed");
    RequireSameAsReference(Units(u"G \U0001D11E", 20), "a surrogate pair then blanks");
    RequireSameAsReference(Units(u"\U0001D11E"), "a surrogate pair at the end");
    // Invalid UTF-16: an unpaired high surrogate right before the blanks is
    // left to the convert-then-trim path, so it converts as it always did.
    std::vector<SAP_UC> lone = {'A', 0xD834, ' ', ' '};
    RequireSameAsReference(lone, "an unpaired high surrogate before blanks");
    std::vector<SAP_UC> lone_low = {'A', 0xDD1E, ' '};
    RequireSameAsReference(lone_low, "an unpaired low surrogate before a blank");
    // Seeded random cells over the characters that matter.
    const SAP_UC alphabet[] = {'A', 'z', '0', ' ', '\t', '\n', '\r', 0x0B, 0x0C, 0, 0x00A0, 0x00DF, 0x20AC, 0x3000, 0xD834, 0xDD1E};
    uint32_t seed = 0x5EED;
    for (int n = 0; n < 2000; n++) {
        seed = seed * 1664525u + 1013904223u;
        std::vector<SAP_UC> units(1 + (seed >> 24) % 24);
        for (auto &u : units) {
            seed = seed * 1664525u + 1013904223u;
            u = alphabet[(seed >> 20) % (sizeof(alphabet) / sizeof(alphabet[0]))];
        }
        RequireSameAsReference(units, "random cell " + std::to_string(n));
    }
}

TEST_CASE("UcRtrimLength measures on UTF-16 units", "[sap_type_conversion]") {
    auto blanks = Units(u"ABC", 512);
    REQUIRE(UcRtrimLength(blanks.data(), (unsigned int)blanks.size()) == 3);
    auto none = Units(u"", 512);
    REQUIRE(UcRtrimLength(none.data(), (unsigned int)none.size()) == 0);
    std::vector<SAP_UC> lone = {'A', 0xD834, ' '};
    REQUIRE(UcRtrimLength(lone.data(), 3) == 3);
}

TEST_CASE("A packed field holds every digit its DDIC length declares (issue #255)",
          "[sap_function][issue255]") {
    // DDIC LENG of DEC/CURR/QUAN counts digits; the packed storage is LENG/2 + 1
    // bytes (digits plus a sign nibble). The read path used (LENG + 1) / 2,
    // which is one byte short for every even LENG: DEC(4,1) became
    // DECIMAL(3,1), so a value such as 100.0 or 999.9 aborted the whole scan
    // with "Could not convert string '100.0' to DECIMAL(3,1)".
    struct Case { const char *name; unsigned int leng; unsigned int dec; };
    const Case cases[] = {
        {"DEC", 1, 0},  {"DEC", 2, 0},   {"DEC", 2, 1},   {"DEC", 2, 2},
        {"DEC", 3, 1},  {"DEC", 4, 1},   {"DEC", 4, 2},   {"DEC", 5, 5},
        {"CURR", 13, 2},{"CURR", 15, 2}, {"CURR", 16, 2}, {"QUAN", 13, 3},
        {"QUAN", 14, 3},{"DEC", 30, 14}, {"DEC", 31, 14}, {"DEC", 31, 0},
    };

    for (auto &c : cases) {
        INFO("DDIC " << c.name << "(" << c.leng << "," << c.dec << ")");
        auto rfc_type = RfcType::FromTypeName(c.name, c.leng, c.dec);
        auto declared = rfc_type.CreateDuckDbType();

        // The storage size SAP itself uses, and therefore the digits a value
        // can physically carry: never fewer than the DDIC length.
        const unsigned int bytes = c.leng / 2 + 1;
        REQUIRE(rfc_type.GetLength() == bytes);
        REQUIRE(DecimalType::GetWidth(declared) == 2 * bytes - 1);
        REQUIRE(DecimalType::GetWidth(declared) >= c.leng);
        REQUIRE(DecimalType::GetWidth(declared) <= 38);
        REQUIRE(DecimalType::GetScale(declared) == c.dec);

        // The largest value the DDIC field can hold, positive and in SAP's
        // trailing-minus form, must convert without loss.
        const std::string integer_part(c.leng - c.dec, '9');
        const std::string fraction_part(c.dec, '9');
        std::string text = (integer_part.empty() ? "0" : integer_part);
        if (c.dec > 0) {
            text += "." + fraction_part;
        }
        for (const std::string &cell : {text, text + "-"}) {
            INFO("cell '" << cell << "'");
            Value converted;
            REQUIRE_NOTHROW(converted = rfc_type.ConvertCsvValue(Value(cell)));
            REQUIRE(converted.type() == declared);
            // Compared as values: DuckDB prints DECIMAL(5,5) as ".99999".
            const std::string expect = (cell.back() == '-' ? "-" : "") + text;
            REQUIRE(converted == Value(expect).DefaultCastAs(declared));
        }
    }
}

TEST_CASE("INT1 is unsigned: 0..255 survives every path (issue #255 audit)",
          "[sap_function][issue255]") {
    // ABAP INT1 holds 0..255. Mapped to DuckDB's signed TINYINT, 255 was a
    // cast error through sap_read_table and came back as -1 through invoke,
    // silently, and a caller could not send 128..255 at all. Measured on a4h
    // with ZERPL_TYPES_BV / Z_ERPL_TYPES_BV.
    auto rfc_type = RfcType::FromTypeName("INT1", 1, 0);
    REQUIRE(rfc_type.CreateDuckDbType().id() == LogicalTypeId::UTINYINT);
    for (int v : {0, 1, 127, 128, 255}) {
        INFO("INT1 " << v);
        auto read = rfc_type.ConvertCsvValue(Value(std::to_string(v)));
        REQUIRE(read == Value::UTINYINT((uint8_t)v));
        RFC_INT1 raw = (RFC_INT1)v;
        REQUIRE(rfc2duck(raw) == Value::UTINYINT((uint8_t)v));
        Value out = Value::UTINYINT((uint8_t)v);
        RFC_INT1 sent = 0;
        duck2rfc(out, sent);
        REQUIRE((int)sent == v);
    }
    REQUIRE(rfctype2logicaltype(RFCTYPE_INT1) == LogicalTypeId::UTINYINT);
}

TEST_CASE("UTCLONG text in both SAP spellings parses, nothing is lost silently (issue #255 audit)",
          "[sap_type_conversion][issue255]") {
    // SAP's library renders a UTCLONG as YYYY-MM-DDTHH:MM:SS,fffffff (and its
    // initial value as 0000-00-00T00:00:00,0000000); RFC_READ_TABLE and the
    // older shim use YYYY-MM-DD HH:MM:SS.fffffff. The parser expected the
    // compact YYYYMMDDHHMMSS,fffffff only and turned everything else into
    // NULL without an error -- every UTCLONG read as NULL, on both paths.
    auto ts = [](const char *s) { return Value(s).DefaultCastAs(LogicalType::TIMESTAMP); };
    struct Case { const char *in; const char *want; };
    const Case cases[] = {
        {"9999-12-31T23:59:59,9999999", "9999-12-31 23:59:59.999999"},
        {"9999-12-31 23:59:59.9999999", "9999-12-31 23:59:59.999999"},
        {"0001-01-01T00:00:00,0000001", "0001-01-01 00:00:00"},
        {"2026-11-25T12:00:00,1234567", "2026-11-25 12:00:00.123456"},
        {"20261125120000,1234567",      "2026-11-25 12:00:00.123456"},
    };
    for (auto &c : cases) {
        INFO("UTCLONG '" << c.in << "'");
        std::string s = c.in;
        REQUIRE(sap_utc2timestamp(s) == ts(c.want));
    }
    for (const char *initial : {"", "0000-00-00T00:00:00,0000000", "00000000000000,0000000"}) {
        std::string s = initial;
        REQUIRE(sap_utc2timestamp(s).IsNull());
    }
    std::string garbage = "not a timestamp";
    REQUIRE_THROWS(sap_utc2timestamp(garbage));

    // Writing: SAP's library accepts YYYY-MM-DDTHH:MM:SS,fffffff and refuses
    // the compact form with RFC_CONVERSION_FAILURE (measured), which is what
    // erpl used to send.
    REQUIRE(timestamp2sap_utc(ts("2026-11-25 12:00:00.123456")) == "2026-11-25T12:00:00,1234560");
    REQUIRE(timestamp2sap_utc(ts("2026-11-25 12:00:00")) == "2026-11-25T12:00:00,0000000");
}

TEST_CASE("UTCLONG: malformed text and unrepresentable timestamps are errors (codex review)",
          "[sap_type_conversion][issue255]") {
    // Initial-value detection ran before validation, so text with no non-zero
    // digit read as NULL; and infinity was sliced as if it were a date.
    for (const char *bad : {"garbage0", "0000-00-00T00:00:00,0000000junk", "2026-11-25X12:00:00"}) {
        INFO("UTCLONG '" << bad << "'");
        std::string s = bad;
        REQUIRE_THROWS(sap_utc2timestamp(s));
    }
    for (const char *ts : {"infinity", "-infinity"}) {
        INFO("TIMESTAMP " << ts);
        REQUIRE_THROWS(timestamp2sap_utc(Value(ts).DefaultCastAs(LogicalType::TIMESTAMP)));
    }
}

TEST_CASE("DECFLOAT16/34 are DOUBLE on every path (issue #255 audit, decision)",
          "[sap_function][issue255]") {
    // A decimal float has no fixed scale: as DECIMAL(16|34, 0) its fraction was
    // cut off -- 1.234567890123456 read as 1 -- on both read and invoke.
    for (const char *name : {"D16D", "D16N", "DECF16", "D34D", "D34N", "DECF34"}) {
        INFO("DDIC " << name);
        auto t = RfcType::FromTypeName(name, 16, 0);
        REQUIRE(t.CreateDuckDbType().id() == LogicalTypeId::DOUBLE);
        REQUIRE(t.ConvertCsvValue(Value("1.23456789012346")) == Value::DOUBLE(1.23456789012346));
        REQUIRE(t.ConvertCsvValue(Value("-1.000000000E+16")) == Value::DOUBLE(-1e16));
        REQUIRE(t.ConvertCsvValue(Value("   ")).IsNull());
    }
    REQUIRE(rfctype2logicaltype(RFCTYPE_DECF16) == LogicalTypeId::DOUBLE);
    REQUIRE(rfctype2logicaltype(RFCTYPE_DECF34) == LogicalTypeId::DOUBLE);
}

TEST_CASE("An initial TIMS is midnight, not NULL (issue #255 audit, decision)",
          "[sap_type_conversion][issue255]") {
    // 000000 is a valid time; reading it as NULL made midnight indistinguishable
    // from no value. Blank still reads as NULL, and DATS initial stays NULL.
    std::string zero = "000000";
    REQUIRE(tims2duck(zero) == Value::TIME(dtime_t(0)));
    std::string blank = "      ";
    REQUIRE(tims2duck(blank).IsNull());
    REQUIRE(RfcType::FromTypeName("TIMS", 6, 0).ConvertCsvValue(Value("000000")) == Value::TIME(dtime_t(0)));
}
