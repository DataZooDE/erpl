#include "catch.hpp"
#include "test_helpers.hpp"
#include "duckdb.hpp"

#include <thread>
#include <set>
#include <vector>

#include "sap_rfc.hpp"

using namespace duckdb;

// Hand-out of row windows for a partitioned sap_read_table scan.
//
// This is the piece that decides which rows each worker reads, so it is also the
// piece that loses or duplicates them if it is wrong. It is deliberately free of
// any RFC dependency: the equivalent logic in erpl_odp could only be exercised
// through a live connection, which is why an observed under-count there went
// unexplained for so long.
//
// Two SAP-side rules constrain it, both verified live against the trial:
//
//   1. RFC_READ_TABLE rejects ROWSKIPS % ROWCOUNT != 0 outright --
//      "RFC_READ_TABLE (ROWSKIPS MOD ROWCOUNT <> 0)". So every offset handed out
//      must be a multiple of the batch size.
//   2. Reading at or past the end returns zero rows rather than erroring, so a
//      short read is how a worker learns the table is exhausted.

TEST_CASE("windows are handed out in order and do not overlap", "[erpl_rfc][partition]") {
	RfcRowWindowScheduler sched(/*window_size=*/4096, /*batch_size=*/2048, /*max_rows=*/0);

	idx_t off = 0, count = 0;
	REQUIRE(sched.Claim(off, count));
	REQUIRE(off == 0);
	REQUIRE(count == 4096);

	REQUIRE(sched.Claim(off, count));
	REQUIRE(off == 4096);
	REQUIRE(count == 4096);

	REQUIRE(sched.Claim(off, count));
	REQUIRE(off == 8192);
}

TEST_CASE("every offset is a multiple of the batch size", "[erpl_rfc][partition]") {
	// Violating this is an immediate ABAP error, not a wrong answer -- but only on
	// the call that violates it, so a scheduler that drifts would fail sporadically.
	RfcRowWindowScheduler sched(/*window_size=*/6144, /*batch_size=*/2048, /*max_rows=*/0);
	idx_t off = 0, count = 0;
	for (int i = 0; i < 10; i++) {
		REQUIRE(sched.Claim(off, count));
		REQUIRE(off % 2048 == 0);
	}
}

TEST_CASE("a short read stops further claims", "[erpl_rfc][partition]") {
	RfcRowWindowScheduler sched(/*window_size=*/4096, /*batch_size=*/2048, /*max_rows=*/0);
	idx_t off = 0, count = 0;
	REQUIRE(sched.Claim(off, count));
	REQUIRE(sched.Claim(off, count));

	// The second worker found the table ends inside its window.
	sched.ReportExhausted();

	REQUIRE_FALSE(sched.Claim(off, count));
}

TEST_CASE("exhaustion does not invalidate a window already claimed", "[erpl_rfc][partition]") {
	// The bug this guards against: in erpl_odp one worker's end-of-data flag was
	// treated as globally terminal, and other workers stopped mid-flight and
	// truncated the result. A worker holding a window must always be allowed to
	// finish reading it.
	RfcRowWindowScheduler sched(/*window_size=*/4096, /*batch_size=*/2048, /*max_rows=*/0);
	idx_t a_off = 0, a_count = 0;
	idx_t b_off = 0, b_count = 0;
	REQUIRE(sched.Claim(a_off, a_count));
	REQUIRE(sched.Claim(b_off, b_count));

	sched.ReportExhausted();

	// Both claims stay valid and stay disjoint; only NEW claims are refused.
	REQUIRE(a_off + a_count == b_off);
	idx_t off = 0, count = 0;
	REQUIRE_FALSE(sched.Claim(off, count));
}

// ---------------------------------------------------------------------------
// MAX_ROWS
// ---------------------------------------------------------------------------

TEST_CASE("MAX_ROWS is a scan-wide limit, not a per-worker one", "[erpl_rfc][partition]") {
	// Giving each worker its own limit would return workers x MAX_ROWS rows.
	RfcRowWindowScheduler sched(/*window_size=*/4096, /*batch_size=*/2048, /*max_rows=*/5000);

	idx_t off = 0, count = 0;
	REQUIRE(sched.Claim(off, count));
	REQUIRE(off == 0);
	REQUIRE(count == 4096);

	// Only 904 rows remain under the limit.
	REQUIRE(sched.Claim(off, count));
	REQUIRE(off == 4096);
	REQUIRE(count == 904);

	REQUIRE_FALSE(sched.Claim(off, count));
}

TEST_CASE("a MAX_ROWS below one window still yields exactly that many", "[erpl_rfc][partition]") {
	RfcRowWindowScheduler sched(/*window_size=*/4096, /*batch_size=*/2048, /*max_rows=*/10);
	idx_t off = 0, count = 0;
	REQUIRE(sched.Claim(off, count));
	REQUIRE(off == 0);
	REQUIRE(count == 10);
	REQUIRE_FALSE(sched.Claim(off, count));
}

TEST_CASE("the total handed out never exceeds MAX_ROWS", "[erpl_rfc][partition]") {
	RfcRowWindowScheduler sched(/*window_size=*/1024, /*batch_size=*/512, /*max_rows=*/3000);
	idx_t off = 0, count = 0, total = 0;
	while (sched.Claim(off, count)) {
		total += count;
	}
	REQUIRE(total == 3000);
}

// ---------------------------------------------------------------------------
// Concurrency: this is the property the whole design rests on.
// ---------------------------------------------------------------------------

TEST_CASE("concurrent claims never overlap and never skip a row", "[erpl_rfc][partition]") {
	constexpr idx_t kWindow = 1024;
	constexpr idx_t kMaxRows = 100 * kWindow;
	RfcRowWindowScheduler sched(kWindow, /*batch_size=*/512, kMaxRows);

	std::mutex m;
	std::vector<std::pair<idx_t, idx_t>> claims;
	std::vector<std::thread> threads;
	for (int t = 0; t < 8; t++) {
		threads.emplace_back([&]() {
			idx_t off = 0, count = 0;
			while (sched.Claim(off, count)) {
				std::lock_guard<std::mutex> g(m);
				claims.emplace_back(off, count);
			}
		});
	}
	for (auto &t : threads) {
		t.join();
	}

	// Every row in [0, kMaxRows) must be covered exactly once.
	std::sort(claims.begin(), claims.end());
	idx_t expected_next = 0;
	idx_t total = 0;
	for (auto &c : claims) {
		REQUIRE(c.first == expected_next);   // no gap, no overlap
		expected_next = c.first + c.second;
		total += c.second;
	}
	REQUIRE(total == kMaxRows);
}
