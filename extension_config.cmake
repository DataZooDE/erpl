if(MSVC)
    add_compile_options(/bigobj)
endif()

include(CheckCXXCompilerFlag)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(../scripts/functions.cmake)
enable_mold_linker()

get_filename_component(rfc_ext "${PROJECT_SOURCE_DIR}/../rfc" REALPATH)
get_filename_component(bics_ext "${PROJECT_SOURCE_DIR}/../bics" REALPATH)
get_filename_component(odp_ext "${PROJECT_SOURCE_DIR}/../odp" REALPATH)
get_filename_component(ape_ext "${PROJECT_SOURCE_DIR}/../ape" REALPATH)
get_filename_component(erpl_ext "${PROJECT_SOURCE_DIR}/../trampoline" REALPATH)


if(CMAKE_BUILD_TYPE STREQUAL "Release")    
    if(DUCKDB_MAJOR_VERSION LESS 2)
        set(ERPL_DONT_LINK DONT_LINK)
    endif()
    duckdb_extension_load(erpl_rfc
        SOURCE_DIR "${rfc_ext}"
        ${ERPL_DONT_LINK}
    )
    if (EXISTS "${bics_ext}/CMakeLists.txt")
        duckdb_extension_load(erpl_bics
            SOURCE_DIR "${bics_ext}"
            ${ERPL_DONT_LINK}
        )
    endif()
    if (EXISTS "${odp_ext}/CMakeLists.txt")
        duckdb_extension_load(erpl_odp
            SOURCE_DIR "${odp_ext}"
            ${ERPL_DONT_LINK}
        )
    endif()
    if (EXISTS "${ape_ext}/CMakeLists.txt")
        duckdb_extension_load(erpl_ape
            SOURCE_DIR "${ape_ext}"
            ${ERPL_DONT_LINK}
        )
    endif()
    duckdb_extension_load(erpl
        SOURCE_DIR "${erpl_ext}"
        ${ERPL_DONT_LINK}
    )
else()
    if(DUCKDB_MAJOR_VERSION GREATER_EQUAL 2)
        duckdb_extension_statically_link(erpl_rfc)
        if (EXISTS "${bics_ext}/CMakeLists.txt")
            duckdb_extension_statically_link(erpl_bics)
        endif()
        if (EXISTS "${odp_ext}/CMakeLists.txt")
            duckdb_extension_statically_link(erpl_odp)
        endif()
        if (EXISTS "${ape_ext}/CMakeLists.txt")
            duckdb_extension_statically_link(erpl_ape)
        endif()
    endif()
    duckdb_extension_load(erpl_rfc
        SOURCE_DIR "${rfc_ext}"
    )
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

    if (EXISTS "${ape_ext}/CMakeLists.txt")
        duckdb_extension_load(erpl_ape
            SOURCE_DIR "${ape_ext}"
        )
    endif()
endif()





