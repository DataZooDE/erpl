#include "catch.hpp"
#include "test_helpers.hpp"
#include "duckdb.hpp"
#include "duckdb_json_helper.hpp"

using namespace duckdb;
using namespace std;

std::string remove_whitespace(std::string &json) {
    json.erase(std::remove_if(json.begin(), json.end(), [](char c) { return ::isspace(c) || c == '\n'; }), json.end());
    return json;
}

TEST_CASE("Test serialize primitive types", "[duckdb_json_helper]") {
    auto str_val = Value::CreateValue("TEST");
    auto int_val = Value::CreateValue(123);
    auto double_val = Value::CreateValue(123.456);
    auto bool_val = Value::CreateValue(true);
    auto null_val = Value();
    auto date_val = Value::CreateValue(Date::FromString("2020-01-01"));
    auto dt_val = Value::CreateValue(dtime_t(123456));
    auto ts_val = Value::CreateValue(Timestamp::FromString("2020-01-01 00:00:00"));
    
    auto json = ErpelSerializer::Serialize(str_val);    
    REQUIRE(json == "\"TEST\"");
    json = ErpelSerializer::Serialize(int_val);
    REQUIRE(json == "123");
    json = ErpelSerializer::Serialize(double_val);
    REQUIRE(json == "123.456");
    json = ErpelSerializer::Serialize(bool_val);
    REQUIRE(json == "true");
    json = ErpelSerializer::Serialize(null_val);
    REQUIRE(json == "null");
    json = ErpelSerializer::Serialize(date_val);
    REQUIRE(json == "\"2020-01-01\"");
    json = ErpelSerializer::Serialize(dt_val);
    REQUIRE(json == "123456");
    json = ErpelSerializer::Serialize(ts_val);
    REQUIRE(json == "\"2020-01-01 00:00:00\"");
}

TEST_CASE("Test serialize primitive types with type information", "[duckdb_json_helper]") {
    auto str_val = Value::CreateValue("TEST");
    auto int_val = Value::CreateValue(123);
    auto double_val = Value::CreateValue(123.456);
    auto bool_val = Value::CreateValue(true);
    auto null_val = Value();
    auto date_val = Value::CreateValue(Date::FromString("2020-01-01"));

    auto json = ErpelSerializer::Serialize(str_val, true);
    REQUIRE(remove_whitespace(json) == "{\"$type\":\"VARCHAR\",\"$value\":\"TEST\"}");
    json = ErpelSerializer::Serialize(int_val, true);
    REQUIRE(remove_whitespace(json) == "{\"$type\":\"INTEGER\",\"$value\":123}");
    json = ErpelSerializer::Serialize(double_val, true);
    REQUIRE(remove_whitespace(json) == "{\"$type\":\"DOUBLE\",\"$value\":123.456}");
    json = ErpelSerializer::Serialize(bool_val, true);
    REQUIRE(remove_whitespace(json) == "{\"$type\":\"BOOLEAN\",\"$value\":true}");
    json = ErpelSerializer::Serialize(null_val, true);
    REQUIRE(remove_whitespace(json) == "{\"$type\":\"NULL\",\"$value\":null}");
    json = ErpelSerializer::Serialize(date_val, true);
    REQUIRE(remove_whitespace(json) == "{\"$type\":\"DATE\",\"$value\":\"2020-01-01\"}");
}

TEST_CASE("Test serialize blob type", "[duckdb_json_helper]") {
    auto blob_val = Value::BLOB("TEST");
    auto json = ErpelSerializer::Serialize(blob_val);
    // Base64 encoded "TEST" is "VEVTVA=="
    REQUIRE(json == "\"VEVTVA==\"");
}

TEST_CASE("Test serialize blog type with type information", "[duckdb_json_helper]") {
    auto blob_val = Value::BLOB("TEST");
    auto json = ErpelSerializer::Serialize(blob_val, true);
    // Base64 encoded "TEST" is "VEVTVA=="
    REQUIRE(remove_whitespace(json) == "{\"$type\":\"BLOB\",\"$value\":\"VEVTVA==\"}");
}

TEST_CASE("Test serialize struct type", "[duckdb_json_helper]") {
    auto struct_val = Value::STRUCT({{"a", Value::CreateValue(123)}, 
                                     {"b", Value::CreateValue("TEST")},
                                     {"c", Value::CreateValue(true)},
                                     {"d", Value::STRUCT({{"e", Value::CreateValue(456)}})}});
    auto json = ErpelSerializer::Serialize(struct_val);
    REQUIRE(remove_whitespace(json) == "{\"a\":123,\"b\":\"TEST\",\"c\":true,\"d\":{\"e\":456}}");
}

TEST_CASE("Test serialize struct type with type information", "[duckdb_json_helper]") {
    auto struct_val = Value::STRUCT({{"a", Value::CreateValue(123)}, 
                                     {"b", Value::CreateValue("TEST")}});
    auto json = ErpelSerializer::Serialize(struct_val, true);
    REQUIRE(remove_whitespace(json) == "{\"$type\":\"STRUCT\",\"$value\":{\"a\":{\"$type\":\"INTEGER\",\"$value\":123},\"b\":{\"$type\":\"VARCHAR\",\"$value\":\"TEST\"}}}");
}

TEST_CASE("Test serialize list type", "[duckdb_json_helper]") {
    auto list_val = Value::LIST({Value::STRUCT({{"a", Value::CreateValue(123)}, {"b", Value::CreateValue("TEST")}}),
                                 Value::STRUCT({{"a", Value::CreateValue(456)}, {"b", Value::CreateValue("TEST2")}}),
                                 Value::STRUCT({{"a", Value::CreateValue(789)}, {"b", Value::CreateValue("TEST3")}})});
    auto json = ErpelSerializer::Serialize(list_val);
    REQUIRE(remove_whitespace(json) == "[{\"a\":123,\"b\":\"TEST\"},{\"a\":456,\"b\":\"TEST2\"},{\"a\":789,\"b\":\"TEST3\"}]");
}

TEST_CASE("Test serialize list type with type information", "[duckdb_json_helper]") {
    auto list_val = Value::LIST({Value::STRUCT({{"a", Value::CreateValue(123)}, {"b", Value::CreateValue("TEST")}}),
                                 Value::STRUCT({{"a", Value::CreateValue(456)}, {"b", Value::CreateValue("TEST2")}}),
                                 Value::STRUCT({{"a", Value::CreateValue(789)}, {"b", Value::CreateValue("TEST3")}})});
    auto json = ErpelSerializer::Serialize(list_val, true);
    REQUIRE(remove_whitespace(json) == "{\"$type\":\"LIST\",\"$value\":[{\"$type\":\"STRUCT\",\"$value\":{\"a\":{\"$type\":\"INTEGER\",\"$value\":123},\"b\":{\"$type\":\"VARCHAR\",\"$value\":\"TEST\"}}},{\"$type\":\"STRUCT\",\"$value\":{\"a\":{\"$type\":\"INTEGER\",\"$value\":456},\"b\":{\"$type\":\"VARCHAR\",\"$value\":\"TEST2\"}}},{\"$type\":\"STRUCT\",\"$value\":{\"a\":{\"$type\":\"INTEGER\",\"$value\":789},\"b\":{\"$type\":\"VARCHAR\",\"$value\":\"TEST3\"}}}]}");
}

// ---------------------------------------------------------------------------

TEST_CASE("Deserialize primitive types", "[duckdb_json_helper]") 
{
    auto str = string("{\"$type\":\"VARCHAR\",\"$value\":\"TEST\"}");
    auto int_str = string("{\"$type\":\"INTEGER\",\"$value\":123}");
    auto double_str = string("{\"$type\":\"DOUBLE\",\"$value\":123.456}");
    auto bool_str = string("{\"$type\":\"BOOLEAN\",\"$value\":true}");
    auto null_str = string("{\"$type\":\"NULL\",\"$value\":null}");
    auto date_str = string("{\"$type\":\"DATE\",\"$value\":\"2020-01-01\"}");
    auto dt_str = string("{\"$type\":\"TIME\",\"$value\":123456}");
    auto ts_str = string("{\"$type\":\"TIMESTAMP\",\"$value\":\"2020-01-01 00:00:00\"}");

    auto val = ErpelSerializer::Deserialize(str);
    REQUIRE(val.type().id() == LogicalTypeId::VARCHAR);
    REQUIRE(val.GetValue<string>() == "TEST");

    val = ErpelSerializer::Deserialize(int_str);
    REQUIRE(val.type().id() == LogicalTypeId::INTEGER);
    REQUIRE(val.GetValue<int>() == 123);

    val = ErpelSerializer::Deserialize(double_str);
    REQUIRE(val.type().id() == LogicalTypeId::DOUBLE);
    REQUIRE(val.GetValue<double>() == 123.456);

    val = ErpelSerializer::Deserialize(bool_str);
    REQUIRE(val.type().id() == LogicalTypeId::BOOLEAN);
    REQUIRE(val.GetValue<bool>() == true);

    val = ErpelSerializer::Deserialize(null_str);
    REQUIRE(val.type().id() == LogicalTypeId::SQLNULL);
    REQUIRE(val.IsNull() == true);

    val = ErpelSerializer::Deserialize(date_str);
    REQUIRE(val.type().id() == LogicalTypeId::DATE);
    REQUIRE(val.GetValue<date_t>() == Date::FromString("2020-01-01"));

    val = ErpelSerializer::Deserialize(dt_str);
    REQUIRE(val.type().id() == LogicalTypeId::TIME);
    REQUIRE(val.GetValue<dtime_t>() == dtime_t(123456));

    val = ErpelSerializer::Deserialize(ts_str);
    REQUIRE(val.type().id() == LogicalTypeId::TIMESTAMP);
    REQUIRE(val.GetValue<timestamp_t>() == Timestamp::FromString("2020-01-01 00:00:00"));
}

TEST_CASE("Deserialize blob type", "[duckdb_json_helper]") 
{
    auto blob_str = string("{\"$type\":\"BLOB\",\"$value\":\"VEVTVA==\"}");
    auto val = ErpelSerializer::Deserialize(blob_str);
    REQUIRE(val.type().id() == LogicalTypeId::BLOB);
    REQUIRE(val.GetValue<string>() == "TEST");
}

TEST_CASE("Deserialize struct type", "[duckdb_json_helper]") 
{
    auto struct_str = string("{\"$type\":\"STRUCT\",\"$value\":{\"a\":{\"$type\":\"INTEGER\",\"$value\":123},\"b\":{\"$type\":\"VARCHAR\",\"$value\":\"TEST\"}}}");
    auto val = ErpelSerializer::Deserialize(struct_str);
    REQUIRE(val.type().id() == LogicalTypeId::STRUCT);

    auto child_values = StructValue::GetChildren(val);
    
    REQUIRE(child_values.size() == 2);
    REQUIRE(StructType::GetChildName(val.type(), 0) == "a");
    REQUIRE(child_values[0].GetValue<int>() == 123);
    REQUIRE(StructType::GetChildName(val.type(), 1) == "b");
    REQUIRE(child_values[1].GetValue<string>() == "TEST");
}

TEST_CASE("Deserialize list type", "[duckdb_json_helper]")
{
    auto list_str = string("{\"$type\":\"LIST\",\"$value\":[{\"$type\":\"STRUCT\",\"$value\":{\"a\":{\"$type\":\"INTEGER\",\"$value\":123},\"b\":{\"$type\":\"VARCHAR\",\"$value\":\"TEST\"}}},{\"$type\":\"STRUCT\",\"$value\":{\"a\":{\"$type\":\"INTEGER\",\"$value\":456},\"b\":{\"$type\":\"VARCHAR\",\"$value\":\"TEST2\"}}},{\"$type\":\"STRUCT\",\"$value\":{\"a\":{\"$type\":\"INTEGER\",\"$value\":789},\"b\":{\"$type\":\"VARCHAR\",\"$value\":\"TEST3\"}}}]}");
    auto val = ErpelSerializer::Deserialize(list_str);

    REQUIRE(val.type().id() == LogicalTypeId::LIST);

    auto child_values = ListValue::GetChildren(val);
    REQUIRE(child_values.size() == 3);
}