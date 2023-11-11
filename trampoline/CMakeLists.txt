cmake_minimum_required(VERSION 3.5)

set(TARGET_NAME erpl_trampoline)
set(EXTENSION_NAME ${TARGET_NAME}_extension)
set(LOADABLE_EXTENSION_NAME ${TARGET_NAME}_loadable_extension)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

project(${TARGET_NAME})

if(UNIX AND NOT APPLE)
    find_sap_libraries(SAPNWRFC_LIB_FILES ${SAPNWRFC_HOME} "sapnwrfc" "sapucum")

    # Attach the SAP libraries as objects to the trampoline file, such that it can be extracted
    foreach(LIB ${SAPNWRFC_LIB_FILES})
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

    # Attach the erpl.duckdb_extension file as an object to the trampoline file, such that it can be extracted
    set(OBJ_PATH "${CMAKE_CURRENT_BINARY_DIR}/erpl.duckdb_extension.o")
    get_filename_component(ERPL_EXT_PATH "${CMAKE_CURRENT_BINARY_DIR}/../erpl.duckdb_extension" ABSOLUTE)
    get_filename_component(ERPL_EXT_NAME "${ERPL_EXT_PATH}" NAME)
    get_filename_component(ERPL_EXT_DIR "${ERPL_EXT_PATH}" DIRECTORY)
    add_custom_command(
        OUTPUT "${OBJ_PATH}"
        COMMAND objcopy --input binary --output elf64-x86-64 --binary-architecture i386:x86-64 "${ERPL_EXT_NAME}" "${OBJ_PATH}"
        DEPENDS "${ERPL_EXT_PATH}"
        COMMENT "Creating object file ${CMAKE_CURRENT_BINARY_DIR}/erpl.duckdb_extension.o"
        WORKING_DIRECTORY "${ERPL_EXT_DIR}"
    )

    set(ERPL_EXTENSION_OBJECTS "erpl.duckdb_extension.o")
endif()

if(WIN32)
    find_sap_libraries(SAPNWRFC_LIB_FILES ${SAPNWRFC_HOME} "sapnwrfc" "libsapucum")

    foreach(LIB ${SAPNWRFC_LIB_FILES})
        get_filename_component(RC_NAME ${LIB} NAME_WE)
        file(APPEND "${CMAKE_CURRENT_BINARY_DIR}/resources.rc" "RC_NAME RCDATA \"${LIB}\"\n")
    endforeach()

    set(SAPNWRFC_LIB_OBJECTS "resources.rc")
endif()

include_directories(src/include)

message(STATUS "SAPNWRFC_LIB_OBJECTS: ${SAPNWRFC_LIB_OBJECTS}")

set(EXTENSION_SOURCES
    src/erpl_trampoline_extension.cpp
    ${SAPNWRFC_LIB_OBJECTS}
    ${ERPL_EXTENSION_OBJECTS}
)

build_static_extension(${TARGET_NAME} ${EXTENSION_SOURCES})
build_loadable_extension(${TARGET_NAME} " " ${EXTENSION_SOURCES})

install(
  TARGETS ${EXTENSION_NAME}
  EXPORT "${DUCKDB_EXPORT_SET}"
  LIBRARY DESTINATION "${INSTALL_LIB_DIR}"
  ARCHIVE DESTINATION "${INSTALL_LIB_DIR}")