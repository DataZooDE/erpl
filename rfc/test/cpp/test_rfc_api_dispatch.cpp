// Tests for the runtime RFC dispatch layer. No SAP system is involved: resolving a
// backend is a dlopen and a table of dlsyms, so all of this runs offline.
//
// ERPL_RFC_API_IMPLEMENTATION suppresses the dispatch macros, so this file can name the
// entry points as plain tokens.
#define ERPL_RFC_API_IMPLEMENTATION
#include "sap_rfc_api.hpp"

#include "catch.hpp"

#include <algorithm>
#include <cstdlib>
#include <string>
#include <vector>

#ifndef _WIN32
#include <dlfcn.h>
#endif

using namespace duckdb;

namespace {

// The entry points erpl dispatches through, taken from the header itself so this cannot
// drift from what the extension actually calls.
std::vector<std::string> EntryPointNames() {
	return {
#define ERPL_RFC_API_NAME(name) #name,
	    ERPL_RFC_API_ENTRY_POINTS(ERPL_RFC_API_NAME)
#undef ERPL_RFC_API_NAME
	};
}

} // namespace

TEST_CASE("The dispatch table covers a plausible entry-point set", "[rfc_api]") {
	const auto names = EntryPointNames();

	// A guard against the X-macro list being silently emptied or mangled by an edit: the
	// table would then resolve nothing and every RFC call would go through a null pointer.
	REQUIRE(names.size() > 40);

	// One name from each family, so a partial deletion is caught rather than only a total one.
	for (const auto &expected : {"RfcOpenConnection", "RfcInvoke", "RfcGetTable", "RfcSetString",
	                             "RfcGetFunctionDesc", "RfcUTF8ToSAPUC"}) {
		REQUIRE(std::find(names.begin(), names.end(), expected) != names.end());
	}
}

TEST_CASE("Every dispatch slot is filled after resolution", "[rfc_api]") {
	// Resolves the default backend (nwrfc). A missing entry point throws during
	// resolution rather than leaving a null slot, so reaching here at all is most of the
	// assertion; the loop covers the case of the table being filled but incompletely.
	const auto &api = GetRfcApi();

	idx_t null_slots = 0;
#define ERPL_RFC_API_CHECK_SLOT(name)                                                                                  \
	if (api.name == nullptr) {                                                                                         \
		null_slots++;                                                                                                  \
	}
	ERPL_RFC_API_ENTRY_POINTS(ERPL_RFC_API_CHECK_SLOT)
#undef ERPL_RFC_API_CHECK_SLOT

	REQUIRE(null_slots == 0);
	REQUIRE_FALSE(GetRfcBackendDescription().empty());
}

TEST_CASE("An unknown backend name is rejected", "[rfc_api]") {
	// Rejected before any state is touched, so this cannot disturb the backend the rest
	// of the suite is using.
	REQUIRE_THROWS_AS(SetRfcBackend("libsapnwrfc"), InvalidInputException);
	REQUIRE_THROWS_AS(SetRfcBackend(""), InvalidInputException);
}

TEST_CASE("Backend names are accepted case- and alias-insensitively", "[rfc_api]") {
	// Naming the backend that is already active must succeed: a caller stating its
	// precondition should not depend on whether something else resolved it first.
	const auto active = std::string(RfcBackendName(GetResolvedRfcBackend()));
	REQUIRE_NOTHROW(SetRfcBackend(active));
	REQUIRE_NOTHROW(SetRfcBackend(StringUtil::Upper(active)));
	REQUIRE_NOTHROW(SetRfcBackend("  " + active + "  "));
}

TEST_CASE("Switching backend after resolution is refused", "[rfc_api]") {
	const auto active = GetResolvedRfcBackend();
	const auto other = active == RfcBackend::PROTO ? "nwrfc" : "proto";

	// Handles and function descriptors belong to one implementation. Silently switching
	// would hand a handle from one library to the other, which is why this is an error
	// and not a best-effort reconfiguration.
	REQUIRE_THROWS_AS(SetRfcBackend(other), InvalidInputException);

	// And the refusal must not have changed anything.
	REQUIRE(GetResolvedRfcBackend() == active);
}

#ifndef _WIN32
TEST_CASE("The proto backend provides every entry point erpl needs", "[rfc_api]") {
	// The contract between erpl and erpl-proto. Skipped when the private submodule was
	// not part of this build; failing here would only punish a contributor without access.
	const char *proto_path = std::getenv("ERPL_RFC_BACKEND_PATH");
	std::string path = proto_path ? proto_path : "";
	if (path.empty() || path.find("erpl_proto") == std::string::npos) {
		WARN("ERPL_RFC_BACKEND_PATH does not point at the proto shim; skipping the contract check");
		return;
	}

	// RTLD_LOCAL matters even here: the SDK may already be resident, and a global load of
	// a library exporting the same names would have the two interposing.
	void *handle = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
	REQUIRE(handle != nullptr);

	std::vector<std::string> missing;
	for (const auto &name : EntryPointNames()) {
		if (!dlsym(handle, name.c_str())) {
			missing.push_back(name);
		}
	}

	std::string missing_list;
	for (const auto &name : missing) {
		missing_list += (missing_list.empty() ? "" : ", ") + name;
	}
	INFO("missing from the proto backend: " << missing_list);
	REQUIRE(missing.empty());
}
#endif
