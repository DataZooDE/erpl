#include "pragma_tunnel_deprecated.hpp"
#include "duckdb/parser/parsed_data/create_table_function_info.hpp"
#include "duckdb/parser/parsed_data/create_pragma_function_info.hpp"

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

// These stubs exist to be FOUND -- their whole job is to tell a caller the feature
// moved. An undocumented stub is the one case where missing metadata is actively
// counterproductive: an agent reading duckdb_functions() sees a callable tunnel_create
// with no explanation, tries it, and gets an error it could have been told about.
//
// Pragmas can carry descriptions; see RegisterDocumentedPragma in erpl_rfc_extension.cpp
// for why that is not obvious from the ExtensionLoader API.
void RegisterIfAbsent(ExtensionLoader &loader, const string &name, PragmaFunction pragma,
                      const string &description) {
	if (AlreadyRegistered(loader.GetDatabaseInstance(), CatalogType::PRAGMA_FUNCTION_ENTRY, name)) {
		return;
	}
	auto pragma_name = pragma.name;
	PragmaFunctionSet set(pragma_name);
	set.AddFunction(std::move(pragma));

	CreatePragmaFunctionInfo info(std::move(pragma_name), std::move(set));
	FunctionDescription d;
	d.description = description;
	d.examples = {"INSTALL erpl_tunnel FROM community; LOAD erpl_tunnel;"};
	d.categories = {"tunnel", "deprecated"};
	info.descriptions.push_back(std::move(d));

	auto &db = loader.GetDatabaseInstance();
	auto &system_catalog = Catalog::GetSystemCatalog(db);
	auto transaction = CatalogTransaction::GetSystemTransaction(db);
	system_catalog.CreatePragmaFunction(transaction, info);
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
		RegisterIfAbsent(loader, "tunnel_create", std::move(pragma),
		                 "MOVED to the erpl_tunnel extension, which owns tunnelling now. Calling this raises a message saying so. Install and load erpl_tunnel, then use tunnel_import (tunnel_create remains as its deprecated alias).");
	}

	RegisterIfAbsent(loader, "tunnel_close",
	                 PragmaFunction::PragmaCall("tunnel_close", MovedTunnelClose, {LogicalType::INTEGER}),
	                 "MOVED to the erpl_tunnel extension, which owns tunnelling now. Calling this raises a message saying so. Install and load erpl_tunnel, then use tunnel_close there.");
	RegisterIfAbsent(loader, "tunnel_close_all",
	                 PragmaFunction::PragmaCall("tunnel_close_all", MovedTunnelCloseAll, {}),
	                 "MOVED to the erpl_tunnel extension, which owns tunnelling now. Calling this raises a message saying so. Install and load erpl_tunnel, then use tunnel_close_all there.");

	// tunnels() was a table function, not a pragma. Raising from bind is early enough that
	// the message is the only thing the caller sees.
	if (!AlreadyRegistered(loader.GetDatabaseInstance(), CatalogType::TABLE_FUNCTION_ENTRY, "tunnels")) {
		TableFunction tunnels("tunnels", {}, nullptr, MovedTunnelsBind);
		CreateTableFunctionInfo info(std::move(tunnels));
		FunctionDescription d;
		d.description =
		    "MOVED to the erpl_tunnel extension, which owns tunnelling now. Selecting from this "
		    "raises a message saying so. Install and load erpl_tunnel, then use tunnels() there.";
		d.examples = {"INSTALL erpl_tunnel FROM community; LOAD erpl_tunnel; SELECT * FROM tunnels();"};
		d.categories = {"tunnel", "deprecated"};
		info.descriptions.push_back(std::move(d));
		loader.RegisterFunction(std::move(info));
	}
}

} // namespace duckdb
