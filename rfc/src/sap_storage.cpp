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
#include "sap_rfc.hpp"
#include "erpl_tracing.hpp"
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
		// entry knows its return types up front.  If discovery fails
		// (table unknown, connection problem, …) fall through to a
		// nullptr return so DuckDB surfaces a normal CatalogException
		// instead of leaking an RFC error here.
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
			ERPL_TRACE_DEBUG_DATA("sap_storage",
			                      StringUtil::Format("Could not discover schema for '%s'", entry_name),
			                      ex.what());
			return nullptr;
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
// SapStorageAttach
// ---------------------------------------------------------------------------

static unique_ptr<Catalog> SapStorageAttach(optional_ptr<StorageExtensionInfo> storage_info, ClientContext &context,
                                            AttachedDatabase &db, const string &name, AttachInfo &info,
                                            AttachOptions &attach_options) {
	string secret_name;
	vector<string> allowed_tables;

	for (auto &entry : attach_options.options) {
		auto lower_name = StringUtil::Lower(entry.first);
		if (lower_name == "secret") {
			secret_name = entry.second.ToString();
		} else if (lower_name == "tables") {
			auto tables_str = entry.second.ToString();
			auto split = StringUtil::Split(tables_str, ',');
			for (auto &s : split) {
				StringUtil::Trim(s);
				if (!s.empty()) {
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
