#include "duckdb/storage/storage_extension.hpp"
#include "duckdb/catalog/duck_catalog.hpp"
#include "duckdb/transaction/duck_transaction_manager.hpp"
#include "duckdb/catalog/default/default_generator.hpp"
#include "duckdb/catalog/catalog_entry/duck_schema_entry.hpp"
#include "duckdb/catalog/catalog_entry/view_catalog_entry.hpp"
#include "duckdb/parser/parsed_data/create_view_info.hpp"
#include "duckdb/main/config.hpp"

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
}

static unique_ptr<TransactionManager> SapStorageTransactionManager(optional_ptr<StorageExtensionInfo> storage_info,
                                                                   AttachedDatabase &db, Catalog &catalog) {
	return make_uniq<DuckTransactionManager>(db);
}

void RegisterSapStorageExtension(ExtensionLoader &loader) {
	auto &instance = loader.GetDatabaseInstance();
	auto &config = DBConfig::GetConfig(instance);

	auto storage_ext = make_uniq<StorageExtension>();
	storage_ext->attach = SapStorageAttach;
	storage_ext->create_transaction_manager = SapStorageTransactionManager;
	config.storage_extensions["sap_rfc"] = std::move(storage_ext);
}

} // namespace duckdb
