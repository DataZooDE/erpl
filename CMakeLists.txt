cmake_minimum_required(VERSION 3.5)

option(BUILD_UNITTESTS "Build ERPL C++ Unit Tests." TRUE)
option(BUILD_BICS "Build BICS functionality in ERPL." TRUE)
option(BUILD_ODP "Build ODP functionality in ERPL." TRUE)

# Set extension name here
set(TARGET_NAME erpl_impl)
set(EXTENSION_NAME ${TARGET_NAME}_extension)
set(LOADABLE_EXTENSION_NAME ${TARGET_NAME}_loadable_extension)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

project(${TARGET_NAME})

function(find_sap_libraries LIB_LIST_VAR SAPNWRFC_HOME_PATH SAPNWRFC_LIB_NAME SAPUCUM_LIB_NAME )
      find_library(SAPNWRFC_LIB ${SAPNWRFC_LIB_NAME} PATHS ${SAPNWRFC_HOME_PATH}/lib)
      find_library(SAPUCUM_LIB ${SAPUCUM_LIB_NAME} PATHS ${SAPNWRFC_HOME_PATH}/lib)

      if(NOT SAPNWRFC_LIB)
            message(FATAL_ERROR "Could not find ${SAPNWRFC_LIB_NAME} library")
      endif()

      if(NOT SAPUCUM_LIB)
            message(FATAL_ERROR "Could not find ${SAPUCUM_LIB_NAME} library")
      endif()

      list(APPEND SAP_LIBS ${SAPNWRFC_LIB} ${SAPUCUM_LIB})
      set(${LIB_LIST_VAR} ${SAP_LIBS} PARENT_SCOPE)
endfunction()

if(APPLE)
      set(CMAKE_OSX_ARCHITECTURES "x86_64" CACHE INTERNAL "" FORCE)
      add_definitions(-DSAPonDARW -DSAPwithUNICODE -DSAPwithTHREADS -DSAP_PLATFORM_MAKENAME=ntintel)
      set(SAPNWRFC_HOME ${CMAKE_CURRENT_SOURCE_DIR}/nwrfcsdk/osx)
      set(OSX_BUILD_ARCH x86_64)
      set(BUILD_UNITTESTS FALSE)
      set(OPENSSL_USE_STATIC_LIBS TRUE)

      find_package(OpenSSL REQUIRED)
      message(STATUS "SAPNWRFC_HOME: ${SAPNWRFC_HOME}")
      find_sap_libraries(SAPNWRFC_LIB_FILES ${SAPNWRFC_HOME} "sapnwrfc" "sapucum")
endif()

if(WIN32)
      add_definitions(-DSAPonNT -D_AFXDLL -DWIN32 -D_WIN32_WINNT=0x0502 -DWIN64 -D_AMD64_ -DSAPwithUNICODE -DUNICODE -D_UNICODE -DSAPwithTHREADS -DSAP_PLATFORM_MAKENAME=ntintel)
      add_definitions(-DSAP_API=)
      set(SAPNWRFC_HOME ${CMAKE_CURRENT_SOURCE_DIR}/nwrfcsdk/win)
      set(BUILD_UNITTESTS FALSE)
      set(OPENSSL_USE_STATIC_LIBS TRUE)

      find_package(OpenSSL REQUIRED)
      message(STATUS "SAPNWRFC_HOME: ${SAPNWRFC_HOME}")
      find_sap_libraries(SAPNWRFC_LIB_FILES ${SAPNWRFC_HOME} "sapnwrfc" "libsapucum")

      foreach(LIB ${SAPNWRFC_LIB_FILES})
            get_filename_component(RC_NAME ${LIB} NAME_WE)
            file(APPEND "${CMAKE_CURRENT_BINARY_DIR}/resources.rc" "RC_NAME RCDATA \"${LIB}\"\n")
      endforeach()

      set(SAPNWRFC_LIB_OBJECTS "resources.rc")
endif()

if(UNIX AND NOT APPLE)
      add_definitions(-DSAPonUNIX -DSAPonLIN -DSAPwithUNICODE -DSAPwithTHREADS -Wno-deprecated-declarations)
      set(SAPNWRFC_HOME ${CMAKE_CURRENT_SOURCE_DIR}/nwrfcsdk/linux)
      SET(CMAKE_SKIP_BUILD_RPATH  FALSE)
      SET(CMAKE_BUILD_WITH_INSTALL_RPATH TRUE)
      SET(CMAKE_INSTALL_RPATH "\${ORIGIN}")
      find_package(OpenSSL REQUIRED)
      message(STATUS "SAPNWRFC_HOME: ${SAPNWRFC_HOME}")
      find_sap_libraries(SAPNWRFC_LIB_FILES ${SAPNWRFC_HOME} "sapnwrfc" "sapucum")      
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
      src/erpl_impl_extension.cpp
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
      ${SAPNWRFC_LIB_OBJECTS}
)

message(STATUS "CMAKE_TOOLCHAIN_FILE: ${CMAKE_TOOLCHAIN_FILE}")
message(STATUS "SAPNWRFC_LIB_FILES: ${SAPNWRFC_LIB_FILES}")
message(STATUS "OPENSSL_LIBRARIES: ${OPENSSL_LIBRARIES}")

# Build loadable extension
build_static_extension(${TARGET_NAME} ${EXTENSION_SOURCES})
build_loadable_extension(${TARGET_NAME} " " ${EXTENSION_SOURCES})

target_link_libraries(${EXTENSION_NAME} ${SAPNWRFC_LIB_FILES} OpenSSL::SSL OpenSSL::Crypto)
target_link_libraries(${LOADABLE_EXTENSION_NAME} ${SAPNWRFC_LIB_FILES} OpenSSL::SSL OpenSSL::Crypto)

if (WIN32)
      target_link_libraries(${EXTENSION_NAME} iphlpapi)
      target_link_libraries(${LOADABLE_EXTENSION_NAME} iphlpapi)
endif()

install(
  TARGETS ${EXTENSION_NAME}
  EXPORT "${DUCKDB_EXPORT_SET}"
  LIBRARY DESTINATION "${INSTALL_LIB_DIR}"
  ARCHIVE DESTINATION "${INSTALL_LIB_DIR}")

# Build trampoline which is used to package extension and its dependencies
# into a single shared library, and then extract them at runtime.
add_subdirectory(trampoline)
