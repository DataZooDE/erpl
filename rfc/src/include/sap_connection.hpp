#pragma once

#include "duckdb.hpp"
#include "sap_rfc_api.hpp"
#include "sap_secret.hpp"

namespace duckdb 
{

    // Process-wide count of RFC connections erpl has opened and closed.
    //
    // Exists so a test can assert that a scan releases what it acquired -- every open
    // connection is a session and a work-process reservation on the SAP system, and
    // wall-clock timing does not reveal one that is never released. Deliberately a
    // plain counter rather than a registry: it must be cheap enough to leave on in
    // release builds, and it is read by `PRAGMA sap_rfc_connection_stats`.
    struct RfcConnectionStats {
        static void NoteOpened();
        static void NoteClosed();
        static uint64_t Opened();
        static uint64_t Closed();
        // Opened - Closed. Non-zero after a query has finished means erpl is still
        // holding SAP sessions.
        static int64_t Live();
        static void Reset();
    };
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
		// Connection / logon
		string ashost;
		string sysnr;
		string user;
		string password;
		string client;
		string lang;
		// Load balancing via a message server
		string mshost;
		string msserv;
		string sysid;
		string group;
		// SNC (Secure Network Communications)
		string snc_mode;
		string snc_sso;
		string snc_qop;
		string snc_myname;
		string snc_partnername;
		string snc_lib;
		// Other credential material
		string mysapsso2;
		string x509cert;
		// Routing and miscellaneous
		string saprouter;
		string gwhost;
		string gwserv;
		string codepage;
		string trace;
		string dest;

		static RfcAuthParams FromContext(ClientContext &context, const string &secret_name = SAP_SECRET_DEFAULT_PATH);
		string ToString();
		std::shared_ptr<RfcConnection> Connect();

		// The (name, value) pairs handed to RfcOpenConnection, in table order and
		// with unset parameters omitted — the SDK treats an empty value as an
		// explicit empty setting for some parameters.
		vector<std::pair<string, string>> BuildConnectionParams() const;

		// Enumerated auth kind (basic|sso|snc) for telemetry — derived purely
		// from which credential fields are set. Returns no credential material.
		const char *TelemetryAuthKind() const;
	};

	// One entry per supported connection parameter. `name` is used verbatim as
	// the `sap_rfc` secret parameter, as the key in the secret map, and as the
	// RfcOpenConnection parameter name, so a parameter is added in exactly one
	// place. Keep this the single source of truth — the secret registration,
	// the secret -> RfcAuthParams conversion, ToString() and the SDK parameter
	// array are all derived from it.
	struct RfcAuthParamDefinition {
		const char *name;
		string RfcAuthParams::*member;
		// Whether the value must be hidden in any human-readable rendering.
		bool secret;
	};

	const vector<RfcAuthParamDefinition> &RfcAuthParamDefinitions();

	// Maps an RFC_RC failure code to an enumerated telemetry error_class
	// (auth_error|connection_failed|timeout|rfc_error). Code-controlled enum in,
	// enum out — never inspects or forwards the SAP error message.
	const char *RfcTelemetryErrorClass(RFC_RC code);
} // namespace duckdb