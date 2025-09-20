if(MSVC)
    add_compile_options(/bigobj)
endif()

include(CheckCXXCompilerFlag)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(../scripts/functions.cmake)
enable_mold_linker()

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





