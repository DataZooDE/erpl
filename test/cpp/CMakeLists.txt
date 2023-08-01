include_directories(../../duckdb/third_party/catch/)
include_directories(../../duckdb/test/include)

include_directories(../../duckdb/extension/json/include)
include_directories(../../duckdb/extension/json/yyjson/include)

add_definitions(-DSAPonUNIX -DSAPwithUNICODE -DSAPwithTHREADS -DSAPonLIN)

add_definitions(-DWITH_BICS)
include_directories(../../src/bics/include)

set(TEST_SOURCES
    test_type_conversion.cpp
    test_serialization_helper.cpp
    test_bics_model.cpp
    test_main.cpp
)

add_executable(erpel_tests ${TEST_SOURCES})
target_link_libraries(erpel_tests duckdb ${EXTENSION_NAME} test_helpers ${SAPNWRFC_LIB_FILES})