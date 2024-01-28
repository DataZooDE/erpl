option(BUILD_UNITTESTS "Build ERPL C++ Unit Tests." OFF)

# Set extension name here
set(TARGET_NAME erpl_rfc)
set(EXTENSION_NAME ${TARGET_NAME}_extension)
set(LOADABLE_EXTENSION_NAME ${TARGET_NAME}_loadable_extension)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

project(${TARGET_NAME})

include(../scripts/functions.cmake)

if(WIN32)
      default_win32_libraries()
endif()

if(UNIX AND NOT APPLE)
      default_linux_libraries()
endif()

include_directories(src/include
                    ../duckdb/extension/json/yyjson/include
                    ../duckdb/third_party/
                    ../duckdb/third_party/httplib
                    ../duckdb/third_party/miniz
                    ${SAPNWRFC_HOME}/include
                    ${OPENSSL_INCLUDE_DIR})

add_subdirectory(../duckdb/extension/json/yyjson/ ../build/yyjson)

set(EXTENSION_SOURCES
      src/erpl_rfc_extension.cpp
      src/pragma_ping.cpp
      src/pragma_set_trace.cpp
      src/pragma_ini.cpp
      src/sap_connection.cpp
      src/sap_function.cpp
      src/sap_type_conversion.cpp
      src/sap_rfc.cpp
      src/duckdb_argument_helper.cpp
      src/duckdb_serialization_helper.cpp
      src/scanner_invoke.cpp
      src/scanner_show_groups.cpp
      src/scanner_show_functions.cpp
      src/scanner_describe_function.cpp
      src/scanner_show_tables.cpp
      src/scanner_describe_fields.cpp
      src/scanner_describe_references.cpp
      src/scanner_read_table.cpp
      src/telemetry.cpp
      ${YYJSON_OBJECT_FILES}
      ${SAPNWRFC_LIB_OBJECTS}
)

# Build loadable extension
build_static_extension(${TARGET_NAME} ${EXTENSION_SOURCES})
set(PARAMETERS "-warnings")
build_loadable_extension(${TARGET_NAME} ${PARAMETERS} ${EXTENSION_SOURCES})

if(WIN32)
      default_win32_definitions(${EXTENSION_NAME})
      default_win32_definitions(${LOADABLE_EXTENSION_NAME})
endif()

if(UNIX AND NOT APPLE)
      default_linux_definitions(${EXTENSION_NAME})
      default_linux_definitions(${LOADABLE_EXTENSION_NAME})
endif()

target_link_libraries(${EXTENSION_NAME} ${SAPNWRFC_LIB_FILES} OpenSSL::SSL OpenSSL::Crypto)
target_link_libraries(${LOADABLE_EXTENSION_NAME} ${SAPNWRFC_LIB_FILES} OpenSSL::SSL OpenSSL::Crypto)

if (WIN32)
      target_link_libraries(${EXTENSION_NAME} iphlpapi)
      target_link_libraries(${LOADABLE_EXTENSION_NAME} iphlpapi)
endif()

if(${BUILD_UNITTESTS})
      add_subdirectory(test)
endif()

install(
  TARGETS ${EXTENSION_NAME} ${LOADABLE_EXTENSION_NAME}
  EXPORT "${DUCKDB_EXPORT_SET}"
  LIBRARY DESTINATION "${INSTALL_LIB_DIR}"
  ARCHIVE DESTINATION "${INSTALL_LIB_DIR}")
