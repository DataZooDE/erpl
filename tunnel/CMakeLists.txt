option(BUILD_UNITTESTS "Build ERPL C++ Unit Tests." OFF)

# Set extension name here
set(TARGET_NAME erpl_tunnel)
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

# Try to find libssh2 from vcpkg first, then system
find_package(libssh2 QUIET)
if(NOT libssh2_FOUND)
    # Try pkg-config for system libssh2
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(PC_LIBSSH2 QUIET libssh2)
    endif()
    
    find_library(LIBSSH2_LIBRARY NAMES ssh2 libssh2
                 HINTS ${PC_LIBSSH2_LIBRARY_DIRS}
                 PATHS ${PC_LIBSSH2_LIBDIR})
    find_path(LIBSSH2_INCLUDE_DIR NAMES libssh2.h
              HINTS ${PC_LIBSSH2_INCLUDE_DIRS}
              PATHS ${PC_LIBSSH2_INCLUDEDIR})
    
    if(LIBSSH2_LIBRARY AND LIBSSH2_INCLUDE_DIR)
        set(libssh2_FOUND TRUE)
        set(libssh2_LIBRARIES ${LIBSSH2_LIBRARY})
        set(libssh2_INCLUDE_DIRS ${LIBSSH2_INCLUDE_DIR})
        message(STATUS "Found system libssh2: ${LIBSSH2_LIBRARY}")
    else()
        message(FATAL_ERROR "libssh2 not found. Please install libssh2 development package.")
    endif()
endif()

include_directories(src/include
                    ../rfc/src/include
                    ${OPENSSL_INCLUDE_DIR}
                    ${libssh2_INCLUDE_DIRS}
)

add_yyjson_from_duckdb()

set(EXTENSION_SOURCES
      src/tunnel_extension.cpp
      src/tunnel_connection.cpp
      src/tunnel_secret.cpp
      src/tunnel_manager.cpp
      src/pragma_tunnel_create.cpp
      src/pragma_tunnel_close.cpp
      src/pragma_tunnel_close_all.cpp
      src/scanner_tunnels.cpp
      ${YYJSON_OBJECT_FILES}
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

target_link_libraries(${EXTENSION_NAME} erpl_rfc_extension ${OPENSSL_LIBRARIES} ${libssh2_LIBRARIES})
target_link_libraries(${LOADABLE_EXTENSION_NAME} erpl_rfc_extension ${OPENSSL_LIBRARIES} ${libssh2_LIBRARIES})

if(${BUILD_UNITTESTS})
      add_subdirectory(test)
endif()

install(
  TARGETS ${EXTENSION_NAME} ${LOADABLE_EXTENSION_NAME}
  EXPORT "${DUCKDB_EXPORT_SET}"
  LIBRARY DESTINATION "${INSTALL_LIB_DIR}"
  ARCHIVE DESTINATION "${INSTALL_LIB_DIR}")
