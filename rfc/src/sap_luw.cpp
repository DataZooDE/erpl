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

#include <chrono>
#include <cstring>

namespace duckdb
{
	bool InExplicitTransaction(ClientContext &context)
	{
		return context.transaction.HasActiveTransaction() && !context.transaction.IsAutoCommit();
	}

	std::shared_ptr<RfcConnection> ConnectInContext(ClientContext &context, RfcAuthParams &auth)
	{
		auto existing = RfcLuwState::Lookup(context, false);
		if (existing) {
			existing->RaisePendingError();
		}
		if (!InExplicitTransaction(context)) {
			return auth.Connect();
		}
		auto state = existing ? existing : RfcLuwState::Lookup(context, true);
		return state->Acquire(auth);
	}

	std::shared_ptr<RfcConnection> ConnectInContext(ClientContext &context, const TableFunctionBindInput &input)
	{
		auto auth = GetAuthParamsFromContext(context, input);
		return ConnectInContext(context, auth);
	}

	std::shared_ptr<RfcConnection> ConnectInContext(ClientContext &context, const FunctionParameters &parameters)
	{
		auto auth = GetAuthParamsFromContext(context, parameters);
		return ConnectInContext(context, auth);
	}

	std::shared_ptr<RfcConnection> ConnectOutsideLuw(ClientContext &, const RfcAuthParams &auth)
	{
		auto copy = auth; // Connect() is non-const
		return copy.Connect();
	}

	// --- commit verdict ---------------------------------------------------------

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

	RfcCommitVerdict EvaluateCommitReturn(const Value &return_value)
	{
		RfcCommitVerdict verdict;
		if (return_value.IsNull()) {
			return verdict;
		}
		vector<Value> messages;
		if (return_value.type().id() == LogicalTypeId::LIST) {
			for (auto &row : ListValue::GetChildren(return_value)) {
				messages.push_back(row);
			}
		} else {
			messages.push_back(return_value);
		}
		for (auto &message : messages) {
			auto type = StringUtil::Upper(StructChildAsString(message, "TYPE"));
			if (type == "E" || type == "A") {
				verdict.rejected = true;
				verdict.type = type;
				verdict.id = StructChildAsString(message, "ID");
				verdict.number = StructChildAsString(message, "NUMBER");
				verdict.message = StructChildAsString(message, "MESSAGE");
				break;
			}
		}
		return verdict;
	}

	string RfcCommitRejectedMessage(const RfcCommitVerdict &verdict)
	{
		return StringUtil::Format("SAP LUW: BAPI_TRANSACTION_COMMIT returned %s %s(%s): %s -- SAP rejected the "
		                          "commit, the SAP changes of this transaction were not committed",
		                          verdict.type, verdict.id, verdict.number, verdict.message);
	}

	string RfcCommitOutcomeUnknownMessage(const string &cause)
	{
		return StringUtil::Format("SAP LUW: the outcome of BAPI_TRANSACTION_COMMIT is unknown (%s). SAP may or may "
		                          "not have committed the changes of this transaction: check the SAP side (SM13, or "
		                          "the objects themselves) before retrying",
		                          cause);
	}

	// --- RfcLuwState ----------------------------------------------------------

	RfcLuwState::RfcLuwState()
	    : RfcLuwState([](RfcAuthParams &auth) { return auth.Connect(); }, &RfcLuwState::FinishWithBapi)
	{ }

	RfcLuwState::RfcLuwState(Connector connector_p, Finisher finisher_p, Closer closer_p)
	    : layout_size(sizeof(RfcLuwState)), connector(std::move(connector_p)), finisher(std::move(finisher_p)),
	      closer(closer_p ? std::move(closer_p) : Closer([](RfcConnection &connection) { connection.Close(); }))
	{ }

	RfcLuwState::~RfcLuwState()
	{
		// Normally empty: COMMIT / ROLLBACK hand every session back. Something left here
		// means the transaction never ended through DuckDB (e.g. the client context was
		// torn down during an exception unwind, which skips its rollback). Closing a
		// session without a commit discards its SAP LUW, so close and do not make a
		// rollback round-trip that may hang on a dead host at process exit.
		try {
			auto left = TakeSessions();
			RollbackAll(left, false);
		} catch (...) {
			// A destructor must never throw.
		}
	}

	// Where layout_magic / layout_size sit inside an RfcLuwState, seen from its
	// ClientContextState base -- measured on a real object of this build.
	static size_t LayoutOffset()
	{
		static const size_t offset = [] {
			RfcLuwState probe;
			auto base = reinterpret_cast<const char *>(static_cast<const ClientContextState *>(&probe));
			return static_cast<size_t>(reinterpret_cast<const char *>(&probe.layout_magic) - base);
		}();
		return offset;
	}

	duckdb::shared_ptr<RfcLuwState> RfcLuwState::Lookup(ClientContext &context, bool create)
	{
		// Inspect the registered object through its base before treating it as ours: it may
		// come from another erpl binary of a different build (see KEY).
		auto base = context.registered_state->Get<ClientContextState>(KEY);
		if (base) {
			uint64_t words[2];
			std::memcpy(words, reinterpret_cast<const char *>(base.get()) + LayoutOffset(), sizeof(words));
			if (words[0] != LAYOUT_MAGIC || words[1] != sizeof(RfcLuwState)) {
				throw InvalidInputException("erpl: the erpl extensions loaded in this process come from different "
				                            "builds (their SAP transaction state does not match); install erpl_rfc, "
				                            "erpl_bics, erpl_odp and erpl_ape from the same release");
			}
			return context.registered_state->Get<RfcLuwState>(KEY);
		}
		if (!create) {
			return nullptr;
		}
		return context.registered_state->GetOrCreate<RfcLuwState>(KEY);
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

	static const char *kSessionLost = "SAP LUW: the SAP session of this transaction was lost (a communication failure "
	                                  "or an ABAP runtime error ended it); SAP discarded its changes. ROLLBACK and "
	                                  "run the transaction again";

	std::shared_ptr<RfcConnection> RfcLuwState::Acquire(RfcAuthParams &auth)
	{
		auto key = SessionKey(auth);
		{
			std::lock_guard<std::mutex> guard(lock);
			for (auto &session : sessions) {
				if (session.key == key) {
					if (session.connection->IsLost()) {
						throw IOException(kSessionLost);
					}
					return session.connection->Borrow();
				}
			}
		}
		// Log on outside the lock: other threads keep using the sessions already held.
		auto opened = connector(auth);
		if (!opened) {
			throw IOException("SAP LUW: could not open an RFC session");
		}
		opened->Share();
		std::shared_ptr<RfcConnection> existing;
		{
			std::lock_guard<std::mutex> guard(lock);
			for (auto &session : sessions) {
				if (session.key == key) {
					existing = session.connection;
					break;
				}
			}
			if (!existing) {
				ERPL_TRACE_DEBUG("SAP_LUW", "Opened the SAP session for this transaction");
				sessions.push_back(Session {std::move(key), opened});
				has_sessions.store(true);
				return opened->Borrow();
			}
		}
		// Another thread opened the session for these parameters meanwhile: use that one.
		try {
			closer(*opened);
		} catch (...) {
		}
		return existing->Borrow();
	}

	idx_t RfcLuwState::HeldSessions()
	{
		std::lock_guard<std::mutex> guard(lock);
		return sessions.size();
	}

	void RfcLuwState::RaisePendingError()
	{
		string error;
		{
			std::lock_guard<std::mutex> guard(pending_lock);
			error.swap(pending_error);
		}
		if (!error.empty()) {
			throw IOException("SAP LUW: the previous COMMIT, which ran after DuckDB had committed locally (a COMMIT "
			                  "prepared earlier and executed later), did not complete on the SAP side: %s",
			                  error);
		}
	}

	std::vector<RfcLuwState::Session> RfcLuwState::TakeSessions()
	{
		std::lock_guard<std::mutex> guard(lock);
		std::vector<Session> taken;
		taken.swap(sessions);
		has_sessions.store(false);
		return taken;
	}

	void RfcLuwState::CloseSession(Session &session)
	{
		try {
			if (session.connection) {
				closer(*session.connection);
			}
		} catch (std::exception &ex) {
			ERPL_TRACE_WARN_DATA("SAP_LUW", "Closing the SAP session failed", ex.what());
		} catch (...) {
		}
	}

	void RfcLuwState::RollbackAll(std::vector<Session> &to_roll_back, bool call_rollback)
	{
		for (auto &session : to_roll_back) {
			if (call_rollback && session.connection && !session.connection->IsLost()) {
				try {
					finisher(session.connection, RfcLuwOutcome::ROLLBACK);
				} catch (std::exception &ex) {
					// Closing the session without a commit discards its LUW anyway, so a
					// failed BAPI_TRANSACTION_ROLLBACK changes nothing on the SAP side.
					ERPL_TRACE_WARN_DATA("SAP_LUW", "Rolling back the SAP LUW failed", ex.what());
				} catch (...) {
				}
			}
			CloseSession(session);
		}
		to_roll_back.clear();
	}

	void RfcLuwState::CommitAll()
	{
		auto to_commit = TakeSessions();
		for (idx_t i = 0; i < to_commit.size(); i++) {
			auto &session = to_commit[i];
			try {
				if (session.connection->IsLost()) {
					throw IOException(kSessionLost);
				}
				auto started = std::chrono::steady_clock::now();
				finisher(session.connection, RfcLuwOutcome::COMMIT);
				auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() -
				                                                                started)
				              .count();
				ERPL_TRACE_INFO("SAP_LUW", StringUtil::Format("BAPI_TRANSACTION_COMMIT (WAIT = 'X') took %d ms", ms));
				CloseSession(session);
			} catch (...) {
				// Close this session and roll back every one not committed yet, so a
				// rejected commit on the first secret leaves the others untouched.
				CloseSession(session);
				std::vector<Session> rest(to_commit.begin() + i + 1, to_commit.end());
				RollbackAll(rest, true);
				throw;
			}
		}
	}

	void RfcLuwState::TransactionCommit(MetaTransaction &, ClientContext &)
	{
		// Throwing here would skip the other states' TransactionCommit and the
		// transaction's Finalize(); see the header.
		try {
			CommitAll();
		} catch (std::exception &ex) {
			ErrorData error(ex);
			ERPL_TRACE_ERROR_DATA("SAP_LUW", "SAP commit after DuckDB's local commit failed", error.RawMessage());
			std::lock_guard<std::mutex> guard(pending_lock);
			pending_error = error.RawMessage();
		} catch (...) {
			std::lock_guard<std::mutex> guard(pending_lock);
			pending_error = "unknown error";
		}
	}

	bool RfcLuwState::CanRequestRebind()
	{
		// Only while a session is held -- otherwise every statement of this connection
		// would pay for DuckDB binding a copy of it first.
		return has_sessions.load();
	}

	static bool IsCommitStatement(PreparedStatementData &prepared)
	{
		// PreparedStatementData::unbound_statement is not set yet when OnFinalizePrepare
		// runs (DuckDB 1.5.6 and 2.0 assign it only in PrepareInternal, afterwards), so the
		// statement is recognised by its plan: a COMMIT plans to a PhysicalTransaction.
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
			ErrorData error(ex);
			ValidChecker::Invalidate(transaction, error.RawMessage());
			throw;
		}
		return RebindQueryInfo::DO_NOT_REBIND;
	}

	void RfcLuwState::TransactionRollback(MetaTransaction &, ClientContext &, optional_ptr<ErrorData>)
	{
		auto to_roll_back = TakeSessions();
		RollbackAll(to_roll_back, true);
	}

	void RfcLuwState::FinishWithBapi(std::shared_ptr<RfcConnection> &session, RfcLuwOutcome outcome)
	{
		if (outcome == RfcLuwOutcome::ROLLBACK) {
			std::vector<Value> no_args;
			RfcResultSet::InvokeFunction(session, "BAPI_TRANSACTION_ROLLBACK", no_args);
			ERPL_TRACE_DEBUG("SAP_LUW", "Rolled back the SAP LUW");
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
			// The call itself failed: SAP may have committed before the reply was lost.
			ErrorData error(ex);
			throw IOException(RfcCommitOutcomeUnknownMessage(error.RawMessage()));
		}
		Value ret;
		try {
			ret = result->GetResultValue(string("RETURN"));
		} catch (...) {
			// No RETURN parameter: nothing to check.
		}
		auto verdict = EvaluateCommitReturn(ret);
		if (verdict.rejected) {
			throw IOException(RfcCommitRejectedMessage(verdict));
		}
		ERPL_TRACE_DEBUG("SAP_LUW", "Committed the SAP LUW");
	}
} // namespace duckdb
