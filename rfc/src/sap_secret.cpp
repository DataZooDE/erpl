#include "duckdb.hpp"
#include "duckdb/main/extension/extension_loader.hpp"

#include "sap_secret.hpp"
#include "sap_connection.hpp"

#include <algorithm>

namespace duckdb {

const vector<string> &SapSecretParameterNames() {
	static const vector<string> names = []() {
		vector<string> result;
		for (auto &definition : RfcAuthParamDefinitions()) {
			result.emplace_back(definition.name);
		}
		return result;
	}();
	return names;
}

unique_ptr<BaseSecret> CreateSapSecretFunction(ClientContext &context, CreateSecretInput &input) {
	// apply any overridden settings
	vector<string> prefix_paths;
	auto result = make_uniq<KeyValueSecret>(prefix_paths, "sap_rfc", "config", input.name);
	auto &names = SapSecretParameterNames();
	for (const auto &named_param : input.options) {
		auto lower_name = StringUtil::Lower(named_param.first);

		if (std::find(names.begin(), names.end(), lower_name) == names.end()) {
			// Normally unreachable — DuckDB rejects unregistered named parameters
			// while binding CREATE SECRET — but the two lists are only kept in
			// sync by RfcAuthParamDefinitions(), so fail on user input, not with
			// an INTERNAL error.
			throw InvalidInputException("Unknown parameter '%s' for secret type 'sap_rfc'", lower_name);
		}
		result->secret_map[lower_name] = named_param.second.ToString();
	}

	//! Set redact keys. The key names are the secret map keys, so `passwd` —
	//! naming the RfcAuthParams member (`password`) here silently disabled
	//! redaction and printed the password in duckdb_secrets().
	for (auto &definition : RfcAuthParamDefinitions()) {
		if (definition.secret) {
			result->redact_keys.insert(definition.name);
		}
	}
	return std::move(result);
}

void SetSapSecretParameters(CreateSecretFunction &function) {
	for (auto &name : SapSecretParameterNames()) {
		function.named_parameters[name] = LogicalType::VARCHAR;
	}
}

void RegisterSapSecretType(ExtensionLoader &loader) 
{
    // Register the new type
	duckdb::SecretType sap_rfc_secret_type;
	sap_rfc_secret_type.name = SAP_SECRET_TYPE_NAME;
	sap_rfc_secret_type.deserializer = KeyValueSecret::Deserialize<KeyValueSecret>;
	sap_rfc_secret_type.default_provider = SAP_SECRET_PROVIDER;

	loader.RegisterSecretType(sap_rfc_secret_type);

	CreateSecretFunction sap_rfc_secret_function = {SAP_SECRET_TYPE_NAME, SAP_SECRET_PROVIDER, CreateSapSecretFunction};
	SetSapSecretParameters(sap_rfc_secret_function);
	loader.RegisterFunction(sap_rfc_secret_function);
}

RfcAuthParams ConvertSecretToAuthParams(const KeyValueSecret &duck_secret)
{
	RfcAuthParams auth_params;

	for (auto &definition : RfcAuthParamDefinitions()) {
		auto value = duck_secret.TryGetValue(definition.name);
		if (value.IsNull()) {
			continue;
		}
		auth_params.*definition.member = value.ToString();
	}

	return auth_params;
}

// ------------------------------------------------------------------------------------------------

std::string GetSecretNameFromParams(const TableFunctionBindInput &input) 
{
	return GetSecretNameFromParams(input.named_parameters);
}

std::string GetSecretNameFromParams(const FunctionParameters &parameters) 
{
	return GetSecretNameFromParams(parameters.named_parameters);
}

std::string GetSecretNameFromParams(const named_parameter_map_t &named_params) 
{
	if (named_params.find("secret") != named_params.end()) {
		auto val = named_params.at("secret").ToString();
		return val;
	} else {
		return std::string();
	}
}

RfcAuthParams GetAuthParamsFromContext(ClientContext &context, const TableFunctionBindInput &parameters) 
{
	auto secret_name = GetSecretNameFromParams(parameters);
	return GetAuthParamsFromContext(context, secret_name);
}

RfcAuthParams GetAuthParamsFromContext(ClientContext &context, const FunctionParameters &parameters)
{
	auto secret_name = GetSecretNameFromParams(parameters);
	return GetAuthParamsFromContext(context, secret_name);
}

RfcAuthParams GetAuthParamsFromContext(ClientContext &context, const std::string &secret_name) 
{
	RfcAuthParams auth_params;
	if (!secret_name.empty()) {
		// Use secret for connection
		auth_params = RfcAuthParams::FromContext(context, secret_name);
	} else {
		// Use context settings for connection
		auth_params = RfcAuthParams::FromContext(context);
	}
	return auth_params;
}

} // namespace duckdb 