#include "sap_table_entry.hpp"

#include "duckdb/parser/parsed_data/create_table_info.hpp"

#include "scanner_read_table.hpp"
#include "sap_rfc.hpp"

namespace duckdb {

SapTableEntry::SapTableEntry(Catalog &catalog, SchemaCatalogEntry &schema, CreateTableInfo &info,
                             string sap_table_name_p, string secret_name_p)
    : TableCatalogEntry(catalog, schema, info), sap_table_name(std::move(sap_table_name_p)),
      secret_name(std::move(secret_name_p)) {
}

TableFunction SapTableEntry::GetScanFunction(ClientContext &context, unique_ptr<FunctionData> &bind_data) {
	auto fn = CreateRfcReadTableScanFunction();

	auto data = make_uniq<RfcReadTableBindData>(sap_table_name, /*max_read_threads=*/0,
	                                            /*limit=*/0, "RFC_READ_TABLE", "",
	                                            /*read_table_function_user_set=*/false,
	                                            &DefaultRfcConnectionFactory, context);
	if (!secret_name.empty()) {
		data->SetSecretName(secret_name);
	}
	// Discover all fields — the catalog entry's ColumnList was populated at
	// creation, so this must agree on the projected return types and order.
	data->InitAndVerifyFields({});

	bind_data = std::move(data);
	return fn;
}

unique_ptr<BaseStatistics> SapTableEntry::GetStatistics(ClientContext &, column_t) {
	return nullptr;
}

TableStorageInfo SapTableEntry::GetStorageInfo(ClientContext &) {
	return TableStorageInfo();
}

} // namespace duckdb
