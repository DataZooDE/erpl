#pragma once

#include "duckdb.hpp"
#include <atomic>
#include <memory>
#include <mutex>
#include "sap_rfc_api.hpp"
#include "sap_secret.hpp"

namespace duckdb 
{
    duckdb::CatalogTransaction SapSystemTransaction(ClientContext &context);

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

    // The SAP session behind a shared connection -- the SAP LUW session a DuckDB transaction
    // holds (sap_luw.hpp) -- and every non-owning view of it. One lock serialises the
    // round-trips (an RFC handle must not be used by two threads at once) and lets the owner
    // close the session without racing a call in flight; once it is closed, `handle` is
    // NULL and every view's next call fails cleanly instead of handing the SDK a dead
    // handle.
    struct RfcSessionCore {
        std::mutex lock;
        RFC_CONNECTION_HANDLE handle = NULL;
        // A round-trip failed in a way that ends an RFC session (communication failure,
        // short dump, MESSAGE-raised failure, cancel, closed or invalid handle): the LUW is
        // gone on the SAP side.
        std::atomic<bool> lost {false};
        // The return code that ended it, to tell "nothing was sent" (an invalid or closed
        // handle) from "the reply was lost" (a communication failure).
        std::atomic<int> lost_rc {0};
    };

    // The error a call on a lost SAP session gets.
    extern const char *const kRfcSessionLostMessage;

    // Held for the duration of one SDK call that takes a connection handle. For a shared
    // session it holds the session's lock; for an ordinary connection it is free.
    class RfcCallGuard {
    public:
        RfcCallGuard(RFC_CONNECTION_HANDLE handle, std::shared_ptr<RfcSessionCore> core,
                     std::unique_lock<std::mutex> lock);
        RFC_CONNECTION_HANDLE Handle() const { return handle; }
        // Report the call's return code: one that ends the RFC session marks a shared
        // session lost (see RfcSessionCore::lost).
        void Note(RFC_RC rc);

    private:
        RFC_CONNECTION_HANDLE handle;
        std::shared_ptr<RfcSessionCore> core;
        std::unique_lock<std::mutex> lock;
    };

    // Owns the handle of an ordinary connection. A connection that is shared (Share())
    // keeps its handle in an RfcSessionCore that its non-owning views (Borrow()) see too.
    typedef struct RfcConnection
    {
        // The handle as this object last knew it. Non-NULL means "this object has not
        // been closed"; SDK calls must take the live handle from CallGuard().
        RFC_CONNECTION_HANDLE handle;
        // False for a view onto a session somebody else owns. Close() on a view only
        // forgets the session; the owner closes it.
        bool owns_handle = true;
        // Set for shared sessions and their views.
        std::shared_ptr<RfcSessionCore> core;

        RfcConnection(RFC_CONNECTION_HANDLE handle);
        RfcConnection(std::shared_ptr<RfcSessionCore> core, bool owns_handle);
        ~RfcConnection();

        // Make this (owning) connection shareable: move its handle into a session core.
        void Share();
        // A non-owning view of this shared connection.
        std::shared_ptr<RfcConnection> Borrow();
        // Lock (for a shared session) and return the live handle for one SDK call.
        // Throws if the shared session has been closed -- the SAP transaction a statement
        // was bound in has ended.
        RfcCallGuard CallGuard();
        bool IsLost() const;
        // Forget the handle without closing it (tests with fake handles only).
        void Abandon();

		void Close();
        void Ping();
		RfcConnectionAttributes ConnectionAttributes();
    } RfcConnection;

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
		// WebSocket RFC (wsRFC). SAP's Public Cloud has no gateway to dial, so the
		// only transport there is wsRFC over TLS: a host and port rather than
		// ashost/sysnr, and a client certificate rather than a password. The trial
		// systems reach it too, on the ICM's wss port.
		string wshost;
		string wsport;
		string use_tls;
		// The SDK authenticates wsRFC from a SAP PSE; alias_user names the
		// communication user a cloud tenant maps the certificate to.
		string tls_client_pse;
		string tls_trust_all;
		string alias_user;
		// The erpl-proto backend has no CommonCryptoLib and therefore no PSE: it
		// takes the certificate and key as files (DER or PEM). Both spellings exist
		// because which one applies depends on erpl_rfc_backend.
		string ws_client_cert;
		string ws_client_key;
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