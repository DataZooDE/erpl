// Resolution of the SAP RFC C ABI at runtime. See sap_rfc_api.hpp for why.

#define ERPL_RFC_API_IMPLEMENTATION
#include "sap_rfc_api.hpp"

#include "erpl_tracing.hpp"

#include <cstdlib>
#include <mutex>

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace duckdb {

namespace {

// The SDK's own library, under each platform's spelling. Opened through the loader's
// search path, so LD_LIBRARY_PATH (or the equivalent) keeps working exactly as it did
// when erpl linked the library directly.
#ifdef _WIN32
constexpr const char *NWRFC_LIBRARY_NAME = "sapnwrfc.dll";
#elif defined(__APPLE__)
constexpr const char *NWRFC_LIBRARY_NAME = "libsapnwrfc.dylib";
#else
constexpr const char *NWRFC_LIBRARY_NAME = "libsapnwrfc.so";
#endif

// The erpl-proto shim, staged under its own name rather than libsapnwrfc's. See
// add_erpl_proto_backend() in scripts/functions.cmake for why it is renamed.
#ifdef _WIN32
constexpr const char *PROTO_LIBRARY_NAME = "erpl_proto_nwrfc.dll";
#elif defined(__APPLE__)
constexpr const char *PROTO_LIBRARY_NAME = "liberpl_proto_nwrfc.dylib";
#else
constexpr const char *PROTO_LIBRARY_NAME = "liberpl_proto_nwrfc.so";
#endif

#ifndef _WIN32
// A witness that the SDK is already mapped into the global symbol scope. The trampoline
// extension extracts the SDK and dlopens it RTLD_GLOBAL before erpl_rfc loads, and it
// extracts to a directory that is on no search path -- so in a released build this is
// the only way to reach it. Same probe the trampoline itself uses.
constexpr const char *NWRFC_PRELOAD_WITNESS = "RfcGetVersion";
#endif

struct LibraryHandle {
	// Null means "resolve against the process's global scope" rather than "not loaded".
	void *handle = nullptr;
	string description;
};

string LastLibraryError() {
#ifdef _WIN32
	const auto code = GetLastError();
	if (code == 0) {
		return "unknown error";
	}
	char *buffer = nullptr;
	const auto length =
	    FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
	                   nullptr, code, 0, reinterpret_cast<char *>(&buffer), 0, nullptr);
	string message = length && buffer ? string(buffer, length) : StringUtil::Format("error %lu", (unsigned long)code);
	if (buffer) {
		LocalFree(buffer);
	}
	// StringUtil::Trim mutates in place and returns void.
	StringUtil::Trim(message);
	return message;
#else
	const char *error = dlerror();
	return error ? string(error) : string("unknown error");
#endif
}

void *OpenLibrary(const string &path) {
#ifdef _WIN32
	return reinterpret_cast<void *>(LoadLibraryA(path.c_str()));
#else
	// RTLD_LOCAL is the load-bearing flag. The SDK and the erpl-proto shim export the
	// same symbol names, so a library brought in with RTLD_GLOBAL would interpose on
	// anything loaded after it -- including the other backend -- and calls would be
	// served by whichever won that race rather than by the one that was selected.
	//
	// RTLD_NODELETE keeps it mapped for the life of the process: the SDK registers
	// static destructors and atexit handlers, and unmapping it while DuckDB still holds
	// connection state has no safe ordering.
	return dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL | RTLD_NODELETE);
#endif
}

void *FindSymbol(const LibraryHandle &library, const char *name) {
#ifdef _WIN32
	// handle is never null on Windows -- see IsPreloadedInGlobalScope.
	return reinterpret_cast<void *>(GetProcAddress(reinterpret_cast<HMODULE>(library.handle), name));
#else
	return dlsym(library.handle ? library.handle : RTLD_DEFAULT, name);
#endif
}

bool IsPreloadedInGlobalScope() {
#ifdef _WIN32
	// Windows has no RTLD_DEFAULT: there is no process-wide symbol scope to search, so
	// there is nothing for a "preloaded" handle to mean. It needs none either --
	// LoadLibrary resolves a DLL that is already loaded to the same module and simply
	// increments its reference count, so the ordinary path below already picks up the
	// copy the trampoline loaded.
	return false;
#else
	return dlsym(RTLD_DEFAULT, NWRFC_PRELOAD_WITNESS) != nullptr;
#endif
}

LibraryHandle OpenNwRfcLibrary(const string &explicit_path) {
	if (!explicit_path.empty()) {
		auto *handle = OpenLibrary(explicit_path);
		if (!handle) {
			throw IOException("Could not load the SAP NW RFC SDK from the configured path '%s': %s", explicit_path,
			                  LastLibraryError());
		}
		return {handle, StringUtil::Format("nwrfc (%s)", explicit_path)};
	}

	// A deliberately preloaded SDK wins over a search-path lookup. In a released build
	// the trampoline has already extracted and loaded it from a private directory, and
	// opening a second copy from the search path would give two SDK instances with
	// separate static state in one process.
	if (IsPreloadedInGlobalScope()) {
		ERPL_TRACE_INFO("RfcApi", "SAP NW RFC SDK already present in the global symbol scope, using it");
		return {nullptr, "nwrfc (preloaded)"};
	}

	auto *handle = OpenLibrary(NWRFC_LIBRARY_NAME);
	if (!handle) {
		throw IOException("Could not load the SAP NW RFC SDK (%s): %s. The SDK is not linked into erpl; it is "
		                  "loaded at runtime, so its directory must be reachable through the library search path "
		                  "(LD_LIBRARY_PATH on Linux, DYLD_LIBRARY_PATH on macOS, PATH on Windows).",
		                  NWRFC_LIBRARY_NAME, LastLibraryError());
	}
	return {handle, StringUtil::Format("nwrfc (%s)", NWRFC_LIBRARY_NAME)};
}

// Fills the table, failing on the first missing entry point rather than leaving a null
// slot to be discovered later at a call site.
RfcApi ResolveApi(const LibraryHandle &library) {
	RfcApi api {};
	idx_t resolved = 0;

#define ERPL_RFC_API_RESOLVE_SLOT(name)                                                                                \
	{                                                                                                                  \
		auto *symbol = FindSymbol(library, #name);                                                                     \
		if (!symbol) {                                                                                                 \
			throw IOException("The RFC backend '%s' does not provide the entry point %s, which erpl requires.",        \
			                  library.description, #name);                                                             \
		}                                                                                                              \
		api.name = reinterpret_cast<decltype(api.name)>(symbol);                                                       \
		resolved++;                                                                                                    \
	}
	ERPL_RFC_API_ENTRY_POINTS(ERPL_RFC_API_RESOLVE_SLOT)
#undef ERPL_RFC_API_RESOLVE_SLOT

	ERPL_TRACE_INFO("RfcApi", StringUtil::Format("Resolved %llu RFC entry points from %s", (uint64_t)resolved,
	                                             library.description));
	return api;
}

// The directory holding the binary this code is linked into: the erpl_rfc extension in a
// released build, the duckdb executable in a statically linked debug build. The proto
// shim is staged next to both, so one lookup covers both layouts.
string OwnModuleDirectory() {
#ifdef _WIN32
	HMODULE module = nullptr;
	if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
	                        reinterpret_cast<LPCSTR>(&OwnModuleDirectory), &module)) {
		return string();
	}
	char path[MAX_PATH];
	const auto length = GetModuleFileNameA(module, path, MAX_PATH);
	if (length == 0 || length == MAX_PATH) {
		return string();
	}
	const string full(path, length);
	const auto slash = full.find_last_of("\\/");
	return slash == string::npos ? string() : full.substr(0, slash);
#else
	Dl_info info;
	if (!dladdr(reinterpret_cast<void *>(&OwnModuleDirectory), &info) || !info.dli_fname) {
		return string();
	}
	const string full(info.dli_fname);
	const auto slash = full.find_last_of('/');
	return slash == string::npos ? string() : full.substr(0, slash);
#endif
}

string EnvironmentValue(const char *name) {
	const char *value = std::getenv(name);
	return value ? string(value) : string();
}

LibraryHandle OpenProtoLibrary(const string &explicit_path) {
	vector<string> candidates;
	if (!explicit_path.empty()) {
		// An explicitly named library is the only candidate. Falling back to a search
		// after the caller named a file would run a different build than the one asked
		// for, which is exactly the confusion a dual-backend setup must not create.
		candidates.push_back(explicit_path);
	} else {
		const auto own_directory = OwnModuleDirectory();
		if (!own_directory.empty()) {
			candidates.push_back(own_directory + "/" + PROTO_LIBRARY_NAME);
		}
		candidates.push_back(PROTO_LIBRARY_NAME);
	}

	for (const auto &candidate : candidates) {
		auto *handle = OpenLibrary(candidate);
		if (handle) {
			return {handle, StringUtil::Format("proto (%s)", candidate)};
		}
		ERPL_TRACE_DEBUG("RfcApi", StringUtil::Format("erpl-proto backend not at %s: %s", candidate,
		                                              LastLibraryError()));
	}

	// Never fall back to the SDK. A silent downgrade would make a green proto test run
	// meaningless -- it would be reporting on nwrfc.
	throw IOException("The 'proto' RFC backend was requested but %s could not be loaded. Tried: %s. Set "
	                  "erpl_rfc_backend_path (or ERPL_RFC_BACKEND_PATH) to point at the library, or build erpl "
	                  "with the erpl-proto submodule present.",
	                  PROTO_LIBRARY_NAME, StringUtil::Join(candidates, ", "));
}

// The requested configuration, and whether it has been acted on yet. Guarded because a
// SET on one connection can race the first RFC call on another.
std::mutex &SelectionLock() {
	static std::mutex lock;
	return lock;
}

RfcBackend g_requested_backend = RfcBackend::NWRFC;
bool g_backend_explicitly_requested = false;
string g_requested_library_path;
bool g_backend_resolved = false;

RfcBackend ParseBackendName(const string &name) {
	auto normalized = StringUtil::Lower(name);
	StringUtil::Trim(normalized);
	if (normalized == "nwrfc" || normalized == "sdk") {
		return RfcBackend::NWRFC;
	}
	if (normalized == "proto" || normalized == "erpl-proto" || normalized == "erpl_proto") {
		return RfcBackend::PROTO;
	}
	throw InvalidInputException("Unknown RFC backend '%s'. Valid backends are 'nwrfc' (SAP's NetWeaver RFC SDK, "
	                            "the default) and 'proto' (the pure-Rust erpl-proto implementation).",
	                            name);
}

struct ResolvedBackend {
	RfcApi api;
	RfcBackend backend;
	string description;
};

const ResolvedBackend &Resolve() {
	// Function-local static: initialised once, thread-safely, on first RFC use. Doing
	// this at load time instead would turn a missing SDK into a failure to load the
	// extension at all, with no room for a diagnostic.
	static ResolvedBackend resolved = [] {
		RfcBackend backend;
		string library_path;
		{
			std::lock_guard<std::mutex> guard(SelectionLock());
			// An explicit SET outranks the environment; the environment exists so a test
			// run or a container can pick a backend without editing SQL.
			backend = g_backend_explicitly_requested ? g_requested_backend
			                                         : [] {
				                                           const auto from_env = EnvironmentValue("ERPL_RFC_BACKEND");
				                                           return from_env.empty() ? RfcBackend::NWRFC
				                                                                   : ParseBackendName(from_env);
			                                           }();
			library_path = g_requested_library_path.empty() ? EnvironmentValue("ERPL_RFC_BACKEND_PATH")
			                                                : g_requested_library_path;
		}

		auto library = backend == RfcBackend::PROTO ? OpenProtoLibrary(library_path)
		                                            : OpenNwRfcLibrary(library_path);
		auto api = ResolveApi(library);

		// Marked resolved only once a backend is actually serving calls. Setting it before
		// the load would mean a failed load still froze the selection, and the obvious
		// recovery -- correct the path, SET again -- would be refused for a backend that
		// was never loaded. Static initialisation is serialised, so this cannot race, and
		// throwing above leaves the static uninitialised so the next call retries.
		{
			std::lock_guard<std::mutex> guard(SelectionLock());
			g_requested_backend = backend;
			g_backend_resolved = true;
		}

		ERPL_TRACE_INFO("RfcApi", StringUtil::Format("RFC backend: %s", library.description));
		return ResolvedBackend {std::move(api), backend, library.description};
	}();
	return resolved;
}

} // namespace

const char *RfcBackendName(RfcBackend backend) {
	return backend == RfcBackend::PROTO ? "proto" : "nwrfc";
}

void SetRfcBackend(const string &name) {
	const auto requested = ParseBackendName(name);

	std::lock_guard<std::mutex> guard(SelectionLock());
	if (g_backend_resolved) {
		if (requested == g_requested_backend) {
			// Idempotent: re-asserting the active backend is how a test file states its
			// precondition, and refusing that would make every such file order-dependent.
			return;
		}
		throw InvalidInputException(
		    "The RFC backend is already resolved as '%s' and cannot be changed to '%s' in this process. Connections "
		    "and function descriptors belong to one implementation, so switching mid-flight would hand a handle from "
		    "one library to the other. Set erpl_rfc_backend before the first SAP call, or start a new process.",
		    RfcBackendName(g_requested_backend), RfcBackendName(requested));
	}
	g_requested_backend = requested;
	g_backend_explicitly_requested = true;
}

void SetRfcBackendLibraryPath(const string &path) {
	std::lock_guard<std::mutex> guard(SelectionLock());
	// Deliberately does not report the loaded path here: reading it means calling
	// Resolve(), which takes this same lock.
	if (g_backend_resolved && path != g_requested_library_path) {
		throw InvalidInputException("The RFC backend library is already loaded, so setting erpl_rfc_backend_path "
		                            "now would have no effect. Set it before the first SAP call, or start a new "
		                            "process.");
	}
	g_requested_library_path = path;
}

const RfcApi &GetRfcApi() {
	return Resolve().api;
}

RfcBackend GetResolvedRfcBackend() {
	return Resolve().backend;
}

const string &GetRfcBackendDescription() {
	return Resolve().description;
}

} // namespace duckdb
