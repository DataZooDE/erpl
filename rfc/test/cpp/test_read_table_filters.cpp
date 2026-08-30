#include "catch.hpp"
#include "test_helpers.hpp"
#include "duckdb.hpp"

#include <algorithm>

#include "duckdb/planner/filter/conjunction_filter.hpp"
#include "duckdb/planner/filter/constant_filter.hpp"
#include "duckdb/planner/filter/in_filter.hpp"
#include "duckdb/planner/filter/null_filter.hpp"

#include "sap_rfc.hpp"

using namespace duckdb;

// Filter pushdown into RFC_READ_TABLE's OPTIONS table.
//
// Everything that does NOT reach the server is evaluated by DuckDB *after* the
// whole column has been dragged across the wire.  On a large fact table that
// transfer dominates every other cost in the scan, so which predicates are
// pushable is the single biggest performance lever we have.
//
// Two hard rules constrain what may be generated here:
//
//  1. Pushdown is a pure optimisation.  DuckDB re-evaluates every filter
//     regardless, so emitting *nothing* is always safe.  Emitting a predicate
//     that means something subtly different is not -- it silently drops rows.
//     When in doubt, return "".
//
//  2. The generated text is spliced into an ABAP dynamic WHERE clause and then
//     chopped into 70-character lines.  A literal that is not escaped, or a
//     line split mid-token, produces either a loud syntax error or a silently
//     different predicate.
//
// These tests need no SAP system, so the translation can be pinned in CI
// instead of only via sap_read_table.test against the trial.

static std::string Push(const std::string &column, TableFilter &filter) {
	auto col = column;   // TransformFilter takes a non-const reference
	return RfcReadTableBindData::TransformFilter(col, filter);
}

static unique_ptr<ConstantFilter> Cmp(ExpressionType type, Value v) {
	return make_uniq<ConstantFilter>(type, std::move(v));
}

// The chunks are split at -- not around -- the whitespace, so plain concatenation
// must reproduce the input exactly: no characters dropped or doubled at the seams.
static std::string Rejoin(const std::vector<std::string> &parts) {
	std::string out;
	for (auto &p : parts) {
		out += p;
	}
	return out;
}

// ---------------------------------------------------------------------------
// Comparison operators
// ---------------------------------------------------------------------------

TEST_CASE("equality is pushed down", "[erpl_rfc][filters]") {
	auto f = Cmp(ExpressionType::COMPARE_EQUAL, Value("DE"));
	REQUIRE(Push("LAND1", *f) == "LAND1 = 'DE'");
}

// TransformComparision already maps all six operators; only a guard in
// TransformFilter suppressed everything but '='.  Range predicates are exactly
// the shape a date-window extract uses, so this is the important one.
TEST_CASE("range comparisons are pushed down", "[erpl_rfc][filters]") {
	SECTION("greater or equal") {
		auto f = Cmp(ExpressionType::COMPARE_GREATERTHANOREQUALTO, Value("20240101"));
		REQUIRE(Push("BUDAT", *f) == "BUDAT >= '20240101'");
	}
	SECTION("less or equal") {
		auto f = Cmp(ExpressionType::COMPARE_LESSTHANOREQUALTO, Value("20241231"));
		REQUIRE(Push("BUDAT", *f) == "BUDAT <= '20241231'");
	}
	SECTION("strictly greater") {
		auto f = Cmp(ExpressionType::COMPARE_GREATERTHAN, Value("100"));
		REQUIRE(Push("DMBTR", *f) == "DMBTR > '100'");
	}
	SECTION("strictly less") {
		auto f = Cmp(ExpressionType::COMPARE_LESSTHAN, Value("100"));
		REQUIRE(Push("DMBTR", *f) == "DMBTR < '100'");
	}
	SECTION("not equal") {
		auto f = Cmp(ExpressionType::COMPARE_NOTEQUAL, Value("DE"));
		REQUIRE(Push("LAND1", *f) == "LAND1 <> 'DE'");
	}
}

// ---------------------------------------------------------------------------
// Conjunctions.  DuckDB expresses `BETWEEN a AND b` on one column as a
// CONJUNCTION_AND of two ConstantFilters, so without this a BETWEEN reaches
// the server as nothing at all.
// ---------------------------------------------------------------------------

TEST_CASE("AND conjunctions are pushed down with explicit parentheses", "[erpl_rfc][filters]") {
	auto conj = make_uniq<ConjunctionAndFilter>();
	conj->child_filters.push_back(Cmp(ExpressionType::COMPARE_GREATERTHANOREQUALTO, Value("20240101")));
	conj->child_filters.push_back(Cmp(ExpressionType::COMPARE_LESSTHANOREQUALTO, Value("20241231")));

	REQUIRE(Push("BUDAT", *conj) == "( BUDAT >= '20240101' AND BUDAT <= '20241231' )");
}

TEST_CASE("OR conjunctions are pushed down with explicit parentheses", "[erpl_rfc][filters]") {
	auto conj = make_uniq<ConjunctionOrFilter>();
	conj->child_filters.push_back(Cmp(ExpressionType::COMPARE_EQUAL, Value("DE")));
	conj->child_filters.push_back(Cmp(ExpressionType::COMPARE_EQUAL, Value("FR")));

	REQUIRE(Push("LAND1", *conj) == "( LAND1 = 'DE' OR LAND1 = 'FR' )");
}

// A conjunction is only safe to push if EVERY child is representable.  Dropping
// one child of an AND widens the predicate (more rows -- DuckDB filters them
// out again, so merely wasteful); dropping one child of an OR *narrows* it and
// would lose rows.  Neither may be emitted partially.
TEST_CASE("a conjunction with an unrepresentable child is not pushed", "[erpl_rfc][filters]") {
	SECTION("AND") {
		auto conj = make_uniq<ConjunctionAndFilter>();
		conj->child_filters.push_back(Cmp(ExpressionType::COMPARE_EQUAL, Value("DE")));
		conj->child_filters.push_back(make_uniq<IsNullFilter>());
		REQUIRE(Push("LAND1", *conj) == "");
	}
	SECTION("OR") {
		auto conj = make_uniq<ConjunctionOrFilter>();
		conj->child_filters.push_back(Cmp(ExpressionType::COMPARE_EQUAL, Value("DE")));
		conj->child_filters.push_back(make_uniq<IsNullFilter>());
		REQUIRE(Push("LAND1", *conj) == "");
	}
}

TEST_CASE("nested conjunctions round-trip", "[erpl_rfc][filters]") {
	auto inner = make_uniq<ConjunctionOrFilter>();
	inner->child_filters.push_back(Cmp(ExpressionType::COMPARE_EQUAL, Value("DE")));
	inner->child_filters.push_back(Cmp(ExpressionType::COMPARE_EQUAL, Value("FR")));

	auto outer = make_uniq<ConjunctionAndFilter>();
	outer->child_filters.push_back(std::move(inner));
	outer->child_filters.push_back(Cmp(ExpressionType::COMPARE_NOTEQUAL, Value("XX")));

	REQUIRE(Push("LAND1", *outer) == "( ( LAND1 = 'DE' OR LAND1 = 'FR' ) AND LAND1 <> 'XX' )");
}

// ---------------------------------------------------------------------------
// IN lists.  The old cap was an arbitrary 5 values; the real constraint is the
// length of the generated clause, so the cap is now a length budget.
// ---------------------------------------------------------------------------

TEST_CASE("IN lists beyond five values are still pushed", "[erpl_rfc][filters]") {
	vector<Value> vals;
	for (auto &v : {"DE", "FR", "IT", "ES", "NL", "BE"}) {
		vals.push_back(Value(v));
	}
	auto f = make_uniq<InFilter>(std::move(vals));
	REQUIRE(Push("LAND1", *f) == "LAND1 IN ( 'DE', 'FR', 'IT', 'ES', 'NL', 'BE' )");
}

TEST_CASE("an over-long IN list is dropped rather than truncated", "[erpl_rfc][filters]") {
	vector<Value> vals;
	for (idx_t i = 0; i < 2000; i++) {
		vals.push_back(Value(StringUtil::Format("VALUE%08d", i)));
	}
	auto f = make_uniq<InFilter>(std::move(vals));
	// Truncating would silently drop rows; the only safe answer is to push nothing.
	REQUIRE(Push("LAND1", *f) == "");
}

// ---------------------------------------------------------------------------
// Literal escaping.  ABAP quotes literals with '' -- an unescaped apostrophe
// terminates the literal early and changes the predicate's meaning.
// ---------------------------------------------------------------------------

TEST_CASE("single quotes in literals are escaped", "[erpl_rfc][filters]") {
	auto f = Cmp(ExpressionType::COMPARE_EQUAL, Value("O'Brien"));
	REQUIRE(Push("NAME1", *f) == "NAME1 = 'O''Brien'");
}

TEST_CASE("single quotes inside IN lists are escaped", "[erpl_rfc][filters]") {
	vector<Value> vals;
	vals.push_back(Value("O'Brien"));
	vals.push_back(Value("Smith"));
	auto f = make_uniq<InFilter>(std::move(vals));
	REQUIRE(Push("NAME1", *f) == "NAME1 IN ( 'O''Brien', 'Smith' )");
}

// ---------------------------------------------------------------------------
// Deliberately not pushed
// ---------------------------------------------------------------------------

TEST_CASE("null checks are not pushed", "[erpl_rfc][filters]") {
	// ABAP has no NULL; the initial value is not equivalent in general.
	IsNullFilter is_null;
	IsNotNullFilter is_not_null;
	REQUIRE(Push("LAND1", is_null) == "");
	REQUIRE(Push("LAND1", is_not_null) == "");
}

// ---------------------------------------------------------------------------
// The 70-character OPTIONS chunker
// ---------------------------------------------------------------------------

TEST_CASE("where clauses are chunked without splitting tokens", "[erpl_rfc][filters]") {
	std::string clause = "BUDAT >= '20240101' AND BUDAT <= '20241231' AND LAND1 = 'DE' "
	                     "AND BUKRS = '1000' AND GJAHR = '2024'";
	auto parts = RfcReadTableBindData::ChunkWhereClause(clause, RfcReadTableBindData::MAX_OPTION_LEN);

	REQUIRE(parts.size() > 1);
	for (auto &p : parts) {
		REQUIRE(p.size() <= RfcReadTableBindData::MAX_OPTION_LEN);
	}
	// Reassembling must reproduce the clause exactly -- no dropped or doubled
	// characters at the seams.
	REQUIRE(Rejoin(parts) == clause);
}

TEST_CASE("a quoted literal containing spaces is never split", "[erpl_rfc][filters]") {
	// This is the case the old whitespace-only rule got wrong.  The literal
	// straddles the 70-character mark AND contains spaces, so walking back to the
	// nearest whitespace lands *inside* it -- leaving one apostrophe on each line
	// and an ABAP syntax error.
	std::string clause = "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA = 'John Smith Junior The Third'";
	REQUIRE(clause.size() > RfcReadTableBindData::MAX_OPTION_LEN);

	auto parts = RfcReadTableBindData::ChunkWhereClause(clause, RfcReadTableBindData::MAX_OPTION_LEN);

	for (auto &p : parts) {
		REQUIRE(p.size() <= RfcReadTableBindData::MAX_OPTION_LEN);
		// An odd number of apostrophes means a literal was cut in half.
		auto quotes = std::count(p.begin(), p.end(), '\'');
		REQUIRE(quotes % 2 == 0);
	}
	REQUIRE(Rejoin(parts) == clause);
}
