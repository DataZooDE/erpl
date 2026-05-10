#include "duckdb/storage/storage_extension.hpp"
#include "duckdb/catalog/duck_catalog.hpp"
#include "duckdb/transaction/duck_transaction_manager.hpp"
#include "duckdb/catalog/default/default_generator.hpp"
#include "duckdb/catalog/catalog_entry/duck_schema_entry.hpp"
#include "duckdb/catalog/catalog_entry/view_catalog_entry.hpp"
#include "duckdb/parser/parsed_data/create_view_info.hpp"
#include "duckdb/main/config.hpp"

// entry_lookup_info.hpp was introduced alongside TryLookupEntryInternal in DuckDB v1.5+
#if DUCKDB_MINOR_VERSION >= 5
#include "duckdb/catalog/entry_lookup_info.hpp"
#endif

#include "sap_storage.hpp"
#include "sap_connection.hpp"

namespace duckdb {

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

#if DUCKDB_MINOR_VERSION >= 5
// SapCatalog wraps DuckCatalog and overrides the private virtual TryLookupEntryInternal
// (the NVI hook that backs all entry lookups) to reset created_all_entries on the
// DefaultGenerator before each lookup.
//
// Background: DuckDB's CatalogSet::CreateDefaultEntries() sets created_all_entries=true
// after iterating GetDefaultEntries(), even when it returns [] (the no-TABLES case where
// any SAP table name is valid on demand). After a Scan() — triggered e.g. by Ducklake
// enumerating all attached catalogs — this flag is permanently true, so GetEntryDefault()
// skips the generator and every subsequent SAP table lookup fails.
//
// By resetting the flag immediately before the catalog-set lookup we ensure the generator
// stays active across Scans without requiring any change to DuckDB's own code.
class SapCatalog : public DuckCatalog {
public:
	explicit SapCatalog(AttachedDatabase &db) : DuckCatalog(db), sap_generator_(nullptr) {}

	void SetGenerator(SapDefaultGenerator *gen) {
		sap_generator_ = gen;
	}

private:
	SapDefaultGenerator *sap_generator_; // non-owning; lifetime tied to this catalog's CatalogSet

	CatalogEntryLookup TryLookupEntryInternal(CatalogTransaction transaction, const string &schema_name,
	                                          const EntryLookupInfo &lookup_info) override {
		if (sap_generator_) {
			sap_generator_->created_all_entries = false;
		}
		if (lookup_info.GetAtClause()) {
			// SAP catalog does not support time-travel AT CLAUSE syntax
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
#endif

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
	auto &catalog_set = duck_schema.GetCatalogSet(CatalogType::VIEW_ENTRY);
	auto default_generator =
	    make_uniq<SapDefaultGenerator>(*sap_catalog, schema, std::move(secret_name), std::move(allowed_tables));
	sap_catalog->SetGenerator(default_generator.get());
	catalog_set.SetDefaultGenerator(std::move(default_generator));

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
