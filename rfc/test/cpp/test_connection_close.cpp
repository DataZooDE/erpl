#include "catch.hpp"
#include "duckdb.hpp"

#include "sapnwrfc.h"
#include "sap_connection.hpp"

#include <cstdlib>

using namespace duckdb;

// Issue #78: a stale / already-closed RFC_CONNECTION_HANDLE made
// RfcConnection::Close() throw an IOException ("...RFC_INVALID_HANDLE: An
// invalid handle 'RFC_CONNECTION_HANDLE' was passed to the API call").  Because
// the throw escaped the (implicitly noexcept) destructor during connection
// teardown, the host process aborted via std::terminate
// ("terminate called after throwing an instance of 'duckdb::IOException'").
//
// This test reproduces the exact precondition against the live SAP system:
// open a real connection, close its underlying handle out-of-band so the
// wrapper still holds a now-invalid handle, then assert that neither Close()
// nor the destructor throw.  It needs the same ERPL_SAP_* connection
// parameters as the SQL tests and performs a single valid logon.
TEST_CASE("RfcConnection::Close on an already-invalid handle does not throw",
          "[erpl_rfc][connection]") {
	const char *ashost = std::getenv("ERPL_SAP_ASHOST");
	const char *sysnr  = std::getenv("ERPL_SAP_SYSNR");
	const char *user   = std::getenv("ERPL_SAP_USER");
	const char *passwd = std::getenv("ERPL_SAP_PASSWORD");
	const char *client = std::getenv("ERPL_SAP_CLIENT");
	const char *lang   = std::getenv("ERPL_SAP_LANG");

	if (!ashost || !sysnr || !user || !passwd || !client || !lang) {
		WARN("Skipping: ERPL_SAP_* environment variables not set (needs a live SAP system)");
		return;
	}

	RfcAuthParams params;
	params.ashost = ashost;
	params.sysnr = sysnr;
	params.user = user;
	params.password = passwd;
	params.client = client;
	params.lang = lang;

	auto conn = params.Connect();
	REQUIRE(conn != nullptr);
	REQUIRE(conn->handle != NULL);

	// Force the handle invalid out-of-band: close it directly via the SDK,
	// leaving conn->handle non-null.  This is exactly the stale / double-close
	// state that triggered issue #78 on long-running sessions.
	RFC_ERROR_INFO error_info;
	RFC_RC rc = RfcCloseConnection(conn->handle, &error_info);
	REQUIRE(rc == RFC_OK);

	// Before the fix this threw IOException (RFC_INVALID_HANDLE); afterwards it
	// is benign because Close() treats an already-gone handle as nothing to do.
	REQUIRE_NOTHROW(conn->Close());

	// And destruction must also never throw — an escaping exception here would
	// std::terminate the whole process.
	REQUIRE_NOTHROW(conn.reset());
}
