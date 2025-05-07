find_package(OpenSSL REQUIRED)
add_definitions(-DSAPonUNIX -DSAPwithUNICODE -DSAPwithTHREADS -DSAPonLIN)

include_directories(./include
                    ../../src/include
                    ../../src/bics/include
                    ../../src/odp/include
                    ../../../duckdb/third_party/catch/
                    ../../../duckdb/third_party/httplib/
                    ../../../duckdb/test/include
                    ../../../duckdb/extension/json/include
                    ../../../duckdb/extension/json/yyjson/include
                    ${SAPNWRFC_HOME}/include
                    ${OPENSSL_INCLUDE_DIR})

set(TEST_SOURCES
    test_type_conversion.cpp
    test_serialization_helper.cpp
    test_value_helper.cpp
    test_table_wrapper.cpp
    test_telemetry.cpp
    test_main.cpp
)

add_executable(erpl_rfc_tests ${TEST_SOURCES})
target_link_libraries(erpl_rfc_tests ${EXTENSION_NAME} duckdb_static test_helpers ${SAPNWRFC_LIB_FILES} ${OPENSSL_LIBRARIES})