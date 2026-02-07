option(BUILD_UNITTESTS "Build ERPL C++ Unit Tests." OFF)

# Set extension name here
set(TARGET_NAME erpl_rfc)
set(EXTENSION_NAME ${TARGET_NAME}_extension)
set(LOADABLE_EXTENSION_NAME ${TARGET_NAME}_loadable_extension)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

project(${TARGET_NAME})

include(../scripts/functions.cmake)
add_duckdb_version_definition()

if(WIN32)
      default_win32_libraries()
endif()

if(UNIX AND NOT APPLE)
      default_linux_libraries()
endif()

if(UNIX AND APPLE)
      default_osx_libraries()
endif()

find_package(OpenSSL REQUIRED)

# Add posthog-telemetry submodule
# Disable posthog-telemetry's own tests - we use erpl's tests instead
set(POSTHOG_BUILD_UNITTESTS_SAVED ${BUILD_UNITTESTS})
set(BUILD_UNITTESTS OFF CACHE BOOL "" FORCE)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../third_party/posthog-telemetry ${CMAKE_BINARY_DIR}/posthog-telemetry)
set(BUILD_UNITTESTS ${POSTHOG_BUILD_UNITTESTS_SAVED} CACHE BOOL "" FORCE)

# Add DuckDB httplib includes to posthog_telemetry target (it needs httplib.hpp for HTTP requests)
target_include_directories(posthog_telemetry PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../duckdb/third_party/
    ${CMAKE_CURRENT_SOURCE_DIR}/../duckdb/third_party/httplib
)

include_directories(src/include
                    ../duckdb/third_party/
                    ../duckdb/third_party/httplib
                    ../duckdb/third_party/miniz
                    ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/posthog-telemetry/include
                    ${SAPNWRFC_HOME}/include
                    ${OPENSSL_INCLUDE_DIR}
)

add_yyjson_from_duckdb()

set(EXTENSION_SOURCES
      src/erpl_rfc_extension.cpp
      src/pragma_ping.cpp
      src/pragma_set_trace.cpp
      src/pragma_ini.cpp
      src/sap_secret.cpp
      src/erpl_tracing.cpp
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
      src/sap_storage.cpp
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

if(UNIX AND APPLE)
      default_osx_definitions(${EXTENSION_NAME})
      default_osx_definitions(${LOADABLE_EXTENSION_NAME})
endif()

target_link_libraries(${EXTENSION_NAME} ${SAPNWRFC_LIB_FILES} ${OPENSSL_LIBRARIES} posthog_telemetry)
target_link_libraries(${LOADABLE_EXTENSION_NAME} ${SAPNWRFC_LIB_FILES} ${OPENSSL_LIBRARIES} posthog_telemetry)

if(MINGW)
   target_link_libraries(${EXTENSION_NAME} -lcrypt32)
   target_link_libraries(${LOADABLE_EXTENSION_NAME} -lcrypt32)
endif()

if(${BUILD_UNITTESTS})
      add_subdirectory(test)
endif()

install(
  TARGETS ${EXTENSION_NAME} ${LOADABLE_EXTENSION_NAME}
  EXPORT "${DUCKDB_EXPORT_SET}"
  LIBRARY DESTINATION "${INSTALL_LIB_DIR}"
  ARCHIVE DESTINATION "${INSTALL_LIB_DIR}")