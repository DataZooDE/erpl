#include "catch.hpp"
#include "duckdb.hpp"

#include "sap_rfc.hpp"
#include "sap_rfc_api.hpp"
#include "sap_secret.hpp"
#include "sap_connection.hpp"
#include "erpl_rfc_extension.hpp"
#include "duckdb/common/enums/on_create_conflict.hpp"

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

TEST_CASE("NormalizeAndValidateReadTableFunctionName normalizes and validates", "[erpl_rfc][read_table_func]") {
	REQUIRE(NormalizeAndValidateReadTableFunctionName("") == "");
	REQUIRE(NormalizeAndValidateReadTableFunctionName("   ") == "");
	REQUIRE(NormalizeAndValidateReadTableFunctionName("  rfc_read_table  ") == "RFC_READ_TABLE");
	REQUIRE(NormalizeAndValidateReadTableFunctionName("/sapds/rfc_read_table2") == "/SAPDS/RFC_READ_TABLE2");
	REQUIRE_THROWS_AS(NormalizeAndValidateReadTableFunctionName("bad name!"), InvalidInputException);
}

TEST_CASE("ValidateReadTableDelimiter validates delimiter length", "[erpl_rfc][read_table_func]") {
	REQUIRE_NOTHROW(ValidateReadTableDelimiter(""));
	REQUIRE_NOTHROW(ValidateReadTableDelimiter("~"));
	REQUIRE_NOTHROW(ValidateReadTableDelimiter("|"));
	REQUIRE_THROWS_AS(ValidateReadTableDelimiter("~~"), InvalidInputException);
	REQUIRE_THROWS_AS(ValidateReadTableDelimiter("DELIM"), InvalidInputException);
}

TEST_CASE("CREATE SECRET validates read_table_function", "[erpl_rfc][read_table_func]") {
	DuckDB db(nullptr);
	db.LoadStaticExtension<ErplRfcExtension>();
	Connection conn(db);
	auto res = conn.Query("CREATE SECRET sec_invalid (TYPE sap_rfc, ashost 's4', user 'demo', read_table_function 'bad-name!')");
	REQUIRE(res->HasError());
	REQUIRE(res->GetError().find("Invalid READ_TABLE_FUNCTION name") != string::npos);

	auto res_ok = conn.Query("CREATE SECRET sec_valid (TYPE sap_rfc, ashost 's4', user 'demo', read_table_function ' /sapds/rfc_read_table2 ')");
	REQUIRE(!res_ok->HasError());
}

TEST_CASE("ResolveReadTableFunctionOptions follows 5-tier precedence hierarchy", "[erpl_rfc][read_table_func]") {
	DuckDB db(nullptr);
	db.LoadStaticExtension<ErplRfcExtension>();
	Connection conn(db);
	auto &context = *conn.context;

	auto run_query = [&](const string &q) {
		auto res = conn.Query(q);
		REQUIRE(!res->HasError());
		return res;
	};

	// Level 5: Default when nothing is set
	{
		auto opts = ResolveReadTableFunctionOptions(context);
		REQUIRE(opts.function_name == "RFC_READ_TABLE");
		REQUIRE(opts.delimiter.empty());
	}

	// Level 4: Session setting erpl_rfc_read_table_function
	{
		run_query("SET erpl_rfc_read_table_function = 'Z_SESSION_FUNC'");
		auto opts = ResolveReadTableFunctionOptions(context);
		REQUIRE(opts.function_name == "Z_SESSION_FUNC");

		// Reset session setting to empty
		run_query("SET erpl_rfc_read_table_function = ''");
		opts = ResolveReadTableFunctionOptions(context);
		REQUIRE(opts.function_name == "RFC_READ_TABLE");
	}

	// Level 3: Secret option overrides session setting
	{
		run_query("CREATE SECRET sec_custom (TYPE sap_rfc, ashost 's4', user 'demo', read_table_function 'Z_SECRET_FUNC')");
		run_query("SET erpl_rfc_read_table_function = 'Z_SESSION_FUNC'");

		auto opts = ResolveReadTableFunctionOptions(context, nullptr, "sec_custom");
		REQUIRE(opts.function_name == "Z_SECRET_FUNC");

		run_query("DROP SECRET sec_custom");
		run_query("SET erpl_rfc_read_table_function = ''");
	}

	// Level 2: ATTACH option overrides secret and session setting
	{
		run_query("CREATE SECRET sec_custom (TYPE sap_rfc, ashost 's4', user 'demo', read_table_function 'Z_SECRET_FUNC')");
		run_query("SET erpl_rfc_read_table_function = 'Z_SESSION_FUNC'");

		auto opts = ResolveReadTableFunctionOptions(context, nullptr, "sec_custom", "Z_ATTACH_FUNC");
		REQUIRE(opts.function_name == "Z_ATTACH_FUNC");

		run_query("DROP SECRET sec_custom");
		run_query("SET erpl_rfc_read_table_function = ''");
	}

	// Level 1: Named parameter overrides ATTACH, secret, and session
	{
		run_query("CREATE SECRET sec_custom (TYPE sap_rfc, ashost 's4', user 'demo', read_table_function 'Z_SECRET_FUNC')");
		run_query("SET erpl_rfc_read_table_function = 'Z_SESSION_FUNC'");

		named_parameter_map_t named_params;
		named_params["READ_TABLE_FUNCTION"] = Value("z_named_func"); // lowercase should be normalized
		named_params["READ_TABLE_DELIMITER"] = Value("~");

		auto opts = ResolveReadTableFunctionOptions(context, &named_params, "sec_custom", "Z_ATTACH_FUNC");
		REQUIRE(opts.function_name == "Z_NAMED_FUNC");
		REQUIRE(opts.delimiter == "~");

		run_query("DROP SECRET sec_custom");
		run_query("SET erpl_rfc_read_table_function = ''");
	}

	// Delimiter precedence hierarchy across all tiers (atomic resolution)
	{
		// Tier 5: Default empty
		auto opts5 = ResolveReadTableFunctionOptions(context);
		REQUIRE(opts5.delimiter.empty());

		// Tier 4: Session setting
		run_query("SET erpl_rfc_read_table_delimiter = ';'");
		auto opts4 = ResolveReadTableFunctionOptions(context);
		REQUIRE(opts4.delimiter == ";");

		// Tier 3: Secret option overrides session
		run_query("CREATE SECRET sec_delim (TYPE sap_rfc, ashost 's4', user 'demo', read_table_delimiter '|')");
		auto opts3 = ResolveReadTableFunctionOptions(context, nullptr, "sec_delim");
		REQUIRE(opts3.delimiter == "|");

		// Tier 2: ATTACH override delimiter overrides secret & session
		auto opts2 = ResolveReadTableFunctionOptions(context, nullptr, "sec_delim", "", "^");
		REQUIRE(opts2.delimiter == "^");

		// Tier 1: Query parameter overrides ATTACH, secret & session
		named_parameter_map_t named_params;
		named_params["READ_TABLE_DELIMITER"] = Value("~");
		auto opts1 = ResolveReadTableFunctionOptions(context, &named_params, "sec_delim", "", "^");
		REQUIRE(opts1.delimiter == "~");
		REQUIRE(opts1.function_name == "RFC_READ_TABLE");

		run_query("DROP SECRET sec_delim");
		run_query("SET erpl_rfc_read_table_delimiter = ''");
	}

	// Secret resolution throws on missing named secret
	{
		REQUIRE_THROWS_AS(ResolveReadTableFunctionOptions(context, nullptr, "nonexistent_secret"), InvalidInputException);
	}

	// Explicit setting of RFC_READ_TABLE pins the reader: allow_fallback is false (F4)
	{
		run_query("SET erpl_rfc_read_table_function = 'RFC_READ_TABLE'");
		auto opts = ResolveReadTableFunctionOptions(context);
		REQUIRE(opts.function_name == "RFC_READ_TABLE");
		REQUIRE(opts.AllowsFallback() == false);
		run_query("SET erpl_rfc_read_table_function = ''");
	}
}

TEST_CASE("ValidateReadTableDelimiter validates printable ASCII characters", "[erpl_rfc][read_table_func]") {
	REQUIRE_NOTHROW(ValidateReadTableDelimiter(""));
	REQUIRE_NOTHROW(ValidateReadTableDelimiter("~"));
	REQUIRE_NOTHROW(ValidateReadTableDelimiter("|"));
	REQUIRE_NOTHROW(ValidateReadTableDelimiter(";"));
	REQUIRE_NOTHROW(ValidateReadTableDelimiter(","));

	REQUIRE_THROWS_AS(ValidateReadTableDelimiter("~~"), InvalidInputException);
	REQUIRE_THROWS_AS(ValidateReadTableDelimiter("DELIM"), InvalidInputException);
	// Whitespace characters (rejected)
	REQUIRE_THROWS_AS(ValidateReadTableDelimiter(" "), InvalidInputException);
	REQUIRE_THROWS_AS(ValidateReadTableDelimiter("\t"), InvalidInputException);
	// Non-printable control characters
	REQUIRE_THROWS_AS(ValidateReadTableDelimiter("\n"), InvalidInputException);
	REQUIRE_THROWS_AS(ValidateReadTableDelimiter("\x01"), InvalidInputException);
	// High-byte characters (> 0x7E)
	REQUIRE_THROWS_AS(ValidateReadTableDelimiter("\xE9"), InvalidInputException);
	REQUIRE_THROWS_AS(ValidateReadTableDelimiter("\xFF"), InvalidInputException);
}

TEST_CASE("CREATE SECRET validates read_table_delimiter", "[erpl_rfc][read_table_func]") {
	DuckDB db(nullptr);
	db.LoadStaticExtension<ErplRfcExtension>();
	Connection conn(db);
	auto res_bad = conn.Query("CREATE SECRET sec_bad_del (TYPE sap_rfc, ashost 's4', user 'demo', read_table_delimiter '~~')");
	REQUIRE(res_bad->HasError());
	REQUIRE(res_bad->GetError().find("READ_TABLE_DELIMITER must be a single printable non-whitespace ASCII character") != string::npos);

	auto res_space = conn.Query("CREATE SECRET sec_bad_spc (TYPE sap_rfc, ashost 's4', user 'demo', read_table_delimiter ' ')");
	REQUIRE(res_space->HasError());
	REQUIRE(res_space->GetError().find("READ_TABLE_DELIMITER must be a single printable non-whitespace ASCII character") != string::npos);

	auto res_ok = conn.Query("CREATE SECRET sec_ok_del (TYPE sap_rfc, ashost 's4', user 'demo', read_table_delimiter '~')");
	REQUIRE(!res_ok->HasError());
}

TEST_CASE("ResolveReadTableFunctionOptions coupling, clearing, and fallback rules", "[erpl_rfc][read_table_func]") {
	DuckDB db(nullptr);
	db.LoadStaticExtension<ErplRfcExtension>();
	Connection conn(db);
	auto &context = *conn.context;

	auto run_query = [&](const string &q) {
		auto res = conn.Query(q);
		REQUIRE(!res->HasError());
		return res;
	};

	// 1. Explicit empty delimiter at query level clears lower-tier secret delimiter
	{
		run_query("CREATE SECRET sec_del_override (TYPE sap_rfc, ashost 's4', user 'demo', read_table_delimiter '~')");
		named_parameter_map_t empty_del_params;
		empty_del_params["READ_TABLE_DELIMITER"] = Value("");
		auto opts = ResolveReadTableFunctionOptions(context, &empty_del_params, "sec_del_override");
		REQUIRE(opts.delimiter.empty());
		run_query("DROP SECRET sec_del_override");
	}

	// 2. Independent precedence: function and delimiter are each resolved by highest tier independently.
	{
		run_query("CREATE SECRET sec_bods (TYPE sap_rfc, ashost 's4', user 'demo', read_table_function '/BODS/RFC_READ_TABLE', read_table_delimiter '~')");
		
		// Query specifies function only -> delimiter is inherited from secret ('~') independently.
		// Explicit RFC_READ_TABLE query override pins the reader (AllowsFallback is false).
		named_parameter_map_t fn_override_params;
		fn_override_params["READ_TABLE_FUNCTION"] = Value("RFC_READ_TABLE");
		auto opts = ResolveReadTableFunctionOptions(context, &fn_override_params, "sec_bods");
		REQUIRE(opts.function_name == "RFC_READ_TABLE");
		REQUIRE(opts.delimiter == "~");
		REQUIRE(opts.AllowsFallback() == false);

		// Query specifies delimiter only -> function is inherited from secret ('/BODS/RFC_READ_TABLE'), delimiter is overridden by query ('^')
		named_parameter_map_t del_override_params;
		del_override_params["READ_TABLE_DELIMITER"] = Value("^");
		auto opts_del = ResolveReadTableFunctionOptions(context, &del_override_params, "sec_bods");
		REQUIRE(opts_del.function_name == "/BODS/RFC_READ_TABLE");
		REQUIRE(opts_del.delimiter == "^");
		REQUIRE(opts_del.AllowsFallback() == false);

		run_query("DROP SECRET sec_bods");
	}

	// 3. AllowsFallback rules: true for default RFC_READ_TABLE, false for pinned or custom (F4)
	{
		// Default
		auto opts_def = ResolveReadTableFunctionOptions(context);
		REQUIRE(opts_def.AllowsFallback() == true);

		// Explicit RFC_READ_TABLE via session pins the reader (AllowsFallback is false)
		run_query("SET erpl_rfc_read_table_function = 'RFC_READ_TABLE'");
		auto opts_rfc = ResolveReadTableFunctionOptions(context);
		REQUIRE(opts_rfc.AllowsFallback() == false);
		run_query("SET erpl_rfc_read_table_function = ''");

		// Custom function via session
		run_query("SET erpl_rfc_read_table_function = 'Z_CUSTOM'");
		auto opts_custom = ResolveReadTableFunctionOptions(context);
		REQUIRE(opts_custom.AllowsFallback() == false);
		run_query("SET erpl_rfc_read_table_function = ''");
	}
}

TEST_CASE("ValidateReadTableDelimiter reports specific whitespace character names", "[erpl_rfc][read_table_func]") {
	try {
		ValidateReadTableDelimiter(" ");
		FAIL("Expected exception for space");
	} catch (const InvalidInputException &ex) {
		REQUIRE(string(ex.what()).find("(space, U+0020)") != string::npos);
	}

	try {
		ValidateReadTableDelimiter("\t");
		FAIL("Expected exception for tab");
	} catch (const InvalidInputException &ex) {
		REQUIRE(string(ex.what()).find("(tab, U+0009)") != string::npos);
	}

	try {
		ValidateReadTableDelimiter("\n");
		FAIL("Expected exception for newline");
	} catch (const InvalidInputException &ex) {
		REQUIRE(string(ex.what()).find("(line feed, U+000A)") != string::npos);
	}

	try {
		ValidateReadTableDelimiter("\r");
		FAIL("Expected exception for CR");
	} catch (const InvalidInputException &ex) {
		REQUIRE(string(ex.what()).find("(carriage return, U+000D)") != string::npos);
	}
}

TEST_CASE("ReadTableFunctionDescriptor contract validation and helper methods", "[erpl_rfc][read_table_func]") {
	ReadTableFunctionDescriptor desc;
	desc.function_name = "Z_MY_READER";

	// Valid descriptor
	desc.contract_error.clear();
	REQUIRE(desc.IsValidContract());
	REQUIRE_NOTHROW(desc.ValidateContract());

	// Invalid descriptor throws and mentions sap_rfc_describe_function
	desc.contract_error = "missing required parameter 'QUERY_TABLE'";
	REQUIRE(!desc.IsValidContract());
	try {
		desc.ValidateContract();
		FAIL("Expected InvalidInputException");
	} catch (const InvalidInputException &ex) {
		REQUIRE(string(ex.what()).find("missing required parameter 'QUERY_TABLE'") != string::npos);
		REQUIRE(string(ex.what()).find("SELECT * FROM sap_rfc_describe_function('Z_MY_READER')") != string::npos);
	}
}

TEST_CASE("Fallback candidate filtering requires valid contract and supports_et_data", "[erpl_rfc][read_table_func]") {
	// A candidate lacking ET_DATA or with invalid contract must not qualify as a string fallback
	ReadTableFunctionDescriptor valid_et_data;
	valid_et_data.function_name = "/SAPDS/RFC_READ_TABLE2";
	valid_et_data.supports_et_data = true;
	valid_et_data.contract_error.clear();
	REQUIRE(valid_et_data.IsValidContract());
	REQUIRE(valid_et_data.supports_et_data);

	ReadTableFunctionDescriptor no_et_data;
	no_et_data.function_name = "/SAPDS/RFC_READ_TABLE";
	no_et_data.supports_et_data = false;
	no_et_data.contract_error.clear();
	REQUIRE(no_et_data.IsValidContract());
	REQUIRE(!no_et_data.supports_et_data);
	// Condition in TrySelectFallbackReadTableFunction:
	// if (!desc.IsValidContract() || !desc.supports_et_data) continue;
	REQUIRE((!no_et_data.IsValidContract() || !no_et_data.supports_et_data) == true);
	REQUIRE((!valid_et_data.IsValidContract() || !valid_et_data.supports_et_data) == false);
}

TEST_CASE("PlanReadCall call planning rules", "[erpl_rfc][read_table_func]") {
	ReadTableFunctionDescriptor desc_standard;
	desc_standard.function_name = "RFC_READ_TABLE";
	desc_standard.result_path = "/DATA";
	desc_standard.supports_et_data = true;
	desc_standard.supports_et_data_switch = true;

	ReadTableFunctionDescriptor desc_bods;
	desc_bods.function_name = "/BODS/RFC_READ_TABLE";
	desc_bods.result_path = "/ET_DATA";
	desc_bods.supports_et_data = true;

	ReadTableFunctionDescriptor desc_legacy;
	desc_legacy.function_name = "Z_LEGACY_READER";
	desc_legacy.result_path = "/DATA";
	desc_legacy.supports_et_data = false;

	auto str_type = RfcType::FromTypeName("STRING", 0, 0);
	auto char_type = RfcType::FromTypeName("CHAR", 10, 0);

	// 1. Non-string column with /DATA result path
	{
		auto plan = PlanReadCall(desc_standard, char_type, "");
		REQUIRE(plan.function_name == "RFC_READ_TABLE");
		REQUIRE(plan.data_path == "/DATA");
		REQUIRE(plan.use_et_data == false);
		REQUIRE(plan.delimiter.empty());
	}

	// 2. Non-string column with custom delimiter
	{
		auto plan = PlanReadCall(desc_standard, char_type, "|");
		REQUIRE(plan.function_name == "RFC_READ_TABLE");
		REQUIRE(plan.data_path == "/DATA");
		REQUIRE(plan.use_et_data == false);
		REQUIRE(plan.delimiter == "|");
	}

	// 3. String column with ET_DATA support defaults delimiter to kDefaultReadTableDelimiter ("~")
	{
		auto plan = PlanReadCall(desc_standard, str_type, "");
		REQUIRE(plan.function_name == "RFC_READ_TABLE");
		REQUIRE(plan.data_path == "/ET_DATA");
		REQUIRE(plan.use_et_data == true);
		REQUIRE(plan.delimiter == kDefaultReadTableDelimiter);
	}

	// 4. String column with ET_DATA support respects configured delimiter
	{
		auto plan = PlanReadCall(desc_standard, str_type, "^");
		REQUIRE(plan.function_name == "RFC_READ_TABLE");
		REQUIRE(plan.data_path == "/ET_DATA");
		REQUIRE(plan.use_et_data == true);
		REQUIRE(plan.delimiter == "^");
	}

	// 5. Non-string column with /ET_DATA result path uses ET_DATA and defaults delimiter to "~"
	{
		auto plan = PlanReadCall(desc_bods, char_type, "");
		REQUIRE(plan.function_name == "/BODS/RFC_READ_TABLE");
		REQUIRE(plan.data_path == "/ET_DATA");
		REQUIRE(plan.use_et_data == true);
		REQUIRE(plan.delimiter == kDefaultReadTableDelimiter);
	}

	// 6. String column on reader WITHOUT ET_DATA throws InvalidInputException
	{
		REQUIRE_THROWS_AS(PlanReadCall(desc_legacy, str_type, ""), InvalidInputException);
	}
}

TEST_CASE("LookupSecretOption throws InvalidInputException on wrong secret type", "[erpl_rfc][read_table_func]") {
	DuckDB db(nullptr);
	db.LoadStaticExtension<ErplRfcExtension>();
	Connection conn(db);
	auto &context = *conn.context;

	auto &secret_manager = SecretManager::Get(context);
	SecretType sec_type {"custom_dummy", nullptr, "config", "test"};
	secret_manager.RegisterSecretType(sec_type);

	SecretType kv_sec_type {"custom_kv", KeyValueSecret::Deserialize<KeyValueSecret>, "config", "test"};
	secret_manager.RegisterSecretType(kv_sec_type);

	auto transaction = SapSystemTransaction(context);
	secret_manager.RegisterSecret(transaction,
		make_uniq<BaseSecret>(vector<string>(), "custom_dummy", "config", "sec_wrong_type"),
		OnCreateConflict::ERROR_ON_CONFLICT,
		SecretPersistType::TEMPORARY);

	auto wrong_kv = make_uniq<KeyValueSecret>(vector<string>(), "custom_kv", "config", "sec_wrong_kv");
	wrong_kv->secret_map["read_table_function"] = Value("Z_CUSTOM");
	secret_manager.RegisterSecret(transaction,
		std::move(wrong_kv),
		OnCreateConflict::ERROR_ON_CONFLICT,
		SecretPersistType::TEMPORARY);

	REQUIRE_THROWS_AS(LookupSecretOption(context, "sec_wrong_type", "read_table_function"), InvalidInputException);
	REQUIRE_THROWS_AS(LookupSecretOption(context, "sec_wrong_kv", "read_table_function"), InvalidInputException);

	// Default secret with non-sap_rfc type does not throw (F8)
	auto default_wrong = make_uniq<BaseSecret>(vector<string>(), "custom_dummy", "config", "default_wrong");
	secret_manager.RegisterSecret(transaction,
		std::move(default_wrong),
		OnCreateConflict::ERROR_ON_CONFLICT,
		SecretPersistType::TEMPORARY);
	auto default_opts = LookupSecretOptions(context, "");
	REQUIRE(default_opts.read_table_function.empty());
	REQUIRE(default_opts.read_table_delimiter.empty());
}

TEST_CASE("AllowsFallback accurately distinguishes default from pinned RFC_READ_TABLE", "[erpl_rfc][read_table_func]") {
	DuckDB db(nullptr);
	db.LoadStaticExtension<ErplRfcExtension>();
	Connection conn(db);
	auto &context = *conn.context;

	// 1. Default (no tier sets anything): AllowsFallback is true
	auto def_opts = ResolveReadTableFunctionOptions(context, nullptr, "");
	REQUIRE(def_opts.function_name == "RFC_READ_TABLE");
	REQUIRE(!def_opts.explicitly_set);
	REQUIRE(def_opts.AllowsFallback());

	// 2. Query parameter pins RFC_READ_TABLE: AllowsFallback is false
	named_parameter_map_t params;
	params["read_table_function"] = Value("RFC_READ_TABLE");
	auto pinned_opts = ResolveReadTableFunctionOptions(context, &params, "");
	REQUIRE(pinned_opts.function_name == "RFC_READ_TABLE");
	REQUIRE(pinned_opts.explicitly_set);
	REQUIRE(!pinned_opts.AllowsFallback());

	// 3. Custom function: AllowsFallback is false
	params["read_table_function"] = Value("Z_CUSTOM_TABLE");
	auto custom_opts = ResolveReadTableFunctionOptions(context, &params, "");
	REQUIRE(custom_opts.function_name == "Z_CUSTOM_TABLE");
	REQUIRE(custom_opts.explicitly_set);
	REQUIRE(!custom_opts.AllowsFallback());

	// 4. Session setting pins RFC_READ_TABLE: AllowsFallback is false
	conn.Query("SET erpl_rfc_read_table_function = 'RFC_READ_TABLE'");
	auto sess_opts = ResolveReadTableFunctionOptions(context, nullptr, "");
	REQUIRE(sess_opts.function_name == "RFC_READ_TABLE");
	REQUIRE(sess_opts.explicitly_set);
	REQUIRE(!sess_opts.AllowsFallback());

	// 5. Session setting cleared with '': AllowsFallback is true again
	conn.Query("SET erpl_rfc_read_table_function = ''");
	auto reset_opts = ResolveReadTableFunctionOptions(context, nullptr, "");
	REQUIRE(reset_opts.function_name == "RFC_READ_TABLE");
	REQUIRE(!reset_opts.explicitly_set);
	REQUIRE(reset_opts.AllowsFallback());
}

TEST_CASE("PlanReadCall provides rich error context when reader lacks ET_DATA", "[erpl_rfc][read_table_func]") {
	ReadTableFunctionDescriptor desc;
	desc.function_name = "Z_OLD_READER";
	desc.supports_et_data = false;
	desc.result_path = "/DATA";

	RfcType str_type(RFCTYPE_STRING, 0);
	try {
		PlanReadCall(desc, str_type, "", "NOTE_TEXT", "STXH", "query parameter");
		FAIL("Expected InvalidInputException");
	} catch (const InvalidInputException &ex) {
		string msg = ex.what();
		REQUIRE(msg.find("column 'NOTE_TEXT'") != string::npos);
		REQUIRE(msg.find("table 'STXH'") != string::npos);
		REQUIRE(msg.find("custom reader 'Z_OLD_READER'") != string::npos);
		REQUIRE(msg.find("configured via query parameter") != string::npos);
		REQUIRE(msg.find("reader does not support ET_DATA") != string::npos);
	}
}

TEST_CASE("ReadTableFunctionDescriptor::Inspect validates connection", "[erpl_rfc][read_table_func]") {
	REQUIRE_THROWS_AS(ReadTableFunctionDescriptor::Inspect(nullptr, "ANY_FUNC"), InvalidInputException);
}

