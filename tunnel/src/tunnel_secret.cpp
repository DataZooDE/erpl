#include "tunnel_secret.hpp"
#include "tunnel_connection.hpp"
#include "duckdb/main/extension/extension_loader.hpp"
#include "duckdb/common/string_util.hpp"

namespace duckdb {

unique_ptr<BaseSecret> CreateTunnelSecretFunction(ClientContext &context, CreateSecretInput &input) {
    // Create a new tunnel secret
    vector<string> prefix_paths;
    auto result = make_uniq<KeyValueSecret>(prefix_paths, TUNNEL_SECRET_TYPE_NAME, TUNNEL_SECRET_PROVIDER, input.name);
    
    // Process named parameters
    for (const auto &named_param : input.options) {
        auto lower_name = StringUtil::Lower(named_param.first);
        
        if (lower_name == "ssh_host") {
            result->secret_map["ssh_host"] = named_param.second.ToString();
        } else if (lower_name == "ssh_port") {
            result->secret_map["ssh_port"] = named_param.second.ToString();
        } else if (lower_name == "ssh_user") {
            result->secret_map["ssh_user"] = named_param.second.ToString();
        } else if (lower_name == "password") {
            result->secret_map["password"] = named_param.second.ToString();
        } else if (lower_name == "private_key_path") {
            result->secret_map["private_key_path"] = named_param.second.ToString();
        } else if (lower_name == "passphrase") {
            result->secret_map["passphrase"] = named_param.second.ToString();
        } else if (lower_name == "auth_method") {
            result->secret_map["auth_method"] = named_param.second.ToString();
        } else {
            throw InternalException("Unknown named parameter passed to CreateTunnelSecretFunction: " + lower_name);
        }
    }
    
    // Set redact keys for sensitive information
    result->redact_keys = {"password", "passphrase", "private_key_path"};
    return std::move(result);
}

void SetTunnelSecretParameters(CreateSecretFunction &function) {
    function.named_parameters["ssh_host"] = LogicalType::VARCHAR;
    function.named_parameters["ssh_port"] = LogicalType::INTEGER;
    function.named_parameters["ssh_user"] = LogicalType::VARCHAR;
    function.named_parameters["password"] = LogicalType::VARCHAR;
    function.named_parameters["private_key_path"] = LogicalType::VARCHAR;
    function.named_parameters["passphrase"] = LogicalType::VARCHAR;
    function.named_parameters["auth_method"] = LogicalType::VARCHAR;
}

void RegisterTunnelSecretType(ExtensionLoader &loader) {
    // Register the tunnel secret type
    duckdb::SecretType tunnel_secret_type;
    tunnel_secret_type.name = TUNNEL_SECRET_TYPE_NAME;
    tunnel_secret_type.deserializer = KeyValueSecret::Deserialize<KeyValueSecret>;
    tunnel_secret_type.default_provider = TUNNEL_SECRET_PROVIDER;
    
    loader.RegisterSecretType(tunnel_secret_type);
    
    // Register the create secret function
    CreateSecretFunction tunnel_secret_function = {TUNNEL_SECRET_TYPE_NAME, TUNNEL_SECRET_PROVIDER, CreateTunnelSecretFunction};
    SetTunnelSecretParameters(tunnel_secret_function);
    loader.RegisterFunction(tunnel_secret_function);
}

TunnelAuthParams ConvertTunnelSecretToAuthParams(const KeyValueSecret &duck_secret) {
    TunnelAuthParams auth_params;
    
    // Extract SSH host
    auto ssh_host_val = duck_secret.TryGetValue("ssh_host");
    if (!ssh_host_val.IsNull()) {
        auth_params.ssh_host = ssh_host_val.ToString();
    }
    
    // Extract SSH port
    auto ssh_port_val = duck_secret.TryGetValue("ssh_port");
    if (!ssh_port_val.IsNull()) {
        try {
            auth_params.ssh_port = std::stoi(ssh_port_val.ToString());
        } catch (const std::exception &) {
            auth_params.ssh_port = 22; // Default SSH port
        }
    } else {
        auth_params.ssh_port = 22; // Default SSH port
    }
    
    // Extract SSH user
    auto ssh_user_val = duck_secret.TryGetValue("ssh_user");
    if (!ssh_user_val.IsNull()) {
        auth_params.ssh_user = ssh_user_val.ToString();
    }
    
    // Extract password
    auto password_val = duck_secret.TryGetValue("password");
    if (!password_val.IsNull()) {
        auth_params.password = password_val.ToString();
    }
    
    // Extract private key path
    auto private_key_path_val = duck_secret.TryGetValue("private_key_path");
    if (!private_key_path_val.IsNull()) {
        auth_params.private_key_path = private_key_path_val.ToString();
    }
    
    // Extract passphrase
    auto passphrase_val = duck_secret.TryGetValue("passphrase");
    if (!passphrase_val.IsNull()) {
        auth_params.passphrase = passphrase_val.ToString();
    }
    
    // Extract auth method
    auto auth_method_val = duck_secret.TryGetValue("auth_method");
    if (!auth_method_val.IsNull()) {
        auth_params.auth_method = auth_method_val.ToString();
    } else {
        // Determine auth method based on available credentials
        if (!auth_params.password.empty()) {
            auth_params.auth_method = "password";
        } else if (!auth_params.private_key_path.empty()) {
            auth_params.auth_method = "key";
        } else {
            auth_params.auth_method = "agent";
        }
    }
    
    return auth_params;
}

// ------------------------------------------------------------------------------------------------

std::string GetTunnelSecretNameFromParams(const TableFunctionBindInput &input) {
    return GetTunnelSecretNameFromParams(input.named_parameters);
}

std::string GetTunnelSecretNameFromParams(const FunctionParameters &parameters) {
    return GetTunnelSecretNameFromParams(parameters.named_parameters);
}

std::string GetTunnelSecretNameFromParams(const named_parameter_map_t &named_params) {
    if (named_params.find("secret") != named_params.end()) {
        return named_params.at("secret").ToString();
    } else {
        return std::string();
    }
}

TunnelAuthParams GetTunnelAuthParamsFromContext(ClientContext &context, const TableFunctionBindInput &parameters) {
    auto secret_name = GetTunnelSecretNameFromParams(parameters);
    return GetTunnelAuthParamsFromContext(context, secret_name);
}

TunnelAuthParams GetTunnelAuthParamsFromContext(ClientContext &context, const FunctionParameters &parameters) {
    auto secret_name = GetTunnelSecretNameFromParams(parameters);
    return GetTunnelAuthParamsFromContext(context, secret_name);
}

TunnelAuthParams GetTunnelAuthParamsFromContext(ClientContext &context, const std::string &secret_name) {
    TunnelAuthParams auth_params;
    
    if (!secret_name.empty()) {
        // Use secret for connection
        auth_params = TunnelAuthParams::FromContext(context, secret_name);
    } else {
        // Use context settings for connection
        auth_params = TunnelAuthParams::FromContext(context);
    }
    
    return auth_params;
}

} // namespace duckdb 