#include "catch.hpp"
#include "test_helpers.hpp"
#include "duckdb.hpp"

#include "sap_rfc.hpp"

using namespace duckdb;

// Issue #63: very high memory usage during querying.
//
// `SELECT * FROM sap.<wide_table> LIMIT 10` against an ATTACHed SAP catalog
// used >16 GB of RAM and OOMed.  Two factors combined:
//
//  1. The ATTACH layer exposed each SAP table as a VIEW whose body was
//     `SELECT * FROM sap_read_table(...)`, hiding the underlying table
//     function from the optimizer and preventing column/filter/limit
//     pushdown into the scan (architectural fix lives in sap_storage.cpp /
//     SapTableEntry).
//
//  2. `RfcReadColumnStateMachine::desired_batch_size` must stay constant
//     for the whole scan: RFC_READ_TABLE rejects any call where
//     ROWSKIPS is not an integer multiple of ROWCOUNT, so we cannot
//     adaptively grow the batch size between successive round-trips.
//     When the user provides an explicit MAX_ROWS the constructor trims
//     the batch size down so LIMIT N still fits in one small fetch.

TEST_CASE("desired_batch_size defaults to MAX_BATCH_SIZE without an explicit limit",
          "[erpl_rfc][batching]") {
	RfcReadColumnStateMachine sm(/*bind_data=*/nullptr, /*column_idx=*/0, /*limit=*/0);
	REQUIRE(sm.GetDesiredBatchSize() == RfcReadColumnStateMachine::MAX_BATCH_SIZE);
}

TEST_CASE("Explicit MAX_ROWS limit caps batch size", "[erpl_rfc][batching]") {
	RfcReadColumnStateMachine sm(/*bind_data=*/nullptr, /*column_idx=*/0, /*limit=*/7);
	REQUIRE(sm.GetDesiredBatchSize() == 7u);
}

TEST_CASE("Explicit MAX_ROWS limit larger than the ceiling leaves batch size at MAX_BATCH_SIZE",
          "[erpl_rfc][batching]") {
	RfcReadColumnStateMachine sm(/*bind_data=*/nullptr, /*column_idx=*/0,
	                             /*limit=*/RfcReadColumnStateMachine::MAX_BATCH_SIZE * 2);
	REQUIRE(sm.GetDesiredBatchSize() == RfcReadColumnStateMachine::MAX_BATCH_SIZE);
}
