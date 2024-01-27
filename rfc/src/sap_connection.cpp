#include "duckdb.hpp"
#include "sap_connection.hpp"
#include "sap_type_conversion.hpp"

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
    
    RfcAuthParams RfcAuthParams::FromContext(ClientContext &context) {
        RfcAuthParams params;

        Value v_ashost;
        if (context.TryGetCurrentSetting("sap_ashost", v_ashost)) {
		    params.ashost = StringValue::Get(v_ashost);
	    }

        Value v_sysnr;
        if (context.TryGetCurrentSetting("sap_sysnr", v_sysnr)) {
            params.sysnr = StringValue::Get(v_sysnr);
        }

        Value v_user;
        if (context.TryGetCurrentSetting("sap_user", v_user)) {
            params.user = StringValue::Get(v_user);
        }

        Value v_password;
        if (context.TryGetCurrentSetting("sap_password", v_password)) {
            params.password = StringValue::Get(v_password);
        }

        Value v_client;
        if (context.TryGetCurrentSetting("sap_client", v_client)) {
            params.client = StringValue::Get(v_client);
        }

        Value v_lang;
        if (context.TryGetCurrentSetting("sap_lang", v_lang)) {
            params.lang = StringValue::Get(v_lang);
        }

        return params;
    }

    std::string RfcAuthParams::ToString() {
        return StringUtil::Format("ashost=%s sysnr=%s user=%s password=%s client=%s lang=%s",
                                  this->ashost, this->sysnr, this->user, this->password, this->client, this->lang);
    }

    std::shared_ptr<RfcConnection> RfcAuthParams::Connect() 
    {
        RFC_ERROR_INFO error_info;
        RFC_CONNECTION_PARAMETER params[6];

        auto ashost = std2uc(this->ashost);
        params[0].name = cU("ashost");
        params[0].value = ashost.get();
	    
        auto sysnr = std2uc(this->sysnr);
        params[1].name = cU("sysnr");	
        params[1].value = sysnr.get();
	    
        auto user = std2uc(this->user);
        params[2].name = cU("user");	
        params[2].value = user.get();
	    
        auto password = std2uc(this->password);
        params[3].name = cU("passwd");	
        params[3].value = password.get();
	    
        auto client = std2uc(this->client);
        params[4].name = cU("client");	
        params[4].value = client.get();
	    
        auto lang = std2uc(this->lang);
        params[5].name = cU("lang");	
        params[5].value = lang.get(); 
        
        auto connection_handle = RfcOpenConnection(params, 6, &error_info);

        if (connection_handle == NULL) {
            throw IOException(StringUtil::Format("Error during SAP RFC logon: %s: %s",rfcrc2std(error_info.code), uc2std(error_info.message)));
        }
        return std::make_shared<RfcConnection>(connection_handle);
    }

    // RfcAuthParams ------------------------------------------------------------
    
    RfcConnection::RfcConnection(RFC_CONNECTION_HANDLE handle) : handle(handle)
    { }

    RfcConnection::~RfcConnection()
    {
        Close();
    }
    
    void RfcConnection::Close()
    {
        if (handle == NULL)
            return;
        
        RFC_RC rc = RFC_OK;
        RFC_ERROR_INFO error_info;

        rc = RfcCloseConnection(handle, &error_info);
        if (rc != RFC_OK) {
            throw IOException(StringUtil::Format("Error during SAP RFC connection closing: %s: %s",rfcrc2std(error_info.code), uc2std(error_info.message)));
        }
        handle = NULL;
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