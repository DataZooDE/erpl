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
//  2. `RfcReadColumnStateMachine` started with `desired_batch_size =
//     20 * STANDARD_VECTOR_SIZE` (~40 960 rows).  One state machine runs
//     per column, so for a 300-column table the very first RFC fetch
//     could request ~12 M cells before DuckDB ever applied the outer
//     LIMIT.  This file pins down (2): the initial batch must be small so
//     that early-termination by the LIMIT operator caps total RFC traffic.

TEST_CASE("Initial desired_batch_size stays small without an explicit limit (issue #63)",
          "[erpl_rfc][batching]") {
	RfcReadColumnStateMachine sm(/*bind_data=*/nullptr, /*column_idx=*/0, /*limit=*/0);
	// Before the fix the initial size was 20 * STANDARD_VECTOR_SIZE
	// (~40k rows).  After the fix the first RFC fetch is bounded to a
	// single DuckDB vector so a downstream LIMIT N can stop the scan
	// after one tiny round-trip.  Adaptive growth (handled separately
	// in the state transitions) ramps the size back up for full scans.
	REQUIRE(sm.GetDesiredBatchSize() <= STANDARD_VECTOR_SIZE);
}

TEST_CASE("Explicit MAX_ROWS limit caps initial batch size", "[erpl_rfc][batching]") {
	RfcReadColumnStateMachine sm(/*bind_data=*/nullptr, /*column_idx=*/0, /*limit=*/7);
	REQUIRE(sm.GetDesiredBatchSize() == 7u);
}
