#include "sap_luw.hpp"

#include "duckdb/main/client_context.hpp"
#include "duckdb/transaction/transaction_context.hpp"
#include "duckdb/transaction/meta_transaction.hpp"
#include "duckdb/main/valid_checker.hpp"
#include "duckdb/main/prepared_statement_data.hpp"
#include "duckdb/execution/physical_plan_generator.hpp"
#include "duckdb/execution/operator/helper/physical_transaction.hpp"
#include "duckdb/parser/parsed_data/transaction_info.hpp"

#include "duckdb_argument_helper.hpp"
#include "erpl_tracing.hpp"
#include "sap_function.hpp"
#include "duckdb_compat.hpp"

namespace duckdb
{
	bool InExplicitTransaction(ClientContext &context)
	{
		return context.transaction.HasActiveTransaction() && !context.transaction.IsAutoCommit();
	}

	std::shared_ptr<RfcConnection> ConnectInContext(ClientContext &context, RfcAuthParams &auth)
	{
		if (!InExplicitTransaction(context)) {
			return auth.Connect();
		}
		auto state = context.registered_state->GetOrCreate<RfcLuwState>(RfcLuwState::KEY);
		return state->Acquire(auth);
	}

	// --- RfcLuwState ----------------------------------------------------------

	RfcLuwState::RfcLuwState()
	    : RfcLuwState([](RfcAuthParams &auth) { return auth.Connect(); }, &RfcLuwState::FinishWithBapi)
	{ }

	RfcLuwState::RfcLuwState(Connector connector_p, Finisher finisher_p)
	    : connector(std::move(connector_p)), finisher(std::move(finisher_p))
	{ }

	RfcLuwState::~RfcLuwState()
	{
		// Normally empty: COMMIT / ROLLBACK hand every session back. Something left here
		// means the transaction never ended through DuckDB (e.g. the client context was
		// torn down during an exception unwind, which skips its rollback) -- roll the
		// SAP LUW back and close, rather than leak a logged-on session.
		try {
			auto left = TakeSessions();
			RollbackAll(left);
		} catch (...) {
			// A destructor must never throw.
		}
	}

	static string SessionKey(const RfcAuthParams &auth)
	{
		// Every connection parameter, credentials included: two secrets that differ
		// only in the user are two SAP sessions. Kept in memory only, never printed.
		string key;
		for (auto &param : auth.BuildConnectionParams()) {
			key += param.first;
			key += '\x1f';
			key += param.second;
			key += '\x1e';
		}
		return key;
	}

	std::shared_ptr<RfcConnection> RfcLuwState::Acquire(RfcAuthParams &auth)
	{
		auto key = SessionKey(auth);
		std::lock_guard<std::mutex> guard(lock);
		for (auto &session : sessions) {
			if (session.key == key) {
				return session.connection->Borrow();
			}
		}
		auto opened = connector(auth);
		if (!opened) {
			throw IOException("SAP LUW: could not open an RFC session");
		}
		if (!opened->call_lock) {
			opened->call_lock = std::make_shared<std::mutex>();
		}
		ERPL_TRACE_DEBUG("SAP_LUW", "Opened the SAP session for this transaction");
		sessions.push_back(Session {std::move(key), opened});
		return opened->Borrow();
	}

	idx_t RfcLuwState::HeldSessions()
	{
		std::lock_guard<std::mutex> guard(lock);
		return sessions.size();
	}

	std::vector<RfcLuwState::Session> RfcLuwState::TakeSessions()
	{
		std::lock_guard<std::mutex> guard(lock);
		std::vector<Session> taken;
		taken.swap(sessions);
		return taken;
	}

	void RfcLuwState::RollbackAll(std::vector<Session> &to_roll_back)
	{
		for (auto &session : to_roll_back) {
			try {
				finisher(session.connection, RfcLuwOutcome::ROLLBACK);
			} catch (std::exception &ex) {
				// Closing the session without a commit discards its LUW anyway, so a
				// failed BAPI_TRANSACTION_ROLLBACK changes nothing on the SAP side.
				ERPL_TRACE_WARN_DATA("SAP_LUW", "Rolling back the SAP LUW failed", ex.what());
			} catch (...) {
			}
			try {
				if (session.connection) {
					session.connection->Close();
				}
			} catch (...) {
			}
		}
		to_roll_back.clear();
	}

	void RfcLuwState::CommitAll()
	{
		auto to_commit = TakeSessions();
		for (idx_t i = 0; i < to_commit.size(); i++) {
			try {
				finisher(to_commit[i].connection, RfcLuwOutcome::COMMIT);
			} catch (...) {
				// Close this session and roll back every one not committed yet, so a
				// rejected commit on the first secret leaves the others untouched.
				try {
					to_commit[i].connection->Close();
				} catch (...) {
				}
				std::vector<Session> rest(to_commit.begin() + i + 1, to_commit.end());
				RollbackAll(rest);
				throw;
			}
		}
	}

	void RfcLuwState::TransactionCommit(MetaTransaction &, ClientContext &)
	{
		CommitAll();
	}

	bool RfcLuwState::CanRequestRebind()
	{
		return true;
	}

	static bool IsCommitStatement(PreparedStatementData &prepared)
	{
		if (prepared.statement_type != StatementType::TRANSACTION_STATEMENT || !prepared.physical_plan) {
			return false;
		}
		auto &root = prepared.physical_plan->Root();
		if (root.type != PhysicalOperatorType::TRANSACTION) {
			return false;
		}
		auto &transaction = root.Cast<PhysicalTransaction>();
		return transaction.info && transaction.info->type == TransactionType::COMMIT;
	}

	RebindQueryInfo RfcLuwState::OnFinalizePrepare(ClientContext &context, PreparedStatementData &prepared,
	                                               PreparedStatementMode mode)
	{
		if (mode != PreparedStatementMode::PREPARE_AND_EXECUTE || !IsCommitStatement(prepared) ||
		    !InExplicitTransaction(context) || HeldSessions() == 0) {
			return RebindQueryInfo::DO_NOT_REBIND;
		}
		auto &transaction = context.ActiveTransaction();
		if (ValidChecker::IsInvalidated(transaction)) {
			// DuckDB turns this COMMIT into a ROLLBACK; TransactionRollback handles SAP.
			return RebindQueryInfo::DO_NOT_REBIND;
		}
		try {
			CommitAll();
		} catch (std::exception &ex) {
			// Nothing of this transaction may commit locally now: a later COMMIT becomes a
			// ROLLBACK, as after any failed statement.
			ValidChecker::Invalidate(transaction, ex.what());
			throw;
		}
		return RebindQueryInfo::DO_NOT_REBIND;
	}

	void RfcLuwState::TransactionRollback(MetaTransaction &, ClientContext &, optional_ptr<ErrorData>)
	{
		auto to_roll_back = TakeSessions();
		RollbackAll(to_roll_back);
	}

	static string StructChildAsString(const Value &value, const string &name)
	{
		if (value.IsNull() || value.type().id() != LogicalTypeId::STRUCT) {
			return string();
		}
		auto &children = StructValue::GetChildren(value);
		for (idx_t i = 0; i < children.size(); i++) {
			if (StringUtil::CIEquals(duckdb_compat::Str(StructType::GetChildName(value.type(), i)), name)) {
				return children[i].IsNull() ? string() : children[i].ToString();
			}
		}
		return string();
	}

	void RfcLuwState::FinishWithBapi(std::shared_ptr<RfcConnection> &session, RfcLuwOutcome outcome)
	{
		if (outcome == RfcLuwOutcome::ROLLBACK) {
			std::vector<Value> no_args;
			RfcResultSet::InvokeFunction(session, "BAPI_TRANSACTION_ROLLBACK", no_args);
			session->Close();
			ERPL_TRACE_DEBUG("SAP_LUW", "Rolled back and closed the SAP session");
			return;
		}

		// WAIT = 'X': return only once the update task has finished, so the next
		// statement reads what the transaction wrote, and update errors come back in
		// RETURN instead of only in SM13.
		auto args = ArgBuilder().Add("WAIT", Value("X")).BuildArgList();
		std::shared_ptr<RfcResultSet> result;
		try {
			result = RfcResultSet::InvokeFunction(session, "BAPI_TRANSACTION_COMMIT", args);
		} catch (std::exception &ex) {
			throw IOException("SAP LUW: BAPI_TRANSACTION_COMMIT failed, the SAP changes of this transaction "
			                  "were not committed: %s", ex.what());
		}
		Value ret;
		try {
			ret = result->GetResultValue(string("RETURN"));
		} catch (...) {
			// No RETURN parameter: nothing to check.
		}
		auto type = StringUtil::Upper(StructChildAsString(ret, "TYPE"));
		session->Close();
		if (type == "E" || type == "A") {
			auto message = StructChildAsString(ret, "MESSAGE");
			auto id = StructChildAsString(ret, "ID");
			auto number = StructChildAsString(ret, "NUMBER");
			throw IOException("SAP LUW: BAPI_TRANSACTION_COMMIT returned %s %s(%s): %s -- the SAP changes of this "
			                  "transaction were not committed", type, id, number, message);
		}
		ERPL_TRACE_DEBUG("SAP_LUW", "Committed and closed the SAP session");
	}
} // namespace duckdb
