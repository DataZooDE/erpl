#include "duckdb.hpp"
#include "duckdb/main/extension_util.hpp"

#include "sap_secret.hpp"
#include "sap_connection.hpp"

namespace duckdb {

unique_ptr<BaseSecret> CreateSapSecretFunction(ClientContext &context, CreateSecretInput &input) {
	// apply any overridden settings
	vector<string> prefix_paths;
	auto result = make_uniq<KeyValueSecret>(prefix_paths, "sap_rfc", "config", input.name);
	for (const auto &named_param : input.options) {
		auto lower_name = StringUtil::Lower(named_param.first);

		if (lower_name == "ashost") {
			result->secret_map["ashost"] = named_param.second.ToString();
		} else if (lower_name == "sysnr") {
			result->secret_map["sysnr"] = named_param.second.ToString();
		} else if (lower_name == "user") {
			result->secret_map["user"] = named_param.second.ToString();
		} else if (lower_name == "passwd") {
			result->secret_map["passwd"] = named_param.second.ToString();
		} else if (lower_name == "client") {
			result->secret_map["client"] = named_param.second.ToString();
		} else if (lower_name == "lang") {
			result->secret_map["lang"] = named_param.second.ToString();
		} else if (lower_name == "mshost") {
			result->secret_map["mshost"] = named_param.second.ToString();
		} else if (lower_name == "msserv") {
			result->secret_map["msserv"] = named_param.second.ToString();
		} else if (lower_name == "sysid") {
			result->secret_map["sysid"] = named_param.second.ToString();
		} else if (lower_name == "group") {
			result->secret_map["group"] = named_param.second.ToString();
		} else if (lower_name == "snc_qop") {
			result->secret_map["snc_qop"] = named_param.second.ToString();
		} else if (lower_name == "snc_myname")  {
			result->secret_map["snc_myname"] = named_param.second.ToString();
		} else if (lower_name == "snc_partnername") {
			result->secret_map["snc_partnername"] = named_param.second.ToString();
		} else if (lower_name == "snc_lib") {
			result->secret_map["snc_lib"] = named_param.second.ToString();
		} else if (lower_name == "mysapsso2") {
			result->secret_map["mysapsso2"] = named_param.second.ToString();
		} else {
			throw InternalException("Unknown named parameter passed to CreateSapSecretFunction: " + lower_name);
		}
	}

	//! Set redact keys
	result->redact_keys = {"password"};
	return std::move(result);
}

void SetSapSecretParameters(CreateSecretFunction &function) {
	function.named_parameters["ashost"] = LogicalType::VARCHAR;
	function.named_parameters["sysnr"] = LogicalType::VARCHAR;
	function.named_parameters["user"] = LogicalType::VARCHAR;
	function.named_parameters["passwd"] = LogicalType::VARCHAR;
	function.named_parameters["client"] = LogicalType::VARCHAR;
	function.named_parameters["lang"] = LogicalType::VARCHAR;
	function.named_parameters["mshost"] = LogicalType::VARCHAR;
	function.named_parameters["msserv"] = LogicalType::VARCHAR;
	function.named_parameters["sysid"] = LogicalType::VARCHAR;
	function.named_parameters["group"] = LogicalType::VARCHAR;
	function.named_parameters["snc_qop"] = LogicalType::VARCHAR;
	function.named_parameters["snc_myname"] = LogicalType::VARCHAR;
	function.named_parameters["snc_partnername"] = LogicalType::VARCHAR;
	function.named_parameters["snc_lib"] = LogicalType::VARCHAR;
	function.named_parameters["mysapsso2"] = LogicalType::VARCHAR;
}

void RegisterSapSecretType(DatabaseInstance &db) 
{
    // Register the new type
	duckdb::SecretType sap_rfc_secret_type;
	sap_rfc_secret_type.name = SAP_SECRET_TYPE_NAME;
	sap_rfc_secret_type.deserializer = KeyValueSecret::Deserialize<KeyValueSecret>;
	sap_rfc_secret_type.default_provider = SAP_SECRET_PROVIDER;

	ExtensionUtil::RegisterSecretType(db, sap_rfc_secret_type);

	CreateSecretFunction sap_rfc_secret_function = {SAP_SECRET_TYPE_NAME, SAP_SECRET_PROVIDER, CreateSapSecretFunction};
	SetSapSecretParameters(sap_rfc_secret_function);
	ExtensionUtil::RegisterFunction(db, sap_rfc_secret_function);
}

RfcAuthParams ConvertSecretToAuthParams(const KeyValueSecret &duck_secret) 
{
	RfcAuthParams auth_params;

    auto ashost_val = duck_secret.TryGetValue("ashost");
    if (! ashost_val.IsNull()) {
        auth_params.ashost = ashost_val.ToString();
    }

    auto sysnr_val = duck_secret.TryGetValue("sysnr");
    if (! sysnr_val.IsNull()) {
        auth_params.sysnr = sysnr_val.ToString();
    }

    auto user_val = duck_secret.TryGetValue("user");
    if (! user_val.IsNull()) {
        auth_params.user = user_val.ToString();
    }

    auto passwd_val = duck_secret.TryGetValue("passwd");
    if (! passwd_val.IsNull()) {
        auth_params.password = passwd_val.ToString();
    }

    auto client_val = duck_secret.TryGetValue("client");
    if (! client_val.IsNull()) {
        auth_params.client = client_val.ToString();
    }

    auto lang_val = duck_secret.TryGetValue("lang");
    if (! lang_val.IsNull()) {
        auth_params.lang = lang_val.ToString();
    }

    auto mshost_val = duck_secret.TryGetValue("mshost");    
    if (! mshost_val.IsNull()) {
        auth_params.mshost = mshost_val.ToString();
    }

    auto msserv_val = duck_secret.TryGetValue("msserv");
    if (! msserv_val.IsNull()) {
        auth_params.msserv = msserv_val.ToString();
    }

    auto sysid_val = duck_secret.TryGetValue("sysid");
    if (! sysid_val.IsNull()) {
        auth_params.sysid = sysid_val.ToString();
    }

    auto group_val = duck_secret.TryGetValue("group");
    if (! group_val.IsNull()) {
        auth_params.group = group_val.ToString();
    }

    auto snc_qop_val = duck_secret.TryGetValue("snc_qop");
    if (! snc_qop_val.IsNull()) {
        auth_params.snc_qop = snc_qop_val.ToString();
    }

    auto snc_myname_val = duck_secret.TryGetValue("snc_myname");
    if (! snc_myname_val.IsNull()) {
        auth_params.snc_myname = snc_myname_val.ToString();
    }

    auto snc_partnername_val = duck_secret.TryGetValue("snc_partnername");
    if (! snc_partnername_val.IsNull()) {
        auth_params.snc_partnername = snc_partnername_val.ToString();
    }

    auto snc_lib_val = duck_secret.TryGetValue("snc_lib");
    if (! snc_lib_val.IsNull()) {
        auth_params.snc_lib = snc_lib_val.ToString();
    }

    auto mysapsso2_val = duck_secret.TryGetValue("mysapsso2");
    if (! mysapsso2_val.IsNull()) {
        auth_params.mysapsso2 = mysapsso2_val.ToString();
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