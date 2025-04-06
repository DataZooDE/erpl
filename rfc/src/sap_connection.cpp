#include "duckdb.hpp"
#include "sap_connection.hpp"
#include "sap_type_conversion.hpp"
#include "sap_secret.hpp"
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

        // Lookup the secret
        auto secret_match = secret_manager.LookupSecret(transaction, secret_name, "sap_rfc");
        if (! secret_match.HasMatch()) {

            throw InvalidInputException("Secret '%s' not found", secret_name);
        }

        // Cast to SapSecret
        const auto &duck_secret = dynamic_cast<const KeyValueSecret &>(secret_match.GetSecret());
        return ConvertSecretToAuthParams(duck_secret);
    }

    std::string RfcAuthParams::ToString() {
        std::stringstream ss;
        ss << "ashost=" << ashost;
        if (!sysnr.empty()) ss << " sysnr=" << sysnr;
        if (!user.empty()) ss << " user=" << user;
        if (!password.empty()) ss << " password=***";
        if (!client.empty()) ss << " client=" << client;
        if (!lang.empty()) ss << " lang=" << lang;
        if (!mshost.empty()) ss << " mshost=" << mshost;
        if (!msserv.empty()) ss << " msserv=" << msserv;
        if (!sysid.empty()) ss << " sysid=" << sysid;
        if (!group.empty()) ss << " group=" << group;
        if (!snc_qop.empty()) ss << " snc_qop=" << snc_qop;
        if (!snc_myname.empty()) ss << " snc_myname=" << snc_myname;
        if (!snc_partnername.empty()) ss << " snc_partnername=" << snc_partnername;
        if (!snc_lib.empty()) ss << " snc_lib=" << snc_lib;
        if (!mysapsso2.empty()) ss << " mysapsso2=" << mysapsso2;
        return ss.str();
    }

    std::shared_ptr<RfcConnection> RfcAuthParams::Connect() 
    {
        RFC_ERROR_INFO error_info;
        RFC_CONNECTION_PARAMETER params[15];
        size_t param_count = 0; 

        auto ashost = std2uc(this->ashost);
        if (strlenR(ashost.get()) > 0) {
            params[param_count].name = cU("ashost");
            params[param_count].value = ashost.get();
            param_count++;
        }

        auto sysnr = std2uc(this->sysnr);
        if (strlenR(sysnr.get()) > 0) {
            params[param_count].name = cU("sysnr");	
            params[param_count].value = sysnr.get();
            param_count++;
        }

        auto user = std2uc(this->user);
        if (strlenR(user.get()) > 0) {
            params[param_count].name = cU("user");	
            params[param_count].value = user.get();
            param_count++;
        }

        auto password = std2uc(this->password);
        if (strlenR(password.get()) > 0) {
            params[param_count].name = cU("passwd");	
            params[param_count].value = password.get();
            param_count++;
        }

        auto client = std2uc(this->client);
        if (strlenR(client.get()) > 0) {
            params[param_count].name = cU("client");	
            params[param_count].value = client.get();
            param_count++;
        }

        auto lang = std2uc(this->lang);
        if (strlenR(lang.get()) > 0) {
            params[param_count].name = cU("lang");	
            params[param_count].value = lang.get(); 
            param_count++;
        }

        auto mshost = std2uc(this->mshost);
        if (strlenR(mshost.get()) > 0) {
            params[param_count].name = cU("mshost");	
            params[param_count].value = mshost.get();
            param_count++;
        }

        auto msserv = std2uc(this->msserv);
        if (strlenR(msserv.get()) > 0) {
            params[param_count].name = cU("msserv");	
            params[param_count].value = msserv.get();
            param_count++;
        }

        auto sysid = std2uc(this->sysid);
        if (strlenR(sysid.get()) > 0) {
            params[param_count].name = cU("sysid");	
            params[param_count].value = sysid.get();
            param_count++;
        }   

        auto group = std2uc(this->group);
        if (strlenR(group.get()) > 0) {
            params[param_count].name = cU("group");	
            params[param_count].value = group.get();
            param_count++;
        }

        auto snc_qop = std2uc(this->snc_qop);
        if (strlenR(snc_qop.get()) > 0) {
            params[param_count].name = cU("snc_qop");	
            params[param_count].value = snc_qop.get();
            param_count++;
        }
        
        auto snc_myname = std2uc(this->snc_myname);
        if (strlenR(snc_myname.get()) > 0) {
            params[param_count].name = cU("snc_myname");	
            params[param_count].value = snc_myname.get();
            param_count++;
        }
        
        auto snc_partnername = std2uc(this->snc_partnername);
        if (strlenR(snc_partnername.get()) > 0) {
            params[param_count].name = cU("snc_partnername");	
            params[param_count].value = snc_partnername.get();
            param_count++;
        }
        
        auto snc_lib = std2uc(this->snc_lib);
        if (strlenR(snc_lib.get()) > 0) {
            params[param_count].name = cU("snc_lib");	
            params[param_count].value = snc_lib.get();
            param_count++;
        }
        
        auto mysapsso2 = std2uc(this->mysapsso2);
        if (strlenR(mysapsso2.get()) > 0) {
            params[param_count].name = cU("mysapsso2");	
            params[param_count].value = mysapsso2.get();
            param_count++;
        }
        
        auto connection_handle = RfcOpenConnection(params, param_count, &error_info);

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