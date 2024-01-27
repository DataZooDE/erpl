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
      message(STATUS "SAPNWRFC_HOME: ${SAPNWRFC_HOME}")
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
    target_compile_options(${target} PRIVATE 
        MP 
        INCREMENTAL
    )

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
   message(STATUS "SAPNWRFC_HOME: ${SAPNWRFC_HOME}")
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

    # Apply compile options to the specified target
    target_compile_options(${target} PRIVATE 
        -Wno-deprecated-declarations
    )

endfunction()

#---------------------------------------------------------------------------------------

function(attach_extension_as_object extension_home extension_name)
      set(OBJ_NAME "${extension_name}.o")
      get_filename_component(OBJ_PATH "${CMAKE_CURRENT_BINARY_DIR}/${OBJ_NAME}" ABSOLUTE)
      get_filename_component(EXT_PATH "${extension_home}/${extension_name}" ABSOLUTE)
      get_filename_component(EXT_DIR "${EXT_PATH}" DIRECTORY)

      add_custom_command(
            OUTPUT "${OBJ_PATH}"
            COMMAND objcopy --input binary --output elf64-x86-64 --binary-architecture i386:x86-64 "${extension_name}" "${OBJ_PATH}"
            DEPENDS "${EXT_PATH}"
            COMMENT "Creating object file ${OBJ_PATH}"
            WORKING_DIRECTORY "${EXT_DIR}"
      )

      set(ERPL_EXTENSION_OBJECTS ${ERPL_EXTENSION_OBJECTS} "${OBJ_NAME}" PARENT_SCOPE)
endfunction()
