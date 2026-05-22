#include "catch.hpp"
#include "test_helpers.hpp"
#include "duckdb.hpp"

#include "sap_rfc.hpp"

using namespace duckdb;

// Issue #63: very high memory usage during querying.
//
// Two design constraints encoded by RfcReadColumnStateMachine:
//
//  1. RFC_READ_TABLE enforces server-side that ROWSKIPS % ROWCOUNT == 0.
//     Doubling the batch size between successive round-trips violates this
//     invariant unless we wait for the running total_rows to be divisible
//     by the larger size.  See NextDesiredBatchSize.
//
//  2. The final batch under MAX_ROWS must not produce an illegal ROWCOUNT
//     either — when the trimmed value wouldn't divide ROWSKIPS, we
//     deliberately over-fetch and let LoadNextBatchToDuckDBColumn clip
//     the surplus client-side.  See TrimmedActualBatchSize.
//
// These tests cover both rules without needing a live SAP system, so a
// future change to the constants or growth strategy can be caught by CI
// rather than only by sap_read_table.test against the trial.

TEST_CASE("desired_batch_size starts at STANDARD_VECTOR_SIZE without an explicit limit",
          "[erpl_rfc][batching]") {
	RfcReadColumnStateMachine sm(/*bind_data=*/nullptr, /*column_idx=*/0, /*limit=*/0);
	REQUIRE(sm.GetDesiredBatchSize() == STANDARD_VECTOR_SIZE);
}

TEST_CASE("Explicit MAX_ROWS limit below MAX_BATCH_SIZE collapses the scan to one batch",
          "[erpl_rfc][batching]") {
	// Tiny limit: single small batch.
	{
		RfcReadColumnStateMachine sm(/*bind_data=*/nullptr, /*column_idx=*/0, /*limit=*/7);
		REQUIRE(sm.GetDesiredBatchSize() == 7u);
	}
	// Limit between STANDARD_VECTOR_SIZE and MAX_BATCH_SIZE — previously
	// regressed to STANDARD_VECTOR_SIZE and then required a second batch
	// whose trimmed ROWCOUNT violated ROWSKIPS % ROWCOUNT == 0.  Must
	// now use ROWCOUNT == limit for a single safe batch.
	{
		const unsigned int limit = STANDARD_VECTOR_SIZE + 1;
		RfcReadColumnStateMachine sm(/*bind_data=*/nullptr, /*column_idx=*/0, limit);
		REQUIRE(sm.GetDesiredBatchSize() == limit);
	}
	{
		RfcReadColumnStateMachine sm(/*bind_data=*/nullptr, /*column_idx=*/0,
		                             /*limit=*/RfcReadColumnStateMachine::MAX_BATCH_SIZE);
		REQUIRE(sm.GetDesiredBatchSize() == RfcReadColumnStateMachine::MAX_BATCH_SIZE);
	}
}

TEST_CASE("Explicit MAX_ROWS limit above MAX_BATCH_SIZE keeps the warm-up start",
          "[erpl_rfc][batching]") {
	RfcReadColumnStateMachine sm(/*bind_data=*/nullptr, /*column_idx=*/0,
	                             /*limit=*/RfcReadColumnStateMachine::MAX_BATCH_SIZE + 1);
	REQUIRE(sm.GetDesiredBatchSize() == STANDARD_VECTOR_SIZE);
}

TEST_CASE("NextDesiredBatchSize doubles geometrically while keeping ROWSKIPS valid",
          "[erpl_rfc][batching]") {
	using SM = RfcReadColumnStateMachine;
	unsigned int size = STANDARD_VECTOR_SIZE;
	unsigned int total = 0;
	// Walk 32 batches at the current size each — long enough to reach the
	// cap and a few "stuck at cap" steady-state batches afterwards.
	for (int i = 0; i < 32; i++) {
		// Pre-call invariant: ROWSKIPS=total must be a multiple of ROWCOUNT=size.
		REQUIRE(total % size == 0);
		total += size;
		size = SM::NextDesiredBatchSize(size, total);
		REQUIRE(size <= SM::MAX_BATCH_SIZE);
	}
	// We must have reached the cap.
	REQUIRE(size == SM::MAX_BATCH_SIZE);
}

TEST_CASE("TrimmedActualBatchSize preserves ROWSKIPS % ROWCOUNT == 0",
          "[erpl_rfc][batching]") {
	using SM = RfcReadColumnStateMachine;

	// limit==0 means no MAX_ROWS: trim never fires.
	REQUIRE(SM::TrimmedActualBatchSize(2048, 4096, 0) == 2048u);

	// limit is larger than what the current batch would fetch: no trim.
	REQUIRE(SM::TrimmedActualBatchSize(2048, 0, 5000) == 2048u);

	// First batch covers the limit exactly: trim to the limit.
	REQUIRE(SM::TrimmedActualBatchSize(2048, 0, 1000) == 1000u);

	// Mid-scan, remaining divides ROWSKIPS cleanly: trim safely.
	// total=2048, limit=3072 → remaining=1024, 2048 % 1024 == 0 → trim to 1024.
	REQUIRE(SM::TrimmedActualBatchSize(2048, 2048, 3072) == 1024u);

	// Mid-scan, remaining does NOT divide ROWSKIPS: must over-fetch.
	// total=2048, limit=3000 → remaining=952, 2048 % 952 != 0 → keep desired.
	REQUIRE(SM::TrimmedActualBatchSize(2048, 2048, 3000) == 2048u);

	// LIMIT exactly STANDARD_VECTOR_SIZE+1 on the second batch: the
	// constructor trims desired_batch_size to limit (covered above), so
	// the second batch never fires.  Spot-check the trim helper itself
	// also handles the boundary safely.
	REQUIRE(SM::TrimmedActualBatchSize(STANDARD_VECTOR_SIZE + 1, 0,
	                                   STANDARD_VECTOR_SIZE + 1) ==
	        STANDARD_VECTOR_SIZE + 1);
}
