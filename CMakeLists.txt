cmake_minimum_required(VERSION 3.5)

option(BUILD_UNITTESTS "Build ERPL C++ Unit Tests." TRUE)
option(BUILD_BICS "Build BICS functionality in ERPL." TRUE)
option(BUILD_ODP "Build ODP functionality in ERPL." TRUE)

# Set extension name here
set(TARGET_NAME erpl)
set(EXTENSION_NAME ${TARGET_NAME}_extension)
set(LOADABLE_EXTENSION_NAME ${TARGET_NAME}_loadable_extension)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

project(${TARGET_NAME})

find_package(OpenSSL REQUIRED)

if(APPLE)
      set(CMAKE_OSX_ARCHITECTURES "x86_64" CACHE INTERNAL "" FORCE)
      add_definitions(-DSAPonDARW -DSAPwithUNICODE -DSAPwithTHREADS -DSAP_PLATFORM_MAKENAME=ntintel)
      set(SAPNWRFC_HOME ${CMAKE_CURRENT_SOURCE_DIR}/nwrfcsdk/osx/)
      set(SAPNWRFC_LIB_FILES ${SAPNWRFC_HOME}/lib/libsapnwrfc.dylib ${SAPNWRFC_HOME}/lib/libsapucum.dylib)
      set(OSX_BUILD_ARCH x86_64)
      set(BUILD_UNITTESTS FALSE)
      set(OPENSSL_USE_STATIC_LIBS TRUE)
endif()

if(WIN32)
      add_definitions(-DSAPonNT -D_AFXDLL -DWIN32 -D_WIN32_WINNT=0x0502 -DWIN64 -D_AMD64_ -DSAPwithUNICODE -DUNICODE -D_UNICODE -DSAPwithTHREADS -DSAP_PLATFORM_MAKENAME=ntintel -Wno-deprecated-declarations -Wno-attributes)
      add_definitions(-DSAP_API=)
      set(SAPNWRFC_HOME ${CMAKE_CURRENT_SOURCE_DIR}/nwrfcsdk/win/)
      set(SAPNWRFC_LIB_FILES ${SAPNWRFC_HOME}/lib/sapnwrfc.lib ${SAPNWRFC_HOME}/lib/libsapucum.lib)
      set(BUILD_UNITTESTS FALSE)
      set(OPENSSL_USE_STATIC_LIBS TRUE)
endif()

if(UNIX AND NOT APPLE)
      add_definitions(-DSAPonUNIX -DSAPonLIN -DSAPwithUNICODE -DSAPwithTHREADS -Wno-deprecated-declarations)
      set(OSX_BUILD_ARCH x86_64)
      set(SAPNWRFC_HOME ${CMAKE_CURRENT_SOURCE_DIR}/nwrfcsdk/linux/)
      set(SAPNWRFC_LIB_FILES ${SAPNWRFC_HOME}/lib/libsapnwrfc.so ${SAPNWRFC_HOME}/lib/libsapucum.so)
endif()

if(${BUILD_UNITTESTS})
      add_subdirectory(test)
endif()

if (${BUILD_BICS})
      add_definitions(-DWITH_BICS)
      add_subdirectory(src/bics)
      include_directories(src/bics/include)
endif()

if (${BUILD_ODP})
      add_definitions(-DWITH_ODP)
      add_subdirectory(src/odp)
      include_directories(src/odp/include)
endif()

include_directories(src/include
                    src/yyjson/include
                    duckdb/third_party/httplib
                    ${SAPNWRFC_HOME}/include
                    ${OPENSSL_INCLUDE_DIR})

add_subdirectory(src/yyjson)

set(EXTENSION_SOURCES
      src/erpl_extension.cpp
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
      src/scanner_search_group.cpp
      src/scanner_search_function.cpp
      #src/scanner_describe_function.cpp
      src/scanner_show_tables.cpp
      src/scanner_describe_fields.cpp
      src/scanner_describe_references.cpp
      src/scanner_read_table.cpp
      src/telemetry.cpp
      ${YYJSON_OBJECT_FILES}
      ${BICS_SOURCES}
      ${ODP_SOURCES}
)

find_library(SAPNWRFC_LIB_FILES
      NAMES sapnwrfc sapucum
)

message(STATUS "SAPNWRFC_LIB_FILES: ${SAPNWRFC_LIB_FILES}")

set(PARAMETERS "-warnings")

# Build loadable extension
build_static_extension(${TARGET_NAME} ${EXTENSION_SOURCES})
build_loadable_extension(${TARGET_NAME} ${PARAMETERS} ${EXTENSION_SOURCES})

target_link_libraries(${EXTENSION_NAME} ${SAPNWRFC_LIB_FILES} ${OPENSSL_LIBRARIES})
target_link_libraries(${LOADABLE_EXTENSION_NAME} ${SAPNWRFC_LIB_FILES} ${OPENSSL_LIBRARIES})

if(WIN32)
  find_package(ZLIB)
  target_link_libraries(${EXTENSION_NAME} ZLIB::ZLIB -lcrypt32 -liphlpapi)
  target_link_libraries(${LOADABLE_EXTENSION_NAME} ZLIB::ZLIB -lcrypt32 -liphlpapi)
endif()

install(
  TARGETS ${EXTENSION_NAME}
  EXPORT "${DUCKDB_EXPORT_SET}"
  LIBRARY DESTINATION "${INSTALL_LIB_DIR}"
  ARCHIVE DESTINATION "${INSTALL_LIB_DIR}")
