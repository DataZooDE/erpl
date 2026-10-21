#include "catch.hpp"
#include "test_helpers.hpp"
#include "duckdb.hpp"

#include <algorithm>

#include "duckdb/planner/filter/conjunction_filter.hpp"
#include "duckdb/planner/filter/constant_filter.hpp"
#include "duckdb/planner/filter/in_filter.hpp"
#include "duckdb/planner/filter/null_filter.hpp"
#include "duckdb/planner/filter/expression_filter.hpp"
#include "duckdb/planner/expression/bound_reference_expression.hpp"
#include "duckdb/planner/expression/bound_constant_expression.hpp"
#include "duckdb/planner/expression/bound_conjunction_expression.hpp"
#include "duckdb/planner/expression/bound_operator_expression.hpp"
#include "duckdb/planner/filter/optional_filter.hpp"
#include "duckdb/planner/filter/dynamic_filter.hpp"

#include "sap_rfc.hpp"
#include "duckdb_compat/filter.hpp"

using namespace duckdb;

namespace duckdb {
struct RfcReadTableFilterTestAccess {
	static void SetColumn(RfcReadTableBindData &bind, const string &name) {
		bind.column_names = {name};
		bind.projected_to_rfc_column = {0};
	}
	static void SetResidual(RfcReadTableBindData &bind, unique_ptr<TableFilter> filter) {
		bind.residual_filters.clear();
		bind.residual_filters.emplace_back(0, std::move(filter));
	}
};
} // namespace duckdb

// Filter pushdown into RFC_READ_TABLE's OPTIONS table.
//
// Everything that does NOT reach the server is evaluated by the scan after the
// whole column has been dragged across the wire.  On a large fact table that
// transfer dominates every other cost in the scan, so which predicates are
// pushable is the single biggest performance lever we have.
//
// Two hard rules constrain what may be generated here:
//
//  1. Pushdown is a pure optimisation. The scan evaluates every predicate it
//     cannot send to SAP, so emitting *nothing* is always safe. Emitting one
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

static unique_ptr<duckdb_compat::ConstantFilter> Cmp(ExpressionType type, Value v) {
	return make_uniq<duckdb_compat::ConstantFilter>(type, std::move(v));
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

TEST_CASE("expression filter comparison is pushed down", "[erpl_rfc][filters]") {
	auto varchar_type = LogicalType::VARCHAR;
	auto ref = make_uniq<BoundReferenceExpression>(varchar_type, 0);
	auto constant = make_uniq<BoundConstantExpression>(Value("LH"));
	auto comparison = duckdb_compat::MakeComparisonExpression(
	    ExpressionType::COMPARE_EQUAL, std::move(ref), std::move(constant));
	auto filter = make_uniq<ExpressionFilter>(std::move(comparison));
	REQUIRE(Push("CARRIER_ID", *filter) ==
	        (duckdb_compat::NEW_TABLE_FILTERS ? "CARRIER_ID = 'LH'" : ""));
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
	auto conj = make_uniq<duckdb_compat::ConjunctionAndFilter>();
	conj->child_filters.push_back(Cmp(ExpressionType::COMPARE_GREATERTHANOREQUALTO, Value("20240101")));
	conj->child_filters.push_back(Cmp(ExpressionType::COMPARE_LESSTHANOREQUALTO, Value("20241231")));

	REQUIRE(Push("BUDAT", *conj) == "( BUDAT >= '20240101' AND BUDAT <= '20241231' )");
}

TEST_CASE("OR conjunctions are pushed down with explicit parentheses", "[erpl_rfc][filters]") {
	auto conj = make_uniq<duckdb_compat::ConjunctionOrFilter>();
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
		auto conj = make_uniq<duckdb_compat::ConjunctionAndFilter>();
		conj->child_filters.push_back(Cmp(ExpressionType::COMPARE_EQUAL, Value("DE")));
		conj->child_filters.push_back(make_uniq<duckdb_compat::IsNullFilter>());
		REQUIRE(Push("LAND1", *conj) == "");
	}
	SECTION("OR") {
		auto conj = make_uniq<duckdb_compat::ConjunctionOrFilter>();
		conj->child_filters.push_back(Cmp(ExpressionType::COMPARE_EQUAL, Value("DE")));
		conj->child_filters.push_back(make_uniq<duckdb_compat::IsNullFilter>());
		REQUIRE(Push("LAND1", *conj) == "");
	}
}

TEST_CASE("nested conjunctions round-trip", "[erpl_rfc][filters]") {
	auto inner = make_uniq<duckdb_compat::ConjunctionOrFilter>();
	inner->child_filters.push_back(Cmp(ExpressionType::COMPARE_EQUAL, Value("DE")));
	inner->child_filters.push_back(Cmp(ExpressionType::COMPARE_EQUAL, Value("FR")));

	auto outer = make_uniq<duckdb_compat::ConjunctionAndFilter>();
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
	auto f = make_uniq<duckdb_compat::InFilter>(std::move(vals));
	REQUIRE(Push("LAND1", *f) == "LAND1 IN ( 'DE', 'FR', 'IT', 'ES', 'NL', 'BE' )");
}

TEST_CASE("an over-long IN list is dropped rather than truncated", "[erpl_rfc][filters]") {
	vector<Value> vals;
	for (idx_t i = 0; i < 2000; i++) {
		vals.push_back(Value(StringUtil::Format("VALUE%08d", i)));
	}
	auto f = make_uniq<duckdb_compat::InFilter>(std::move(vals));
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
	auto f = make_uniq<duckdb_compat::InFilter>(std::move(vals));
	REQUIRE(Push("NAME1", *f) == "NAME1 IN ( 'O''Brien', 'Smith' )");
}

// ---------------------------------------------------------------------------
// Deliberately not pushed
// ---------------------------------------------------------------------------

TEST_CASE("null checks are not pushed", "[erpl_rfc][filters]") {
	// ABAP has no NULL; the initial value is not equivalent in general.
	duckdb_compat::IsNullFilter is_null;
	duckdb_compat::IsNotNullFilter is_not_null;
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

// ---------------------------------------------------------------------------
// Literal rendering per type.
//
// The DDIC validates every literal against the field's own type, so DuckDB's
// spelling of a value is not automatically ABAP's.  Pushing a DATE as
// '2020-01-01' makes the server answer "is not a valid value for D(8,0)" and
// breaks a query that worked before only because it was never pushed.
// Verified live against the trial: quoted CHAR / integers / decimals are
// accepted, and DATS wants YYYYMMDD.
// ---------------------------------------------------------------------------

TEST_CASE("dates render in ABAP's DATS format, not ISO", "[erpl_rfc][filters]") {
	auto f = Cmp(ExpressionType::COMPARE_GREATERTHANOREQUALTO, Value::DATE(2020, 1, 1));
	REQUIRE(Push("FLIGHT_DATE", *f) == "FLIGHT_DATE >= '20200101'");
}

TEST_CASE("times render as HHMMSS", "[erpl_rfc][filters]") {
	auto f = Cmp(ExpressionType::COMPARE_LESSTHAN, Value::TIME(9, 5, 3, 0));
	REQUIRE(Push("DEPTIME", *f) == "DEPTIME < '090503'");
}

TEST_CASE("integers and decimals are pushed quoted", "[erpl_rfc][filters]") {
	SECTION("integer") {
		auto f = Cmp(ExpressionType::COMPARE_GREATERTHAN, Value::BIGINT(350));
		REQUIRE(Push("SEATS_MAX", *f) == "SEATS_MAX > '350'");
	}
	SECTION("decimal") {
		auto f = Cmp(ExpressionType::COMPARE_GREATERTHAN, Value::DECIMAL(50000, 15, 2));
		REQUIRE(Push("PRICE", *f) == "PRICE > '500.00'");
	}
}

TEST_CASE("types with no established ABAP spelling are not pushed", "[erpl_rfc][filters]") {
	SECTION("timestamp") {
		auto f = Cmp(ExpressionType::COMPARE_EQUAL, Value::TIMESTAMP(Timestamp::FromEpochSeconds(0)));
		REQUIRE(Push("CREATED_AT", *f) == "");
	}
	SECTION("blob") {
		// TransformBlob emits Postgres' ::BYTEA syntax, which ABAP would reject.
		auto f = Cmp(ExpressionType::COMPARE_EQUAL, Value::BLOB("\\xAB\\xCD"));
		REQUIRE(Push("RAWFIELD", *f) == "");
	}
	SECTION("double") {
		// A binary float round-tripped through a decimal literal can select a
		// different set of rows than DuckDB would.
		auto f = Cmp(ExpressionType::COMPARE_GREATERTHAN, Value::DOUBLE(1.5));
		REQUIRE(Push("SOMEFLOAT", *f) == "");
	}
	// No NULL section: DuckDB asserts "ConstantFilter constant cannot be NULL -
	// use IsNullFilter instead", so a NULL constant cannot reach TransformLiteral
	// through this path at all.  The IsNull() guard there is defence in depth.
}

TEST_CASE("an unpushable literal poisons its whole conjunction", "[erpl_rfc][filters]") {
	// Otherwise an AND would silently widen and an OR would silently narrow.
	auto conj = make_uniq<duckdb_compat::ConjunctionAndFilter>();
	conj->child_filters.push_back(Cmp(ExpressionType::COMPARE_EQUAL, Value("LH")));
	conj->child_filters.push_back(Cmp(ExpressionType::COMPARE_GREATERTHAN, Value::DOUBLE(1.5)));
	REQUIRE(Push("CARRIER_ID", *conj) == "");
}

TEST_CASE("times with a fractional second are not pushed", "[erpl_rfc][filters]") {
	// SAP TIMS is second-precision. Truncating the fraction changes the predicate:
	// `t < TIME '09:05:03.500'` must keep a row stored as 09:05:03, but the truncated
	// clause `t < '090503'` rejects it. Correct rounding would depend on the operator,
	// which TransformLiteral cannot see -- so it declines and the residual path applies
	// the predicate exactly.
	auto f = Cmp(ExpressionType::COMPARE_LESSTHAN, Value::TIME(9, 5, 3, 500000));
	REQUIRE(Push("DEPTIME", *f) == "");

	// A whole second still goes to the server.
	auto g = Cmp(ExpressionType::COMPARE_LESSTHAN, Value::TIME(9, 5, 3, 0));
	REQUIRE(Push("DEPTIME", *g) == "DEPTIME < '090503'");
}

// A future or legacy filter kind is deliberately opaque to this scan. An optional
// filter may be ignored; a required filter must fail before opening an RFC call.
class OpaqueFilter final : public TableFilter {
public:
	explicit OpaqueFilter(TableFilterType kind = static_cast<TableFilterType>(250)) : TableFilter(kind) {
	}
	FilterPropagateResult CheckStatistics(BaseStatistics &) const {
		return FilterPropagateResult::NO_PRUNING_POSSIBLE;
	}
	string ToString(const string &) const {
		return "opaque";
	}
	unique_ptr<TableFilter> Copy() const {
		return make_uniq<OpaqueFilter>(filter_type);
	}
	unique_ptr<Expression> ToExpression(const Expression &) const override {
		throw NotImplementedException("opaque test filter");
	}
};

static unique_ptr<ExpressionFilter> ExprCmp(ExpressionType type, Value constant, bool constant_first = false) {
	auto ref = make_uniq<BoundReferenceExpression>(constant.type(), 0);
	auto val = make_uniq<BoundConstantExpression>(std::move(constant));
	unique_ptr<Expression> expression;
	if (constant_first) {
		expression = duckdb_compat::MakeComparisonExpression(type, std::move(val), std::move(ref));
	} else {
		expression = duckdb_compat::MakeComparisonExpression(type, std::move(ref), std::move(val));
	}
	return make_uniq<ExpressionFilter>(std::move(expression));
}

static vector<Value> ApplyOneResidual(unique_ptr<TableFilter> filter, const vector<Value> &values) {
	DuckDB db(nullptr);
	Connection conn(db);
	auto &context = *conn.context;
	RfcReadTableBindData bind("T", 0, 0, ReadTableFunctionOptions {}, DefaultRfcConnectionFactory, context);
	RfcReadTableFilterTestAccess::SetResidual(bind, std::move(filter));
	DataChunk chunk;
	chunk.Initialize(context, {LogicalType::VARCHAR});
	for (idx_t i = 0; i < values.size(); i++) {
		chunk.SetValue(0, i, values[i]);
	}
	duckdb_compat::SetChildCardinality(chunk, values.size());
	bind.ApplyResidualFilters(context, chunk);
	vector<Value> result;
	for (idx_t i = 0; i < chunk.size(); i++) {
		result.push_back(chunk.GetValue(0, i));
	}
	return result;
}

TEST_CASE("round3 residual filters handle NULL and rejected batches", "[erpl_rfc][round3]") {
	REQUIRE(ApplyOneResidual(Cmp(ExpressionType::COMPARE_EQUAL, Value("LH")),
	                         {Value(), Value("LH"), Value("UA")}) == vector<Value>{Value("LH")});
	REQUIRE(ApplyOneResidual(Cmp(ExpressionType::COMPARE_EQUAL, Value("LH")),
	                         {Value("UA"), Value("BA")}).empty());
}

TEST_CASE("round3 residual filters handle nested expressions", "[erpl_rfc][round3]") {
	auto and_filter = make_uniq<duckdb_compat::ConjunctionAndFilter>();
	and_filter->child_filters.push_back(ExprCmp(ExpressionType::COMPARE_GREATERTHAN, Value("B")));
	and_filter->child_filters.push_back(make_uniq<duckdb_compat::IsNotNullFilter>());
	REQUIRE(ApplyOneResidual(std::move(and_filter), {Value(), Value("A"), Value("C")}) ==
	        vector<Value>{Value("C")});
	auto optional = make_uniq<duckdb_compat::OptionalFilter>(
	    ExprCmp(ExpressionType::COMPARE_GREATERTHAN, Value("B")));
	REQUIRE(ApplyOneResidual(std::move(optional), {Value("A"), Value("C")}) ==
	        vector<Value>{Value("C")});
}

TEST_CASE("round3 dynamic residual filters handle absent and present data", "[erpl_rfc][round3]") {
	REQUIRE(ApplyOneResidual(make_uniq<duckdb_compat::DynamicFilter>(),
	                         {Value("A"), Value("C")}) == vector<Value>{Value("A"), Value("C")});
	auto data = duckdb_compat::MakeDynamicFilterData(ExpressionType::COMPARE_GREATERTHAN,
	                                               Value("B"), true);
	REQUIRE(ApplyOneResidual(make_uniq<duckdb_compat::DynamicFilter>(data),
	                         {Value("A"), Value("C")}) == vector<Value>{Value("C")});
}

TEST_CASE("round3 optional unknown and bloom filters keep rows", "[erpl_rfc][round3]") {
	REQUIRE(ApplyOneResidual(make_uniq<duckdb_compat::OptionalFilter>(make_uniq<OpaqueFilter>()),
	                         {Value("A"), Value("C")}) == vector<Value>{Value("A"), Value("C")});
	if (duckdb_compat::FilterKind::BLOOM_FILTER) {
		auto bloom = make_uniq<OpaqueFilter>(*duckdb_compat::FilterKind::BLOOM_FILTER);
		REQUIRE(ApplyOneResidual(make_uniq<duckdb_compat::OptionalFilter>(std::move(bloom)),
		                         {Value("A"), Value("C")}) == vector<Value>{Value("A"), Value("C")});
	}
}

TEST_CASE("round3 required unknown filter fails during bind initialization", "[erpl_rfc][round3]") {
	DuckDB db(nullptr);
	Connection conn(db);
	auto &context = *conn.context;
	RfcReadTableBindData bind("T", 0, 0, ReadTableFunctionOptions {}, DefaultRfcConnectionFactory, context);
	RfcReadTableFilterTestAccess::SetColumn(bind, "CARRIER_ID");
	TableFilterSet set;
	duckdb_compat::SetFilter(set, 0, make_uniq<OpaqueFilter>());
	REQUIRE_THROWS_AS(bind.AddOptionsFromFilters(&set), NotImplementedException);
}

static unique_ptr<Expression> CmpExpr(ExpressionType type, Value constant, bool constant_first = false) {
	auto ref = make_uniq<BoundReferenceExpression>(constant.type(), 0);
	auto val = make_uniq<BoundConstantExpression>(std::move(constant));
	if (constant_first) return duckdb_compat::MakeComparisonExpression(type, std::move(val), std::move(ref));
	return duckdb_compat::MakeComparisonExpression(type, std::move(ref), std::move(val));
}

static unique_ptr<ExpressionFilter> ExprOp(ExpressionType type, vector<Value> constants) {
	auto bool_type = LogicalType::BOOLEAN;
	auto op = make_uniq<BoundOperatorExpression>(type, bool_type);
	auto &children = duckdb_compat::MutableExpressionChildren(*op);
	auto varchar_type = LogicalType::VARCHAR;
	children.push_back(make_uniq<BoundReferenceExpression>(varchar_type, 0));
	for (auto &val : constants) children.push_back(make_uniq<BoundConstantExpression>(std::move(val)));
	return make_uniq<ExpressionFilter>(std::move(op));
}

static string ExprOptions(const string &column, unique_ptr<TableFilter> filter) {
	DuckDB db(nullptr);
	Connection conn(db);
	auto &context = *conn.context;
	RfcReadTableBindData bind("T", 0, 0, ReadTableFunctionOptions {}, DefaultRfcConnectionFactory, context);
	RfcReadTableFilterTestAccess::SetColumn(bind, column);
	TableFilterSet set;
	duckdb_compat::SetFilter(set, 0, std::move(filter));
	bind.AddOptionsFromFilters(&set);
	return Rejoin(bind.options);
}

TEST_CASE("round3 ExpressionFilter OPTIONS match legacy comparisons and literals", "[erpl_rfc][round3]") {
	struct Case { string column; ExpressionType type; Value value; string clause; };
	vector<Case> cases = {
		{"LAND1", ExpressionType::COMPARE_EQUAL, Value("DE"), "LAND1 = 'DE'"},
		{"LAND1", ExpressionType::COMPARE_NOTEQUAL, Value("DE"), "LAND1 <> 'DE'"},
		{"BUDAT", ExpressionType::COMPARE_GREATERTHANOREQUALTO, Value("20240101"), "BUDAT >= '20240101'"},
		{"BUDAT", ExpressionType::COMPARE_LESSTHANOREQUALTO, Value("20241231"), "BUDAT <= '20241231'"},
		{"DMBTR", ExpressionType::COMPARE_GREATERTHAN, Value("100"), "DMBTR > '100'"},
		{"DMBTR", ExpressionType::COMPARE_LESSTHAN, Value("100"), "DMBTR < '100'"},
		{"CARRIER_ID", ExpressionType::COMPARE_EQUAL, Value("O'Brien"), "CARRIER_ID = 'O''Brien'"},
		{"FLIGHT_DATE", ExpressionType::COMPARE_GREATERTHANOREQUALTO, Value::DATE(2020, 1, 1), "FLIGHT_DATE >= '20200101'"},
		{"DEPTIME", ExpressionType::COMPARE_LESSTHAN, Value::TIME(9, 5, 3, 0), "DEPTIME < '090503'"},
		{"SEATS_MAX", ExpressionType::COMPARE_GREATERTHAN, Value::BIGINT(350), "SEATS_MAX > '350'"},
		{"PRICE", ExpressionType::COMPARE_GREATERTHAN, Value::DECIMAL(50000, 15, 2), "PRICE > '500.00'"}
	};
	for (auto &test : cases) {
		INFO(test.column);
		REQUIRE(ExprOptions(test.column, ExprCmp(test.type, test.value)) ==
		        (duckdb_compat::NEW_TABLE_FILTERS ? test.clause : ""));
	}
	REQUIRE(ExprOptions("CARRIER_ID", ExprCmp(ExpressionType::COMPARE_LESSTHAN, Value("M"), true)) ==
	        (duckdb_compat::NEW_TABLE_FILTERS ? "CARRIER_ID > 'M'" : ""));
}

TEST_CASE("round3 ExpressionFilter OPTIONS handle conjunction, IN, NOT and unpushable values", "[erpl_rfc][round3]") {
	auto and_expr = make_uniq<BoundConjunctionExpression>(ExpressionType::CONJUNCTION_AND,
	    CmpExpr(ExpressionType::COMPARE_GREATERTHAN, Value("A")),
	    CmpExpr(ExpressionType::COMPARE_LESSTHAN, Value("Z")));
	REQUIRE(ExprOptions("CARRIER_ID", make_uniq<ExpressionFilter>(std::move(and_expr))) ==
	        (duckdb_compat::NEW_TABLE_FILTERS ? "( CARRIER_ID > 'A' AND CARRIER_ID < 'Z' )" : ""));
	auto or_expr = make_uniq<BoundConjunctionExpression>(ExpressionType::CONJUNCTION_OR,
	    CmpExpr(ExpressionType::COMPARE_EQUAL, Value("LH")),
	    CmpExpr(ExpressionType::COMPARE_EQUAL, Value("UA")));
	REQUIRE(ExprOptions("CARRIER_ID", make_uniq<ExpressionFilter>(std::move(or_expr))) ==
	        (duckdb_compat::NEW_TABLE_FILTERS ? "( CARRIER_ID = 'LH' OR CARRIER_ID = 'UA' )" : ""));
	REQUIRE(ExprOptions("CARRIER_ID", ExprOp(ExpressionType::COMPARE_IN, {Value("LH"), Value("O'Brien")})) ==
	        (duckdb_compat::NEW_TABLE_FILTERS ? "( CARRIER_ID = 'LH' OR CARRIER_ID = 'O''Brien' )" : ""));
	REQUIRE(ExprOptions("CARRIER_ID", ExprOp(ExpressionType::COMPARE_IN,
	        {Value("AA"), Value("BA"), Value("CA"), Value("DA"), Value("EA"), Value("FA")})) ==
	        (duckdb_compat::NEW_TABLE_FILTERS ?
	         "( CARRIER_ID = 'AA' OR CARRIER_ID = 'BA' OR CARRIER_ID = 'CA' OR CARRIER_ID = 'DA' OR CARRIER_ID = 'EA' OR CARRIER_ID = 'FA' )" : ""));
	auto bool_type = LogicalType::BOOLEAN;
	auto not_expr = make_uniq<BoundOperatorExpression>(ExpressionType::OPERATOR_NOT, bool_type);
	duckdb_compat::MutableExpressionChildren(*not_expr).push_back(CmpExpr(ExpressionType::COMPARE_EQUAL, Value("LH")));
	REQUIRE(ExprOptions("CARRIER_ID", make_uniq<ExpressionFilter>(std::move(not_expr))) ==
	        (duckdb_compat::NEW_TABLE_FILTERS ? "CARRIER_ID <> 'LH'" : ""));
	REQUIRE(ExprOptions("DEPTIME", ExprCmp(ExpressionType::COMPARE_LESSTHAN, Value::TIME(9, 5, 3, 500000))) == "");
	REQUIRE(ExprOptions("SOMEFLOAT", ExprCmp(ExpressionType::COMPARE_GREATERTHAN, Value::DOUBLE(1.5))) == "");
	REQUIRE(ExprOptions("CREATED_AT", ExprCmp(ExpressionType::COMPARE_EQUAL,
	        Value::TIMESTAMP(Timestamp::FromEpochSeconds(0)))) == "");
	REQUIRE(ExprOptions("RAWFIELD", ExprCmp(ExpressionType::COMPARE_EQUAL, Value::BLOB("\\xAB\\xCD"))) == "");
	REQUIRE(ExprOptions("CARRIER_ID", ExprCmp(ExpressionType::COMPARE_EQUAL, Value())) == "");
	REQUIRE(ExprOptions("CARRIER_ID", ExprOp(ExpressionType::OPERATOR_IS_NULL, {})) == "");
	REQUIRE(ExprOptions("CARRIER_ID", ExprOp(ExpressionType::OPERATOR_IS_NOT_NULL, {})) == "");
	REQUIRE(ExprOptions("CARRIER_ID", ExprCmp(ExpressionType::COMPARE_EQUAL, Value(string(71, 'X')))) == "");
	REQUIRE(ExprOptions("CARRIER_ID", ExprOp(ExpressionType::COMPARE_IN, {Value("LH"), Value(string(4001, 'X'))})) == "");
	vector<Value> long_list(300, Value("X"));
	REQUIRE(ExprOptions("CARRIER_ID", ExprOp(ExpressionType::COMPARE_IN, std::move(long_list))) == "");
	auto nested = make_uniq<BoundConjunctionExpression>(ExpressionType::CONJUNCTION_AND,
	    CmpExpr(ExpressionType::COMPARE_EQUAL, Value("LH")),
	    make_uniq<BoundConjunctionExpression>(ExpressionType::CONJUNCTION_OR,
	        CmpExpr(ExpressionType::COMPARE_EQUAL, Value("UA")),
	        CmpExpr(ExpressionType::COMPARE_EQUAL, Value("BA"))));
	REQUIRE(ExprOptions("CARRIER_ID", make_uniq<ExpressionFilter>(std::move(nested))) ==
	        (duckdb_compat::NEW_TABLE_FILTERS ?
	         "( CARRIER_ID = 'LH' AND ( CARRIER_ID = 'UA' OR CARRIER_ID = 'BA' ) )" : ""));
	auto unpushable = make_uniq<BoundConjunctionExpression>(ExpressionType::CONJUNCTION_AND,
	    CmpExpr(ExpressionType::COMPARE_EQUAL, Value("LH")),
	    CmpExpr(ExpressionType::COMPARE_GREATERTHAN, Value::DOUBLE(1.5)));
	REQUIRE(ExprOptions("CARRIER_ID", make_uniq<ExpressionFilter>(std::move(unpushable))) == "");
}

TEST_CASE("round3 optional AND child keeps whole expression residual", "[erpl_rfc][round3]") {
	vector<int> children = {1, 2};
	auto folded = duckdb_compat::detail::FoldConjunction(
	    duckdb_compat::FilterExpressionNode::Kind::CONJUNCTION_AND, children,
	    [](int child) { return child == 2; },
	    [](int) -> std::optional<duckdb_compat::FilterExpressionNode> {
	        return duckdb_compat::FilterExpressionNode{duckdb_compat::FilterExpressionNode::Kind::COMPARISON,
	            ExpressionType::COMPARE_EQUAL, Value("LH"), {}};
	    });
	REQUIRE_FALSE(folded.has_value());

	// DuckDB 2.0 identifies this internal function by its name when it asks
	// whether an expression is optional. The scalar function is never executed:
	// AddOptionsFromFilters must keep the whole AND tree as a residual.
	auto varchar_type = LogicalType::VARCHAR;
	auto boolean_type = LogicalType::BOOLEAN;
	ScalarFunction optional_function(duckdb_compat::Ident("__internal_tablefilter_optional"),
	                                 {varchar_type}, boolean_type, nullptr);
	vector<unique_ptr<Expression>> arguments;
	arguments.push_back(make_uniq<BoundReferenceExpression>(varchar_type, 0));
	auto optional_child = duckdb_compat::MakeBoundScalarExpression(
	    boolean_type, std::move(optional_function), std::move(arguments));
	auto and_expr = make_uniq<BoundConjunctionExpression>(ExpressionType::CONJUNCTION_AND,
	    CmpExpr(ExpressionType::COMPARE_EQUAL, Value("LH")), std::move(optional_child));
	REQUIRE(ExprOptions("CARRIER_ID", make_uniq<ExpressionFilter>(std::move(and_expr))) == "");
}
