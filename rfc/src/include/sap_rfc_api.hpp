#pragma once

// Runtime dispatch over the SAP RFC C ABI.
//
// erpl no longer links libsapnwrfc. Every SDK entry point it uses is resolved at
// runtime through the table below, so which implementation actually serves a call --
// SAP's SDK or the pure-Rust erpl-proto shim -- is a runtime decision rather than a
// link-time one. See ERPL_PROTO_INTEGRATION_PLAN.md.
//
// Both libraries export the same symbol names, so at most one of them can ever be
// linked or loaded into the global symbol scope; whichever the loader reaches first
// would silently serve every call. That is why this header exists, why the library is
// opened with RTLD_LOCAL, and why nothing here is resolved through RTLD_DEFAULT unless
// the SDK was deliberately preloaded (which the trampoline extension does).
//
// sapnwrfc.h is still included, for its types, structs and enums -- it is a
// compile-time dependency only. The slots below take their signatures from it via
// decltype, so a slot cannot drift from the declaration it stands in for.
//
// A translation unit that needs the real symbols rather than the dispatch macros --
// only sap_rfc_api.cpp does -- defines ERPL_RFC_API_IMPLEMENTATION before including.

#include "duckdb.hpp"
#include "sapnwrfc.h"

namespace duckdb {

#ifndef strlenU16
// The single symbol erpl needs from libsapucum -- a UTF-16 strlen. Supplying it here is
// what lets that library leave the link line as well; keeping a whole shared library of
// the SDK's for one four-line loop would defeat the point of the exercise. Declared in
// namespace duckdb so it shadows the SDK's declaration at every call site, which are all
// inside this namespace. On Windows sapucrfc.h macro-defines strlenU16 to wcslen, so
// there the SDK's own definition stands and this is skipped.
inline size_t strlenU16(const SAP_UTF16 *s) {
	const SAP_UTF16 *cursor = s;
	while (*cursor) {
		cursor++;
	}
	return static_cast<size_t>(cursor - s);
}
#endif

// Every SDK entry point erpl, bics and odp call between them. Adding a call to an entry
// point that is not listed here fails to link, which is the intended safety net: the
// missing name is named, rather than resolving to whatever the loader happens to hold.
#define ERPL_RFC_API_ENTRY_POINTS(X) \
	X(RfcAppendNewRow) \
	X(RfcCloseConnection) \
	X(RfcCreateFunction) \
	X(RfcDestroyFunction) \
	X(RfcGetBytes) \
	X(RfcGetChars) \
	X(RfcGetConnectionAttributes) \
	X(RfcGetCurrentRow) \
	X(RfcGetDate) \
	X(RfcGetDirectionAsString) \
	X(RfcGetFieldCount) \
	X(RfcGetFieldDescByIndex) \
	X(RfcGetFloat) \
	X(RfcGetFunctionDesc) \
	X(RfcGetInt) \
	X(RfcGetInt1) \
	X(RfcGetInt2) \
	X(RfcGetInt8) \
	X(RfcGetNum) \
	X(RfcGetParameterCount) \
	X(RfcGetParameterDescByIndex) \
	X(RfcGetRcAsString) \
	X(RfcGetRowCount) \
	X(RfcGetString) \
	X(RfcGetStringLength) \
	X(RfcGetStructure) \
	X(RfcGetTable) \
	X(RfcGetTime) \
	X(RfcGetTypeAsString) \
	X(RfcGetTypeName) \
	X(RfcGetXString) \
	X(RfcInvoke) \
	X(RfcMoveTo) \
	X(RfcOpenConnection) \
	X(RfcPing) \
	X(RfcReloadIniFile) \
	X(RfcSAPUCToUTF8) \
	X(RfcSetBytes) \
	X(RfcSetDate) \
	X(RfcSetFloat) \
	X(RfcSetIniPath) \
	X(RfcSetInt) \
	X(RfcSetInt1) \
	X(RfcSetInt2) \
	X(RfcSetInt8) \
	X(RfcSetMaximumStoredTraceFiles) \
	X(RfcSetMaximumTraceFileSize) \
	X(RfcSetNum) \
	X(RfcSetParameterActive) \
	X(RfcSetString) \
	X(RfcSetTime) \
	X(RfcSetTraceDir) \
	X(RfcSetTraceLevel) \
	X(RfcSetXString) \
	X(RfcUTF8ToSAPUC)

// The resolved entry points. Signatures come from sapnwrfc.h, never from transcription.
struct RfcApi {
#define ERPL_RFC_API_DECLARE_SLOT(name) decltype(&::name) name;
	ERPL_RFC_API_ENTRY_POINTS(ERPL_RFC_API_DECLARE_SLOT)
#undef ERPL_RFC_API_DECLARE_SLOT
};

// Which implementation serves RFC calls.
enum class RfcBackend : uint8_t {
	// SAP's own libsapnwrfc from the NetWeaver RFC SDK. The default, and what every
	// released build has used to date.
	NWRFC,
	// The pure-Rust erpl-proto shim. Opt-in while its test legs are still being closed.
	PROTO
};

const char *RfcBackendName(RfcBackend backend);

// Selects the backend, by the name RfcBackendName returns. Backed by the
// erpl_rfc_backend setting and the ERPL_RFC_BACKEND environment variable.
//
// A backend can only be chosen before the first RFC call. Once resolved it is frozen for
// the life of the process: connections, function descriptors and cached handles all
// belong to one implementation, and letting a later SET redirect calls half-way through
// would hand a handle from one library to the other. Selecting the backend that is
// already active is accepted, so re-running a SET is not an error.
void SetRfcBackend(const string &name);

// An explicit path to the backend library, overriding the search. Backed by the
// erpl_rfc_backend_path setting and ERPL_RFC_BACKEND_PATH.
void SetRfcBackendLibraryPath(const string &path);

// Resolves the backend library on first call and fills the table. Throws if the library
// cannot be opened or an entry point is missing -- a half-filled table would fail later,
// at a call site with no clue as to why.
const RfcApi &GetRfcApi();

// The backend actually serving calls, resolving it if that has not happened yet.
RfcBackend GetResolvedRfcBackend();

// Human-readable description of what is serving RFC calls, including which library file
// it came from. For tracing and diagnostics; sap_rfc_backend() reports the bare name,
// which is stable enough to assert on in tests.
const string &GetRfcBackendDescription();

} // namespace duckdb

#ifndef ERPL_RFC_API_IMPLEMENTATION
// Call sites are left verbatim: the macro expands to a call through a pointer of exactly
// the declared type, so overload resolution and implicit conversions behave as before.
#define RfcAppendNewRow (::duckdb::GetRfcApi().RfcAppendNewRow)
#define RfcCloseConnection (::duckdb::GetRfcApi().RfcCloseConnection)
#define RfcCreateFunction (::duckdb::GetRfcApi().RfcCreateFunction)
#define RfcDestroyFunction (::duckdb::GetRfcApi().RfcDestroyFunction)
#define RfcGetBytes (::duckdb::GetRfcApi().RfcGetBytes)
#define RfcGetChars (::duckdb::GetRfcApi().RfcGetChars)
#define RfcGetConnectionAttributes (::duckdb::GetRfcApi().RfcGetConnectionAttributes)
#define RfcGetCurrentRow (::duckdb::GetRfcApi().RfcGetCurrentRow)
#define RfcGetDate (::duckdb::GetRfcApi().RfcGetDate)
#define RfcGetDirectionAsString (::duckdb::GetRfcApi().RfcGetDirectionAsString)
#define RfcGetFieldCount (::duckdb::GetRfcApi().RfcGetFieldCount)
#define RfcGetFieldDescByIndex (::duckdb::GetRfcApi().RfcGetFieldDescByIndex)
#define RfcGetFloat (::duckdb::GetRfcApi().RfcGetFloat)
#define RfcGetFunctionDesc (::duckdb::GetRfcApi().RfcGetFunctionDesc)
#define RfcGetInt (::duckdb::GetRfcApi().RfcGetInt)
#define RfcGetInt1 (::duckdb::GetRfcApi().RfcGetInt1)
#define RfcGetInt2 (::duckdb::GetRfcApi().RfcGetInt2)
#define RfcGetInt8 (::duckdb::GetRfcApi().RfcGetInt8)
#define RfcGetNum (::duckdb::GetRfcApi().RfcGetNum)
#define RfcGetParameterCount (::duckdb::GetRfcApi().RfcGetParameterCount)
#define RfcGetParameterDescByIndex (::duckdb::GetRfcApi().RfcGetParameterDescByIndex)
#define RfcGetRcAsString (::duckdb::GetRfcApi().RfcGetRcAsString)
#define RfcGetRowCount (::duckdb::GetRfcApi().RfcGetRowCount)
#define RfcGetString (::duckdb::GetRfcApi().RfcGetString)
#define RfcGetStringLength (::duckdb::GetRfcApi().RfcGetStringLength)
#define RfcGetStructure (::duckdb::GetRfcApi().RfcGetStructure)
#define RfcGetTable (::duckdb::GetRfcApi().RfcGetTable)
#define RfcGetTime (::duckdb::GetRfcApi().RfcGetTime)
#define RfcGetTypeAsString (::duckdb::GetRfcApi().RfcGetTypeAsString)
#define RfcGetTypeName (::duckdb::GetRfcApi().RfcGetTypeName)
#define RfcGetXString (::duckdb::GetRfcApi().RfcGetXString)
#define RfcInvoke (::duckdb::GetRfcApi().RfcInvoke)
#define RfcMoveTo (::duckdb::GetRfcApi().RfcMoveTo)
#define RfcOpenConnection (::duckdb::GetRfcApi().RfcOpenConnection)
#define RfcPing (::duckdb::GetRfcApi().RfcPing)
#define RfcReloadIniFile (::duckdb::GetRfcApi().RfcReloadIniFile)
#define RfcSAPUCToUTF8 (::duckdb::GetRfcApi().RfcSAPUCToUTF8)
#define RfcSetBytes (::duckdb::GetRfcApi().RfcSetBytes)
#define RfcSetDate (::duckdb::GetRfcApi().RfcSetDate)
#define RfcSetFloat (::duckdb::GetRfcApi().RfcSetFloat)
#define RfcSetIniPath (::duckdb::GetRfcApi().RfcSetIniPath)
#define RfcSetInt (::duckdb::GetRfcApi().RfcSetInt)
#define RfcSetInt1 (::duckdb::GetRfcApi().RfcSetInt1)
#define RfcSetInt2 (::duckdb::GetRfcApi().RfcSetInt2)
#define RfcSetInt8 (::duckdb::GetRfcApi().RfcSetInt8)
#define RfcSetMaximumStoredTraceFiles (::duckdb::GetRfcApi().RfcSetMaximumStoredTraceFiles)
#define RfcSetMaximumTraceFileSize (::duckdb::GetRfcApi().RfcSetMaximumTraceFileSize)
#define RfcSetNum (::duckdb::GetRfcApi().RfcSetNum)
#define RfcSetParameterActive (::duckdb::GetRfcApi().RfcSetParameterActive)
#define RfcSetString (::duckdb::GetRfcApi().RfcSetString)
#define RfcSetTime (::duckdb::GetRfcApi().RfcSetTime)
#define RfcSetTraceDir (::duckdb::GetRfcApi().RfcSetTraceDir)
#define RfcSetTraceLevel (::duckdb::GetRfcApi().RfcSetTraceLevel)
#define RfcSetXString (::duckdb::GetRfcApi().RfcSetXString)
#define RfcUTF8ToSAPUC (::duckdb::GetRfcApi().RfcUTF8ToSAPUC)
#endif
