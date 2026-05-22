#include "catch.hpp"
#include "test_helpers.hpp"
#include "duckdb.hpp"

#include "sap_rfc.hpp"

using namespace duckdb;

// Issue #63: very high memory usage during querying.
//
// Two design constraints:
//
//  1. RFC_READ_TABLE enforces server-side that ROWSKIPS % ROWCOUNT == 0.
//     Doubling the batch size between successive round-trips violates this
//     invariant unless we wait for the running total_rows to be divisible
//     by the larger size.  The state machine walks the batch size up
//     STANDARD_VECTOR_SIZE → 2× → 4× → … → MAX_BATCH_SIZE only at those
//     safe moments (logic in RfcReadColumnTask::ExecuteTask).
//
//  2. The first batch dominates peak RSS for `LIMIT N` queries — DuckDB
//     stops pulling once N rows arrive, so any SDK buffer pre-allocated
//     for a larger batch is wasted memory.  We therefore start small
//     by default so LIMIT N pays for at most STANDARD_VECTOR_SIZE rows.
//
// These tests pin down the constructor side of (2); the divisibility
// behaviour of (1) is covered by sap_read_table.test against a live SAP
// system.

TEST_CASE("desired_batch_size starts at STANDARD_VECTOR_SIZE without an explicit limit",
          "[erpl_rfc][batching]") {
	RfcReadColumnStateMachine sm(/*bind_data=*/nullptr, /*column_idx=*/0, /*limit=*/0);
	REQUIRE(sm.GetDesiredBatchSize() == STANDARD_VECTOR_SIZE);
}

TEST_CASE("Explicit MAX_ROWS limit below the warm-up start caps batch size",
          "[erpl_rfc][batching]") {
	RfcReadColumnStateMachine sm(/*bind_data=*/nullptr, /*column_idx=*/0, /*limit=*/7);
	REQUIRE(sm.GetDesiredBatchSize() == 7u);
}

TEST_CASE("Explicit MAX_ROWS limit above the warm-up start leaves it at STANDARD_VECTOR_SIZE",
          "[erpl_rfc][batching]") {
	RfcReadColumnStateMachine sm(/*bind_data=*/nullptr, /*column_idx=*/0,
	                             /*limit=*/STANDARD_VECTOR_SIZE * 3);
	REQUIRE(sm.GetDesiredBatchSize() == STANDARD_VECTOR_SIZE);
}
