#pragma once

#include "duckdb.hpp"
#include "duckdb/main/client_context_state.hpp"
#include "sap_connection.hpp"

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>

namespace duckdb
{
	// The DuckDB transaction is the SAP LUW (issue #244).
	//
	// SAP write BAPIs post through the update task: the BAPI call and
	// BAPI_TRANSACTION_COMMIT must run on the *same* RFC session, otherwise the
	// session that did the work closes without a commit and its update records are
	// discarded. ERPL opens a connection per call, so outside a transaction no write
	// BAPI can ever be committed.
	//
	// Between BEGIN and COMMIT / ROLLBACK every ERPL RFC call that goes through
	// ConnectInContext() shares ONE session per set of connection parameters (i.e.
	// per secret). COMMIT calls BAPI_TRANSACTION_COMMIT (WAIT = 'X') on each held
	// session and fails the COMMIT statement if SAP reports TYPE E/A; ROLLBACK -- also
	// DuckDB's rollback after a failed statement -- calls BAPI_TRANSACTION_ROLLBACK.
	// Either way the session is closed afterwards; closing the DuckDB connection with
	// an open transaction just closes it, which discards the SAP LUW as well.
	// Autocommit statements are unaffected: a connection per call, no commit.
	//
	// The SAP commit runs when the COMMIT statement is prepared, before DuckDB commits
	// locally (see OnFinalizePrepare): a rejected SAP commit aborts the DuckDB
	// transaction. The two are still not atomic -- a DuckDB commit that fails after SAP
	// committed cannot take the SAP changes back.

	// True while the client is inside an explicit BEGIN ... COMMIT / ROLLBACK.
	bool InExplicitTransaction(ClientContext &context);

	// The connection every ERPL RFC call should use. Outside an explicit transaction
	// this is auth.Connect(): a fresh connection the caller owns and closes. Inside
	// one it is a non-owning view of the transaction's session for these connection
	// parameters, opened on first use; Close() on it is a no-op, and once the
	// transaction has ended every call on it fails with a clean error.
	std::shared_ptr<RfcConnection> ConnectInContext(ClientContext &context, RfcAuthParams &auth);
	// The same, resolving the secret named by the call's `secret` parameter (or the
	// default sap_rfc secret) -- the replacement for
	// GetAuthParamsFromContext(context, input).Connect() in every scanner and pragma,
	// erpl_bics, erpl_odp and erpl_ape included.
	std::shared_ptr<RfcConnection> ConnectInContext(ClientContext &context, const TableFunctionBindInput &input);
	std::shared_ptr<RfcConnection> ConnectInContext(ClientContext &context, const FunctionParameters &parameters);

	// A connection of its own, even inside BEGIN ... COMMIT: for calls that must stay off
	// the transaction's SAP session (the ODP extraction calls, which ODQ commits itself
	// and which run in parallel). The one place that decides to bypass the LUW.
	std::shared_ptr<RfcConnection> ConnectOutsideLuw(ClientContext &context, const RfcAuthParams &auth);

	enum class RfcLuwOutcome { COMMIT, ROLLBACK };

	// How SAP answered BAPI_TRANSACTION_COMMIT, from its RETURN (a BAPIRET2 structure, or
	// a table of them): rejected when any message has TYPE E or A.
	struct RfcCommitVerdict {
		bool rejected = false;
		string type;
		string id;
		string number;
		string message;
	};
	RfcCommitVerdict EvaluateCommitReturn(const Value &return_value);
	// The error texts of a commit SAP rejected, and of one whose outcome is unknown.
	string RfcCommitRejectedMessage(const RfcCommitVerdict &verdict);
	string RfcCommitOutcomeUnknownMessage(const string &cause);

	class RfcLuwState : public ClientContextState {
	public:
		// Versioned: in release builds erpl_rfc, erpl_bics, erpl_odp and erpl_ape are
		// separate binaries, each with its own copy of this class, and they share one
		// state object per client through this key. A layout change bumps the version, so
		// binaries of different builds never cast each other's state.
		static constexpr const char *KEY = "erpl_rfc_luw/v1";
		static constexpr uint64_t LAYOUT_MAGIC = 0x45524C504C555701ULL; // "ERLPLUW" v1

		// Opens a session. Injectable so the state logic can be tested without SAP.
		using Connector = std::function<std::shared_ptr<RfcConnection>(RfcAuthParams &)>;
		// Commits or rolls back the SAP LUW of a session; the state closes it afterwards.
		// A COMMIT that SAP rejects (or whose outcome is unknown) throws.
		using Finisher = std::function<void(std::shared_ptr<RfcConnection> &, RfcLuwOutcome)>;
		// Closes a session the state owns.
		using Closer = std::function<void(RfcConnection &)>;

		RfcLuwState();
		RfcLuwState(Connector connector, Finisher finisher, Closer closer = nullptr);
		~RfcLuwState() override;

		// The client's state, checked against this build's layout; created on demand.
		static duckdb::shared_ptr<RfcLuwState> Lookup(ClientContext &context, bool create);

		std::shared_ptr<RfcConnection> Acquire(RfcAuthParams &auth);
		idx_t HeldSessions();
		// Throws (once) the error of a commit that failed where it could not be reported
		// (see TransactionCommit).
		void RaisePendingError();

		// The SAP commit runs when the COMMIT statement is prepared, i.e. just BEFORE DuckDB
		// commits locally. That is the one place where a rejected SAP commit can fail the
		// COMMIT statement safely: an exception from TransactionCommit would skip the
		// TransactionCommit of every other registered state (an unordered map) and the
		// transaction's Finalize(). On failure the DuckDB transaction is invalidated, so it
		// can only be rolled back. OnFinalizePrepare is only called while some state
		// answers CanRequestRebind(), which this one does only while it holds a session.
		bool CanRequestRebind() override;
		RebindQueryInfo OnFinalizePrepare(ClientContext &context, PreparedStatementData &prepared,
		                                  PreparedStatementMode mode) override;
		// Fallback for a commit that did not go through a COMMIT statement prepared and
		// executed in one go (a COMMIT prepared earlier and executed later): DuckDB has
		// already committed locally. Commits what is still held and never throws -- a
		// failure is traced and raised by the next erpl call on this connection.
		void TransactionCommit(MetaTransaction &transaction, ClientContext &context) override;
		using ClientContextState::TransactionRollback;
		void TransactionRollback(MetaTransaction &transaction, ClientContext &context,
		                         optional_ptr<ErrorData> error) override;

		// The production finisher: BAPI_TRANSACTION_COMMIT (WAIT = 'X') with a check of its
		// RETURN, or BAPI_TRANSACTION_ROLLBACK.
		static void FinishWithBapi(std::shared_ptr<RfcConnection> &session, RfcLuwOutcome outcome);

		// Must stay the first data members: Lookup() checks them before using an object
		// another binary may have created.
		const uint64_t layout_magic = LAYOUT_MAGIC;
		const uint64_t layout_size;

	private:
		struct Session {
			string key;
			std::shared_ptr<RfcConnection> connection;
		};

		std::vector<Session> TakeSessions();
		void CommitAll();
		void RollbackAll(std::vector<Session> &sessions, bool call_rollback);
		void CloseSession(Session &session);

		Connector connector;
		Finisher finisher;
		Closer closer;
		std::mutex lock;
		// In order of first use; COMMIT walks them in that order.
		std::vector<Session> sessions;
		std::atomic<bool> has_sessions {false};
		std::mutex pending_lock;
		string pending_error;
	};
} // namespace duckdb
