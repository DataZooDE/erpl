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

# Try to find libssh2 using CMake config first (works with vcpkg)
find_package(libssh2 CONFIG QUIET)
if(libssh2_FOUND)
    # Use CMake target from vcpkg - this will automatically handle static/shared linking
    set(LIBSSH2_LIBRARIES libssh2::libssh2)
    set(LIBSSH2_INCLUDE_DIRS "")  # CMake targets handle includes automatically
else()
    # Fallback to pkg-config for system installations
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(LIBSSH2 REQUIRED libssh2)
    if(LIBSSH2_FOUND)
        # Prefer static libraries when available
        if(LIBSSH2_STATIC_LIBRARIES)
            set(LIBSSH2_LIBRARIES ${LIBSSH2_STATIC_LIBRARIES})
            set(LIBSSH2_LIBRARY_DIRS ${LIBSSH2_STATIC_LIBRARY_DIRS})
            set(LIBSSH2_LDFLAGS ${LIBSSH2_STATIC_LDFLAGS})
        endif()
    endif()
endif()


include_directories(src/include
                    ../rfc/src/include
                    ${OPENSSL_INCLUDE_DIR}
                    ${LIBSSH2_INCLUDE_DIRS}
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

# Add library directories for static linking (only needed for pkg-config)
if(NOT libssh2_FOUND AND LIBSSH2_LIBRARY_DIRS)
    link_directories(${LIBSSH2_LIBRARY_DIRS})
endif()

target_link_libraries(${EXTENSION_NAME} erpl_rfc_extension ${OPENSSL_LIBRARIES} ${LIBSSH2_LIBRARIES})
target_link_libraries(${LOADABLE_EXTENSION_NAME} erpl_rfc_extension ${OPENSSL_LIBRARIES} ${LIBSSH2_LIBRARIES})

if(WIN32)
    target_link_libraries(${EXTENSION_NAME} ws2_32)
    target_link_libraries(${LOADABLE_EXTENSION_NAME} ws2_32)
endif()

# Add additional linker flags for static linking if needed (only for pkg-config)
if(NOT libssh2_FOUND AND LIBSSH2_LDFLAGS)
    target_link_options(${EXTENSION_NAME} PRIVATE ${LIBSSH2_LDFLAGS})
    target_link_options(${LOADABLE_EXTENSION_NAME} PRIVATE ${LIBSSH2_LDFLAGS})
endif()

if(${BUILD_UNITTESTS})
      add_subdirectory(test)
endif()

install(
  TARGETS ${EXTENSION_NAME} ${LOADABLE_EXTENSION_NAME}
  EXPORT "${DUCKDB_EXPORT_SET}"
  LIBRARY DESTINATION "${INSTALL_LIB_DIR}"
  ARCHIVE DESTINATION "${INSTALL_LIB_DIR}")
