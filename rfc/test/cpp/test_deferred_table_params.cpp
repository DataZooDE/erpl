#include "catch.hpp"
#include "duckdb.hpp"

#include "sap_rfc_api.hpp"
#include "sap_connection.hpp"
#include "sap_function.hpp"
#include "duckdb_argument_helper.hpp"

#include <cstdlib>

using namespace duckdb;

// Issue #120 (memory): RfcResultSet materialises *every* result parameter into a
// duckdb::Value tree in its constructor.  For BICS that means E_T_DATA_CELLS -- the
// single biggest table in the response -- is fully boxed before a single output row is
// produced, at ~1.1 KB per cell even after the shared-type fix.
//
// A caller that intends to stream a table straight off the SDK handle can now mark it
// deferred: the conversion is skipped and the raw RFC_TABLE_HANDLE is exposed instead.
// The handle stays valid as long as the RfcResultSet (and through it the invocation)
// is alive.

namespace {

bool SapEnvPresent() {
	return std::getenv("ERPL_SAP_ASHOST") && std::getenv("ERPL_SAP_SYSNR") &&
	       std::getenv("ERPL_SAP_USER") && std::getenv("ERPL_SAP_PASSWORD") &&
	       std::getenv("ERPL_SAP_CLIENT") && std::getenv("ERPL_SAP_LANG");
}

std::shared_ptr<RfcConnection> Connect() {
	RfcAuthParams params;
	params.ashost   = std::getenv("ERPL_SAP_ASHOST");
	params.sysnr    = std::getenv("ERPL_SAP_SYSNR");
	params.user     = std::getenv("ERPL_SAP_USER");
	params.password = std::getenv("ERPL_SAP_PASSWORD");
	params.client   = std::getenv("ERPL_SAP_CLIENT");
	params.lang     = std::getenv("ERPL_SAP_LANG");
	return params.Connect();
}

std::vector<Value> ReadTableArgs() {
	return {ArgBuilder().Add("QUERY_TABLE", Value("T000"))
	                    .Add("DELIMITER", Value("|"))
	                    .Add("ROWCOUNT", Value::INTEGER(5))
	                    .Build()};
}

} // namespace

TEST_CASE("A deferred table parameter is not materialised", "[erpl_rfc][deferred_tables]") {
	if (!SapEnvPresent()) {
		WARN("Skipping: ERPL_SAP_* environment variables not set (needs a live SAP system)");
		return;
	}

	auto conn = Connect();
	auto args = ReadTableArgs();
	auto deferred = std::set<std::string>{"DATA"};
	auto result_set = RfcResultSet::InvokeFunction(conn, "RFC_READ_TABLE", args, "", deferred);

	// The Value tree for DATA was never built ...
	auto data = result_set->GetResultValue("DATA");
	REQUIRE(data.type().id() == LogicalTypeId::LIST);
	REQUIRE(ListValue::GetChildren(data).empty());

	// ... but the rows are reachable through the SDK handle.  (T000 holds one row per
	// client, so pin only that there are some -- not how many.)
	REQUIRE(result_set->GetResultTableRowCount("DATA") > 0);
	REQUIRE(result_set->GetResultTableHandle("DATA") != nullptr);

	// A parameter that was NOT deferred is materialised as before.
	auto fields = result_set->GetResultValue("FIELDS");
	REQUIRE(ListValue::GetChildren(fields).size() > 0);
}

TEST_CASE("Streaming a deferred table yields the same values as materialising it",
          "[erpl_rfc][deferred_tables]") {
	if (!SapEnvPresent()) {
		WARN("Skipping: ERPL_SAP_* environment variables not set (needs a live SAP system)");
		return;
	}

	auto conn = Connect();

	auto materialised_args = ReadTableArgs();
	auto materialised = RfcResultSet::InvokeFunction(conn, "RFC_READ_TABLE", materialised_args);
	auto expected_rows = ListValue::GetChildren(materialised->GetResultValue("DATA"));
	REQUIRE(expected_rows.size() > 0);

	auto streamed_args = ReadTableArgs();
	auto deferred = std::set<std::string>{"DATA"};
	auto streamed = RfcResultSet::InvokeFunction(conn, "RFC_READ_TABLE", streamed_args, "", deferred);

	auto table_handle = streamed->GetResultTableHandle("DATA");
	REQUIRE(table_handle != nullptr);
	// GetResultTableRowType returns the TABLES parameter's type; its field infos describe
	// the row, so a single field is read through that field's own RfcType.
	auto row_type = streamed->GetResultTableRowType("DATA");
	auto wa_type = row_type->GetFieldInfo("WA").GetRfcType();

	REQUIRE(streamed->GetResultTableRowCount("DATA") == expected_rows.size());

	RFC_ERROR_INFO error_info;
	for (unsigned int i = 0; i < streamed->GetResultTableRowCount("DATA"); i++) {
		REQUIRE(RfcMoveTo(table_handle, i, &error_info) == RFC_OK);
		auto row_handle = RfcGetCurrentRow(table_handle, &error_info);
		auto wa = wa_type->ConvertRfcValueFromContainer(row_handle, "WA");
		auto expected_wa = StructValue::GetChildren(expected_rows[i])[0];
		REQUIRE(wa.ToString() == expected_wa.ToString());
	}
}

TEST_CASE("Selecting a deferred table as the result path is rejected",
          "[erpl_rfc][deferred_tables]") {
	if (!SapEnvPresent()) {
		WARN("Skipping: ERPL_SAP_* environment variables not set (needs a live SAP system)");
		return;
	}

	auto conn = Connect();
	auto args = ReadTableArgs();
	auto deferred = std::set<std::string>{"DATA"};
	// The rows of DATA only exist behind the SDK handle, so a result path that selects
	// DATA would quietly report an empty result rather than the rows the caller asked
	// for.  Refuse the combination instead.
	REQUIRE_THROWS(RfcResultSet::InvokeFunction(conn, "RFC_READ_TABLE", args, "/DATA", deferred));

	// Deferring a *different* table while selecting this one stays legal.
	auto other = std::set<std::string>{"DATA"};
	auto ok = RfcResultSet::InvokeFunction(conn, "RFC_READ_TABLE", args, "/FIELDS", other);
	REQUIRE(ok->TotalRows() > 0);
}

TEST_CASE("Asking for a table handle that was not deferred is an error",
          "[erpl_rfc][deferred_tables]") {
	if (!SapEnvPresent()) {
		WARN("Skipping: ERPL_SAP_* environment variables not set (needs a live SAP system)");
		return;
	}

	auto conn = Connect();
	auto args = ReadTableArgs();
	auto result_set = RfcResultSet::InvokeFunction(conn, "RFC_READ_TABLE", args);
	// Not deferred -> no promise that the handle outlives the conversion, so refuse
	// rather than hand out something whose lifetime the caller cannot reason about.
	REQUIRE_THROWS(result_set->GetResultTableHandle("DATA"));
	// And a name that is not a table at all.
	REQUIRE_THROWS(result_set->GetResultTableRowCount("NO_SUCH_PARAM"));
	REQUIRE_THROWS(result_set->GetResultTableRowType("DATA"));
}
