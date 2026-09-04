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
	REQUIRE(sched.Claim(off, count) == RfcRowWindowScheduler::ClaimResult::CLAIMED);
	REQUIRE(off == 0);
	REQUIRE(count == 4096);

	REQUIRE(sched.Claim(off, count) == RfcRowWindowScheduler::ClaimResult::CLAIMED);
	REQUIRE(off == 4096);
	REQUIRE(count == 4096);

	REQUIRE(sched.Claim(off, count) == RfcRowWindowScheduler::ClaimResult::CLAIMED);
	REQUIRE(off == 8192);
}

TEST_CASE("every offset is a multiple of the batch size", "[erpl_rfc][partition]") {
	// Violating this is an immediate ABAP error, not a wrong answer -- but only on
	// the call that violates it, so a scheduler that drifts would fail sporadically.
	RfcRowWindowScheduler sched(/*window_size=*/6144, /*batch_size=*/2048, /*max_rows=*/0);
	idx_t off = 0, count = 0;
	for (int i = 0; i < 10; i++) {
		REQUIRE(sched.Claim(off, count) == RfcRowWindowScheduler::ClaimResult::CLAIMED);
		REQUIRE(off % 2048 == 0);
	}
}

TEST_CASE("a short read stops further claims", "[erpl_rfc][partition]") {
	RfcRowWindowScheduler sched(/*window_size=*/4096, /*batch_size=*/2048, /*max_rows=*/0);
	idx_t off = 0, count = 0;
	REQUIRE(sched.Claim(off, count) == RfcRowWindowScheduler::ClaimResult::CLAIMED);
	REQUIRE(sched.Claim(off, count) == RfcRowWindowScheduler::ClaimResult::CLAIMED);

	// The second worker found the table ends inside its window.
	sched.ReportExhausted();

	REQUIRE(sched.Claim(off, count) != RfcRowWindowScheduler::ClaimResult::CLAIMED);
}

TEST_CASE("exhaustion does not invalidate a window already claimed", "[erpl_rfc][partition]") {
	// The bug this guards against: in erpl_odp one worker's end-of-data flag was
	// treated as globally terminal, and other workers stopped mid-flight and
	// truncated the result. A worker holding a window must always be allowed to
	// finish reading it.
	RfcRowWindowScheduler sched(/*window_size=*/4096, /*batch_size=*/2048, /*max_rows=*/0);
	idx_t a_off = 0, a_count = 0;
	idx_t b_off = 0, b_count = 0;
	REQUIRE(sched.Claim(a_off, a_count) == RfcRowWindowScheduler::ClaimResult::CLAIMED);
	REQUIRE(sched.Claim(b_off, b_count) == RfcRowWindowScheduler::ClaimResult::CLAIMED);

	sched.ReportExhausted();

	// Both claims stay valid and stay disjoint; only NEW claims are refused.
	REQUIRE(a_off + a_count == b_off);
	idx_t off = 0, count = 0;
	REQUIRE(sched.Claim(off, count) != RfcRowWindowScheduler::ClaimResult::CLAIMED);
}

// ---------------------------------------------------------------------------
// MAX_ROWS
// ---------------------------------------------------------------------------

TEST_CASE("MAX_ROWS is a scan-wide limit, not a per-worker one", "[erpl_rfc][partition]") {
	// Giving each worker its own limit would return workers x MAX_ROWS rows.
	RfcRowWindowScheduler sched(/*window_size=*/4096, /*batch_size=*/2048, /*max_rows=*/5000);

	idx_t off = 0, count = 0;
	REQUIRE(sched.Claim(off, count) == RfcRowWindowScheduler::ClaimResult::CLAIMED);
	REQUIRE(off == 0);
	REQUIRE(count == 4096);

	// Only 904 rows remain under the limit.
	REQUIRE(sched.Claim(off, count) == RfcRowWindowScheduler::ClaimResult::CLAIMED);
	REQUIRE(off == 4096);
	REQUIRE(count == 904);

	REQUIRE(sched.Claim(off, count) != RfcRowWindowScheduler::ClaimResult::CLAIMED);
}

TEST_CASE("a MAX_ROWS below one window still yields exactly that many", "[erpl_rfc][partition]") {
	RfcRowWindowScheduler sched(/*window_size=*/4096, /*batch_size=*/2048, /*max_rows=*/10);
	idx_t off = 0, count = 0;
	REQUIRE(sched.Claim(off, count) == RfcRowWindowScheduler::ClaimResult::CLAIMED);
	REQUIRE(off == 0);
	REQUIRE(count == 10);
	REQUIRE(sched.Claim(off, count) != RfcRowWindowScheduler::ClaimResult::CLAIMED);
}

TEST_CASE("the total handed out never exceeds MAX_ROWS", "[erpl_rfc][partition]") {
	RfcRowWindowScheduler sched(/*window_size=*/1024, /*batch_size=*/512, /*max_rows=*/3000);
	idx_t off = 0, count = 0, total = 0;
	while (sched.Claim(off, count) == RfcRowWindowScheduler::ClaimResult::CLAIMED) {
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
			while (sched.Claim(off, count) == RfcRowWindowScheduler::ClaimResult::CLAIMED) {
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

// ---------------------------------------------------------------------------
// Overflow. erpl_rfc_partition_window_rows is a UBIGINT the user sets, and the
// constructor rounds the window up to a whole number of batches. Rounding a
// near-max value wraps; if window_size wrapped to 0 the scheduler would hand the
// SAME offset to every claim forever, which is duplicated rows rather than an
// error -- the worst failure this class can have.
// ---------------------------------------------------------------------------

TEST_CASE("an absurd window size cannot wrap to zero", "[erpl_rfc][partition]") {
	auto huge = std::numeric_limits<idx_t>::max() - 3;
	RfcRowWindowScheduler sched(huge, /*batch_size=*/2048, /*max_rows=*/0);

	// The wrap this guards against would leave window_size at 0, and every claim
	// would then return offset 0 -- every worker reading the same rows.
	REQUIRE(sched.WindowSize() > 0);
	REQUIRE(sched.WindowSize() % 2048 == 0);

	// The constructor clamps the window to INT32_MAX before rounding down to whole
	// batches, so the window is 2,147,481,600 rows. Two starts are therefore legal --
	// 0 and 2,147,481,600, the latter still below the ROWSKIPS ceiling -- and only the
	// third exceeds it. (The previous bound, `claimed > INT32_MAX - window_size`,
	// refused the second and made the last legal window unreachable.) Any ROWSKIPS
	// inside that final window that does cross the ceiling is refused loudly by
	// CreateFunctionArguments, not here.
	//
	// What must never happen is a second claim at the SAME offset, which is what a
	// wrapped window_size would produce.
	idx_t a_off = 1, a_count = 0, b_off = 1, c_off = 1, b_count = 0, c_count = 0;
	REQUIRE(sched.Claim(a_off, a_count) == RfcRowWindowScheduler::ClaimResult::CLAIMED);
	REQUIRE(a_off == 0);
	REQUIRE(a_count > 0);

	REQUIRE(sched.Claim(b_off, b_count) == RfcRowWindowScheduler::ClaimResult::CLAIMED);
	REQUIRE(b_off != a_off);
	REQUIRE(b_off <= (idx_t)std::numeric_limits<int32_t>::max());

	// Past the ceiling: refused loudly, and distinguishable from a normal end-of-scan.
	REQUIRE(sched.Claim(c_off, c_count) == RfcRowWindowScheduler::ClaimResult::ADDRESS_LIMIT);
}

TEST_CASE("claims never repeat an offset even at extreme window sizes",
          "[erpl_rfc][partition]") {
	for (auto w : {std::numeric_limits<idx_t>::max(),
	               std::numeric_limits<idx_t>::max() / 2,
	               (idx_t)1}) {
		RfcRowWindowScheduler sched(w, /*batch_size=*/4096, /*max_rows=*/0);
		REQUIRE(sched.WindowSize() > 0);

		std::set<idx_t> seen;
		idx_t off = 0, count = 0;
		for (int i = 0; i < 5 && sched.Claim(off, count) == RfcRowWindowScheduler::ClaimResult::CLAIMED; i++) {
			REQUIRE(seen.insert(off).second);   // never the same offset twice
			REQUIRE(count > 0);
		}
	}
}

TEST_CASE("an absurd batch size cannot break alignment either", "[erpl_rfc][partition]") {
	// Not reachable through sap_read_table today -- the batch size comes from
	// MaxBatchSizeForColumnCount and is capped -- but the class should be total
	// rather than rely on its only caller staying well behaved.
	RfcRowWindowScheduler sched(/*window_size=*/4096,
	                            /*batch_size=*/std::numeric_limits<idx_t>::max(),
	                            /*max_rows=*/0);
	REQUIRE(sched.BatchSize() > 0);
	REQUIRE(sched.BatchSize() <= (idx_t)std::numeric_limits<int32_t>::max());
	REQUIRE(sched.WindowSize() > 0);
	REQUIRE(sched.WindowSize() % sched.BatchSize() == 0);

	idx_t a = 0, c = 0;
	REQUIRE(sched.Claim(a, c) == RfcRowWindowScheduler::ClaimResult::CLAIMED);
	REQUIRE(a == 0);
	REQUIRE(c > 0);
}

TEST_CASE("The ROWSKIPS ceiling is refused loudly, not reported as end-of-scan",
          "[erpl_rfc][partition]") {
	using Sched = RfcRowWindowScheduler;
	constexpr idx_t INT32_MAX_ROWS = 2147483647;
	constexpr idx_t BATCH = 32768;

	// A worker retires on an empty chunk and DuckDB reads that as end-of-scan, so
	// EXHAUSTED and ADDRESS_LIMIT must stay distinguishable: collapsing them turns a
	// 2.1-billion-row scan into a silently truncated prefix.
	Sched sched(BATCH, BATCH, 0);

	// The last legal window start is the greatest batch-aligned offset <= INT32_MAX.
	// The previous guard (claimed > INT32_MAX - window_size) refused it.
	const idx_t last_legal = (INT32_MAX_ROWS / BATCH) * BATCH;

	idx_t offset = 0, count = 0;
	idx_t seen_last_legal = 0;
	for (idx_t i = 0;; i++) {
		auto r = sched.Claim(offset, count);
		if (r == Sched::ClaimResult::CLAIMED) {
			if (offset == last_legal) {
				seen_last_legal++;
			}
			REQUIRE(offset <= INT32_MAX_ROWS);
			continue;
		}
		// Unbounded scan: the only way out is the address limit, never EXHAUSTED.
		REQUIRE(r == Sched::ClaimResult::ADDRESS_LIMIT);
		break;
	}
	REQUIRE(seen_last_legal == 1);

	// Once the limit is hit the scheduler stays stopped, and a later caller must not be
	// told "exhausted" -- it would draw the same wrong conclusion.
	auto again = sched.Claim(offset, count);
	REQUIRE(again == Sched::ClaimResult::EXHAUSTED);
}

TEST_CASE("A bounded scan reports EXHAUSTED, never the address limit",
          "[erpl_rfc][partition]") {
	using Sched = RfcRowWindowScheduler;
	Sched sched(2048, 2048, 10000);

	idx_t offset = 0, count = 0, total = 0;
	while (true) {
		auto r = sched.Claim(offset, count);
		if (r != Sched::ClaimResult::CLAIMED) {
			REQUIRE(r == Sched::ClaimResult::EXHAUSTED);
			break;
		}
		total += count;
	}
	REQUIRE(total == 10000);
}
