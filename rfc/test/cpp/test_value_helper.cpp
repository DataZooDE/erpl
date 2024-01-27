#include "catch.hpp"
#include "test_helpers.hpp"
#include "duckdb.hpp"

#include "duckdb_argument_helper.hpp"

using namespace duckdb;
using namespace std;

Value CreateTestStruct()
{
    auto struct_val = Value::STRUCT({
        {"a", Value::CreateValue(123)}, 
        {"b", Value::CreateValue("TEST")},
        {"c", Value::CreateValue(true)},
        {"d", Value::STRUCT({
            {"x", Value::CreateValue(456)},
            {"y", Value::CreateValue("BAR")}
        })}
    });
    return struct_val;
}

Value CreateTestList()
{
    auto list_val = Value::LIST({
        Value::STRUCT({
            {"x", Value::CreateValue(1)},
            {"y", Value::CreateValue("A")}
        }),
        Value::STRUCT({
            {"x", Value::CreateValue(2)},
            {"y", Value::CreateValue("B")}
        }),
        Value::STRUCT({
            {"x", Value::CreateValue(3)},
            {"y", Value::CreateValue("C")}
        })
    });
    return list_val;
}

TEST_CASE("Can get underlying value from ValueHelper", "[value_helper]")
{
    auto struct_val = CreateTestStruct();
    auto helper = ValueHelper(struct_val);

    REQUIRE(helper.Get() == struct_val);
}

TEST_CASE("Test can select by path from a struct", "[value_helper]") 
{
    auto struct_val = CreateTestStruct();
    auto helper = ValueHelper(struct_val);

    REQUIRE(helper["/a"].GetValue<int>() == 123);
    REQUIRE(helper["/b"].GetValue<string>() == "TEST");
    REQUIRE(helper["/c"].GetValue<bool>() == true);
    REQUIRE(helper["/d/x"].GetValue<int>() == 456);
    REQUIRE(helper["/d/y"].GetValue<string>() == "BAR");
}

TEST_CASE("Can select path with root path set", "[value_helper]") 
{
    auto struct_val = CreateTestStruct();
    auto helper = ValueHelper(struct_val, "/d");

    REQUIRE(helper["/x"].GetValue<int>() == 456);
    REQUIRE(helper["/y"].GetValue<string>() == "BAR");
}

TEST_CASE("Test can update a struct by path", "[value_helper]") 
{
    auto struct_val = CreateTestStruct();
    REQUIRE(ValueHelper(struct_val)["/a"].GetValue<int>() == 123);

    auto new_value = Value::CreateValue(789);
    auto new_struct = ValueHelper::CreateMutatedValue(struct_val, new_value, "/a");
    REQUIRE(ValueHelper(new_struct)["/a"].GetValue<int>() == 789);

    new_value = Value::CreateValue("FOO");
    new_struct = ValueHelper::CreateMutatedValue(struct_val, new_value, "/d/y");
    REQUIRE(ValueHelper(new_struct)["/d/y"].GetValue<string>() == "FOO");
}

TEST_CASE("Test can update a list") 
{
    auto list_val = CreateTestList();
    REQUIRE(ValueHelper(list_val)["/0/x"].GetValue<int>() == 1);
    
    auto new_value = Value::CreateValue(8);
    auto new_list = ValueHelper::CreateMutatedValue(list_val, new_value, "/0/x");
    REQUIRE(ValueHelper(new_list)["/0/x"].GetValue<int>() == 8);
    
    
    REQUIRE(ValueHelper(list_val)["/1/y"].GetValue<string>() == "B");
    new_value = Value::CreateValue("FOO");
    new_list = ValueHelper::CreateMutatedValue(list_val, new_value, "/1/y");
    REQUIRE(ValueHelper(new_list)["/1/y"].GetValue<string>() == "FOO");
}

TEST_CASE("Test can add to a list") 
{
    auto curr_list = CreateTestList();
    REQUIRE(ValueHelper(curr_list)["/0/x"].GetValue<int>() == 1);
    
    auto new_value = Value::STRUCT({
            {"x", Value::CreateValue(4)},
            {"y", Value::CreateValue("D")}
    });
    auto new_list = ValueHelper::AddToList(curr_list, new_value);
    REQUIRE(ValueHelper(new_list)["/3/x"].GetValue<int>() == 4);
}

TEST_CASE("Test can remove from a list by value") 
{
    auto curr_list = CreateTestList();
    
    auto remove_value = Value::STRUCT({
            {"x", Value::CreateValue(3)},
            {"y", Value::CreateValue("C")}
    });

    auto new_list = ValueHelper::RemoveFromList(curr_list, remove_value);
    REQUIRE(ListValue::GetChildren(new_list).size() == ListValue::GetChildren(curr_list).size() - 1);
}

TEST_CASE("Test can remove from a list by index") 
{
    auto curr_list = CreateTestList();
    
    auto new_list = ValueHelper::RemoveFromList(curr_list, 0);
    REQUIRE(ListValue::GetChildren(new_list).size() == ListValue::GetChildren(curr_list).size() - 1);
    REQUIRE(ValueHelper(new_list)["/0/x"].GetValue<int>() == 2);
}