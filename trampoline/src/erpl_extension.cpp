#define DUCKDB_EXTENSION_MAIN

#include "duckdb.hpp"
#include "erpl_extension.hpp"

#include <fstream>
#include <filesystem>
#include <iostream>
#include <dlfcn.h>


// Symbols created by objcopy
extern const char _binary_libsapnwrfc_so_start[];
extern const char _binary_libsapnwrfc_so_end[];
extern const char _binary_libsapucum_so_start[];
extern const char _binary_libsapucum_so_end[];
extern const char _binary_erpl_impl_duckdb_extension_start[];
extern const char _binary_erpl_impl_duckdb_extension_end[];

namespace duckdb 
{
    static std::string GetExtensionDir() 
    {
        Dl_info dl_info;
        if (! dladdr((void*)GetExtensionDir, &dl_info)) {
            throw std::runtime_error("Failed to get path of ERPL extension");
        } 

        std::filesystem::path ext_path(std::string(dl_info.dli_fname));
        return ext_path.parent_path();
    }

    static void SaveToFile(const char* start, const char* end, const std::string& filename) 
    {
        std::ofstream ofs(filename, std::ios::binary);
        if (!ofs) {
            throw std::runtime_error(StringUtil::Format("Failed to open file: %s", filename));
        }
        ofs.write(start, end - start);
    }

    static void ExtractExtensionAndSapLibs() 
    {
        auto ext_path = GetExtensionDir();

        try 
        {
            SaveToFile(_binary_libsapnwrfc_so_start, _binary_libsapnwrfc_so_end, StringUtil::Format("%s/libsapnwrfc.so", ext_path));
            SaveToFile(_binary_libsapucum_so_start, _binary_libsapucum_so_end, StringUtil::Format("%s/libsapucum.so", ext_path));
            std::cout << StringUtil::Format("ERPL SAP dependencies extracted and saved to %s.", ext_path) << std::endl;
            SaveToFile(_binary_erpl_impl_duckdb_extension_start, _binary_erpl_impl_duckdb_extension_end, StringUtil::Format("%s/erpl_impl.duckdb_extension", ext_path));
            std::cout << StringUtil::Format("ERPL extension extracted and saved to %s.", ext_path) << std::endl;
        } 
        catch (const std::exception& e) {
            std::cerr << "Error: " << e.what() << std::endl;
        }
    }

    static void LoadMainExtension(DuckDB &db) 
    {
        duckdb::Connection con(db);
        auto ext_path = GetExtensionDir();
        auto result = con.Query(StringUtil::Format("INSTALL '%s/erpl_impl.duckdb_extension'", ext_path));
        if (result->HasError()) {
            throw std::runtime_error(StringUtil::Format("Failed to install ERPL extension: %s", result->GetError()));
        }

        result = con.Query("LOAD 'erpl_impl'");
        if (result->HasError()) {
            throw std::runtime_error(StringUtil::Format("Failed to load ERPL extension: %s", result->GetError()));
        }
        std::cout << "ERPL extension loaded." << std::endl;
    }

    void ErplExtension::Load(DuckDB &db) 
    {
       std::cout << "-- Loading ERPL Trampoline Extension. --" << std::endl 
                 << "(The purpose of the extension is to extract dependencies and load the ERPL implementation)" << std::endl;
       ExtractExtensionAndSapLibs();
       LoadMainExtension(db);
    }

    std::string ErplExtension::Name() {
        return "erpl";
    }
} // namespace duckdb

extern "C" {
    DUCKDB_EXTENSION_API void erpl_init(duckdb::DatabaseInstance &db) 
    {
        duckdb::DuckDB db_wrapper(db);
	    db_wrapper.LoadExtension<duckdb::ErplExtension>();
    }

    DUCKDB_EXTENSION_API const char *erpl_version() 
    {
        return "v0.9.1";
        //return duckdb::DuckDB::LibraryVersion();
    }
}

#ifndef DUCKDB_EXTENSION_MAIN
#error DUCKDB_EXTENSION_MAIN not defined
#endif