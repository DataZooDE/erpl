#include "catch.hpp"
#include "duckdb.hpp"
#include "duckdb/main/client_context.hpp"

#include "sap_function.hpp"
#include "sap_luw.hpp"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

using namespace duckdb;

// The DuckDB transaction is the SAP LUW (issue #244). These cases drive the
// RfcLuwState through real DuckDB BEGIN / COMMIT / ROLLBACK statements, with the
// SAP side replaced by a fake connector, finisher and closer, so they need no SAP
// system. The live counterpart is rfc/test/sql/sap_rfc_luw_transaction.test.

namespace {

struct FakeSap {
	int opened = 0;
	int closed = 0;
	std::vector<std::string> finished; // "COMMIT:<user>" / "ROLLBACK:<user>"
	std::string fail_commit_for_user;  // a COMMIT for this user throws

	duckdb::shared_ptr<RfcLuwState> MakeState() {
		auto connector = [this](RfcAuthParams &auth) {
			opened++;
			// A fake, never dereferenced handle: the closer abandons it, so it never
			// reaches RfcCloseConnection.
			auto handle = reinterpret_cast<RFC_CONNECTION_HANDLE>(static_cast<uintptr_t>(0x1000 + opened));
			auto conn = std::make_shared<RfcConnection>(handle);
			users[handle] = auth.user;
			return conn;
		};
		auto finisher = [this](std::shared_ptr<RfcConnection> &conn, RfcLuwOutcome outcome) {
			auto user = users[conn->handle];
			if (outcome == RfcLuwOutcome::COMMIT) {
				finished.push_back("COMMIT:" + user);
				if (user == fail_commit_for_user) {
					throw IOException(RfcCommitRejectedMessage(RfcCommitVerdict {true, "E", "ZZ", "001", "update failed"}));
				}
			} else {
				finished.push_back("ROLLBACK:" + user);
			}
		};
		auto closer = [this](RfcConnection &conn) {
			closed++;
			conn.Abandon();
		};
		return make_shared_ptr<RfcLuwState>(connector, finisher, closer);
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

Value ReturnStruct(const std::string &type, const std::string &message) {
	child_list_t<Value> fields;
	fields.emplace_back("TYPE", Value(type));
	fields.emplace_back("ID", Value("BC"));
	fields.emplace_back("NUMBER", Value("042"));
	fields.emplace_back("MESSAGE", Value(message));
	return Value::STRUCT(std::move(fields));
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
	// Without a held session the state does not make DuckDB bind every statement twice.
	REQUIRE_FALSE(state->CanRequestRebind());

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
	REQUIRE(state->CanRequestRebind());
	REQUIRE(first->handle == second->handle);
	REQUIRE(first->handle != other->handle);
	// Callers get views: closing one (as every scanner does) must not end the LUW.
	REQUIRE_FALSE(first->owns_handle);
	REQUIRE(first->core != nullptr);
	REQUIRE(first->core == second->core);
	auto shared_handle = second->handle;
	first->Close();
	REQUIRE(first->handle == NULL);
	REQUIRE(ConnectInContext(*con.context, a)->handle == shared_handle);
	REQUIRE(sap.opened == 2);

	REQUIRE_FALSE(con.Query("COMMIT")->HasError());
	REQUIRE(state->HeldSessions() == 0);
	REQUIRE_FALSE(state->CanRequestRebind());
	REQUIRE(sap.finished == std::vector<std::string>({"COMMIT:DEVELOPER", "COMMIT:BWDEVELOPER"}));
	REQUIRE(sap.closed == 2);

	// The next transaction opens a fresh session.
	REQUIRE_FALSE(con.Query("BEGIN")->HasError());
	ConnectInContext(*con.context, a);
	REQUIRE(sap.opened == 3);
	REQUIRE_FALSE(con.Query("ROLLBACK")->HasError());
	REQUIRE(sap.finished.back() == "ROLLBACK:DEVELOPER");
	REQUIRE(state->HeldSessions() == 0);
	REQUIRE(sap.closed == 3);
}

TEST_CASE("LUW: a view outliving its transaction fails cleanly instead of using a closed session",
          "[erpl_rfc][luw]") {
	DuckDB db(nullptr);
	Connection con(db);
	FakeSap sap;
	auto state = sap.MakeState();
	con.context->registered_state->Insert(RfcLuwState::KEY, state);
	auto a = Auth("DEVELOPER");
	const std::string ended = "transaction this statement was bound in has ended";

	for (auto end : {"COMMIT", "ROLLBACK"}) {
		INFO(end);
		REQUIRE_FALSE(con.Query("BEGIN")->HasError());
		// What a prepared statement's bind data keeps.
		auto view = ConnectInContext(*con.context, a);
		REQUIRE_NOTHROW(view->CallGuard());
		REQUIRE_FALSE(con.Query(end)->HasError());

		// The view still has its (now stale) copy of the handle, as every caller's
		// `connection->handle != NULL` check sees it ...
		REQUIRE(view->handle != NULL);
		// ... but no SDK call gets past the guard.
		REQUIRE_THROWS_WITH(view->CallGuard(), Catch::Contains(ended));
		REQUIRE_THROWS_WITH(view->Ping(), Catch::Contains(ended));
		REQUIRE_THROWS_WITH(std::make_shared<RfcFunction>(view, "STFC_CONNECTION"), Catch::Contains(ended));
	}
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
	REQUIRE(sap.closed == 1);
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
	REQUIRE(result->GetError().find("SAP rejected the commit") != std::string::npos);
	REQUIRE(sap.finished == std::vector<std::string>({"COMMIT:DEVELOPER", "ROLLBACK:BWDEVELOPER"}));
	REQUIRE(state->HeldSessions() == 0);
	REQUIRE(sap.closed == 2);
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

TEST_CASE("LUW: a lost session fails the next call and the COMMIT, without committing", "[erpl_rfc][luw]") {
	DuckDB db(nullptr);
	Connection con(db);
	FakeSap sap;
	auto state = sap.MakeState();
	con.context->registered_state->Insert(RfcLuwState::KEY, state);
	auto a = Auth("DEVELOPER");

	REQUIRE_FALSE(con.Query("BEGIN")->HasError());
	auto view = ConnectInContext(*con.context, a);
	// What RfcCallGuard::Note does when a round-trip ends with RFC_COMMUNICATION_FAILURE,
	// RFC_ABAP_RUNTIME_FAILURE (a short dump) or RFC_INVALID_HANDLE.
	{
		auto guard = view->CallGuard();
		guard.Note(RFC_COMMUNICATION_FAILURE);
	}
	REQUIRE(view->IsLost());
	REQUIRE_THROWS_WITH(ConnectInContext(*con.context, a), Catch::Contains("was lost"));

	auto result = con.Query("COMMIT");
	REQUIRE(result->HasError());
	REQUIRE(result->GetError().find("was lost") != std::string::npos);
	// No BAPI_TRANSACTION_COMMIT on a dead session.
	REQUIRE(sap.finished.empty());
	REQUIRE_FALSE(con.Query("ROLLBACK")->HasError());
	REQUIRE(state->HeldSessions() == 0);
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
	REQUIRE(sap.closed == 1);
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

TEST_CASE("LUW: a SAP rejection of a prepared COMMIT is reported later, never thrown from the commit hook",
          "[erpl_rfc][luw]") {
	DuckDB db(nullptr);
	Connection con(db);
	FakeSap sap;
	sap.fail_commit_for_user = "DEVELOPER";
	auto state = sap.MakeState();
	con.context->registered_state->Insert(RfcLuwState::KEY, state);
	auto a = Auth("DEVELOPER");
	auto b = Auth("BWDEVELOPER");

	// A prepared COMMIT executed later commits in TransactionCommit, after DuckDB's local
	// commit. Throwing there would skip the other states' TransactionCommit and
	// Finalize() -- in a debug build DuckDB's own DebugClientContextState then fails the
	// next statement with "transaction already active".
	auto prepared_commit = con.Prepare("COMMIT");
	REQUIRE_FALSE(prepared_commit->HasError());
	REQUIRE_FALSE(con.Query("BEGIN")->HasError());
	ConnectInContext(*con.context, a);
	REQUIRE_FALSE(con.Query("CREATE TABLE committed_locally AS SELECT 1 AS x")->HasError());
	REQUIRE_FALSE(prepared_commit->Execute()->HasError());
	REQUIRE(sap.finished == std::vector<std::string>({"COMMIT:DEVELOPER"}));
	REQUIRE(state->HeldSessions() == 0);

	// DuckDB is consistent: the local write is committed and the connection works.
	REQUIRE_FALSE(con.Query("SELECT * FROM committed_locally")->HasError());
	REQUIRE_FALSE(con.Query("BEGIN")->HasError());
	REQUIRE_FALSE(con.Query("COMMIT")->HasError());

	// The SAP rejection surfaces on the next erpl call, once.
	REQUIRE_FALSE(con.Query("BEGIN")->HasError());
	REQUIRE_THROWS_WITH(ConnectInContext(*con.context, b), Catch::Contains("did not complete on the SAP side"));
	auto next = ConnectInContext(*con.context, b);
	REQUIRE(next != nullptr);
	REQUIRE_FALSE(con.Query("COMMIT")->HasError());
	REQUIRE(sap.finished.back() == "COMMIT:BWDEVELOPER");
}

TEST_CASE("LUW: two DuckDB connections have separate SAP sessions", "[erpl_rfc][luw]") {
	DuckDB db(nullptr);
	Connection con1(db);
	Connection con2(db);
	FakeSap sap1, sap2;
	auto state1 = sap1.MakeState();
	auto state2 = sap2.MakeState();
	con1.context->registered_state->Insert(RfcLuwState::KEY, state1);
	con2.context->registered_state->Insert(RfcLuwState::KEY, state2);
	auto a = Auth("DEVELOPER");

	REQUIRE_FALSE(con1.Query("BEGIN")->HasError());
	REQUIRE_FALSE(con2.Query("BEGIN")->HasError());
	auto v1 = ConnectInContext(*con1.context, a);
	auto v2 = ConnectInContext(*con2.context, a);
	REQUIRE(sap1.opened == 1);
	REQUIRE(sap2.opened == 1);
	REQUIRE(v1->core != v2->core);
	REQUIRE_FALSE(con1.Query("COMMIT")->HasError());
	REQUIRE(sap1.finished == std::vector<std::string>({"COMMIT:DEVELOPER"}));
	// The other connection's transaction is untouched.
	REQUIRE(sap2.finished.empty());
	REQUIRE(state2->HeldSessions() == 1);
	REQUIRE_NOTHROW(v2->CallGuard());
	REQUIRE_FALSE(con2.Query("ROLLBACK")->HasError());
	REQUIRE(sap2.finished == std::vector<std::string>({"ROLLBACK:DEVELOPER"}));
}

TEST_CASE("LUW: a state of a different build under the key is refused", "[erpl_rfc][luw]") {
	// What another erpl binary of a different build would have registered.
	struct Impostor : public ClientContextState {
		uint64_t magic = 1;
		uint64_t size = 2;
	};
	DuckDB db(nullptr);
	Connection con(db);
	con.context->registered_state->Insert(RfcLuwState::KEY, make_shared_ptr<Impostor>());
	REQUIRE_FALSE(con.Query("BEGIN")->HasError());
	auto a = Auth("DEVELOPER");
	REQUIRE_THROWS_WITH(ConnectInContext(*con.context, a), Catch::Contains("different builds"));
	REQUIRE_FALSE(con.Query("ROLLBACK")->HasError());
}

TEST_CASE("LUW: the state's destructor closes what is still held, without a round-trip", "[erpl_rfc][luw]") {
	FakeSap sap;
	{
		auto state = sap.MakeState();
		auto a = Auth("DEVELOPER");
		state->Acquire(a);
		REQUIRE(state->HeldSessions() == 1);
	}
	// Closing without a commit discards the SAP LUW; no BAPI_TRANSACTION_ROLLBACK that
	// could hang on a dead host at process exit.
	REQUIRE(sap.finished.empty());
	REQUIRE(sap.closed == 1);
}

TEST_CASE("LUW: BAPI_TRANSACTION_COMMIT's RETURN is evaluated", "[erpl_rfc][luw]") {
	REQUIRE_FALSE(EvaluateCommitReturn(Value()).rejected);
	REQUIRE_FALSE(EvaluateCommitReturn(ReturnStruct("", "")).rejected);
	REQUIRE_FALSE(EvaluateCommitReturn(ReturnStruct("S", "Saved")).rejected);
	REQUIRE_FALSE(EvaluateCommitReturn(ReturnStruct("W", "Warning")).rejected);
	REQUIRE_FALSE(EvaluateCommitReturn(ReturnStruct("I", "Info")).rejected);

	auto error = EvaluateCommitReturn(ReturnStruct("E", "Update terminated"));
	REQUIRE(error.rejected);
	REQUIRE(error.type == "E");
	REQUIRE(error.id == "BC");
	REQUIRE(error.number == "042");
	REQUIRE(error.message == "Update terminated");
	REQUIRE(EvaluateCommitReturn(ReturnStruct("a", "Abort")).rejected);

	// A RETURN table: any E/A row rejects.
	auto rows = Value::LIST({ReturnStruct("S", "ok"), ReturnStruct("A", "Aborted")});
	auto from_table = EvaluateCommitReturn(rows);
	REQUIRE(from_table.rejected);
	REQUIRE(from_table.type == "A");
	REQUIRE_FALSE(EvaluateCommitReturn(Value::LIST({ReturnStruct("S", "ok")})).rejected);

	// Rejected and outcome-unknown read differently: only the first says "not committed".
	auto rejected = RfcCommitRejectedMessage(error);
	REQUIRE(rejected.find("not committed") != std::string::npos);
	REQUIRE(rejected.find("Update terminated") != std::string::npos);
	auto unknown = RfcCommitOutcomeUnknownMessage("RFC_COMMUNICATION_FAILURE");
	REQUIRE(unknown.find("outcome") != std::string::npos);
	REQUIRE(unknown.find("before retrying") != std::string::npos);
	REQUIRE(unknown.find("not committed") == std::string::npos);
}
