#pragma once

#include "duckdb.hpp"
#include "duckdb/main/client_context_state.hpp"
#include "sap_connection.hpp"

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
	// DuckDB's rollback after a failed statement, and closing the connection with an
	// open transaction -- calls BAPI_TRANSACTION_ROLLBACK. Either way the session is
	// closed afterwards. Autocommit statements are unaffected: a connection per call,
	// no commit, exactly as before.
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
	// parameters, opened on first use; Close() on it is a no-op.
	std::shared_ptr<RfcConnection> ConnectInContext(ClientContext &context, RfcAuthParams &auth);
	// The same, resolving the secret named by the call's `secret` parameter (or the
	// default sap_rfc secret) -- the replacement for
	// GetAuthParamsFromContext(context, input).Connect() in every scanner and pragma,
	// erpl_bics, erpl_odp and erpl_ape included.
	std::shared_ptr<RfcConnection> ConnectInContext(ClientContext &context, const TableFunctionBindInput &input);
	std::shared_ptr<RfcConnection> ConnectInContext(ClientContext &context, const FunctionParameters &parameters);

	enum class RfcLuwOutcome { COMMIT, ROLLBACK };

	class RfcLuwState : public ClientContextState {
	public:
		static constexpr const char *KEY = "erpl_rfc_luw";

		// Opens a session. Injectable so the state logic can be tested without SAP.
		using Connector = std::function<std::shared_ptr<RfcConnection>(RfcAuthParams &)>;
		// Ends a session: commit or roll back the SAP LUW, then close it. A COMMIT
		// that SAP rejects throws; a ROLLBACK must not throw.
		using Finisher = std::function<void(std::shared_ptr<RfcConnection> &, RfcLuwOutcome)>;

		RfcLuwState();
		RfcLuwState(Connector connector, Finisher finisher);
		~RfcLuwState() override;

		std::shared_ptr<RfcConnection> Acquire(RfcAuthParams &auth);
		idx_t HeldSessions();

		// The SAP commit runs when the COMMIT statement is prepared, i.e. just BEFORE DuckDB
		// commits locally. That is the one place where a rejected SAP commit can fail the
		// COMMIT statement safely: an exception from TransactionCommit would skip the
		// TransactionCommit of every other registered state (an unordered map), leaving
		// them -- DuckDB's own connection-secret state among them -- out of step. On
		// failure the DuckDB transaction is invalidated, so it can only be rolled back.
		// OnFinalizePrepare is only called while some state answers CanRequestRebind().
		bool CanRequestRebind() override;
		RebindQueryInfo OnFinalizePrepare(ClientContext &context, PreparedStatementData &prepared,
		                                  PreparedStatementMode mode) override;
		// Fallback for a commit that did not go through a COMMIT statement prepared and
		// executed in one go (e.g. a COMMIT prepared earlier and executed later): commits
		// whatever is still held, and throws if SAP rejects it.
		void TransactionCommit(MetaTransaction &transaction, ClientContext &context) override;
		using ClientContextState::TransactionRollback;
		void TransactionRollback(MetaTransaction &transaction, ClientContext &context,
		                         optional_ptr<ErrorData> error) override;

		// The production finisher: BAPI_TRANSACTION_COMMIT (WAIT = 'X') with a check
		// of its RETURN, or BAPI_TRANSACTION_ROLLBACK; then close.
		static void FinishWithBapi(std::shared_ptr<RfcConnection> &session, RfcLuwOutcome outcome);

	private:
		struct Session {
			string key;
			std::shared_ptr<RfcConnection> connection;
		};

		std::vector<Session> TakeSessions();
		void CommitAll();
		void RollbackAll(std::vector<Session> &sessions);

		Connector connector;
		Finisher finisher;
		std::mutex lock;
		// In order of first use; COMMIT walks them in that order.
		std::vector<Session> sessions;
	};
} // namespace duckdb
