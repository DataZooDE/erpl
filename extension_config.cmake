get_filename_component(rfc_ext "${PROJECT_SOURCE_DIR}/../rfc" REALPATH)
duckdb_extension_load(erpl_rfc
    SOURCE_DIR "${rfc_ext}"
    DONT_LINK
)

get_filename_component(bics_ext "${PROJECT_SOURCE_DIR}/../bics" REALPATH)
if (EXISTS "${bics_ext}/CMakeLists.txt")
    duckdb_extension_load(erpl_bics
        SOURCE_DIR "${bics_ext}"
        DONT_LINK
    )
endif()

get_filename_component(odp_ext "${PROJECT_SOURCE_DIR}/../odp" REALPATH)
if (EXISTS "${odp_ext}/CMakeLists.txt")
    duckdb_extension_load(erpl_odp
        SOURCE_DIR "${odp_ext}"
        DONT_LINK
    )
endif()

get_filename_component(erpl_ext "${PROJECT_SOURCE_DIR}/../trampoline" REALPATH)
duckdb_extension_load(erpl
    SOURCE_DIR "${erpl_ext}"
    DONT_LINK
)