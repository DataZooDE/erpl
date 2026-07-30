#include "duckdb.hpp"
#include "sap_connection.hpp"
#include "sap_type_conversion.hpp"
#include "sap_secret.hpp"
#include "erpl_telemetry.hpp"
#include <fstream>
#include <sstream>

namespace duckdb 
{
    void RfcEnvironmentCredentialsProvider::SetExtensionOptionValue(string key, const char *env_var_name) {
        static char *evar;

        if ((evar = std::getenv(env_var_name)) != NULL) {
            if (StringUtil::Lower(evar) == "false") {
                this->config.SetOption(key, Value(false));
            } else if (StringUtil::Lower(evar) == "true") {
                this->config.SetOption(key, Value(true));
            } else {
                this->config.SetOption(key, Value(evar));
            }
        }
    }

    void RfcEnvironmentCredentialsProvider::SetAll() {
        SetExtensionOptionValue("sap_ashost", ASHOST_ENV_VAR);
        SetExtensionOptionValue("sap_sysnr", SYSNR_ENV_VAR);
        SetExtensionOptionValue("sap_user", USER_ENV_VAR);
        SetExtensionOptionValue("sap_password", PASSWORD_ENV_VAR);
        SetExtensionOptionValue("sap_client", CLIENT_ENV_VAR);
        SetExtensionOptionValue("sap_lang", LANG_ENV_VAR);
    }

    // RfcEnvironmentCredentialsProvider ----------------------------------------


    RfcAuthParams RfcAuthParams::FromContext(ClientContext &context, const string &secret_name) {
        auto &secret_manager = duckdb::SecretManager::Get(context);
        auto transaction = duckdb::CatalogTransaction::GetSystemCatalogTransaction(context);

        // A caller that named a secret gets that secret. LookupSecret() matches
        // the argument against the secret *scopes*, not against the name, so
        // using it here handed back whatever sap_rfc secret happened to be in
        // scope — silently connecting with another system's credentials.
        if (!secret_name.empty() && secret_name != SAP_SECRET_DEFAULT_PATH) {
            auto secret_entry = secret_manager.GetSecretByName(transaction, secret_name);
            if (!secret_entry) {
                throw InvalidInputException("Secret '%s' not found", secret_name);
            }
            if (secret_entry->secret->GetType() != SAP_SECRET_TYPE_NAME) {
                throw InvalidInputException("Secret '%s' is of type '%s', expected '%s'", secret_name,
                                            secret_entry->secret->GetType(), SAP_SECRET_TYPE_NAME);
            }
            const auto &named_secret = dynamic_cast<const KeyValueSecret &>(*secret_entry->secret);
            return ConvertSecretToAuthParams(named_secret);
        }

        // No secret named: fall back to the best scope match.
        auto secret_match = secret_manager.LookupSecret(transaction, secret_name, "sap_rfc");
        if (! secret_match.HasMatch()) {

            throw InvalidInputException("Secret '%s' not found", secret_name);
        }

        // Cast to SapSecret
        const auto &duck_secret = dynamic_cast<const KeyValueSecret &>(secret_match.GetSecret());
        return ConvertSecretToAuthParams(duck_secret);
    }

    const vector<RfcAuthParamDefinition> &RfcAuthParamDefinitions()
    {
        // The names are the ones documented for RfcOpenConnection in the SAP
        // NetWeaver RFC SDK; they are passed through unchanged.
        static const vector<RfcAuthParamDefinition> definitions = {
            {"ashost",          &RfcAuthParams::ashost,          false},
            {"sysnr",           &RfcAuthParams::sysnr,           false},
            {"user",            &RfcAuthParams::user,            false},
            {"passwd",          &RfcAuthParams::password,        true},
            {"client",          &RfcAuthParams::client,          false},
            {"lang",            &RfcAuthParams::lang,            false},
            {"mshost",          &RfcAuthParams::mshost,          false},
            {"msserv",          &RfcAuthParams::msserv,          false},
            {"sysid",           &RfcAuthParams::sysid,           false},
            {"group",           &RfcAuthParams::group,           false},
            {"snc_mode",        &RfcAuthParams::snc_mode,        false},
            {"snc_sso",         &RfcAuthParams::snc_sso,         false},
            {"snc_qop",         &RfcAuthParams::snc_qop,         false},
            {"snc_myname",      &RfcAuthParams::snc_myname,      false},
            {"snc_partnername", &RfcAuthParams::snc_partnername, false},
            {"snc_lib",         &RfcAuthParams::snc_lib,         false},
            {"mysapsso2",       &RfcAuthParams::mysapsso2,       true},
            {"x509cert",        &RfcAuthParams::x509cert,        true},
            {"saprouter",       &RfcAuthParams::saprouter,       false},
            {"gwhost",          &RfcAuthParams::gwhost,          false},
            {"gwserv",          &RfcAuthParams::gwserv,          false},
            {"codepage",        &RfcAuthParams::codepage,        false},
            {"trace",           &RfcAuthParams::trace,           false},
            {"dest",            &RfcAuthParams::dest,            false},
        };
        return definitions;
    }

    vector<std::pair<std::string, std::string>> RfcAuthParams::BuildConnectionParams() const
    {
        vector<std::pair<std::string, std::string>> params;
        for (auto &definition : RfcAuthParamDefinitions()) {
            const auto &value = this->*definition.member;
            if (value.empty()) {
                continue;
            }
            params.emplace_back(definition.name, value);
        }
        return params;
    }

    std::string RfcAuthParams::ToString() {
        std::stringstream ss;
        bool first = true;
        for (auto &definition : RfcAuthParamDefinitions()) {
            const auto &value = this->*definition.member;
            if (value.empty()) {
                continue;
            }
            if (!first) {
                ss << " ";
            }
            first = false;
            ss << definition.name << "=" << (definition.secret ? "***" : value);
        }
        return ss.str();
    }

    std::shared_ptr<RfcConnection> RfcAuthParams::Connect() 
    {
        RFC_ERROR_INFO error_info;

        // Marshal the parameter list into the SDK's wide-character representation.
        // `storage` owns the converted strings and must outlive the RfcOpenConnection
        // call, since RFC_CONNECTION_PARAMETER only holds pointers into it.
        auto param_list = BuildConnectionParams();
        std::vector<unique_ptr<SAP_UC, void (*)(void *)>> storage;
        std::vector<RFC_CONNECTION_PARAMETER> params;
        storage.reserve(param_list.size() * 2);
        params.reserve(param_list.size());

        for (auto &param : param_list) {
            storage.push_back(std2uc(param.first));
            auto name = storage.back().get();
            storage.push_back(std2uc(param.second));
            auto value = storage.back().get();

            RFC_CONNECTION_PARAMETER rfc_param;
            rfc_param.name = name;
            rfc_param.value = value;
            params.push_back(rfc_param);
        }
        auto param_count = params.size();

        // Telemetry: classify the auth method before the round-trip (pure local
        // field checks, no credential material). Emitted only on success below.
        const char *auth = TelemetryAuthKind();

        auto connection_handle = RfcOpenConnection(params.data(), param_count, &error_info);

        if (connection_handle == NULL) {
            // Telemetry: $exception {error_class (from RFC_RC enum), feature,
            // phase}. The SAP error message/code text is NEVER sent.
            erpl_telemetry::CaptureError(RfcTelemetryErrorClass(error_info.code),
                                         erpl_telemetry::feature::kConnectionOpened,
                                         erpl_telemetry::phase::kConnect);
            throw IOException(StringUtil::Format("Error during SAP RFC logon: %s: %s",rfcrc2std(error_info.code), uc2std(error_info.message)));
        }
        // Telemetry: feature_used {feature="connection_opened", auth_kind}.
        erpl_telemetry::CaptureConnectionOpened(auth);
        return std::make_shared<RfcConnection>(connection_handle);
    }

    const char *RfcAuthParams::TelemetryAuthKind() const
    {
        // Order matters: an SSO2 ticket or SNC library takes precedence over a
        // plain user/password even if both are present. Only the *presence* of
        // a field is inspected — never its value.
        if (!mysapsso2.empty()) {
            return erpl_telemetry::auth_kind::kSso;
        }
        if (!snc_mode.empty() || !snc_sso.empty() || !snc_lib.empty() || !snc_myname.empty() ||
            !snc_partnername.empty() || !snc_qop.empty() || !x509cert.empty()) {
            return erpl_telemetry::auth_kind::kSnc;
        }
        return erpl_telemetry::auth_kind::kBasic;
    }

    const char *RfcTelemetryErrorClass(RFC_RC code)
    {
        switch (code) {
            case RFC_LOGON_FAILURE:
            case RFC_AUTHORIZATION_FAILURE:
                return erpl_telemetry::error_class::kAuthError;
            case RFC_COMMUNICATION_FAILURE:
            case RFC_CLOSED:
                return erpl_telemetry::error_class::kConnectionFailed;
            case RFC_TIMEOUT:
            case RFC_CANCELED:
                return erpl_telemetry::error_class::kTimeout;
            default:
                return erpl_telemetry::error_class::kRfcError;
        }
    }

    // RfcAuthParams ------------------------------------------------------------
    
    RfcConnection::RfcConnection(RFC_CONNECTION_HANDLE handle) : handle(handle)
    { }

    RfcConnection::~RfcConnection()
    {
        // A destructor must never throw: an escaping exception calls
        // std::terminate and aborts the host process (issue #78, where a
        // stale/already-closed handle made Close() throw RFC_INVALID_HANDLE
        // during teardown).
        try {
            Close();
        } catch (...) {
            // best-effort cleanup; swallow.
        }
    }

    void RfcConnection::Close()
    {
        if (handle == NULL)
            return;

        RFC_RC rc = RFC_OK;
        RFC_ERROR_INFO error_info;

        rc = RfcCloseConnection(handle, &error_info);
        // Regardless of the outcome the handle must not be reused: a second
        // RfcCloseConnection on the same handle yields RFC_INVALID_HANDLE.
        // Nulling here prevents the double-close path (issue #78).
        handle = NULL;
        // RFC_INVALID_HANDLE means the handle is already gone (the SAP gateway
        // dropped the connection, or it was closed elsewhere) — nothing left
        // to close, so treat it as benign rather than an error.
        if (rc != RFC_OK && rc != RFC_INVALID_HANDLE) {
            throw IOException(StringUtil::Format("Error during SAP RFC connection closing: %s: %s",rfcrc2std(error_info.code), uc2std(error_info.message)));
        }
    }

    void RfcConnection::Ping()
    {
        RFC_RC rc = RFC_OK;
        RFC_ERROR_INFO error_info;
        
        rc = RfcPing(handle, &error_info);
        if (rc != RFC_OK) {
            throw IOException(StringUtil::Format("Error during SAP connection ping: %s: %s",rfcrc2std(error_info.code), uc2std(error_info.message)));
        }
    }

    RfcConnectionAttributes RfcConnection::ConnectionAttributes()
    {
        RFC_RC rc = RFC_OK;
        RFC_ERROR_INFO error_info;
        RFC_ATTRIBUTES attributes;

        rc = RfcGetConnectionAttributes(handle, &attributes, &error_info);
        if (rc != RFC_OK) {
            throw IOException(StringUtil::Format("Error during SAP connection attributes retrieval: %s: %s",rfcrc2std(error_info.code), uc2std(error_info.message)));
        }

        RfcConnectionAttributes conn_attrs;
        
        conn_attrs.destination = uc2std(attributes.dest);
        conn_attrs.host = uc2std(attributes.host);
        conn_attrs.partner_host = uc2std(attributes.partnerHost);
        conn_attrs.sys_number = uc2std(attributes.sysNumber);
        conn_attrs.client = uc2std(attributes.client);
        conn_attrs.user = uc2std(attributes.user);
        conn_attrs.language = uc2std(attributes.language);
        conn_attrs.trace = uc2std(attributes.trace);
        conn_attrs.iso_language = uc2std(attributes.isoLanguage);
        conn_attrs.codepage = uc2std(attributes.codepage);
        conn_attrs.partner_codepage = uc2std(attributes.partnerCodepage);
        conn_attrs.rfc_role = uc2std(attributes.rfcRole);
        conn_attrs.type = uc2std(attributes.type);
        conn_attrs.partner_type = uc2std(attributes.partnerType);
        conn_attrs.release = uc2std(attributes.rel);
        conn_attrs.partner_release = uc2std(attributes.partnerRel);
        conn_attrs.kernel_release = uc2std(attributes.kernelRel);
        conn_attrs.cpic_conv_id = uc2std(attributes.cpicConvId);
        conn_attrs.progname = uc2std(attributes.progName);
        conn_attrs.partner_bytes_per_char = uc2std(attributes.partnerBytesPerChar);
        conn_attrs.partner_system_codepage = uc2std(attributes.partnerSystemCodepage);
        conn_attrs.partner_ip = uc2std(attributes.partnerIP);
        conn_attrs.partner_ipv6 = uc2std(attributes.partnerIPv6);

        return conn_attrs;
    }

    // RfcConnnection -----------------------------------------------------------

} // namespace duckdb