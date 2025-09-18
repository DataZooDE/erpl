if(MSVC)
    add_compile_options(/bigobj)
endif()

include(CheckCXXCompilerFlag)

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


get_filename_component(rfc_ext "${PROJECT_SOURCE_DIR}/../rfc" REALPATH)
get_filename_component(tunnel_ext "${PROJECT_SOURCE_DIR}/../tunnel" REALPATH)
get_filename_component(bics_ext "${PROJECT_SOURCE_DIR}/../bics" REALPATH)
get_filename_component(odp_ext "${PROJECT_SOURCE_DIR}/../odp" REALPATH)
get_filename_component(erpl_ext "${PROJECT_SOURCE_DIR}/../trampoline" REALPATH)

if(CMAKE_BUILD_TYPE STREQUAL "Release")    
    duckdb_extension_load(erpl_rfc
        SOURCE_DIR "${rfc_ext}"
        DONT_LINK
    )
    if (EXISTS "${tunnel_ext}/CMakeLists.txt")
        duckdb_extension_load(erpl_tunnel
            SOURCE_DIR "${tunnel_ext}"
            DONT_LINK
        )
    endif()
    if (EXISTS "${bics_ext}/CMakeLists.txt")
        duckdb_extension_load(erpl_bics
            SOURCE_DIR "${bics_ext}"
            DONT_LINK
        )
    endif()
    if (EXISTS "${odp_ext}/CMakeLists.txt")
        duckdb_extension_load(erpl_odp
            SOURCE_DIR "${odp_ext}"
            DONT_LINK
        )
    endif()
    duckdb_extension_load(erpl
        SOURCE_DIR "${erpl_ext}"
        DONT_LINK
    )
else()
    duckdb_extension_load(erpl_rfc
        SOURCE_DIR "${rfc_ext}"
    )
    if (EXISTS "${tunnel_ext}/CMakeLists.txt")
        duckdb_extension_load(erpl_tunnel
            SOURCE_DIR "${tunnel_ext}"
        )
    endif()        
    if (EXISTS "${bics_ext}/CMakeLists.txt")
        duckdb_extension_load(erpl_bics
            SOURCE_DIR "${bics_ext}"
        )
    endif()

    if (EXISTS "${odp_ext}/CMakeLists.txt")
        duckdb_extension_load(erpl_odp
            SOURCE_DIR "${odp_ext}"
        )
    endif()
endif()





