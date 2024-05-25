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

#---------------------------------------------------------------------------------------

function(default_win32_libraries)
      set(OPENSSL_USE_STATIC_LIBS TRUE PARENT_SCOPE)
      find_package(OpenSSL REQUIRED)

      # Set SAPNWRFC_HOME and SAPNWRFC_LIB_FILES for use in the parent scope
      get_filename_component(SAPNWRFC_HOME ${CMAKE_CURRENT_SOURCE_DIR}/../nwrfcsdk/win ABSOLUTE)
      #message(STATUS "SAPNWRFC_HOME: ${SAPNWRFC_HOME}")
      find_sap_libraries(SAPNWRFC_LIB_FILES ${SAPNWRFC_HOME} "sapnwrfc" "libsapucum")
      set(SAPNWRFC_HOME ${SAPNWRFC_HOME} PARENT_SCOPE)
      set(SAPNWRFC_LIB_FILES ${SAPNWRFC_LIB_FILES} PARENT_SCOPE)
endfunction()

#--------------------------------------------------------------------------------------

function(default_win32_definitions target)
    # Apply compile definitions to the specified target
    target_compile_definitions(${target} PRIVATE 
        SAPonNT 
        _AFXDLL 
        WIN32 
        _WIN32_WINNT=0x0502 
        WIN64 
        _AMD64_ 
        SAPwithUNICODE 
        UNICODE 
        _UNICODE 
        SAPwithTHREADS 
        SAP_PLATFORM_MAKENAME=ntintel
        SAP_API=
    )

    # Apply compile options to the specified target
    target_compile_options(${target} PRIVATE /MP64)
    set_target_properties(${target} PROPERTIES LINK_FLAGS "/ignore:4217")
    target_link_options(${target} INTERFACE "/ignore:4217")

    # Set variables for use in the parent scope
    set(BUILD_UNITTESTS FALSE PARENT_SCOPE)
endfunction()

#---------------------------------------------------------------------------------------

function(default_linux_libraries)
   # Set variables for use in the parent scope
   set(OPENSSL_USE_STATIC_LIBS TRUE PARENT_SCOPE)
   find_package(OpenSSL REQUIRED)

   # Set SAPNWRFC_HOME and SAPNWRFC_LIB_FILES for use in the parent scope
   get_filename_component(SAPNWRFC_HOME ${CMAKE_CURRENT_SOURCE_DIR}/../nwrfcsdk/linux ABSOLUTE)
   #message(STATUS "SAPNWRFC_HOME: ${SAPNWRFC_HOME}")
   find_sap_libraries(SAPNWRFC_LIB_FILES ${SAPNWRFC_HOME} "sapnwrfc" "sapucum")
   set(SAPNWRFC_HOME ${SAPNWRFC_HOME} PARENT_SCOPE)
   set(SAPNWRFC_LIB_FILES ${SAPNWRFC_LIB_FILES} PARENT_SCOPE)
endfunction()

#---------------------------------------------------------------------------------------

function(default_linux_definitions target)
    # Apply compile definitions to the specified target
    target_compile_definitions(${target} PRIVATE 
        SAPonUNIX 
        SAPonLIN 
        SAPwithUNICODE 
        SAPwithTHREADS
    )

    target_link_options(${target} PRIVATE "LINKER:-rpath,\$ORIGIN")

    # Apply compile options to the specified target
    target_compile_options(${target} PRIVATE 
        -Wno-deprecated-declarations
    )

endfunction()

#---------------------------------------------------------------------------------------

function(attach_extension_as_object extension_file)
    set(OBJ_NAME "${extension_file}.o")
    get_filename_component(EXT_NAME "${extension_file}" NAME_WE)
    get_filename_component(OBJ_PATH "${CMAKE_CURRENT_BINARY_DIR}/${OBJ_NAME}" ABSOLUTE)
    get_filename_component(EXT_DIR "${CMAKE_BINARY_DIR}/extension/${EXT_NAME}" ABSOLUTE)
    get_filename_component(EXT_PATH "${EXT_DIR}/${extension_file}" ABSOLUTE)

    add_custom_command(
        OUTPUT "${OBJ_PATH}"
        COMMAND objcopy --input binary --output elf64-x86-64 --binary-architecture i386:x86-64 "${extension_file}" "${OBJ_PATH}"
        DEPENDS "${EXT_PATH}"
        COMMENT "Creating object file ${OBJ_PATH}"
        WORKING_DIRECTORY "${EXT_DIR}"
    )

    set(ERPL_EXTENSION_OBJECTS ${ERPL_EXTENSION_OBJECTS} "${OBJ_NAME}" PARENT_SCOPE)
endfunction()

#---------------------------------------------------------------------------------------

function (init_resource_file)
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/resources.rc" "")
endfunction()

function(attach_extension_and_dependencies_as_resource extension_file ext_objs)
    set(LOCAL_EXTENSION_OBJECTS ${ext_objs})

    get_filename_component(EXT_NAME "${extension_file}" NAME_WE)
    string(TOUPPER ${EXT_NAME} EXT_NAME_UPPER)
    get_filename_component(EXT_DIR "${CMAKE_BINARY_DIR}/extension/${EXT_NAME}" ABSOLUTE)
    get_filename_component(EXT_PATH "${EXT_DIR}/${extension_file}" ABSOLUTE)
    
    list(FIND LOCAL_EXTENSION_OBJECTS "${EXT_NAME_UPPER}" _index)
    if(${_index} EQUAL -1)
        list(APPEND LOCAL_EXTENSION_OBJECTS "${EXT_NAME_UPPER}")
        file(APPEND "${CMAKE_CURRENT_BINARY_DIR}/resources.rc" "${EXT_NAME_UPPER} RCDATA \"${EXT_PATH}\"\n")
    endif()

    # Add extension libraries as resources to the trampoline file, such that it can be extracted
    file(GLOB EXT_DLL_DEPENDENCIES "${EXT_DIR}/*.dll")
    foreach(DLL ${EXT_DLL_DEPENDENCIES})
        get_filename_component(RC_PATH ${DLL} ABSOLUTE)
        get_filename_component(RC_NAME ${DLL} NAME_WE)

        list(FIND LOCAL_EXTENSION_OBJECTS "${RC_NAME}" _index)
        if(${_index} EQUAL -1)
            list(APPEND LOCAL_EXTENSION_OBJECTS "${RC_NAME}")
            file(APPEND "${CMAKE_CURRENT_BINARY_DIR}/resources.rc" "${RC_NAME} RCDATA \"${RC_PATH}\"\n")
        endif()
    endforeach()

    set(ADDED_EXTENSION_OBJECTS ${LOCAL_EXTENSION_OBJECTS} PARENT_SCOPE)
endfunction()

#---------------------------------------------------------------------------------------

function(attach_vcpkg_dlls_as_resources ext_objs)
    set(LOCAL_EXTENSION_OBJECTS ${ext_objs})
    
    file(GLOB VCPKG_DLL_FILES "${VCPKG_INSTALLED_DIR}/x64-*/bin/*.dll")
    foreach(DLL ${VCPKG_DLL_FILES})
        get_filename_component(RC_PATH ${DLL} ABSOLUTE)
        get_filename_component(RC_NAME ${DLL} NAME_WE)

        list(FIND LOCAL_EXTENSION_OBJECTS "${RC_NAME}" _index)
        if(${_index} EQUAL -1)
            list(APPEND LOCAL_EXTENSION_OBJECTS "${RC_NAME}")
            file(APPEND "${CMAKE_CURRENT_BINARY_DIR}/resources.rc" "${RC_NAME} RCDATA \"${RC_PATH}\"\n")
        endif()
    endforeach()

    set(ADDED_EXTENSION_OBJECTS ${LOCAL_EXTENSION_OBJECTS} PARENT_SCOPE)
endfunction()

#---------------------------------------------------------------------------------------

function(add_yyjson_from_duckdb)
    get_filename_component(yyjson_ext "${PROJECT_SOURCE_DIR}/../duckdb/extension/json/yyjson" REALPATH)
    if(EXISTS "${yyjson_ext}")
        include_directories(../duckdb/extension/json/yyjson/include/)
        add_subdirectory(../duckdb/extension/json/yyjson/)
        message (NOTICE "-- Using yyjson extension from ${yyjson_ext}")
    endif()

    get_filename_component(yyjson_thirdparty "${PROJECT_SOURCE_DIR}/../duckdb/third_party/yyjson" REALPATH)
    if(EXISTS "${yyjson_thirdparty}")
        include_directories(../duckdb/third_party/yyjson/include/)
        add_compile_definitions(DUCKDB_YYJSON_THIRDPARTY)
        message(NOTICE "-- Using yyjson third party from ${yyjson_thirdparty}")
    endif()
endfunction()