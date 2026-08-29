#include "catch.hpp"
#include "duckdb.hpp"

#include "sap_rfc_api.hpp"
#include "sap_connection.hpp"
#include "sap_function.hpp"
#include "duckdb_argument_helper.hpp"

#include <cstdlib>

using namespace duckdb;

// Issue #120 (memory): RfcType::ConvertRfcTable used to build every row with
// Value::STRUCT(child_list_t), which *derives and stores a fresh LogicalType per row*.
// For a wide table that is one copy of every field name and type per row -- measured at
// 2160 bytes per row for a 14-field BICS data cell, of which ~1040 was the duplicated
// type.  The rows of one table all have the same type by construction, so the type is now
// built once and shared.
//
// The sharing is directly observable: LogicalType keeps its child list in a refcounted
// ExtraTypeInfo, so rows that share a type report the same AuxInfo pointer.

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

} // namespace

TEST_CASE("Converted table rows share one STRUCT LogicalType", "[erpl_rfc][type_sharing]") {
	if (!SapEnvPresent()) {
		WARN("Skipping: ERPL_SAP_* environment variables not set (needs a live SAP system)");
		return;
	}

	auto conn = Connect();

	// DD02L has many rows and a wide, multi-field row structure -- exactly the shape
	// where the per-row type copy hurt.
	std::vector<Value> args = {ArgBuilder().Add("QUERY_TABLE", Value("T000"))
	                                       .Add("DELIMITER", Value("|"))
	                                       .Add("ROWCOUNT", Value::INTEGER(10))
	                                       .Build()};
	auto result_set = RfcResultSet::InvokeFunction(conn, "RFC_READ_TABLE", args);
	auto data = result_set->GetResultValue("DATA");

	REQUIRE(data.type().id() == LogicalTypeId::LIST);
	auto &rows = ListValue::GetChildren(data);
	REQUIRE(rows.size() > 1);
	REQUIRE(rows[0].type().id() == LogicalTypeId::STRUCT);

	// Every row must point at the *same* ExtraTypeInfo, not an equal copy of it.
	auto first = rows[0].type().AuxInfo();
	REQUIRE(first != nullptr);
	for (idx_t i = 1; i < rows.size(); i++) {
		REQUIRE(rows[i].type().AuxInfo() == first);
	}
}

TEST_CASE("Sharing the type does not change the converted values", "[erpl_rfc][type_sharing]") {
	if (!SapEnvPresent()) {
		WARN("Skipping: ERPL_SAP_* environment variables not set (needs a live SAP system)");
		return;
	}

	auto conn = Connect();
	std::vector<Value> args = {ArgBuilder().Add("QUERY_TABLE", Value("T000"))
	                                       .Add("DELIMITER", Value("|"))
	                                       .Add("ROWCOUNT", Value::INTEGER(5))
	                                       .Build()};
	auto result_set = RfcResultSet::InvokeFunction(conn, "RFC_READ_TABLE", args);
	auto data = result_set->GetResultValue("DATA");
	auto &rows = ListValue::GetChildren(data);

	REQUIRE(rows.size() > 0);
	for (auto &row : rows) {
		// The row still exposes its fields by name, with a non-empty WA payload.
		auto &children = StructValue::GetChildren(row);
		REQUIRE(children.size() >= 1);
		REQUIRE_FALSE(children[0].IsNull());
	}
	// The LIST's declared child type and the rows' actual type must agree -- deriving the
	// type per row used to let these drift apart.
	REQUIRE(ListType::GetChildType(data.type()) == rows[0].type());
}
