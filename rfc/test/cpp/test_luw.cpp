#include "catch.hpp"
#include "duckdb.hpp"
#include "duckdb/main/client_context.hpp"

#include "sap_luw.hpp"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

using namespace duckdb;

// The DuckDB transaction is the SAP LUW (issue #244). These cases drive the
// RfcLuwState through real DuckDB BEGIN / COMMIT / ROLLBACK statements, with the
// SAP side replaced by a fake connector and finisher, so they need no SAP system.
// The live counterpart is rfc/test/sql/sap_rfc_luw_transaction.test.

namespace {

struct FakeSap {
	int opened = 0;
	std::vector<std::string> finished; // "COMMIT:<user>" / "ROLLBACK:<user>"
	std::string fail_commit_for_user;  // a COMMIT for this user throws

	duckdb::shared_ptr<RfcLuwState> MakeState() {
		auto connector = [this](RfcAuthParams &auth) {
			opened++;
			// A fake, never dereferenced handle. The finisher nulls it, so the owning
			// RfcConnection never hands it to RfcCloseConnection.
			auto handle = reinterpret_cast<RFC_CONNECTION_HANDLE>(static_cast<uintptr_t>(0x1000 + opened));
			auto conn = std::make_shared<RfcConnection>(handle);
			users[handle] = auth.user;
			return conn;
		};
		auto finisher = [this](std::shared_ptr<RfcConnection> &conn, RfcLuwOutcome outcome) {
			auto user = users[conn->handle];
			conn->handle = NULL;
			if (outcome == RfcLuwOutcome::COMMIT) {
				finished.push_back("COMMIT:" + user);
				if (user == fail_commit_for_user) {
					throw IOException("SAP LUW: BAPI_TRANSACTION_COMMIT returned E: update failed");
				}
			} else {
				finished.push_back("ROLLBACK:" + user);
			}
		};
		return make_shared_ptr<RfcLuwState>(connector, finisher);
	}

	std::map<RFC_CONNECTION_HANDLE, std::string> users;
};

RfcAuthParams Auth(const std::string &user) {
	RfcAuthParams auth;
	auth.ashost = "fake-host";
	auth.sysnr = "00";
	auth.client = "001";
	auth.user = user;
	auth.password = "not-a-password";
	return auth;
}

} // namespace

TEST_CASE("LUW: no explicit transaction means no held session", "[erpl_rfc][luw]") {
	DuckDB db(nullptr);
	Connection con(db);
	FakeSap sap;
	auto state = sap.MakeState();
	con.context->registered_state->Insert(RfcLuwState::KEY, state);

	// Autocommit: InExplicitTransaction is false, so ConnectInContext takes the
	// unchanged per-call path (auth.Connect()) and never touches the state.
	REQUIRE_FALSE(InExplicitTransaction(*con.context));
	REQUIRE_FALSE(con.Query("SELECT 42")->HasError());
	REQUIRE(state->HeldSessions() == 0);
	REQUIRE(sap.opened == 0);
	REQUIRE(sap.finished.empty());

	REQUIRE_FALSE(con.Query("BEGIN")->HasError());
	REQUIRE(InExplicitTransaction(*con.context));
	REQUIRE_FALSE(con.Query("COMMIT")->HasError());
	REQUIRE_FALSE(InExplicitTransaction(*con.context));
	// A transaction without any SAP call commits nothing on the SAP side.
	REQUIRE(sap.finished.empty());
}

TEST_CASE("LUW: one session per connection parameters, committed on COMMIT", "[erpl_rfc][luw]") {
	DuckDB db(nullptr);
	Connection con(db);
	FakeSap sap;
	auto state = sap.MakeState();
	con.context->registered_state->Insert(RfcLuwState::KEY, state);

	REQUIRE_FALSE(con.Query("BEGIN")->HasError());
	auto a = Auth("DEVELOPER");
	auto b = Auth("BWDEVELOPER");
	auto first = ConnectInContext(*con.context, a);
	auto second = ConnectInContext(*con.context, a);
	auto other = ConnectInContext(*con.context, b);

	REQUIRE(sap.opened == 2);
	REQUIRE(state->HeldSessions() == 2);
	REQUIRE(first->handle == second->handle);
	REQUIRE(first->handle != other->handle);
	// Callers get views: closing one (as every scanner does) must not end the LUW.
	REQUIRE_FALSE(first->owns_handle);
	REQUIRE(first->call_lock != nullptr);
	REQUIRE(first->call_lock == second->call_lock);
	auto shared_handle = second->handle;
	first->Close();
	REQUIRE(first->handle == NULL);
	REQUIRE(ConnectInContext(*con.context, a)->handle == shared_handle);
	REQUIRE(sap.opened == 2);

	REQUIRE_FALSE(con.Query("COMMIT")->HasError());
	REQUIRE(state->HeldSessions() == 0);
	REQUIRE(sap.finished == std::vector<std::string>({"COMMIT:DEVELOPER", "COMMIT:BWDEVELOPER"}));

	// The next transaction opens a fresh session.
	REQUIRE_FALSE(con.Query("BEGIN")->HasError());
	ConnectInContext(*con.context, a);
	REQUIRE(sap.opened == 3);
	REQUIRE_FALSE(con.Query("ROLLBACK")->HasError());
	REQUIRE(sap.finished.back() == "ROLLBACK:DEVELOPER");
	REQUIRE(state->HeldSessions() == 0);
}

TEST_CASE("LUW: ROLLBACK and a failed statement roll the SAP LUW back", "[erpl_rfc][luw]") {
	DuckDB db(nullptr);
	Connection con(db);
	FakeSap sap;
	auto state = sap.MakeState();
	con.context->registered_state->Insert(RfcLuwState::KEY, state);
	auto a = Auth("DEVELOPER");

	SECTION("explicit ROLLBACK") {
		REQUIRE_FALSE(con.Query("BEGIN")->HasError());
		ConnectInContext(*con.context, a);
		REQUIRE_FALSE(con.Query("ROLLBACK")->HasError());
		REQUIRE(sap.finished == std::vector<std::string>({"ROLLBACK:DEVELOPER"}));
	}
	SECTION("failed statement, then COMMIT") {
		REQUIRE_FALSE(con.Query("BEGIN")->HasError());
		ConnectInContext(*con.context, a);
		REQUIRE(con.Query("SELECT error('boom')")->HasError());
		// DuckDB turns the COMMIT of an aborted transaction into a ROLLBACK.
		REQUIRE_FALSE(con.Query("COMMIT")->HasError());
		REQUIRE(sap.finished == std::vector<std::string>({"ROLLBACK:DEVELOPER"}));
	}
	SECTION("failed statement, then ROLLBACK") {
		REQUIRE_FALSE(con.Query("BEGIN")->HasError());
		ConnectInContext(*con.context, a);
		REQUIRE(con.Query("SELECT error('boom')")->HasError());
		REQUIRE_FALSE(con.Query("ROLLBACK")->HasError());
		REQUIRE(sap.finished == std::vector<std::string>({"ROLLBACK:DEVELOPER"}));
	}
	REQUIRE(state->HeldSessions() == 0);
}

TEST_CASE("LUW: a rejected SAP commit fails COMMIT and rolls back the rest", "[erpl_rfc][luw]") {
	DuckDB db(nullptr);
	Connection con(db);
	FakeSap sap;
	sap.fail_commit_for_user = "DEVELOPER";
	auto state = sap.MakeState();
	con.context->registered_state->Insert(RfcLuwState::KEY, state);

	REQUIRE_FALSE(con.Query("BEGIN")->HasError());
	auto a = Auth("DEVELOPER");
	auto b = Auth("BWDEVELOPER");
	ConnectInContext(*con.context, a);
	ConnectInContext(*con.context, b);

	REQUIRE_FALSE(con.Query("CREATE TABLE local_write AS SELECT 1 AS x")->HasError());

	auto result = con.Query("COMMIT");
	REQUIRE(result->HasError());
	REQUIRE(result->GetError().find("update failed") != std::string::npos);
	REQUIRE(sap.finished == std::vector<std::string>({"COMMIT:DEVELOPER", "ROLLBACK:BWDEVELOPER"}));
	REQUIRE(state->HeldSessions() == 0);
	// The SAP commit runs before DuckDB's: rejected, it leaves the DuckDB transaction
	// aborted, so it can only end in a rollback -- the local write is gone too.
	REQUIRE(InExplicitTransaction(*con.context));
	REQUIRE(con.Query("SELECT 1")->HasError());
	REQUIRE_FALSE(con.Query("ROLLBACK")->HasError());
	REQUIRE(con.Query("SELECT * FROM local_write")->HasError());
	// The connection stays usable.
	auto after = con.Query("SELECT 1");
	std::string after_error = after->HasError() ? after->GetError() : std::string("ok");
	INFO(after_error);
	REQUIRE_FALSE(after->HasError());
}

TEST_CASE("LUW: closing the DuckDB connection mid-transaction rolls back", "[erpl_rfc][luw]") {
	DuckDB db(nullptr);
	FakeSap sap;
	auto state = sap.MakeState();
	{
		Connection con(db);
		con.context->registered_state->Insert(RfcLuwState::KEY, state);
		REQUIRE_FALSE(con.Query("BEGIN")->HasError());
		auto a = Auth("DEVELOPER");
		ConnectInContext(*con.context, a);
		REQUIRE(state->HeldSessions() == 1);
	}
	REQUIRE(sap.finished == std::vector<std::string>({"ROLLBACK:DEVELOPER"}));
	REQUIRE(state->HeldSessions() == 0);
}

TEST_CASE("LUW: a COMMIT that SAP accepts commits SAP first, then DuckDB", "[erpl_rfc][luw]") {
	DuckDB db(nullptr);
	Connection con(db);
	FakeSap sap;
	auto state = sap.MakeState();
	con.context->registered_state->Insert(RfcLuwState::KEY, state);

	REQUIRE_FALSE(con.Query("BEGIN")->HasError());
	auto a = Auth("DEVELOPER");
	ConnectInContext(*con.context, a);
	REQUIRE_FALSE(con.Query("CREATE TABLE local_write AS SELECT 1 AS x")->HasError());
	REQUIRE_FALSE(con.Query("COMMIT")->HasError());
	REQUIRE(sap.finished == std::vector<std::string>({"COMMIT:DEVELOPER"}));
	REQUIRE_FALSE(con.Query("SELECT * FROM local_write")->HasError());

	// A COMMIT prepared earlier and executed later still commits SAP, via TransactionCommit.
	REQUIRE_FALSE(con.Query("BEGIN")->HasError());
	ConnectInContext(*con.context, a);
	auto prepared_commit = con.Prepare("COMMIT");
	REQUIRE_FALSE(prepared_commit->HasError());
	REQUIRE_FALSE(prepared_commit->Execute()->HasError());
	REQUIRE(sap.finished.size() == 2);
	REQUIRE(sap.finished.back() == "COMMIT:DEVELOPER");
	REQUIRE(state->HeldSessions() == 0);
}

TEST_CASE("LUW: the state's destructor rolls back what is still held", "[erpl_rfc][luw]") {
	FakeSap sap;
	{
		auto state = sap.MakeState();
		auto a = Auth("DEVELOPER");
		state->Acquire(a);
		REQUIRE(state->HeldSessions() == 1);
	}
	REQUIRE(sap.finished == std::vector<std::string>({"ROLLBACK:DEVELOPER"}));
}
