#define DUCKDB_EXTENSION_MAIN

#include "duckdb.hpp"
#include "erpl_extension.hpp"

#include <codecvt>
#include <locale>
#include <fstream>
#include <iostream>

#ifdef __linux__

#include <dlfcn.h>
#include <libgen.h> // for dirname

// Symbols created by objcopy
extern const char _binary_libsapnwrfc_so_start[];
extern const char _binary_libsapnwrfc_so_end[];
extern const char _binary_libsapucum_so_start[];
extern const char _binary_libsapucum_so_end[];
extern const char _binary_erpl_impl_duckdb_extension_start[];
extern const char _binary_erpl_impl_duckdb_extension_end[];


#elif _WIN32

#define UNICODE 1
#include <windows.h>

#endif


namespace duckdb 
{
#ifdef __linux__
    static std::string GetExtensionDir() 
    {
        Dl_info dl_info;
        if (!dladdr((void*)GetExtensionDir, &dl_info)) {
            throw std::runtime_error("Failed to get path of ERPL extension");
        }

        std::string full_path(dl_info.dli_fname);
        std::string dir_path = dirname(const_cast<char*>(full_path.c_str())); 

        return dir_path;
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
    

    static std::string Wide2Str(const std::wstring& wstr)
    {
        using convert_typeX = std::codecvt_utf8<wchar_t>;
        std::wstring_convert<convert_typeX, wchar_t> converterX;

        return converterX.to_bytes(wstr);
    }

    static std::string Wide2Str(LPWSTR lpwstr)
    {
        std::wstring ws(lpwstr);
        return Wide2Str(ws);
    }

    std::wstring Str2Wide(const std::string& str) {
        std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;
        return converter.from_bytes(str);
    }

    HMODULE GetExtensionHandle()
    {
        HMODULE h_module = NULL;
        if (!GetModuleHandleEx(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | 
                            GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            (LPCTSTR)GetExtensionHandle, 
                            &h_module)) {
            throw std::runtime_error("Failed to get module handle");
        }

        return h_module;
    }

    static std::string GetExtensionDir()
    {
        auto h_module = GetExtensionHandle();

        char path[MAX_PATH];
        if (GetModuleFileNameA(h_module, path, MAX_PATH) == 0) {
            throw std::runtime_error("Failed to get module file name");
        }

        std::string full_path(path);
        size_t last_slash_pos = full_path.find_last_of("\\/"); // Windows paths use backslashes, but forward slashes are also supported

        if (last_slash_pos == std::string::npos) {
            throw std::runtime_error("No directory separator found in path");
        }

        return full_path.substr(0, last_slash_pos);
    }

    static void SaveResourceToFile(LPWSTR resource_name, const std::string& filename)
    {
        HMODULE mod = GetExtensionHandle();
        HRSRC res_info = FindResource(mod, resource_name, RT_RCDATA);
        if (!res_info) {
            throw std::runtime_error(StringUtil::Format("Failed to find resource: %s", Wide2Str(resource_name).c_str()));
        }
        HGLOBAL res = LoadResource(mod, res_info);
        LPVOID data = LockResource(res);
        DWORD data_size = SizeofResource(mod, res_info);

        std::ofstream ofs(filename, std::ios::binary);
        ofs.write(reinterpret_cast<const char*>(data), data_size);
    }

    static void AddDuckDbExtensionPathToDllSearchPath() 
    {
        auto ext_path = Str2Wide(GetExtensionDir());
        auto ret = AddDllDirectory(ext_path.c_str());
        if (ret == NULL) {
            throw std::runtime_error("Failed to add DuckDB extension directory to DLL search path");
        }
    }

    static void ModifyPathEnvironmentVariable(const std::wstring& new_dir_to_add) 
    {
        const unsigned int max_path_size = 32767;
        wchar_t current_path[max_path_size];

        if (GetEnvironmentVariableW(L"PATH", current_path, max_path_size) == 0) {
            throw std::runtime_error("Failed to get PATH variable");
        }

        std::wstring modified_path = std::wstring(current_path) + L";" + new_dir_to_add;
        if (SetEnvironmentVariableW(L"PATH", modified_path.c_str()) == 0) {
            throw std::runtime_error("Failed to set PATH variable");
        }
    }

    static void ModifyPathEnvironmentVariable(const std::string& new_dir_to_add) 
    {
        ModifyPathEnvironmentVariable(Str2Wide(new_dir_to_add));
    }

    static void ExtractExtensionAndSapLibs() 
    {
        auto ext_path = GetExtensionDir();
        try 
        {
            SaveResourceToFile(TEXT("ICUDT50"), StringUtil::Format("%s\\icudt50.dll", ext_path));
            SaveResourceToFile(TEXT("ICUIN50"), StringUtil::Format("%s\\icuin50.dll", ext_path));
            SaveResourceToFile(TEXT("ICUUC50"), StringUtil::Format("%s\\icuuc50.dll", ext_path));
            SaveResourceToFile(TEXT("SAPNWRFC"), StringUtil::Format("%s\\sapnwrfc.dll", ext_path));
            SaveResourceToFile(TEXT("LIBSAPUCUM"), StringUtil::Format("%s\\libsapucum.dll", ext_path));
            SaveResourceToFile(TEXT("LIBCRYPTO-3-x64"), StringUtil::Format("%s\\libcrypto-3-x64.dll", ext_path));
            SaveResourceToFile(TEXT("LIBSSL-3-x64"), StringUtil::Format("%s\\libssl-3-x64.dll", ext_path));
            std::cout << StringUtil::Format("ERPL dependencies extracted and saved to %s.", ext_path) << std::endl;
            
            SaveResourceToFile(TEXT("ERPL_IMPL"), StringUtil::Format("%s\\erpl_impl.duckdb_extension", ext_path));
            std::cout << StringUtil::Format("ERPL extension extracted and saved to %s.", ext_path) << std::endl;

            //AddDuckDbExtensionPathToDllSearchPath();
            //std::cout << "Added DuckDB extension directory to the DLL search path." << std::endl;

            ModifyPathEnvironmentVariable(ext_path);
            std::cout << "Added DuckDB extension directory to the PATH environment variable." << std::endl;
        } 
        catch (const std::exception& e) {
            std::cerr << "Error during extraction: " << e.what() << std::endl;
        }
    }

#endif
    static void LoadMainExtension(DuckDB &db) 
    {
        duckdb::Connection con(db);
        auto ext_path = GetExtensionDir();
        auto result = con.Query(StringUtil::Format("INSTALL '%s\\erpl_impl.duckdb_extension'", ext_path));
        if (result->HasError()) {
            throw std::runtime_error(StringUtil::Format("Failed to install ERPL extension: %s", result->GetError()));
        }
        std::cout << StringUtil::Format("ERPL implementation extension installed from %s\\erpl_impl.duckdb_extension.", ext_path) << std::endl;

        result = con.Query("LOAD 'erpl_impl'");
        if (result->HasError()) {
            throw std::runtime_error(StringUtil::Format("Failed to load ERPL extension: %s", result->GetError()));
        }
        std::cout << "ERPL implementation extension loaded. For instructions how to use it visit https://erpl.io" << std::endl;
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