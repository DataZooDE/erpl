#include "catch.hpp"
#include "test_helpers.hpp"
#include "duckdb.hpp"

#include "duckdb_argument_helper.hpp"

using namespace duckdb;
using namespace std;

Value CreateTestTable()
{
    auto builder = ArgBuilder();
    builder.Add("NODE_ID", Value::LIST({Value::CreateValue("1"), Value::CreateValue("2")}));
    builder.Add("PARENT_ID", Value::LIST({Value::CreateValue(""), Value::CreateValue("")}));
    builder.Add("ROLE_KEY", Value::LIST({Value::CreateValue(""), Value::CreateValue("")}));
    builder.Add("OBJ_KEY", Value::LIST({Value::CreateValue("0BCT_CB"), Value::CreateValue("0BW")}));
    builder.Add("OBJ_TYPE", Value::LIST({Value::CreateValue("AREA"), Value::CreateValue("AREA")}));
    builder.Add("OBJ_TEXT", Value::LIST({Value::CreateValue("Content Browser"), Value::CreateValue("Business Information Warehouse")}));
    builder.Add("OBJ_DESC", Value::LIST({Value::CreateValue("Content Browser"), Value::CreateValue("Business Information Warehouse")}));
    builder.Add("OBJ_TECHNAME", Value::LIST({Value::CreateValue("0BCT_CB"), Value::CreateValue("0BW")}));
    builder.Add("SUBOBJECT_TYPE", Value::LIST({Value::CreateValue("AREA"), Value::CreateValue("AREA")}));
    builder.Add("ATTRIB_KEY", Value::LIST({Value::CreateValue(""), Value::CreateValue("")}));
    builder.Add("ATTRIB_TEXT", Value::LIST({Value::CreateValue(""), Value::CreateValue("")}));
    builder.Add("ISFOLDER", Value::LIST({Value::CreateValue("X"), Value::CreateValue("")}));
    builder.Add("EXPANDER", Value::LIST({Value::CreateValue("0"), Value::CreateValue("0")}));
    builder.Add("LAST_CHANGED", Value::LIST({Value::CreateValue("20170713093410"), Value::CreateValue("20170713093410")}));
    builder.Add("LAST_CHANGED_BY", Value::LIST({Value::CreateValue("DDIC"), Value::CreateValue("DDIC")}));
    builder.Add("ICON_TYPE", Value::LIST({Value::CreateValue(""), Value::CreateValue("")}));
    
    return builder.Build();
}

TEST_CASE("Test type conversion", "[table_wrapper]") {
    auto struct_oriented_table = std::make_shared<Value>(CreateTestTable());
    auto table_wrapper = TableWrapper(struct_oriented_table);

    auto list_oriented_row_type = table_wrapper.ListOrientedRowType();
    REQUIRE(list_oriented_row_type.id() == LogicalTypeId::STRUCT);
    auto struct_types = StructType::GetChildTypes(list_oriented_row_type);
    REQUIRE(struct_types.size() == 16);
    for (auto &struct_type: struct_types) {
        REQUIRE(struct_type.second.id() == LogicalTypeId::VARCHAR);
    }
}

TEST_CASE("Test size", "[table_wrapper]") {
    auto struct_oriented_table = std::make_shared<Value>(CreateTestTable());
    auto table_wrapper = TableWrapper(struct_oriented_table);
    REQUIRE(table_wrapper.Size() == 2);
}

TEST_CASE("Test operator[]", "[table_wrapper]") {
    auto struct_oriented_table = std::make_shared<Value>(CreateTestTable());
    auto table_wrapper = TableWrapper(struct_oriented_table);
    auto value = table_wrapper[0];
    REQUIRE(value.type().id() == LogicalTypeId::STRUCT);

    auto struct_children = StructValue::GetChildren(value);
    REQUIRE(struct_children.size() == 16);

    ValueHelper helper(value);
    REQUIRE(helper["NODE_ID"].ToString() == "1");
    REQUIRE(helper["PARENT_ID"].ToString() == "");
    REQUIRE(helper["ROLE_KEY"].ToString() == "");
    REQUIRE(helper["OBJ_KEY"].ToString() == "0BCT_CB");
    REQUIRE(helper["OBJ_TYPE"].ToString() == "AREA");
    REQUIRE(helper["OBJ_TEXT"].ToString() == "Content Browser");
}
