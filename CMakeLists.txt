cmake_minimum_required(VERSION 2.8.12)

option(BUILD_UNITTESTS "Build ERPEL C++ Unit Tests." TRUE)
option(BUILD_BICS "Build BICS functionality in ERPEL." TRUE)
option(BUILD_ODP "Build ODP functionality in ERPEL." TRUE)

# Set extension name here
set(TARGET_NAME erpel)
set(EXTENSION_NAME ${TARGET_NAME}_extension)
set(SAPNWRFC_HOME ${CMAKE_CURRENT_SOURCE_DIR}/nwrfcsdk)


set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

project(${TARGET_NAME})
include_directories(src/include
                    ${SAPNWRFC_HOME}/include)

add_subdirectory(src/yyjson)
include_directories(src/yyjson/include)

if(APPLE)
      add_definitions(-DSAPonDARW -DSAPwithUNICODE -DSAPwithTHREADS -DSAP_PLATFORM_MAKENAME=ntintel)
endif()
if(WIN32)
      add_definitions(-DSAPonNT -D_AFXDLL -DWIN32 -D_WIN32_WINNT=0x0502 -DWIN64 -D_AMD64_ -DSAPwithUNICODE -DUNICODE -D_UNICODE -DSAPwithTHREADS -DSAP_PLATFORM_MAKENAME=ntintel)
      add_definitions(-DSAP_API=)
      set(SAPNWRFC_LIB_FILES ${SAPNWRFC_HOME}/lib/sapnwrfc.lib ${SAPNWRFC_HOME}/lib/libsapucum.lib)
      set(BUILD_UNITTESTS FALSE)
endif()
if(UNIX AND NOT APPLE)
      add_definitions(-DSAPonUNIX -DSAPonLIN -DSAPwithUNICODE -DSAPwithTHREADS)
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

set(EXTENSION_SOURCES
      src/erpel_extension.cpp
      src/pragma_ping.cpp
      src/sap_connection.cpp
      src/sap_function.cpp
      src/sap_type_conversion.cpp
      src/sap_rfc.cpp
      src/duckdb_argument_helper.cpp
      src/duckdb_serialization_helper.cpp
      src/scanner_invoke.cpp
      src/scanner_search_group.cpp
      src/scanner_search_function.cpp
      src/scanner_describe_function.cpp
      src/scanner_show_tables.cpp
      src/scanner_describe_fields.cpp
      src/scanner_describe_references.cpp
      src/scanner_read_table.cpp
      ${YYJSON_OBJECT_FILES}
      ${BICS_SOURCES}
      ${ODP_SOURCES}
)

add_library(${EXTENSION_NAME} STATIC ${EXTENSION_SOURCES})

set(PARAMETERS "-warnings")

build_loadable_extension(${TARGET_NAME} ${PARAMETERS} ${ALL_OBJECT_FILES} ${EXTENSION_SOURCES})

target_link_directories(${EXTENSION_NAME} PRIVATE ${SAPNWRFC_HOME}/lib)
target_link_libraries(${EXTENSION_NAME} PRIVATE ${SAPNWRFC_LIB_FILES})

message(STATUS "SAPNWRFC_HOME: ${SAPNWRFC_HOME}")
message(STATUS "EXTENSION_SOURCES: ${EXTENSION_SOURCES}")

install(
  TARGETS ${EXTENSION_NAME}
  EXPORT "${DUCKDB_EXPORT_SET}"
  LIBRARY DESTINATION "${INSTALL_LIB_DIR}"
  ARCHIVE DESTINATION "${INSTALL_LIB_DIR}")
