#include "catch.hpp"
#include "duckdb.hpp"

#include "sap_rfc_api.hpp"
#include "sap_secret.hpp"
#include "sap_connection.hpp"

#include <algorithm>

using namespace duckdb;

// Issue #98: SNC_MODE (and a handful of other documented RfcOpenConnection
// parameters) could not be expressed on a `sap_rfc` secret, so the SAP RFC SDK
// never activated SNC and Kerberos/SNC logon was impossible.
//
// These tests cover the parameter mapping end to end *up to* RfcOpenConnection:
// secret map -> RfcAuthParams -> the name/value list handed to the SDK.  The
// SNC handshake itself needs an SNC-enabled SAP system and is out of scope
// here; what is asserted is that every supported parameter survives both hops
// with its name and value intact.

namespace {

KeyValueSecret MakeSecret(const case_insensitive_map_t<string> &values) {
	vector<string> prefix_paths;
	KeyValueSecret secret(prefix_paths, "sap_rfc", "config", "test_secret");
	for (const auto &kv : values) {
		secret.secret_map[kv.first] = Value(kv.second);
	}
	return secret;
}

// Looks up a parameter in the list built for RfcOpenConnection.  Returns the
// value, or a null option when the parameter was not passed at all.
optional_idx IndexOf(const vector<std::pair<string, string>> &params, const string &name) {
	for (idx_t i = 0; i < params.size(); i++) {
		if (params[i].first == name) {
			return optional_idx(i);
		}
	}
	return optional_idx();
}

} // namespace

TEST_CASE("SNC parameters survive the secret -> RfcAuthParams hop", "[erpl_rfc][sap_secret]") {
	auto secret = MakeSecret({{"ashost", "s4.local"},
	                          {"sysnr", "00"},
	                          {"client", "100"},
	                          {"user", "DEMO"},
	                          {"snc_mode", "1"},
	                          {"snc_partnername", "p:saps4hsnc@EXAMPLE.LOCAL"},
	                          {"snc_lib", "/usr/lib/libgsskrb5.so"}});

	auto auth_params = ConvertSecretToAuthParams(secret);

	REQUIRE(auth_params.ashost == "s4.local");
	REQUIRE(auth_params.user == "DEMO");
	REQUIRE(auth_params.snc_mode == "1");
	REQUIRE(auth_params.snc_partnername == "p:saps4hsnc@EXAMPLE.LOCAL");
	REQUIRE(auth_params.snc_lib == "/usr/lib/libgsskrb5.so");
	// The issue's central point: SNC logon must work without a password.
	REQUIRE(auth_params.password.empty());
}

TEST_CASE("All supported secret keys map onto RfcAuthParams", "[erpl_rfc][sap_secret]") {
	// Every registered parameter gets a distinct value so a mis-wired mapping
	// (copy-paste of the wrong member) shows up as a mismatch rather than as
	// two equal empty strings.
	case_insensitive_map_t<string> values;
	for (const auto &key : SapSecretParameterNames()) {
		values[key] = "v_" + key;
	}
	auto auth_params = ConvertSecretToAuthParams(MakeSecret(values));

	auto params = auth_params.BuildConnectionParams();
	REQUIRE(params.size() == SapSecretParameterNames().size());

	for (const auto &key : SapSecretParameterNames()) {
		auto idx = IndexOf(params, key);
		REQUIRE(idx.IsValid());
		REQUIRE(params[idx.GetIndex()].second == "v_" + key);
	}
}

TEST_CASE("The secret exposes the connection parameters issue #98 asks for", "[erpl_rfc][sap_secret]") {
	const auto &names = SapSecretParameterNames();
	for (const char *expected : {"snc_mode", "snc_sso", "snc_qop", "snc_myname", "snc_partnername", "snc_lib",
	                             "x509cert", "saprouter", "gwhost", "gwserv", "codepage", "trace", "dest"}) {
		INFO("missing parameter: " << expected);
		REQUIRE(std::find(names.begin(), names.end(), string(expected)) != names.end());
	}
}

TEST_CASE("Unset parameters are not passed to RfcOpenConnection", "[erpl_rfc][sap_secret]") {
	// The SDK treats an empty value as "explicitly empty" for some parameters,
	// so anything the user did not set must be omitted entirely.
	auto auth_params = ConvertSecretToAuthParams(MakeSecret({{"ashost", "s4.local"}, {"snc_mode", "1"}}));

	auto params = auth_params.BuildConnectionParams();
	REQUIRE(params.size() == 2);
	REQUIRE(IndexOf(params, "ashost").IsValid());
	REQUIRE(IndexOf(params, "snc_mode").IsValid());
	REQUIRE_FALSE(IndexOf(params, "passwd").IsValid());
	REQUIRE_FALSE(IndexOf(params, "snc_lib").IsValid());
}

TEST_CASE("The password parameter keeps its SDK spelling", "[erpl_rfc][sap_secret]") {
	// `passwd` is the secret key *and* the RfcOpenConnection parameter name,
	// while the RfcAuthParams member is called `password` — an easy place to
	// drop the value on the floor.
	auto auth_params = ConvertSecretToAuthParams(MakeSecret({{"passwd", "hunter2"}}));
	REQUIRE(auth_params.password == "hunter2");

	auto params = auth_params.BuildConnectionParams();
	auto idx = IndexOf(params, "passwd");
	REQUIRE(idx.IsValid());
	REQUIRE(params[idx.GetIndex()].second == "hunter2");
}

TEST_CASE("ToString never leaks the password", "[erpl_rfc][sap_secret]") {
	auto auth_params = ConvertSecretToAuthParams(
	    MakeSecret({{"ashost", "s4.local"}, {"passwd", "hunter2"}, {"snc_mode", "1"}}));

	auto rendered = auth_params.ToString();
	REQUIRE(rendered.find("hunter2") == string::npos);
	REQUIRE(rendered.find("passwd=***") != string::npos);
	REQUIRE(rendered.find("snc_mode=1") != string::npos);
}

TEST_CASE("SNC_MODE alone classifies the connection as SNC for telemetry", "[erpl_rfc][sap_secret]") {
	// TelemetryAuthKind inspects only the *presence* of credential fields.
	// Before #98 it could not see snc_mode, so a Kerberos/SNC logon that set
	// nothing but snc_mode was reported as basic auth.
	auto snc = ConvertSecretToAuthParams(MakeSecret({{"ashost", "s4.local"}, {"snc_mode", "1"}}));
	REQUIRE(string(snc.TelemetryAuthKind()) == "snc");

	auto basic = ConvertSecretToAuthParams(MakeSecret({{"ashost", "s4.local"}, {"user", "DEMO"}, {"passwd", "pw"}}));
	REQUIRE(string(basic.TelemetryAuthKind()) == "basic");

	// An SSO2 ticket still wins over SNC.
	auto sso = ConvertSecretToAuthParams(MakeSecret({{"mysapsso2", "ticket"}, {"snc_mode", "1"}}));
	REQUIRE(string(sso.TelemetryAuthKind()) == "sso");
}
