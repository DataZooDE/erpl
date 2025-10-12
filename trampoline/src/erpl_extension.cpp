#define DUCKDB_EXTENSION_MAIN

#include "duckdb.hpp"
#include "duckdb/main/extension/extension_loader.hpp"
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

extern const char _binary_libicudata_so_50_start[];
extern const char _binary_libicudata_so_50_end[];

extern const char _binary_libicui18n_so_50_start[];
extern const char _binary_libicui18n_so_50_end[];

extern const char _binary_libicuuc_so_50_start[];
extern const char _binary_libicuuc_so_50_end[];

extern const char _binary_erpl_rfc_duckdb_extension_start[];
extern const char _binary_erpl_rfc_duckdb_extension_end[];

extern const char _binary_erpl_tunnel_duckdb_extension_start[];
extern const char _binary_erpl_tunnel_duckdb_extension_end[];

#ifdef WITH_ERPL_BICS
extern const char _binary_erpl_bics_duckdb_extension_start[];
extern const char _binary_erpl_bics_duckdb_extension_end[];
#endif

#ifdef WITH_ERPL_ODP
extern const char _binary_erpl_odp_duckdb_extension_start[];
extern const char _binary_erpl_odp_duckdb_extension_end[];
#endif

#elif _WIN32

#define UNICODE 1
#include <windows.h>

#elif __APPLE__

#include <dlfcn.h>
#include <libgen.h> // for dirname
#include <mach-o/dyld.h> // for _NSGetExecutablePath

// Symbols created by xxd & clang compilation
extern const char libsapnwrfc_dylib[];
extern const unsigned int libsapnwrfc_dylib_len;

extern const char libsapucum_dylib[];
extern const unsigned int libsapucum_dylib_len;

extern const char libicudata_50_dylib[];
extern const unsigned int libicudata_50_dylib_len;

extern const char libicui18n_50_dylib[];
extern const unsigned int libicui18n_50_dylib_len;

extern const char libicuuc_50_dylib[];
extern const unsigned int libicuuc_50_dylib_len;

extern const char erpl_rfc_duckdb_extension[];
extern const unsigned int erpl_rfc_duckdb_extension_len;

extern const char erpl_tunnel_duckdb_extension[];
extern const unsigned int erpl_tunnel_duckdb_extension_len;

#ifdef WITH_ERPL_BICS
extern const char erpl_bics_duckdb_extension[];
extern const unsigned int erpl_bics_duckdb_extension_len;
#endif

#ifdef WITH_ERPL_ODP
extern const char erpl_odp_duckdb_extension[];
extern const unsigned int erpl_odp_duckdb_extension_len;
#endif

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

    static void LoadLibraryFromFile(const std::string &filename) 
    {
        void* handle = dlopen(filename.c_str(), RTLD_NOW | RTLD_GLOBAL);
        if (!handle) {
            throw std::runtime_error(StringUtil::Format("Failed to load library: %s", filename));
        }
    }

    static void ExtractExtensionsAndSapLibs() 
    {
        auto ext_path = GetExtensionDir();

        try 
        {
            std::cout << StringUtil::Format("Saving ERPL SAP dependencies to '%s' and loading them ... ", ext_path);

            SaveToFile(_binary_libicudata_so_50_start, _binary_libicudata_so_50_end, StringUtil::Format("%s/libicudata.so.50", ext_path));
            LoadLibraryFromFile(StringUtil::Format("%s/libicudata.so.50", ext_path));

            SaveToFile(_binary_libicuuc_so_50_start, _binary_libicuuc_so_50_end, StringUtil::Format("%s/libicuuc.so.50", ext_path));
            LoadLibraryFromFile(StringUtil::Format("%s/libicuuc.so.50", ext_path));

            SaveToFile(_binary_libicui18n_so_50_start, _binary_libicui18n_so_50_end, StringUtil::Format("%s/libicui18n.so.50", ext_path));
            LoadLibraryFromFile(StringUtil::Format("%s/libicui18n.so.50", ext_path));

            SaveToFile(_binary_libsapnwrfc_so_start, _binary_libsapnwrfc_so_end, StringUtil::Format("%s/libsapnwrfc.so", ext_path));
            LoadLibraryFromFile(StringUtil::Format("%s/libsapnwrfc.so", ext_path));

            SaveToFile(_binary_libsapucum_so_start, _binary_libsapucum_so_end, StringUtil::Format("%s/libsapucum.so", ext_path));
            LoadLibraryFromFile(StringUtil::Format("%s/libsapucum.so", ext_path));
            
            std::cout << "done" << std::endl;

            SaveToFile(_binary_erpl_rfc_duckdb_extension_start, _binary_erpl_rfc_duckdb_extension_end, StringUtil::Format("%s/erpl_rfc.duckdb_extension", ext_path));
            std::cout << StringUtil::Format("ERPL RFC extension extracted and saved to %s.", ext_path) << std::endl;

            SaveToFile(_binary_erpl_tunnel_duckdb_extension_start, _binary_erpl_tunnel_duckdb_extension_end, StringUtil::Format("%s/erpl_tunnel.duckdb_extension", ext_path));
            std::cout << StringUtil::Format("ERPL Tunnel extension extracted and saved to %s.", ext_path) << std::endl;

            #ifdef WITH_ERPL_BICS
            SaveToFile(_binary_erpl_bics_duckdb_extension_start, _binary_erpl_bics_duckdb_extension_end, StringUtil::Format("%s/erpl_bics.duckdb_extension", ext_path));
            std::cout << StringUtil::Format("ERPL BICS extension extracted and saved to %s.", ext_path) << std::endl;
            #endif

            #ifdef WITH_ERPL_ODP
            SaveToFile(_binary_erpl_odp_duckdb_extension_start, _binary_erpl_odp_duckdb_extension_end, StringUtil::Format("%s/erpl_odp.duckdb_extension", ext_path));
            std::cout << StringUtil::Format("ERPL ODP extension extracted and saved to %s.", ext_path) << std::endl;
            #endif
        } 
        catch (const std::exception& e) {
            std::cerr << std::endl << "Error: " << e.what() << std::endl;
        }
    }

    static std::string Separator() {
        return "/";
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

    static void ExtractExtensionsAndSapLibs() 
    {
        auto ext_path = GetExtensionDir();
        try 
        {
            SaveResourceToFile(TEXT("ICUDT57"), StringUtil::Format("%s\\icudt57.dll", ext_path));
            SaveResourceToFile(TEXT("ICUIN57"), StringUtil::Format("%s\\icuin57.dll", ext_path));
            SaveResourceToFile(TEXT("ICUUC57"), StringUtil::Format("%s\\icuuc57.dll", ext_path));
            SaveResourceToFile(TEXT("SAPNWRFC"), StringUtil::Format("%s\\sapnwrfc.dll", ext_path));
            SaveResourceToFile(TEXT("LIBSAPUCUM"), StringUtil::Format("%s\\libsapucum.dll", ext_path));
            SaveResourceToFile(TEXT("LIBCRYPTO-3-x64"), StringUtil::Format("%s\\libcrypto-3-x64.dll", ext_path));
            SaveResourceToFile(TEXT("LIBSSL-3-x64"), StringUtil::Format("%s\\libssl-3-x64.dll", ext_path));
            SaveResourceToFile(TEXT("LIBSSH2"), StringUtil::Format("%s\\libssh2.dll", ext_path));
            SaveResourceToFile(TEXT("ZLIB1"), StringUtil::Format("%s\\zlib1.dll", ext_path));
            std::cout << StringUtil::Format("ERPL dependencies extracted and saved to %s.", ext_path) << std::endl;
            
            SaveResourceToFile(TEXT("ERPL_RFC"), StringUtil::Format("%s\\erpl_rfc.duckdb_extension", ext_path));
            std::cout << StringUtil::Format("ERPL RFC extension extracted and saved to %s.", ext_path) << std::endl;

            SaveResourceToFile(TEXT("ERPL_TUNNEL"), StringUtil::Format("%s\\erpl_tunnel.duckdb_extension", ext_path));
            std::cout << StringUtil::Format("ERPL Tunnel extension extracted and saved to %s.", ext_path) << std::endl;

            #ifdef WITH_ERPL_BICS
            SaveResourceToFile(TEXT("ERPL_BICS"), StringUtil::Format("%s\\erpl_bics.duckdb_extension", ext_path));
            std::cout << StringUtil::Format("ERPL BICS extension extracted and saved to %s.", ext_path) << std::endl;
            #endif

            #ifdef WITH_ERPL_ODP
            SaveResourceToFile(TEXT("ERPL_ODP"), StringUtil::Format("%s\\erpl_odp.duckdb_extension", ext_path));
            std::cout << StringUtil::Format("ERPL ODP extension extracted and saved to %s.", ext_path) << std::endl;
            #endif

            ModifyPathEnvironmentVariable(ext_path);
            std::cout << "Added DuckDB extension directory to the PATH environment variable." << std::endl;
        } 
        catch (const std::exception& e) {
            std::cerr << "Error during extraction: " << e.what() << std::endl;
        }
    }

    static std::string Separator() {
        return "\\";
    }

#elif __APPLE__

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

static void SaveToFile(const char* start, const unsigned int len, const std::string& filename) 
{
    std::ofstream ofs(filename, std::ios::binary);
    if (!ofs) {
        throw std::runtime_error("Failed to open file: " + filename);
    }
    ofs.write(start, len);
}

static void LoadLibraryFromFile(const std::string &filename) 
{
    void* handle = dlopen(filename.c_str(), RTLD_NOW | RTLD_GLOBAL);
    if (!handle) {
        throw std::runtime_error("Failed to load library: " + filename);
    }
}

static void ModifyDyldEnvironmentVariable(const std::string &ext_path) 
{
    const char *cur_path = std::getenv("DYLD_LIBRARY_PATH");
    std::string new_path;

    if (cur_path) {
        new_path = std::string(cur_path) + ":" + ext_path;
    } else {
        new_path = ext_path;
    }

    if (setenv("DYLD_LIBRARY_PATH", new_path.c_str(), 1) != 0) {
        throw std::runtime_error("Failed to set DYLD_LIBRARY_PATH");
    }
}

static void ExtractExtensionsAndSapLibs() 
{
    auto ext_path = GetExtensionDir();

    try 
    {
        std::cout << "Saving ERPL SAP dependencies to '" << ext_path << "' and loading them ... ";

        SaveToFile(libicudata_50_dylib, libicudata_50_dylib_len, ext_path + "/libicudata.50.dylib");
        LoadLibraryFromFile(ext_path + "/libicudata.50.dylib");

        SaveToFile(libicuuc_50_dylib, libicuuc_50_dylib_len, ext_path + "/libicuuc.50.dylib");
        LoadLibraryFromFile(ext_path + "/libicuuc.50.dylib");

        SaveToFile(libicui18n_50_dylib, libicui18n_50_dylib_len, ext_path + "/libicui18n.50.dylib");
        LoadLibraryFromFile(ext_path + "/libicui18n.50.dylib");

        SaveToFile(libsapnwrfc_dylib, libsapnwrfc_dylib_len, ext_path + "/libsapnwrfc.dylib");
        LoadLibraryFromFile(ext_path + "/libsapnwrfc.dylib");

        SaveToFile(libsapucum_dylib, libsapucum_dylib_len, ext_path + "/libsapucum.dylib");
        LoadLibraryFromFile(ext_path + "/libsapucum.dylib");

        std::cout << "done" << std::endl;

        SaveToFile(erpl_rfc_duckdb_extension, erpl_rfc_duckdb_extension_len, ext_path + "/erpl_rfc.duckdb_extension");
        std::cout << "ERPL RFC extension extracted and saved to " << ext_path << "." << std::endl;

        SaveToFile(erpl_tunnel_duckdb_extension, erpl_tunnel_duckdb_extension_len, ext_path + "/erpl_tunnel.duckdb_extension");
        std::cout << "ERPL Tunnel extension extracted and saved to " << ext_path << "." << std::endl;

        #ifdef WITH_ERPL_BICS
        SaveToFile(erpl_bics_duckdb_extension, erpl_bics_duckdb_extension_len, ext_path + "/erpl_bics.duckdb_extension");
        std::cout << "ERPL BICS extension extracted and saved to " << ext_path << "." << std::endl;
        #endif

        #ifdef WITH_ERPL_ODP
        SaveToFile(erpl_odp_duckdb_extension, erpl_odp_duckdb_extension_len, ext_path + "/erpl_odp.duckdb_extension");
        std::cout << "ERPL ODP extension extracted and saved to " << ext_path << "." << std::endl;
        #endif

        ModifyDyldEnvironmentVariable(ext_path);
        std::cout << "Added DuckDB extension directory to the DYLD search path." << std::endl;
    } 
    catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
    }
}

static std::string Separator() {
    return "/";
}

#endif
    
    static std::string PrintableExtensionName(const std::string &extension_name) 
    {
        // This function transforms from erpl_rfc to ERPL RFC
        std::string ret = extension_name;
        std::transform(ret.begin(), ret.end(), ret.begin(), ::toupper);
        std::replace(ret.begin(), ret.end(), '_', ' ');

        return ret;
    }
     
    static void InstallAndLoadExtension(DuckDB &db, const std::string& ext_name)
    {
        duckdb::Connection con(db);
        auto ext_path = GetExtensionDir();
        auto pr_ext_name = PrintableExtensionName(ext_name);

        auto result = con.Query(StringUtil::Format("INSTALL '%s%s%s.duckdb_extension'", ext_path, Separator(), ext_name));
        if (result->HasError()) {
            throw std::runtime_error(StringUtil::Format("Failed to install %s extension: %s", pr_ext_name, result->GetError()));
        }
        
        result = con.Query(StringUtil::Format("LOAD '%s'", ext_name));
        if (result->HasError()) {
            throw std::runtime_error(StringUtil::Format("Failed to load %s extension: %s", pr_ext_name, result->GetError()));
        }

        std::cout << StringUtil::Format("%s extension installed and loaded.", pr_ext_name) << std::endl;
    }

    static void LoadExtensions(DatabaseInstance &db) 
    {
        DuckDB db_wrapper(db);
        InstallAndLoadExtension(db_wrapper, "erpl_rfc");

        InstallAndLoadExtension(db_wrapper, "erpl_tunnel");

        #ifdef WITH_ERPL_BICS
        InstallAndLoadExtension(db_wrapper, "erpl_bics");
        #endif

        #ifdef WITH_ERPL_ODP
        InstallAndLoadExtension(db_wrapper, "erpl_odp");
        #endif

        std::cout << "ERPL extensions loaded. For instructions on how to use them, visit https://erpl.io" << std::endl;
    }

    static void LoadInternal(ExtensionLoader &loader)
    {
       std::cout << "-- Loading ERPL Trampoline Extension. --" << std::endl 
                 << "(The purpose of the extension is to extract dependencies and load the ERPL implementation)" << std::endl;
       ExtractExtensionsAndSapLibs();
       LoadExtensions(loader.GetDatabaseInstance());
    }

    void ErplExtension::Load(ExtensionLoader &loader) 
    {
       LoadInternal(loader);
    }

    std::string ErplExtension::Name() {
        return "erpl";
    }
} // namespace duckdb

extern "C" {
    DUCKDB_CPP_EXTENSION_ENTRY(erpl, loader) {
        duckdb::LoadInternal(loader);
    }
}

#ifndef DUCKDB_EXTENSION_MAIN
#error DUCKDB_EXTENSION_MAIN not defined
#endif