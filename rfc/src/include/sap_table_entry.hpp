#pragma once

#include "duckdb.hpp"
#include "duckdb/catalog/catalog_entry/table_catalog_entry.hpp"
#include "duckdb/storage/table_storage_info.hpp"
#include "duckdb/storage/statistics/base_statistics.hpp"

namespace duckdb {

// A TableCatalogEntry that maps a single SAP table accessed via RFC.  Used
// by SapDefaultGenerator instead of a ViewCatalogEntry wrapper so that
// DuckDB plans a direct LOGICAL_GET over `sap_read_table` — projection,
// filter and (via early termination) LIMIT all flow into the scan
// without a view-expansion hop in between.  See issue #63.
class SapTableEntry : public TableCatalogEntry {
public:
	SapTableEntry(Catalog &catalog, SchemaCatalogEntry &schema, CreateTableInfo &info,
	              string sap_table_name, string secret_name);

	TableFunction GetScanFunction(ClientContext &context, unique_ptr<FunctionData> &bind_data) override;

	unique_ptr<BaseStatistics> GetStatistics(ClientContext &context, column_t column_id) override;

	TableStorageInfo GetStorageInfo(ClientContext &context) override;

private:
	string sap_table_name;
	string secret_name;
};

} // namespace duckdb
