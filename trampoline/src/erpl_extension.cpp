#define DUCKDB_EXTENSION_MAIN

#include "duckdb.hpp"
#include "erpl_extension.hpp"

#include <fstream>
#include <filesystem>
#include <iostream>

#ifdef __linux__

#include <dlfcn.h>

#elif _WIN32

#define UNICODE 1
#include <windows.h>

#endif

// Symbols created by objcopy
extern const char _binary_libsapnwrfc_so_start[];
extern const char _binary_libsapnwrfc_so_end[];
extern const char _binary_libsapucum_so_start[];
extern const char _binary_libsapucum_so_end[];
extern const char _binary_erpl_impl_duckdb_extension_start[];
extern const char _binary_erpl_impl_duckdb_extension_end[];

namespace duckdb 
{
#ifdef __linux__
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
#elif _WIN32
    static std::string GetExtensionDir()
    {
        char path[MAX_PATH];
        GetModuleFileNameA(NULL, path, MAX_PATH);
        auto ext_path = std::filesystem::path(std::string(path));
        return ext_path.parent_path().generic_string();
    }

    static void SaveResourceToFile(LPWSTR resource_name, const std::string& filename)
    {
        HMODULE mod = GetModuleHandle(NULL);
        HRSRC res_info = FindResource(mod, resource_name, RT_RCDATA);
        HGLOBAL res = LoadResource(mod, res_info);
        LPVOID data = LockResource(res);
        DWORD data_size = SizeofResource(mod, res_info);

        std::ofstream ofs(filename, std::ios::binary);
        ofs.write(reinterpret_cast<const char*>(data), data_size);
    }

    static void ExtractExtensionAndSapLibs() 
    {
        auto ext_path = GetExtensionDir();
        try 
        {
            SaveResourceToFile(TEXT("icudt50"), StringUtil::Format("%s/icudt50.dll", ext_path));
            SaveResourceToFile(TEXT("icuin50"), StringUtil::Format("%s/icuin50.dll", ext_path));
            SaveResourceToFile(TEXT("icuuc50"), StringUtil::Format("%s/icuuc50.dll", ext_path));
            SaveResourceToFile(TEXT("sapnwrfc"), StringUtil::Format("%s/sapnwrfc.dll", ext_path));
            SaveResourceToFile(TEXT("libsapucum"), StringUtil::Format("%s/libsapucum.dll", ext_path));
            std::cout << StringUtil::Format("ERPL SAP dependencies extracted and saved to %s.", ext_path) << std::endl;
            
            SaveResourceToFile(TEXT("erpl_impl"), StringUtil::Format("%s/erpl_impl.duckdb_extension", ext_path));
            std::cout << StringUtil::Format("ERPL extension extracted and saved to %s.", ext_path) << std::endl;
        } 
        catch (const std::exception& e) {
            std::cerr << "Error: " << e.what() << std::endl;
        }
    }

#endif
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
       //ExtractExtensionAndSapLibs();
       //LoadMainExtension(db);
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