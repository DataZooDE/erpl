#include "catch.hpp"
#include "duckdb.hpp"

#include "sap_rfc.hpp"
#include "sap_rfc_api.hpp"
#include "sap_secret.hpp"
#include "sap_connection.hpp"
#include "erpl_rfc_extension.hpp"

using namespace duckdb;

namespace {

KeyValueSecret MakeTestSecret(const case_insensitive_map_t<string> &values) {
	vector<string> prefix_paths;
	KeyValueSecret secret(prefix_paths, "sap_rfc", "config", "test_secret");
	for (const auto &kv : values) {
		secret.secret_map[kv.first] = Value(kv.second);
	}
	return secret;
}

} // namespace

TEST_CASE("ValidateReadTableFunctionName accepts valid SAP function names", "[erpl_rfc][read_table_func]") {
	// Standard functions
	REQUIRE_NOTHROW(ValidateReadTableFunctionName("RFC_READ_TABLE"));
	REQUIRE_NOTHROW(ValidateReadTableFunctionName("/SAPDS/RFC_READ_TABLE"));
	REQUIRE_NOTHROW(ValidateReadTableFunctionName("/SAPDS/RFC_READ_TABLE2"));
	REQUIRE_NOTHROW(ValidateReadTableFunctionName("/BODS/RFC_READ_TABLE"));
	REQUIRE_NOTHROW(ValidateReadTableFunctionName("/BODS/RFC_READ_TABLE2"));

	// Custom Z/Y functions and namespaced custom functions
	REQUIRE_NOTHROW(ValidateReadTableFunctionName("Z_RFC_READ_TABLE"));
	REQUIRE_NOTHROW(ValidateReadTableFunctionName("ZCUSTOM_READ"));
	REQUIRE_NOTHROW(ValidateReadTableFunctionName("Y_READ_TABLE"));
	REQUIRE_NOTHROW(ValidateReadTableFunctionName("/MYCORP/Z_TABLE"));
	REQUIRE_NOTHROW(ValidateReadTableFunctionName("/A1234567/Z_TABLE")); // 8-char namespace
	REQUIRE_NOTHROW(ValidateReadTableFunctionName("A12345678901234567890123456789")); // Exactly 30 chars
}

TEST_CASE("ValidateReadTableFunctionName rejects invalid SAP function names", "[erpl_rfc][read_table_func]") {
	// Empty string
	REQUIRE_THROWS_AS(ValidateReadTableFunctionName(""), InvalidInputException);

	// Exceeds 30 characters (31 chars)
	REQUIRE_THROWS_AS(ValidateReadTableFunctionName("A123456789012345678901234567890"), InvalidInputException);

	// Illegal characters
	REQUIRE_THROWS_AS(ValidateReadTableFunctionName("bad-name!"), InvalidInputException);
	REQUIRE_THROWS_AS(ValidateReadTableFunctionName("RFC READ TABLE"), InvalidInputException);
	REQUIRE_THROWS_AS(ValidateReadTableFunctionName("RFC_READ_TABLE;DROP"), InvalidInputException);
	REQUIRE_THROWS_AS(ValidateReadTableFunctionName("RFC$READ"), InvalidInputException);

	// Illegal start character (must start with letter, or /namespace/ then letter)
	REQUIRE_THROWS_AS(ValidateReadTableFunctionName("123_FUNCTION"), InvalidInputException);
	REQUIRE_THROWS_AS(ValidateReadTableFunctionName("_FUNCTION"), InvalidInputException);

	// Malformed namespace
	REQUIRE_THROWS_AS(ValidateReadTableFunctionName("/TOO_LONG_NAMESPACE/FOO"), InvalidInputException);
	REQUIRE_THROWS_AS(ValidateReadTableFunctionName("//FOO"), InvalidInputException);
	REQUIRE_THROWS_AS(ValidateReadTableFunctionName("/MYCORP"), InvalidInputException);
	REQUIRE_THROWS_AS(ValidateReadTableFunctionName("FOO/BAR"), InvalidInputException);
}

TEST_CASE("read_table_function does not leak into RfcAuthParams or RfcOpenConnection", "[erpl_rfc][read_table_func]") {
	case_insensitive_map_t<string> values = {
		{"ashost", "sap.example.com"},
		{"sysnr", "00"},
		{"client", "100"},
		{"user", "RFCUSER"},
		{"passwd", "secret123"},
		{"read_table_function", "Z_MY_READ_TABLE"}
	};
	auto secret = MakeTestSecret(values);

	// ConvertSecretToAuthParams MUST NOT populate read_table_function in RfcAuthParams
	auto auth_params = ConvertSecretToAuthParams(secret);
	REQUIRE(auth_params.ashost == "sap.example.com");
	REQUIRE(auth_params.user == "RFCUSER");

	// BuildConnectionParams() handed to RfcOpenConnection MUST NEVER contain read_table_function
	auto conn_params = auth_params.BuildConnectionParams();
	for (const auto &p : conn_params) {
		REQUIRE(p.first != "read_table_function");
		REQUIRE(p.first != "READ_TABLE_FUNCTION");
		REQUIRE(p.second != "Z_MY_READ_TABLE");
	}
}

TEST_CASE("ResolveReadTableFunctionOptions follows 5-tier precedence hierarchy", "[erpl_rfc][read_table_func]") {
	DuckDB db(nullptr);
	db.LoadStaticExtension<ErplRfcExtension>();
	Connection conn(db);
	auto &context = *conn.context;

	// Level 5: Default when nothing is set
	{
		auto opts = ResolveReadTableFunctionOptions(context);
		REQUIRE(opts.function_name == "RFC_READ_TABLE");
		REQUIRE(opts.user_set == false);
		REQUIRE(opts.delimiter.empty());
	}

	// Level 4: Session setting erpl_rfc_read_table_function
	{
		conn.Query("SET erpl_rfc_read_table_function = 'Z_SESSION_FUNC'");
		auto opts = ResolveReadTableFunctionOptions(context);
		REQUIRE(opts.function_name == "Z_SESSION_FUNC");
		REQUIRE(opts.user_set == true);

		// Reset session setting to empty
		conn.Query("SET erpl_rfc_read_table_function = ''");
		opts = ResolveReadTableFunctionOptions(context);
		REQUIRE(opts.function_name == "RFC_READ_TABLE");
		REQUIRE(opts.user_set == false);
	}

	// Level 3: Secret option overrides session setting
	{
		conn.Query("CREATE SECRET sec_custom (TYPE sap_rfc, ashost 's4', user 'demo', read_table_function 'Z_SECRET_FUNC')");
		conn.Query("SET erpl_rfc_read_table_function = 'Z_SESSION_FUNC'");

		auto opts = ResolveReadTableFunctionOptions(context, nullptr, "sec_custom");
		REQUIRE(opts.function_name == "Z_SECRET_FUNC");
		REQUIRE(opts.user_set == true);

		conn.Query("DROP SECRET sec_custom");
		conn.Query("SET erpl_rfc_read_table_function = ''");
	}

	// Level 2: ATTACH option overrides secret and session setting
	{
		conn.Query("CREATE SECRET sec_custom (TYPE sap_rfc, ashost 's4', user 'demo', read_table_function 'Z_SECRET_FUNC')");
		conn.Query("SET erpl_rfc_read_table_function = 'Z_SESSION_FUNC'");

		auto opts = ResolveReadTableFunctionOptions(context, nullptr, "sec_custom", "Z_ATTACH_FUNC");
		REQUIRE(opts.function_name == "Z_ATTACH_FUNC");
		REQUIRE(opts.user_set == true);

		conn.Query("DROP SECRET sec_custom");
		conn.Query("SET erpl_rfc_read_table_function = ''");
	}

	// Level 1: Named parameter overrides ATTACH, secret, and session
	{
		conn.Query("CREATE SECRET sec_custom (TYPE sap_rfc, ashost 's4', user 'demo', read_table_function 'Z_SECRET_FUNC')");
		conn.Query("SET erpl_rfc_read_table_function = 'Z_SESSION_FUNC'");

		named_parameter_map_t named_params;
		named_params["READ_TABLE_FUNCTION"] = Value("z_named_func"); // lowercase should be normalized
		named_params["READ_TABLE_DELIMITER"] = Value("~");

		auto opts = ResolveReadTableFunctionOptions(context, &named_params, "sec_custom", "Z_ATTACH_FUNC");
		REQUIRE(opts.function_name == "Z_NAMED_FUNC");
		REQUIRE(opts.delimiter == "~");
		REQUIRE(opts.user_set == true);

		conn.Query("DROP SECRET sec_custom");
		conn.Query("SET erpl_rfc_read_table_function = ''");
	}

	// Explicit setting of RFC_READ_TABLE preserves user_set = true
	{
		conn.Query("SET erpl_rfc_read_table_function = 'RFC_READ_TABLE'");
		auto opts = ResolveReadTableFunctionOptions(context);
		REQUIRE(opts.function_name == "RFC_READ_TABLE");
		REQUIRE(opts.user_set == true);
		conn.Query("SET erpl_rfc_read_table_function = ''");
	}
}
