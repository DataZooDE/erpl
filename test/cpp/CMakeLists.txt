
include_directories(../../src/include
                    ../../duckdb/third_party/catch/
                    ../../duckdb/third_party/httplib/
                    ../../duckdb/test/include
                    ../../duckdb/extension/json/include
                    ../../duckdb/extension/json/yyjson/include
                    ${SAPNWRFC_HOME}/include
                    ${OPENSSL_INCLUDE_DIR})

include_directories(${SAPNWRFC_HOME}/include)

add_definitions(-DSAPonUNIX -DSAPwithUNICODE -DSAPwithTHREADS -DSAPonLIN)

find_package(OpenSSL REQUIRED)
include_directories(${OPENSSL_INCLUDE_DIR})

set(TEST_SOURCES
    test_type_conversion.cpp
    test_serialization_helper.cpp
    test_telemetry.cpp
    test_main.cpp
)

add_executable(erpl_tests ${TEST_SOURCES})
target_link_libraries(erpl_tests duckdb ${EXTENSION_NAME} test_helpers ${SAPNWRFC_LIB_FILES} ${OPENSSL_LIBRARIES})