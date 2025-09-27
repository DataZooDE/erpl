set(TARGET_NAME erpl)
set(EXTENSION_NAME ${TARGET_NAME}_extension)
set(LOADABLE_EXTENSION_NAME ${TARGET_NAME}_loadable_extension)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

project(${TARGET_NAME})

include(../scripts/functions.cmake)
add_duckdb_version_definition()

if(WIN32)
    # Attach the erpl_rfc.duckdb_extension file as a resource to the trampoline file
    # (we call this erpl to not confuse the user),
    # such that it can be extracted
    init_resource_file()
    attach_extension_and_dependencies_as_resource("erpl_rfc.duckdb_extension" "${ADDED_EXTENSION_OBJECTS}")
    attach_extension_and_dependencies_as_resource("erpl_tunnel.duckdb_extension" "${ADDED_EXTENSION_OBJECTS}")

    list(FIND DUCKDB_EXTENSION_NAMES "erpl_bics" _index)
    if(NOT ${_index} EQUAL -1)
        message(STATUS "Attaching erpl_bics.duckdb_extension")
        attach_extension_and_dependencies_as_resource("erpl_bics.duckdb_extension" "${ADDED_EXTENSION_OBJECTS}")
    endif()

    list(FIND DUCKDB_EXTENSION_NAMES "erpl_odp" _index)
    if(NOT ${_index} EQUAL -1)
        message(STATUS "Attaching erpl_odp.duckdb_extension")
        attach_extension_and_dependencies_as_resource("erpl_odp.duckdb_extension" "${ADDED_EXTENSION_OBJECTS}")
    endif()

    # Add vcpkg libraries as resources to the trampoline file, such that it can be extracted
    message(STATUS "Attaching vcpkg libraries")
    attach_vcpkg_dlls_as_resources("${ADDED_EXTENSION_OBJECTS}")

    # Add the SAP libraries as resources to the trampoline file, such that it can be extracted
    get_filename_component(SAPNWRFC_HOME ${CMAKE_CURRENT_SOURCE_DIR}/../nwrfcsdk/win ABSOLUTE)
    file(GLOB SAPNWRFC_DLL_FILES "${SAPNWRFC_HOME}/lib/*.dll")
    foreach(DLL ${SAPNWRFC_DLL_FILES})
        get_filename_component(RC_PATH ${DLL} ABSOLUTE)
        get_filename_component(RC_NAME ${DLL} NAME_WE)
        file(APPEND "${CMAKE_CURRENT_BINARY_DIR}/resources.rc" "${RC_NAME} RCDATA \"${RC_PATH}\"\n")
    endforeach()

    set(SAPNWRFC_LIB_OBJECTS "resources.rc")

    file(READ "${CMAKE_CURRENT_BINARY_DIR}/resources.rc" SAPNWRFC_LIB_OBJECTS_CONTENTS)
    message(STATUS "${CMAKE_CURRENT_BINARY_DIR}/resources.rc:\n${SAPNWRFC_LIB_OBJECTS_CONTENTS}")
endif()

if(UNIX AND NOT APPLE)
    # Attach the erpl_XXX.duckdb_extension files as an object to the trampoline file 
    # (we call this erpl to not confuse the user), such that it can be extracted
    attach_extension_as_object("erpl_rfc.duckdb_extension")
    attach_extension_as_object("erpl_tunnel.duckdb_extension")

    list(FIND DUCKDB_EXTENSION_NAMES "erpl_bics" _index)
    if(NOT ${_index} EQUAL -1)
        message(STATUS "Attaching erpl_bics.duckdb_extension")
        attach_extension_as_object("erpl_bics.duckdb_extension")
    endif()
    
    list(FIND DUCKDB_EXTENSION_NAMES "erpl_odp" _index)
    if(NOT ${_index} EQUAL -1)
        message(STATUS "Attaching erpl_odp.duckdb_extension")
        attach_extension_as_object("erpl_odp.duckdb_extension")
    endif()

    message(STATUS "ERPL_EXTENSION_OBJECTS: ${ERPL_EXTENSION_OBJECTS}")

    # Attach the SAP libraries as objects to the trampoline file, such that it can be extracted
    get_filename_component(SAPNWRFC_HOME ${CMAKE_CURRENT_SOURCE_DIR}/../nwrfcsdk/linux ABSOLUTE)
    file(GLOB SAPNWRFC_SO_FILES "${SAPNWRFC_HOME}/lib/*.so*")
    foreach(LIB ${SAPNWRFC_SO_FILES})
        get_filename_component(LIB_DIR ${LIB} DIRECTORY)
        get_filename_component(LIB_NAME ${LIB} NAME)
        get_filename_component(OBJ_NAME ${LIB} NAME_WE)
        file(RELATIVE_PATH REL_LIB ${CMAKE_CURRENT_SOURCE_DIR} ${LIB})

        list(APPEND SAPNWRFC_LIB_OBJECTS "${OBJ_NAME}.o")

        embed_binary_to_object("${LIB_DIR}" "${LIB_NAME}" "${CMAKE_CURRENT_BINARY_DIR}/${OBJ_NAME}.o" "${LIB}")
    endforeach()
endif()

if(UNIX AND APPLE)
    # Attach the erpl_XXX.duckdb_extension files as an object to the trampoline file 
    # (we call this erpl to not confuse the user), such that it can be extracted
    attach_extension_as_object("erpl_rfc.duckdb_extension")
    attach_extension_as_object("erpl_tunnel.duckdb_extension")

    list(FIND DUCKDB_EXTENSION_NAMES "erpl_bics" _index)
    if(NOT ${_index} EQUAL -1)
        message(STATUS "Attaching erpl_bics.duckdb_extension")
        attach_extension_as_object("erpl_bics.duckdb_extension")
    endif()
    
    list(FIND DUCKDB_EXTENSION_NAMES "erpl_odp" _index)
    if(NOT ${_index} EQUAL -1)
        message(STATUS "Attaching erpl_odp.duckdb_extension")
        attach_extension_as_object("erpl_odp.duckdb_extension")
    endif()

    message(STATUS "ERPL_EXTENSION_OBJECTS: ${ERPL_EXTENSION_OBJECTS}")

    # Attach the SAP libraries as objects to the trampoline file, such that it can be extracted
    if("${OSX_BUILD_ARCH}" STREQUAL "arm64")
        get_filename_component(SAPNWRFC_HOME ${CMAKE_CURRENT_SOURCE_DIR}/../nwrfcsdk/osx_arm ABSOLUTE)
    else()
        get_filename_component(SAPNWRFC_HOME ${CMAKE_CURRENT_SOURCE_DIR}/../nwrfcsdk/osx_amd64 ABSOLUTE)
    endif()

    file(GLOB SAPNWRFC_DYLIB_FILES "${SAPNWRFC_HOME}/lib/*.dylib")
    foreach(LIB ${SAPNWRFC_DYLIB_FILES})
        get_filename_component(LIB_DIR ${LIB} DIRECTORY)
        get_filename_component(LIB_NAME ${LIB} NAME)
        get_filename_component(OBJ_NAME ${LIB} NAME_WE)
        file(RELATIVE_PATH REL_LIB ${CMAKE_CURRENT_SOURCE_DIR} ${LIB})

        list(APPEND SAPNWRFC_LIB_OBJECTS "${OBJ_NAME}.o")

        convert_dylib_to_object("${OBJ_NAME}" "${LIB}" "${OBJ_NAME}.o" "${CMAKE_CURRENT_BINARY_DIR}/${OBJ_NAME}.o" ON)
    endforeach()
endif()

include_directories(src/include)

message(STATUS "SAPNWRFC_LIB_OBJECTS: ${SAPNWRFC_LIB_OBJECTS}")

set(EXTENSION_SOURCES
    src/erpl_extension.cpp
    ${SAPNWRFC_LIB_OBJECTS}
    ${ERPL_EXTENSION_OBJECTS}
)

build_static_extension(${TARGET_NAME} ${EXTENSION_SOURCES})
set(PARAMETERS "-warnings")
build_loadable_extension(${TARGET_NAME} " " ${EXTENSION_SOURCES})

add_dependencies(${EXTENSION_NAME} erpl_rfc_loadable_extension)
add_dependencies(${LOADABLE_EXTENSION_NAME} erpl_rfc_loadable_extension)

add_dependencies(${EXTENSION_NAME} erpl_tunnel_loadable_extension)
add_dependencies(${LOADABLE_EXTENSION_NAME} erpl_tunnel_loadable_extension)

list(FIND DUCKDB_EXTENSION_NAMES "erpl_bics" _index)
if(NOT ${_index} EQUAL -1)
    add_compile_definitions(WITH_ERPL_BICS)
    add_dependencies(${EXTENSION_NAME} erpl_bics_loadable_extension)
    add_dependencies(${LOADABLE_EXTENSION_NAME} erpl_bics_loadable_extension)
endif()

list(FIND DUCKDB_EXTENSION_NAMES "erpl_odp" _index)
if(NOT ${_index} EQUAL -1)
    add_compile_definitions(WITH_ERPL_ODP)
    add_dependencies(${EXTENSION_NAME} erpl_odp_loadable_extension)
    add_dependencies(${LOADABLE_EXTENSION_NAME} erpl_odp_loadable_extension)
endif()

if(WIN32)
      default_win32_definitions(${EXTENSION_NAME})
      default_win32_definitions(${LOADABLE_EXTENSION_NAME})
endif()

if(UNIX AND NOT APPLE)
      default_linux_definitions(${EXTENSION_NAME})
      default_linux_definitions(${LOADABLE_EXTENSION_NAME})
endif()

target_link_libraries(${EXTENSION_NAME})
target_link_libraries(${LOADABLE_EXTENSION_NAME})