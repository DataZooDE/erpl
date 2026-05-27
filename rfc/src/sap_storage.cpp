#include "duckdb/storage/storage_extension.hpp"
#include "duckdb/catalog/duck_catalog.hpp"
#include "duckdb/transaction/duck_transaction_manager.hpp"
#include "duckdb/catalog/default/default_generator.hpp"
#include "duckdb/catalog/catalog_entry/duck_schema_entry.hpp"
#include "duckdb/catalog/catalog_entry/view_catalog_entry.hpp"
#include "duckdb/parser/parsed_data/create_view_info.hpp"
#include "duckdb/parser/parsed_data/create_table_info.hpp"
#include "duckdb/parser/parsed_data/create_table_function_info.hpp"
#include "duckdb/parser/column_definition.hpp"
#include "duckdb/main/config.hpp"

// sap_rfc.hpp / erpl_tracing.hpp are version-independent and are needed by the
// TABLES pattern-expansion helper (ResolveTablePatterns), which runs for every
// DuckDB version.
#include "sap_rfc.hpp"
#include "erpl_tracing.hpp"

// entry_lookup_info.hpp was introduced alongside TryLookupEntryInternal in DuckDB v1.5+
#if DUCKDB_MINOR_VERSION >= 5
#include "duckdb/catalog/entry_lookup_info.hpp"
#include "scanner_invoke.hpp"
#include "scanner_read_table.hpp"
#include "scanner_show_functions.hpp"
#include "scanner_show_groups.hpp"
#include "scanner_describe_function.hpp"
#include "scanner_show_tables.hpp"
#include "scanner_describe_fields.hpp"
#include "sap_table_entry.hpp"
#endif

#include "sap_storage.hpp"
#include "sap_connection.hpp"

namespace duckdb {

// ---------------------------------------------------------------------------
// SapDefaultGenerator — lazy on-demand SAP table entries
//
// Each ATTACHed SAP table is exposed as a real TableCatalogEntry
// (SapTableEntry), not a view.  This lets DuckDB plan a direct
// LOGICAL_GET over `sap_read_table` so projection / filter pushdown and
// LIMIT early-termination operate without a view-expansion layer in
// between.  See issue #63 for the OOM the view wrapper caused.
// ---------------------------------------------------------------------------

#if DUCKDB_MINOR_VERSION >= 5

class SapDefaultGenerator : public DefaultGenerator {
public:
	SapDefaultGenerator(Catalog &catalog, SchemaCatalogEntry &schema, string secret_name,
	                    vector<string> allowed_tables)
	    : DefaultGenerator(catalog), schema(schema), secret_name(std::move(secret_name)),
	      allowed_tables(std::move(allowed_tables)) {
	}

	unique_ptr<CatalogEntry> CreateDefaultEntry(ClientContext &context, const string &entry_name) override {
		if (!allowed_tables.empty()) {
			bool found = false;
			for (auto &table : allowed_tables) {
				if (StringUtil::CIEquals(entry_name, table)) {
					found = true;
					break;
				}
			}
			if (!found) {
				return nullptr;
			}
		}

		// Discover the SAP table's column schema via DDIC so the catalog
		// entry knows its return types up front.  Two failure modes to
		// distinguish:
		//
		//   - "Table not in DDIC" (RFC_ABAP_EXCEPTION NOT_FOUND): only
		//     return nullptr when the lookup was a wildcard (no `TABLES`
		//     whitelist), so the user gets DuckDB's regular "table not
		//     found" error.  If the user listed this name explicitly in
		//     `TABLES`, the mismatch is interesting enough to surface.
		//   - Anything else (auth / connection / secret missing / socket
		//     error): surface an IOException with the underlying message
		//     and an actionable hint.  Silently returning nullptr here
		//     either masks the real problem ("table not found" when the
		//     truth is "no auth") or violates DuckDB's contract when the
		//     name is in our explicit GetDefaultEntries() list, which
		//     surfaces as an INTERNAL assertion further up the stack.
		vector<string> names;
		vector<LogicalType> types;
		try {
			auto bind_data = make_uniq<RfcReadTableBindData>(
			    entry_name, /*max_read_threads=*/0, /*limit=*/0, "RFC_READ_TABLE", "",
			    /*read_table_function_user_set=*/false, &DefaultRfcConnectionFactory, context);
			if (!secret_name.empty()) {
				bind_data->SetSecretName(secret_name);
			}
			bind_data->InitAndVerifyFields({});
			names = bind_data->GetRfcColumnNames();
			types = bind_data->GetReturnTypes();
		} catch (std::exception &ex) {
			const std::string err_msg(ex.what());
			ERPL_TRACE_DEBUG_DATA("sap_storage",
			                      StringUtil::Format("Could not discover schema for '%s'", entry_name),
			                      err_msg);
			const bool not_found_in_ddic = err_msg.find("NOT_FOUND") != std::string::npos;
			const bool listed_explicitly = !allowed_tables.empty();
			if (not_found_in_ddic && !listed_explicitly) {
				// Wildcard lookup against an unknown table — let DuckDB
				// raise its standard "table … does not exist" error.
				return nullptr;
			}
			std::string hint;
			if (secret_name.empty()) {
				hint = " Hint: pass SECRET='<name>' on ATTACH so the SAP RFC connection "
				       "can authenticate, or set SAP_ASHOST / SAP_USER / SAP_PASSWORD "
				       "environment variables. Example: "
				       "ATTACH '' AS sap (TYPE sap_rfc, SECRET 'my_secret', TABLES 'BSEG');";
			} else if (not_found_in_ddic) {
				hint = StringUtil::Format(
				    " Hint: '%s' was listed in TABLES but the attached SAP system reports "
				    "it does not exist in DDIC. Check the spelling, the client, or whether "
				    "the table is deployed on this system.",
				    entry_name);
			} else {
				hint = StringUtil::Format(
				    " Hint: verify the SECRET '%s' has valid ASHOST / SYSNR / CLIENT / "
				    "USER / PASSWD entries and the SAP system is reachable.",
				    secret_name);
			}
			throw IOException(
			    "Cannot resolve SAP table '%s' on the attached connection: %s.%s",
			    entry_name, err_msg, hint);
		}

		CreateTableInfo info;
		info.catalog = catalog.GetName();
		info.schema = DEFAULT_SCHEMA;
		info.table = entry_name;
		for (idx_t i = 0; i < names.size(); i++) {
			info.columns.AddColumn(ColumnDefinition(names[i], types[i]));
		}

		return make_uniq_base<CatalogEntry, SapTableEntry>(catalog, schema, info, entry_name, secret_name);
	}

	vector<string> GetDefaultEntries() override {
		return allowed_tables;
	}

private:
	SchemaCatalogEntry &schema;
	string secret_name;
	vector<string> allowed_tables;
};

#else

// Pre-1.5 legacy view-based generator (kept for old DuckDB versions).
class SapDefaultGenerator : public DefaultGenerator {
public:
	SapDefaultGenerator(Catalog &catalog, SchemaCatalogEntry &schema, string secret_name,
	                    vector<string> allowed_tables)
	    : DefaultGenerator(catalog), schema(schema), secret_name(std::move(secret_name)),
	      allowed_tables(std::move(allowed_tables)) {
	}

	unique_ptr<CatalogEntry> CreateDefaultEntry(ClientContext &context, const string &entry_name) override {
		if (!allowed_tables.empty()) {
			bool found = false;
			for (auto &table : allowed_tables) {
				if (StringUtil::CIEquals(entry_name, table)) {
					found = true;
					break;
				}
			}
			if (!found) {
				return nullptr;
			}
		}

		auto result = make_uniq<CreateViewInfo>();
		result->schema = DEFAULT_SCHEMA;
		result->view_name = entry_name;
		if (!secret_name.empty()) {
			result->sql = StringUtil::Format("SELECT * FROM sap_read_table(%s, SECRET=%s)",
			                                 SQLString(entry_name), SQLString(secret_name));
		} else {
			result->sql = StringUtil::Format("SELECT * FROM sap_read_table(%s)", SQLString(entry_name));
		}

		auto view_info = CreateViewInfo::FromSelect(context, std::move(result));
		return make_uniq_base<CatalogEntry, ViewCatalogEntry>(catalog, schema, *view_info);
	}

	vector<string> GetDefaultEntries() override {
		return allowed_tables;
	}

private:
	SchemaCatalogEntry &schema;
	string secret_name;
	vector<string> allowed_tables;
};

#endif

// ---------------------------------------------------------------------------
// SapSecretInjectorInfo / SapSecretBind — secret injection trampoline
//
// table_function_bind_t is a raw function pointer, so lambdas with captures
// cannot be used.  Instead, we carry the secret and original bind through
// function_info (a shared_ptr<TableFunctionInfo>) and forward to the original
// bind from a static trampoline.  This is installed on every overload of each
// SAP table function that is inserted into an attached catalog (issue #55).
// ---------------------------------------------------------------------------

#if DUCKDB_MINOR_VERSION >= 5

struct SapSecretInjectorInfo : public TableFunctionInfo {
	string secret_name;
	table_function_bind_t orig_bind;
	SapSecretInjectorInfo(string secret, table_function_bind_t bind)
	    : secret_name(std::move(secret)), orig_bind(bind) {
	}
};

static unique_ptr<FunctionData> SapSecretBind(ClientContext &ctx, TableFunctionBindInput &input,
                                              vector<LogicalType> &return_types, vector<string> &names) {
	D_ASSERT(input.info);
	auto &injector = input.info->Cast<SapSecretInjectorInfo>();
	if (input.named_parameters.find("secret") == input.named_parameters.end()) {
		input.named_parameters["secret"] = Value(injector.secret_name);
	}
	return injector.orig_bind(ctx, input, return_types, names);
}

// Wrap every overload in info.functions to inject the secret via the trampoline.
static void WrapFunctionInfoWithSecret(CreateTableFunctionInfo &info, const string &secret_name) {
	for (auto &func : info.functions.functions) {
		if (!func.bind) {
			continue;
		}
		func.function_info = make_shared_ptr<SapSecretInjectorInfo>(secret_name, func.bind);
		func.bind = SapSecretBind;
	}
}

// ---------------------------------------------------------------------------
// SapCatalog — DuckCatalog subclass that keeps the SAP DefaultGenerator
// active after Scan() calls (issue #54; same logic applies for both the
// pre-1.5 view-based generator and the 1.5+ SapTableEntry generator
// introduced for issue #63).
//
// DuckDB's CatalogSet::CreateDefaultEntries() sets created_all_entries=true
// after iterating GetDefaultEntries().  When that returns [] (the no-TABLES
// case for our generator), this permanently disables GetEntryDefault()
// for on-demand entry creation.  Overriding TryLookupEntryInternal() to
// reset the flag before each lookup keeps the generator alive across
// Scans (e.g. from Ducklake or `information_schema.tables`).
// ---------------------------------------------------------------------------

class SapCatalog : public DuckCatalog {
public:
	explicit SapCatalog(AttachedDatabase &db) : DuckCatalog(db), view_generator_(nullptr) {}

	void SetViewGenerator(SapDefaultGenerator *gen) {
		view_generator_ = gen;
	}

private:
	// Non-owning pointer; generator is owned by the CatalogSet inside this catalog.
	SapDefaultGenerator *view_generator_;

	CatalogEntryLookup TryLookupEntryInternal(CatalogTransaction transaction, const string &schema_name,
	                                          const EntryLookupInfo &lookup_info) override {
		// Reset so GetEntryDefault() still calls CreateDefaultEntry() even after
		// a Scan() (e.g. from Ducklake) has set created_all_entries=true.
		if (view_generator_) {
			view_generator_->created_all_entries = false;
		}
		if (lookup_info.GetAtClause()) {
			return {nullptr, nullptr, ErrorData(BinderException("SAP catalog does not support time travel"))};
		}
		auto schema_lookup = EntryLookupInfo::SchemaLookup(lookup_info, schema_name);
		auto schema_entry = LookupSchema(transaction, schema_lookup, OnEntryNotFound::RETURN_NULL);
		if (!schema_entry) {
			return {nullptr, nullptr, ErrorData()};
		}
		auto entry = schema_entry->LookupEntry(transaction, lookup_info);
		if (!entry) {
			return {schema_entry, nullptr, ErrorData()};
		}
		return {schema_entry, entry, ErrorData()};
	}
};

#endif // DUCKDB_MINOR_VERSION >= 5

// ---------------------------------------------------------------------------
// TABLES pattern expansion
//
// The `TABLES` ATTACH option accepts a comma-separated mix of exact table
// names and glob patterns (`*` → any run, `?` → single char).  Patterns are
// resolved here, at attach time, against DD02V (the same data-dictionary view
// `sap_show_tables()` uses) and expanded into concrete names.  Those names
// then populate the catalog's allow-list so `SHOW TABLES`, information_schema
// and named lookups all reflect the scoped set — without ERPL having to
// enumerate (and DDIC-discover) the tens of thousands of tables a real SAP
// system exposes.  See issue #70.
// ---------------------------------------------------------------------------

// Upper bound on how many tables a TABLES pattern set may resolve to.  Each
// resolved table becomes a catalog entry whose schema is discovered via an RFC
// roundtrip on first catalog scan; an unbounded pattern (e.g. `TABLES '*'`)
// would otherwise turn the first `SHOW TABLES` into tens of thousands of RFC
// calls.  Narrow the pattern or use `sap_show_tables()` to browse instead.
static constexpr idx_t MAX_ATTACH_TABLE_EXPANSION = 5000;

static bool IsTablePattern(const string &token) {
	return token.find('*') != string::npos || token.find('?') != string::npos ||
	       token.find('%') != string::npos;
}

static vector<string> ResolveTablePatterns(ClientContext &context, const string &secret_name,
                                           const vector<string> &patterns) {
	vector<string> out;
	for (auto pattern : patterns) {
		// glob → SQL LIKE, then guard the literal against quote injection.
		pattern = StringUtil::Replace(pattern, "*", "%");
		pattern = StringUtil::Replace(pattern, "?", "_");
		pattern = StringUtil::Replace(pattern, "'", "''");

		auto where_clause = StringUtil::Format(
		    "DDLANGUAGE = 'E' AND TABNAME LIKE '%s' AND "
		    "( TABCLASS = 'VIEW' OR TABCLASS = 'TRANSP' OR TABCLASS = 'POOL' OR TABCLASS = 'CLUSTER' )",
		    pattern);

		auto bind_data = make_uniq<RfcReadTableBindData>("DD02V", /*max_read_threads=*/0, /*limit=*/0,
		                                                 "RFC_READ_TABLE", "", false,
		                                                 &DefaultRfcConnectionFactory, context);
		if (!secret_name.empty()) {
			bind_data->SetSecretName(secret_name);
		}
		bind_data->InitOptionsFromWhereClause(where_clause);
		bind_data->InitAndVerifyFields({"TABNAME"});
		vector<column_t> column_ids = {0};
		bind_data->ActivateColumns(column_ids);

		DataChunk chunk;
		chunk.Initialize(Allocator::Get(context), bind_data->GetReturnTypes());
		while (bind_data->HasMoreResults()) {
			chunk.Reset();
			bind_data->Step(context, chunk);
			for (idx_t i = 0; i < chunk.size(); i++) {
				auto val = chunk.GetValue(0, i);
				if (val.IsNull()) {
					continue;
				}
				auto name = val.ToString();
				StringUtil::Trim(name);
				if (!name.empty()) {
					out.push_back(std::move(name));
				}
			}
		}
	}
	return out;
}

// ---------------------------------------------------------------------------
// SapStorageAttach
// ---------------------------------------------------------------------------

static unique_ptr<Catalog> SapStorageAttach(optional_ptr<StorageExtensionInfo> storage_info, ClientContext &context,
                                            AttachedDatabase &db, const string &name, AttachInfo &info,
                                            AttachOptions &attach_options) {
	string secret_name;
	vector<string> allowed_tables;
	vector<string> table_patterns;

	for (auto &entry : attach_options.options) {
		auto lower_name = StringUtil::Lower(entry.first);
		if (lower_name == "secret") {
			secret_name = entry.second.ToString();
		} else if (lower_name == "tables") {
			auto tables_str = entry.second.ToString();
			auto split = StringUtil::Split(tables_str, ',');
			for (auto &s : split) {
				StringUtil::Trim(s);
				if (s.empty()) {
					continue;
				}
				if (IsTablePattern(s)) {
					table_patterns.push_back(s);
				} else {
					allowed_tables.push_back(s);
				}
			}
		}
	}

	// Remove consumed options so SingleFileStorageManager doesn't reject them
	attach_options.options.erase("secret");
	attach_options.options.erase("tables");

	// Validate secret exists if specified
	if (!secret_name.empty()) {
		auto &secret_manager = SecretManager::Get(context);
		auto transaction = CatalogTransaction::GetSystemCatalogTransaction(context);
		auto secret_entry = secret_manager.GetSecretByName(transaction, secret_name);
		if (!secret_entry) {
			throw InvalidInputException("Secret '%s' not found", secret_name);
		}
	}

	// Expand any glob patterns in TABLES into concrete table names (issue #70).
	if (!table_patterns.empty()) {
		auto resolved = ResolveTablePatterns(context, secret_name, table_patterns);
		for (auto &name : resolved) {
			allowed_tables.push_back(std::move(name));
		}
	}

	// De-duplicate the allow-list case-insensitively, preserving first occurrence.
	if (!allowed_tables.empty()) {
		vector<string> deduped;
		case_insensitive_set_t seen;
		for (auto &name : allowed_tables) {
			if (seen.insert(name).second) {
				deduped.push_back(name);
			}
		}
		allowed_tables = std::move(deduped);

		if (allowed_tables.size() > MAX_ATTACH_TABLE_EXPANSION) {
			throw InvalidInputException(
			    "ATTACH TABLES resolved to %llu tables, exceeding the limit of %llu. "
			    "Narrow the pattern (e.g. TABLES '/DMO/*' or 'Z*'), or omit TABLES and use "
			    "sap_show_tables() to browse the full catalog.",
			    (idx_t)allowed_tables.size(), MAX_ATTACH_TABLE_EXPANSION);
		}
	}

	// Create an in-memory catalog
	info.path = ":memory:";

#if DUCKDB_MINOR_VERSION >= 5
	auto sap_catalog = make_uniq<SapCatalog>(db);
	sap_catalog->Initialize(false);

	auto system_transaction = CatalogTransaction::GetSystemTransaction(db.GetDatabase());
	auto &schema = sap_catalog->GetSchema(system_transaction, DEFAULT_SCHEMA);
	auto &duck_schema = schema.Cast<DuckSchemaEntry>();

	// ── Table generator (on-demand SapTableEntry per SAP table, issue #63) ─
	auto &table_catalog_set = duck_schema.GetCatalogSet(CatalogType::TABLE_ENTRY);
	auto table_gen = make_uniq<SapDefaultGenerator>(*sap_catalog, schema, secret_name, allowed_tables);
	sap_catalog->SetViewGenerator(table_gen.get());
	table_catalog_set.SetDefaultGenerator(std::move(table_gen));

	// ── SAP table functions (catalog-qualified calls, issue #55) ────────────
	// Insert each SAP table function directly into the catalog at attach time.
	// The bind trampoline injects the attachment secret when the caller hasn't
	// provided one explicitly.  Using scanner factory functions avoids including
	// table_function_catalog_entry.hpp (which causes an ODR conflict with the
	// explicit out-of-line Name definition in libduckdb_static.a).
	{
		vector<CreateTableFunctionInfo> funcs;
		funcs.emplace_back(CreateRfcInvokeScanFunction());
		funcs.emplace_back(CreateRfcReadTableScanFunction());
		funcs.emplace_back(CreateRfcShowFunctionScanFunction());
		funcs.emplace_back(CreateRfcShowGroupScanFunction());
		funcs.emplace_back(CreateRfcDescribeFunctionScanFunction());
		funcs.emplace_back(CreateRfcShowTablesScanFunction());
		funcs.emplace_back(CreateRfcDescribeFieldsScanFunction());

		for (auto &info : funcs) {
			// Functions registered in the system catalog are marked internal; strip
			// that flag so DuckDB allows them to be created in the sap_rfc catalog.
			info.internal = false;
			if (!secret_name.empty()) {
				WrapFunctionInfoWithSecret(info, secret_name);
			}
			duck_schema.CreateTableFunction(system_transaction, info);
		}
	}

	return std::move(sap_catalog);
#else
	auto catalog = make_uniq<DuckCatalog>(db);
	catalog->Initialize(false);

	auto system_transaction = CatalogTransaction::GetSystemTransaction(db.GetDatabase());
	auto &schema = catalog->GetSchema(system_transaction, DEFAULT_SCHEMA);
	auto &duck_schema = schema.Cast<DuckSchemaEntry>();
	auto &catalog_set = duck_schema.GetCatalogSet(CatalogType::VIEW_ENTRY);
	auto default_generator =
	    make_uniq<SapDefaultGenerator>(*catalog, schema, std::move(secret_name), std::move(allowed_tables));
	catalog_set.SetDefaultGenerator(std::move(default_generator));

	return std::move(catalog);
#endif
}

static unique_ptr<TransactionManager> SapStorageTransactionManager(optional_ptr<StorageExtensionInfo> storage_info,
                                                                   AttachedDatabase &db, Catalog &catalog) {
	return make_uniq<DuckTransactionManager>(db);
}

void RegisterSapStorageExtension(ExtensionLoader &loader) {
	auto &instance = loader.GetDatabaseInstance();
	auto &config = DBConfig::GetConfig(instance);

#if DUCKDB_MINOR_VERSION >= 5
	auto storage_ext = make_shared_ptr<StorageExtension>();
	storage_ext->attach = SapStorageAttach;
	storage_ext->create_transaction_manager = SapStorageTransactionManager;
	StorageExtension::Register(config, "sap_rfc", std::move(storage_ext));
#else
	auto storage_ext = make_uniq<StorageExtension>();
	storage_ext->attach = SapStorageAttach;
	storage_ext->create_transaction_manager = SapStorageTransactionManager;
	config.storage_extensions["sap_rfc"] = std::move(storage_ext);
#endif
}

} // namespace duckdb
