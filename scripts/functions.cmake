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
        _WIN32_WINNT=_WIN32_WINNT_WIN10
        NTDDI_VERSION=NTDDI_WIN10
        WIN64 
        _AMD64_ 
        SAPwithUNICODE 
        UNICODE 
        _UNICODE 
        SAPwithTHREADS 
        SAP_PLATFORM_MAKENAME=ntintel
        SAP_API=
    )

    # Apply flags to disable specific warnings for this target
    if (MSVC)
        target_compile_options(${target} PRIVATE /MP64)
        set_target_properties(${target} PROPERTIES LINK_FLAGS "/ignore:4217")
        target_link_options(${target} INTERFACE "/ignore:4217")
    endif()

    if (MINGW)
        target_compile_options(${target} PRIVATE -Wno-attributes -Wno-deprecated-declarations)
    endif()
    
    # Apply compile options to the specified target
    

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

function(default_osx_libraries)
   # Set variables for use in the parent scope
   set(OPENSSL_USE_STATIC_LIBS TRUE PARENT_SCOPE)
   find_package(OpenSSL REQUIRED)

   # Set SAPNWRFC_HOME and SAPNWRFC_LIB_FILES for use in the parent scope
   if("${OSX_BUILD_ARCH}" STREQUAL "arm64")
        get_filename_component(SAPNWRFC_HOME ${CMAKE_CURRENT_SOURCE_DIR}/../nwrfcsdk/osx_arm ABSOLUTE)
   else()
        get_filename_component(SAPNWRFC_HOME ${CMAKE_CURRENT_SOURCE_DIR}/../nwrfcsdk/osx_amd64 ABSOLUTE)
   endif()
   
   #message(STATUS "SAPNWRFC_HOME: ${SAPNWRFC_HOME}")
   find_sap_libraries(SAPNWRFC_LIB_FILES ${SAPNWRFC_HOME} "sapnwrfc" "sapucum")
   set(SAPNWRFC_HOME ${SAPNWRFC_HOME} PARENT_SCOPE)
   set(SAPNWRFC_LIB_FILES ${SAPNWRFC_LIB_FILES} PARENT_SCOPE)
endfunction()

#---------------------------------------------------------------------------------------

function(default_osx_definitions target)
    # Apply compile definitions to the specified target
    target_compile_definitions(${target} PRIVATE 
        SAPonDARW 
        SAPwithUNICODE 
        SAPwithTHREADS
    )

    target_link_options(${target} PRIVATE "LINKER:-rpath,@loader_path")

    # Apply compile options to the specified target
    target_compile_options(${target} PRIVATE 
        -Wno-deprecated-declarations
    )

    set_target_properties(${target} PROPERTIES XCODE_ATTRIBUTE_LD_RUNPATH_SEARCH_PATHS "@executable_path")
endfunction()

#---------------------------------------------------------------------------------------

function(convert_dylib_to_object lib_name lib_path obj_name obj_path change_rpath)
    set(HEADER_PATH "${CMAKE_CURRENT_BINARY_DIR}/${lib_name}.h")
    set(SOURCE_PATH "${CMAKE_CURRENT_BINARY_DIR}/${lib_name}.c")
    get_filename_component(LIB_DIR "${lib_path}" DIRECTORY)
    get_filename_component(LIB_FILE "${lib_path}" NAME)

    # Step 1: Convert the dylib to a C array
    if (change_rpath)
        add_custom_command(
            OUTPUT "${HEADER_PATH}"
            COMMAND install_name_tool -id @rpath/${LIB_FILE} ${LIB_FILE}
            COMMAND xxd -i ${LIB_FILE} ${HEADER_PATH}
            DEPENDS "${lib_path}"
            COMMENT "Converting ${lib_path} to C header ${HEADER_PATH}"
            WORKING_DIRECTORY "${LIB_DIR}"
        )
    else()
        add_custom_command(
            OUTPUT "${HEADER_PATH}"
            COMMAND xxd -i ${LIB_FILE} ${HEADER_PATH}
            DEPENDS "${lib_path}"
            COMMENT "Converting ${lib_path} to C header ${HEADER_PATH}"
            WORKING_DIRECTORY "${LIB_DIR}"
        )
    endif()
    
    # Step 2: Create a C source file that includes the generated header
    file(WRITE "${SOURCE_PATH}" "#include \"${HEADER_PATH}\"")

    # Step 3: Compile the source file to an object file

    if("${OSX_BUILD_ARCH}" STREQUAL "x86_64")
        set(ARCH_FLAG "x86_64")
    else()
        set(ARCH_FLAG "arm64")
    endif()

    add_custom_command(
        OUTPUT "${obj_name}"
        COMMAND clang -arch "${ARCH_FLAG}" -c "${SOURCE_PATH}" -o "${obj_path}"
        DEPENDS "${SOURCE_PATH}" "${HEADER_PATH}"
        COMMENT "Compiling ${SOURCE_PATH} to object file ${obj_path}"
        WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    )
endfunction()

#---------------------------------------------------------------------------------------

function(embed_binary_to_object WORKING_DIR INPUT_NAME OUTPUT_PATH DEPENDS_PATH)
    add_custom_command(
        OUTPUT "${OUTPUT_PATH}"
        COMMAND objcopy -I binary -O elf64-x86-64 --binary-architecture i386:x86-64 "${INPUT_NAME}" "${OUTPUT_PATH}"
        DEPENDS "${DEPENDS_PATH}"
        COMMENT "Creating object file ${OUTPUT_PATH}"
        WORKING_DIRECTORY "${WORKING_DIR}"
    )
endfunction()

#---------------------------------------------------------------------------------------

function(attach_extension_as_object extension_file)
    set(OBJ_NAME "${extension_file}.o")
    get_filename_component(EXT_NAME "${extension_file}" NAME_WE)
    get_filename_component(OBJ_PATH "${CMAKE_CURRENT_BINARY_DIR}/${OBJ_NAME}" ABSOLUTE)
    get_filename_component(EXT_DIR "${CMAKE_BINARY_DIR}/extension/${EXT_NAME}" ABSOLUTE)
    get_filename_component(EXT_PATH "${EXT_DIR}/${extension_file}" ABSOLUTE)

    if(UNIX AND APPLE)
        convert_dylib_to_object("${EXT_NAME}" "${EXT_PATH}" "${OBJ_NAME}" "${OBJ_PATH}" OFF)
    else()
        embed_binary_to_object("${EXT_DIR}" "${extension_file}" "${OBJ_PATH}" "${EXT_PATH}")
    endif()

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

        if (NOT TARGET yyjson_added)
            # Mark the directory as added
            add_subdirectory(../duckdb/extension/json/yyjson/ ../build/yyjson)
            add_library(yyjson_added INTERFACE)
        endif()
        
        message (NOTICE "-- Using yyjson extension from ${yyjson_ext}")
    endif()

    get_filename_component(yyjson_thirdparty "${PROJECT_SOURCE_DIR}/../duckdb/third_party/yyjson" REALPATH)
    if(EXISTS "${yyjson_thirdparty}")
        include_directories(../duckdb/third_party/yyjson/include/)
        add_compile_definitions(DUCKDB_YYJSON_THIRDPARTY)
        message(NOTICE "-- Using yyjson third party from ${yyjson_thirdparty}")
    endif()
endfunction()

#---------------------------------------------------------------------------------------

function(add_duckdb_version_definition)
    add_compile_definitions(DUCKDB_MAJOR_VERSION=${DUCKDB_MAJOR_VERSION})
    add_compile_definitions(DUCKDB_MINOR_VERSION=${DUCKDB_MINOR_VERSION})
    add_compile_definitions(DUCKDB_PATCH_VERSION=${DUCKDB_PATCH_VERSION})
endfunction()


#---------------------------------------------------------------------------------------

# Builds the erpl-proto nwrfc ABI shim -- a pure-Rust libsapnwrfc replacement -- and stages
# it in the build tree as the alternative RFC backend. See ERPL_PROTO_INTEGRATION_PLAN.md.
#
# erpl-proto lives in a private repository, so `proto/` is absent from clones without
# access. That is not an error: the build then simply offers the nwrfc backend only, exactly
# as it did before this existed. Same conditional pattern as `bics/` and `odp/`.
#
# The artifact is deliberately RENAMED on the way in. Cargo names it `libsapnwrfc.so`,
# because an SDK consumer that links `-lsapnwrfc` has to resolve it by that name -- but in
# this build the two libraries coexist and are chosen between at runtime, so a second file
# called `libsapnwrfc.so` in the build tree is a loaded gun. Under its own name it can only
# ever be loaded deliberately, by absolute path.
#
# Sets ERPL_PROTO_AVAILABLE and ERPL_PROTO_BACKEND_LIB in the caller's scope.
function(add_erpl_proto_backend)
    set(ERPL_PROTO_AVAILABLE FALSE PARENT_SCOPE)

    get_filename_component(proto_dir "${CMAKE_CURRENT_SOURCE_DIR}/../proto" ABSOLUTE)
    if(NOT EXISTS "${proto_dir}/Cargo.toml")
        message(STATUS "erpl-proto not present at ${proto_dir} -- building with the nwrfc backend only")
        return()
    endif()

    find_program(CARGO_EXECUTABLE cargo)
    if(NOT CARGO_EXECUTABLE)
        message(WARNING
            "erpl-proto is present at ${proto_dir} but `cargo` was not found, so the proto "
            "backend will not be built. Install a Rust toolchain, or remove the submodule.")
        return()
    endif()

    # A dedicated target directory: sharing the submodule's own `target/` would have this
    # build contend with cargo's lock against interactive work in erpl-proto.
    set(cargo_target_dir "${CMAKE_BINARY_DIR}/erpl-proto-target")

    if(WIN32)
        set(cargo_artifact "sapnwrfc.dll")
        set(staged_name "erpl_proto_nwrfc.dll")
    elseif(APPLE)
        set(cargo_artifact "libsapnwrfc.dylib")
        set(staged_name "liberpl_proto_nwrfc.dylib")
    else()
        set(cargo_artifact "libsapnwrfc.so")
        set(staged_name "liberpl_proto_nwrfc.so")
    endif()

    # Always a release build. This is a dependency, not code under debug here, and a debug
    # cdylib of the shim is ~30x the size for no diagnostic benefit on the erpl side.
    set(cargo_artifact_path "${cargo_target_dir}/release/${cargo_artifact}")
    set(staged_path "${CMAKE_BINARY_DIR}/${staged_name}")

    # Cargo decides whether anything needs rebuilding, so this target always runs and
    # no-ops in a fraction of a second when the shim is current.
    #
    # The obvious alternative -- a file(GLOB_RECURSE ... CONFIGURE_DEPENDS) over the Rust
    # sources feeding an OUTPUT-producing command -- is worse in two ways, and was tried
    # first. CONFIGURE_DEPENDS makes CMake re-verify the glob on *every* ninja invocation,
    # which costs a full re-configure of this (large) project before any build can start.
    # And a glob would have to enumerate what cargo already tracks far better: not just
    # .rs files and manifests but Cargo.lock, build scripts, `include_str!` data, and the
    # toolchain file. Duplicating a build system's change detection in another build
    # system is how the two come to disagree.
    #
    # copy_if_different is what keeps this cheap for dependents: the staged file's
    # timestamp only moves when the bytes actually change, so an unchanged shim does not
    # drag the trampoline through a relink of a ~290 MB artifact.
    add_custom_target(erpl_proto_backend ALL
        COMMAND ${CMAKE_COMMAND} -E env "CARGO_TARGET_DIR=${cargo_target_dir}"
                ${CARGO_EXECUTABLE} build --release --manifest-path "${proto_dir}/Cargo.toml"
                -p erpl-proto-nwrfc
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${cargo_artifact_path}" "${staged_path}"
        BYPRODUCTS "${staged_path}"
        COMMENT "Building erpl-proto nwrfc ABI shim (${staged_name})"
        VERBATIM)

    message(STATUS "erpl-proto backend enabled: ${staged_path}")
    set(ERPL_PROTO_AVAILABLE TRUE PARENT_SCOPE)
    set(ERPL_PROTO_BACKEND_LIB "${staged_path}" PARENT_SCOPE)
endfunction()

#---------------------------------------------------------------------------------------

function(enable_mold_linker)
    # Try to find mold binary
    find_program(MOLD_LINKER mold)
    if(MOLD_LINKER)
        message(STATUS "Found mold: ${MOLD_LINKER}")
        
        # Check if compiler supports -fuse-ld=mold
        check_cxx_compiler_flag("-fuse-ld=mold" HAS_FUSE_LD_MOLD)
        if(HAS_FUSE_LD_MOLD)
            set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fuse-ld=mold")
            set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -fuse-ld=mold")
            set(CMAKE_MODULE_LINKER_FLAGS "${CMAKE_MODULE_LINKER_FLAGS} -fuse-ld=mold")
            message(STATUS "Using mold as linker with -fuse-ld=mold")
        else()
            message(WARNING "Compiler does not support -fuse-ld=mold")
        endif()
    else()
        message(STATUS "mold linker not found, using default linker")
    endif()
endfunction()