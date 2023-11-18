set(TARGET_NAME erpl)
set(EXTENSION_NAME ${TARGET_NAME}_extension)
set(LOADABLE_EXTENSION_NAME ${TARGET_NAME}_loadable_extension)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

project(${TARGET_NAME})

if(WIN32)
    add_definitions(-MP -INCREMENTAL)
    get_filename_component(SAPNWRFC_HOME ${CMAKE_CURRENT_SOURCE_DIR}/../nwrfcsdk/win ABSOLUTE)
    IF(CMAKE_BUILD_TYPE MATCHES DEBUG)
        get_filename_component(ERPL_IMPL_HOME ${CMAKE_CURRENT_SOURCE_DIR}/../build/debug/ERPL_IMPL/extension/erpl_impl ABSOLUTE)
    ELSE()
        get_filename_component(ERPL_IMPL_HOME ${CMAKE_CURRENT_SOURCE_DIR}/../build/release/ERPL_IMPL/extension/erpl_impl ABSOLUTE)
    ENDIF(CMAKE_BUILD_TYPE MATCHES DEBUG)

    # Attach the erpl_impl.duckdb_extension file as a resource to the trampoline file
    # (we call this erpl to not confuse the user),
    # such that it can be extracted
    get_filename_component(ERPL_EXT_PATH "${ERPL_IMPL_HOME}/erpl_impl.duckdb_extension" ABSOLUTE)
    get_filename_component(ERPL_EXT_NAME "${ERPL_EXT_PATH}" NAME)
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/resources.rc" "ERPL_IMPL RCDATA \"${ERPL_EXT_PATH}\"\n")

    # Add SSL libraries as resources to the trampoline file, such that it can be extracted
    file(GLOB ERPL_DLL_DEPENDENCIES "${ERPL_IMPL_HOME}/../../vcpkg_installed/x64-windows/bin/*.dll")
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
    get_filename_component(SAPNWRFC_HOME ${CMAKE_CURRENT_SOURCE_DIR}/../nwrfcsdk/linux ABSOLUTE)
    IF(CMAKE_BUILD_TYPE MATCHES DEBUG)
        get_filename_component(ERPL_IMPL_HOME ${CMAKE_CURRENT_SOURCE_DIR}/../build/debug/ERPL_IMPL/extension/erpl_impl ABSOLUTE)
    ELSE()
        get_filename_component(ERPL_IMPL_HOME ${CMAKE_CURRENT_SOURCE_DIR}/../build/release/ERPL_IMPL/extension/erpl_impl ABSOLUTE)
    ENDIF(CMAKE_BUILD_TYPE MATCHES DEBUG)

    # Attach the erpl_impl.duckdb_extension file as an object to the trampoline file 
    # (we call this erpl to not confuse the user), 
    # such that it can be extracted
    get_filename_component(OBJ_PATH "${CMAKE_CURRENT_BINARY_DIR}/erpl_impl.duckdb_extension.o" ABSOLUTE)
    get_filename_component(ERPL_IMPL_EXT_PATH "${ERPL_IMPL_HOME}/erpl_impl.duckdb_extension" ABSOLUTE)
    get_filename_component(ERPL_IMPL_EXT_NAME "${ERPL_IMPL_EXT_PATH}" NAME)
    get_filename_component(ERPL_IMPL_EXT_DIR "${ERPL_IMPL_EXT_PATH}" DIRECTORY)
    add_custom_command(
        OUTPUT "${OBJ_PATH}"
        COMMAND objcopy --input binary --output elf64-x86-64 --binary-architecture i386:x86-64 "${ERPL_IMPL_EXT_NAME}" "${OBJ_PATH}"
        DEPENDS "${ERPL_IMPL_EXT_PATH}"
        COMMENT "Creating object file ${CMAKE_CURRENT_BINARY_DIR}/erpl_impl.duckdb_extension.o"
        WORKING_DIRECTORY "${ERPL_IMPL_EXT_DIR}"
    )
    set(ERPL_EXTENSION_OBJECTS "erpl_impl.duckdb_extension.o")

    # Attach the SAP libraries as objects to the trampoline file, such that it can be extracted
    file(GLOB SAPNWRFC_SO_FILES "${SAPNWRFC_HOME}/lib/*.so")
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

#build_static_extension(${TARGET_NAME} ${EXTENSION_SOURCES})
build_loadable_extension(${TARGET_NAME} " " ${EXTENSION_SOURCES})
target_link_libraries(${LOADABLE_EXTENSION_NAME})