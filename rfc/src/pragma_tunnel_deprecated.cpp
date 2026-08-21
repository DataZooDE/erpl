#include "pragma_tunnel_deprecated.hpp"

#include "duckdb/catalog/catalog.hpp"
#include "duckdb/catalog/catalog_entry/schema_catalog_entry.hpp"

namespace duckdb {

namespace {

constexpr const char *ERPL_TUNNEL_REPOSITORY = "https://github.com/DataZooDE/erpl-tunnel";

// One message for every moved function, so the migration is always the same two lines the
// caller can paste. Named per call site so the error says which function moved rather than
// something generic.
[[noreturn]] void ThrowMoved(const char *call) {
	throw InvalidInputException("%s has moved out of erpl into the dedicated erpl_tunnel extension, which "
	                            "supports reverse tunnels and Tailscale/NetBird in addition to SSH.\n\n"
	                            "  INSTALL erpl_tunnel FROM 'http://get.erpl.io';\n"
	                            "  LOAD erpl_tunnel;\n\n"
	                            "See %s",
	                            call, ERPL_TUNNEL_REPOSITORY);
}

// True when something already provides this name, which means erpl_tunnel was loaded first.
//
// Registering unconditionally would be wrong in exactly one order. A duplicate name does
// not throw -- the *last* registration wins (verified against DuckDB v1.5.5, despite the
// pragma path leaving CreateInfo::on_conflict at its ERROR_ON_CONFLICT default). So
// `LOAD erpl_tunnel` followed by `LOAD erpl` would have the stub land second and shadow
// the working implementation: the very bug this change exists to fix, inverted.
bool AlreadyRegistered(DatabaseInstance &db, CatalogType type, const string &name) {
	auto &system_catalog = Catalog::GetSystemCatalog(db);
	auto transaction = CatalogTransaction::GetSystemTransaction(db);
	auto schema = system_catalog.GetSchema(transaction, DEFAULT_SCHEMA, OnEntryNotFound::RETURN_NULL);
	if (!schema) {
		return false;
	}
	return schema->GetEntry(transaction, type, name) != nullptr;
}

string MovedTunnelCreate(ClientContext &, const FunctionParameters &) {
	ThrowMoved("PRAGMA tunnel_create");
}

string MovedTunnelClose(ClientContext &, const FunctionParameters &) {
	ThrowMoved("PRAGMA tunnel_close");
}

string MovedTunnelCloseAll(ClientContext &, const FunctionParameters &) {
	ThrowMoved("PRAGMA tunnel_close_all");
}

unique_ptr<FunctionData> MovedTunnelsBind(ClientContext &, TableFunctionBindInput &, vector<LogicalType> &,
                                          vector<string> &) {
	ThrowMoved("tunnels()");
}

void RegisterIfAbsent(ExtensionLoader &loader, const string &name, PragmaFunction pragma) {
	if (AlreadyRegistered(loader.GetDatabaseInstance(), CatalogType::PRAGMA_FUNCTION_ENTRY, name)) {
		return;
	}
	loader.RegisterFunction(std::move(pragma));
}

} // namespace

void RegisterDeprecatedTunnelFunctions(ExtensionLoader &loader) {
	{
		// The parameters the pragma used to accept. A stub taking none would let a
		// realistic call fall through to a signature error instead of the message, which
		// would defeat the point of having a stub at all.
		auto pragma = PragmaFunction::PragmaCall("tunnel_create", MovedTunnelCreate, {});
		pragma.named_parameters["secret"] = LogicalType::VARCHAR;
		pragma.named_parameters["remote_host"] = LogicalType::VARCHAR;
		pragma.named_parameters["remote_port"] = LogicalType::INTEGER;
		pragma.named_parameters["local_port"] = LogicalType::INTEGER;
		pragma.named_parameters["timeout"] = LogicalType::INTEGER;
		RegisterIfAbsent(loader, "tunnel_create", std::move(pragma));
	}

	RegisterIfAbsent(loader, "tunnel_close",
	                 PragmaFunction::PragmaCall("tunnel_close", MovedTunnelClose, {LogicalType::INTEGER}));
	RegisterIfAbsent(loader, "tunnel_close_all",
	                 PragmaFunction::PragmaCall("tunnel_close_all", MovedTunnelCloseAll, {}));

	// tunnels() was a table function, not a pragma. Raising from bind is early enough that
	// the message is the only thing the caller sees.
	if (!AlreadyRegistered(loader.GetDatabaseInstance(), CatalogType::TABLE_FUNCTION_ENTRY, "tunnels")) {
		TableFunction tunnels("tunnels", {}, nullptr, MovedTunnelsBind);
		loader.RegisterFunction(tunnels);
	}
}

} // namespace duckdb
