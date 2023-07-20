include_directories(../../duckdb/third_party/catch/)
include_directories(../../duckdb/test/include)

include_directories(../../duckdb/extension/json/include)
include_directories(../../duckdb/extension/json/yyjson/include)

add_definitions(-DSAPonUNIX -DSAPwithUNICODE -DSAPwithTHREADS -DSAPonLIN)

add_executable(erpel_tests test_type_conversion.cpp test_json_helper.cpp test_main.cpp)
target_link_libraries(erpel_tests duckdb ${EXTENSION_NAME} test_helpers ${SAPNWRFC_LIB_FILES})