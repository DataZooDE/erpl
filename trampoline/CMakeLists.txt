set(TARGET_NAME erpl)
set(EXTENSION_NAME ${TARGET_NAME}_extension)
set(LOADABLE_EXTENSION_NAME ${TARGET_NAME}_loadable_extension)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

project(${TARGET_NAME})

include(../scripts/functions.cmake)

if(WIN32)
    add_definitions(-MP -INCREMENTAL)
    get_filename_component(SAPNWRFC_HOME ${CMAKE_CURRENT_SOURCE_DIR}/../nwrfcsdk/win ABSOLUTE)
    IF(CMAKE_BUILD_TYPE MATCHES Debug)
        get_filename_component(ERPL_RFC_HOME ${CMAKE_CURRENT_SOURCE_DIR}/../build/debug/ERPL_RFC/extension/erpl_rfc ABSOLUTE)
    ELSE()
        get_filename_component(ERPL_RFC_HOME ${CMAKE_CURRENT_SOURCE_DIR}/../build/release/ERPL_RFC/extension/erpl_rfc ABSOLUTE)
    ENDIF(CMAKE_BUILD_TYPE MATCHES Debug)

    # Attach the erpl_rfc.duckdb_extension file as a resource to the trampoline file
    # (we call this erpl to not confuse the user),
    # such that it can be extracted
    get_filename_component(ERPL_EXT_PATH "${ERPL_RFC_HOME}/erpl_rfc.duckdb_extension" ABSOLUTE)
    get_filename_component(ERPL_EXT_NAME "${ERPL_EXT_PATH}" NAME)
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/resources.rc" "ERPL_RFC RCDATA \"${ERPL_EXT_PATH}\"\n")

    # Add SSL libraries as resources to the trampoline file, such that it can be extracted
    file(GLOB ERPL_DLL_DEPENDENCIES "${ERPL_RFC_HOME}/../../vcpkg_installed/x64-windows/bin/*.dll")
    foreach(DLL ${ERPL_DLL_DEPENDENCIES})
        get_filename_component(RC_PATH ${DLL} ABSOLUTE)
        get_filename_component(RC_NAME ${DLL} NAME_WE)
        file(APPEND "${CMAKE_CURRENT_BINARY_DIR}/resources.rc" "${RC_NAME} RCDATA \"${RC_PATH}\"\n")
    endforeach()

    # Add the SAP libraries as resources to the trampoline file, such that it can be extracted
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
    if ("erpl_bics" IN_LIST DUCKDB_EXTENSION_NAMES)
        message(STATUS "Attaching erpl_bics.duckdb_extension")
        attach_extension_as_object("erpl_bics.duckdb_extension")
    endif()
    
    if ("erpl_odp" IN_LIST DUCKDB_EXTENSION_NAMES)
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

        add_custom_command(
            OUTPUT "${OBJ_NAME}.o"
            COMMAND objcopy --input binary --output elf64-x86-64 --binary-architecture i386:x86-64 "${LIB_NAME}" "${CMAKE_CURRENT_BINARY_DIR}/${OBJ_NAME}.o"
            DEPENDS "${LIB}"
            COMMENT "Creating object file ${CMAKE_CURRENT_BINARY_DIR}/${OBJ_NAME}.o"
            WORKING_DIRECTORY "${LIB_DIR}"
        )
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

if ("erpl_bics" IN_LIST DUCKDB_EXTENSION_NAMES)
    add_compile_definitions(WITH_ERPL_BICS)
    add_dependencies(${EXTENSION_NAME} erpl_bics_loadable_extension)
    add_dependencies(${LOADABLE_EXTENSION_NAME} erpl_bics_loadable_extension)
endif()

if ("erpl_odp" IN_LIST DUCKDB_EXTENSION_NAMES)
    add_compile_definitions(WITH_ERPL_ODP)
    add_dependencies(${EXTENSION_NAME} erpl_odp_loadable_extension)
    add_dependencies(${LOADABLE_EXTENSION_NAME} erpl_odp_loadable_extension)
endif()

target_link_libraries(${EXTENSION_NAME})
target_link_libraries(${LOADABLE_EXTENSION_NAME})