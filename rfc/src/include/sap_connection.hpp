#pragma once

#include "duckdb.hpp"
#include "sapnwrfc.h"

namespace duckdb 
{
	typedef struct RfcConnectionAttributes
	{
		std::string destination;
		std::string host;
		std::string partner_host;
		std::string sys_number;
		std::string client;
		std::string user;
		std::string language;
		std::string trace;
		std::string iso_language;
		std::string codepage;
		std::string partner_codepage;
		std::string rfc_role;
		std::string type;
		std::string partner_type;
		std::string release;
		std::string partner_release;
		std::string kernel_release;
		std::string cpic_conv_id;
		std::string progname;
		std::string partner_bytes_per_char;
		std::string partner_system_codepage;
		std::string partner_ip;
		std::string partner_ipv6;
	} RfcConnectionAttributes;

    /**
     * @brief A wrapper class for the RFC_CONNECTION_HANDLE structure.
     * 
     * This class wraps the RFC_CONNECTION_HANDLE structure used in the SAP NWRFC SDK.
     * It manages the lifetime of the connection handle and provides convenient
     * member functions for accessing the connection data.
     */
    typedef struct RfcConnection
    {
        RFC_CONNECTION_HANDLE handle;

        RfcConnection(RFC_CONNECTION_HANDLE handle);
        ~RfcConnection();

		void Close();
        void Ping();
		RfcConnectionAttributes ConnectionAttributes();
    } RfcConnection;

    /** 
     * @brief A class for setting extension variables from environment variables.
    */
    struct RfcEnvironmentCredentialsProvider 
	{
		static constexpr const char *ASHOST_ENV_VAR = "SAP_ASHOST";
		static constexpr const char *SYSNR_ENV_VAR = "SAP_SYSNR";
		static constexpr const char *USER_ENV_VAR = "SAP_USER";
		static constexpr const char *PASSWORD_ENV_VAR = "SAP_PASSWORD";
		static constexpr const char *CLIENT_ENV_VAR = "SAP_CLIENT";
		static constexpr const char *LANG_ENV_VAR = "SAP_LANG";

		explicit RfcEnvironmentCredentialsProvider(DBConfig &config) : config(config) {};

		DBConfig &config;

		void SetExtensionOptionValue(string key, const char *env_var);
		void SetAll();
	};

	struct RfcAuthParams {
		string ashost;
		string sysnr;
		string user;
		string password;
		string client;
		string lang;

		static RfcAuthParams FromContext(ClientContext &context);
		string ToString();
		std::shared_ptr<RfcConnection> Connect();
	};
} // namespace duckdb