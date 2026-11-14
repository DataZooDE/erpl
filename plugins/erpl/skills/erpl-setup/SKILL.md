---
name: erpl-setup
description: >
  Installing and connecting the erpl DuckDB extension suite to SAP: INSTALL erpl FROM get.erpl.io with
  unsigned extensions (CLI, Python, R, Node), what the erpl trampoline extracts (SAP NetWeaver RFC SDK
  libraries plus erpl_rfc, erpl_bics, erpl_odp, erpl_ape), CREATE SECRET of TYPE sap_rfc for direct,
  load-balanced, SAProuter, SNC/Kerberos, SSO2, X.509 and WebSocket RFC (S/4HANA Cloud) logons, PERSISTENT
  secrets, picking a secret with secret =>, the erpl_rfc_backend setting ('nwrfc' SDK vs 'proto'),
  PRAGMA sap_rfc_ping, sap_rfc_backend(), sap_rfc_authorizations(), editions and telemetry opt-out
  (erpl_telemetry_enabled, DATAZOO_DISABLE_TELEMETRY). Use when someone installs erpl, first connects
  DuckDB to an SAP system, gets a logon or "extension not found" error, or asks which secret fields to set.
user-invocable: false
---

# erpl — Setup & Connection Cheat Sheet

erpl is a DuckDB extension suite that reads SAP data with SQL over RFC. One umbrella extension, `erpl`,
installs everything: it embeds the SAP NetWeaver RFC SDK and four sub-extensions and extracts them on
first `LOAD`. Binaries are published to `http://get.erpl.io` per DuckDB release (currently DuckDB v1.5.6
on linux_amd64, linux_amd64_musl, osx_amd64, osx_arm64 and windows_amd64; CHANGELOG.md has the matrix).

| Sub-extension | What it reads | Skill |
|---|---|---|
| `erpl_rfc` | Tables, RFC/BAPI function modules, DDIC metadata, `ATTACH` catalog | `erpl-rfc-tables`, `erpl-rfc-functions` |
| `erpl_odp` | ODP providers (extractors, CDS, BW, SLT): full and delta | `erpl-odp-replication` |
| `erpl_ape` | CDS entities via the ABAP Pipeline Engine: full and delta incl. deletes | `erpl-ape-cds` |
| `erpl_bics` | SAP BW queries and cubes, hierarchies, BW metadata and lineage | `erpl-bics-queries`, `erpl-bics-metadata` |

Not sure which interface fits an SAP object? See `erpl-choose-interface`.

## Critical gotchas

1. **Unsigned extensions must be allowed.** The binaries are not signed by DuckDB Labs. Start the CLI
   with `duckdb -unsigned`, or pass `allow_unsigned_extensions` in the client config. Without it
   `INSTALL`/`LOAD` fails with a signature error.
2. **The DuckDB version must match a published build.** `INSTALL` resolves the binary for the client's
   exact DuckDB version and platform. A client on a version erpl was not built for gets an HTTP 404 /
   "extension not found". Check `SELECT version();` against the CHANGELOG matrix.
3. **`LOAD erpl`, not the sub-extensions.** The trampoline extracts the SDK libraries next to the
   extensions in `~/.duckdb/extensions/<duckdb-version>/<platform>/` and loads `erpl_rfc`, `erpl_bics`,
   `erpl_odp`, `erpl_ape` itself.
4. **Creating a secret does not connect.** `CREATE SECRET` only stores parameters; the first SAP call
   (e.g. `PRAGMA sap_rfc_ping`) is where a wrong host, password or authorization shows up.
5. **Several `sap_rfc` secrets in scope?** Name one with `secret => 'name'` (functions) or
   `secret='name'` (pragmas) — otherwise erpl uses the best match and you may hit the wrong system.
6. **The RFC backend is chosen once per process.** `SET erpl_rfc_backend = 'proto'` must come before
   the first SAP call; afterwards a different value is refused. erpl never falls back silently.
7. **`duckdb -c "…"` with several statements binds them together.** A `LOAD erpl` or `CREATE SECRET` in
   that string is not visible to the statements after it (`Pragma Function … does not exist`,
   `Secret '*' not found`). Pipe a script through stdin instead: `duckdb -unsigned < session.sql`.

## Install

```sql
INSTALL erpl FROM 'http://get.erpl.io';
LOAD erpl;
```

Clients other than the CLI enable unsigned extensions in the connection config:

```text
Python:  con = duckdb.connect(config={"allow_unsigned_extensions": "true"})
R:       con <- dbConnect(duckdb::duckdb(config = list(allow_unsigned_extensions = "true")))
Node:    const db = await DuckDBInstance.create(':memory:', { allow_unsigned_extensions: 'true' })
```

Then run the same `INSTALL`/`LOAD` statements through the client. A successful `LOAD` prints one
"extracted and saved" and one "installed and loaded" line per sub-extension. Check what got loaded:

```sql
-- check:offline
SELECT extension_name, loaded FROM duckdb_extensions() WHERE extension_name LIKE 'erpl%';
```

**Offline or pinned installs:** every GitHub release carries
`erpl-<duckdb-version>-extension-<os>-<arch>.tar.gz`; unpack and `INSTALL '/path/to/erpl.duckdb_extension'`.

## `CREATE SECRET … (TYPE sap_rfc, …)`

All values are strings. A secret is either **direct** (`ashost` + `sysnr`) or **load-balanced**
(`mshost` + `sysid` + `group`); both need `client` and `user`, and a password unless SNC or a ticket logs
on. The full field list is in `references/secret-fields.md`.

```sql
-- Direct logon to one application server
CREATE SECRET my_sap (
    TYPE sap_rfc,
    ASHOST 'sap.example.com', SYSNR '00', CLIENT '100',
    USER 'ERPL_USER', PASSWD 'change-me', LANG 'EN'
);
```

```sql
-- Load-balanced logon through the message server and a logon group
CREATE SECRET my_sap_lb (
    TYPE sap_rfc,
    MSHOST 'sapms.example.com', SYSID 'PRD', GROUP 'PUBLIC',
    CLIENT '100', USER 'ERPL_USER', PASSWD 'change-me'
);
```

```sql
-- Through a SAProuter (string from the Basis team)
CREATE SECRET my_sap_router (
    TYPE sap_rfc,
    ASHOST 'sap.internal', SYSNR '00', CLIENT '100',
    USER 'ERPL_USER', PASSWD 'change-me',
    SAPROUTER '/H/router.example.com/S/3299'
);
```

```sql
-- SNC / Kerberos single sign-on, no password
CREATE SECRET my_sap_snc (
    TYPE sap_rfc,
    ASHOST 'sap.example.com', SYSNR '00', CLIENT '100', USER 'ERPL_USER',
    SNC_MODE '1',
    SNC_PARTNERNAME 'p:CN=SAP/sap.example.com@EXAMPLE.LOCAL',
    SNC_LIB '/usr/lib/libgsskrb5.so'
);
```

```sql
-- S/4HANA Cloud (no gateway): WebSocket RFC over TLS with a client certificate
CREATE SECRET my_sap_cloud (
    TYPE sap_rfc,
    WSHOST 'my-tenant.s4hana.cloud', WSPORT '443', USE_TLS 'true',
    TLS_CLIENT_PSE '/etc/erpl/client.pse', ALIAS_USER 'ERPL_COMM',
    CLIENT '100', LANG 'EN'
);
```

- **`PERSISTENT`** (`CREATE PERSISTENT SECRET …`) writes the secret to `~/.duckdb/stored_secrets/` so it
  survives restarts — **unencrypted**, password included; redaction only affects what `duckdb_secrets()`
  displays. Remove it with `DROP PERSISTENT SECRET my_sap;`. Prefer temporary secrets on shared machines;
  for servers see `erpl-deploy`.
- **`dest`** reads every parameter from a destination in `sapnwrfc.ini` instead
  (`PRAGMA sap_rfc_set_ini_path('/etc/erpl')` points at the file).
- Passwords, SSO2 tickets and X.509 certificates are redacted in `duckdb_secrets()`.
- No direct network path to the SAP host? Tunnel it with the companion `erpl_tunnel` extension (skill
  `erpl-tunnel-import`; install with `/plugin marketplace add DataZooDE/erpl-tunnel` →
  `/plugin install erpl-tunnel@erpl-tunnel`).

## Verify the connection

```sql
PRAGMA sap_rfc_ping;                      -- 'PONG' or a logon error
PRAGMA sap_rfc_ping(secret='my_sap');     -- a specific secret
SELECT sap_rfc_backend();                 -- 'nwrfc' (SAP SDK) or 'proto'
SELECT sap_rfc_live_connections();        -- 0 between queries
```

For the whole check in one go — ping, backend, versions, required authorizations — run the
`/erpl:connect-check` command.

| Error text contains | Meaning | Fix |
|---|---|---|
| `RFC_COMMUNICATION_FAILURE`, `partner … not reached`, timeout | Host/port unreachable (sysnr `NN` → gateway port 33NN) or SAP not responding | Check network, SAProuter string, firewall; tunnel if needed |
| `RFC_LOGON_FAILURE`, `Name or password is incorrect` | Wrong user/password/client | Correct secret; repeated failures lock the user in SAP |
| `User is locked` | Too many failed logons | SAP admin unlocks in SU01 |
| `no RFC authorization` / `S_RFC` | Logged on, but the user may not call a function module | Give the Basis team the list from `sap_rfc_authorizations()` (or `/erpl:basis-handout`) |
| `Unknown parameter` in `CREATE SECRET` | Field name not accepted by `sap_rfc` | See `references/secret-fields.md` |

## Authorizations the SAP user needs

`sap_rfc_authorizations()` lists which RFC function modules each erpl function calls, so `S_RFC` can be
scoped to least privilege. It is static and needs no connection.

```sql
-- check:offline
SELECT DISTINCT rfc_function_module FROM sap_rfc_authorizations()
WHERE extension = 'erpl_rfc' AND rfc_function_module NOT IN ('<user-specified>', '<none>')
ORDER BY 1;
```

APE needs more than `S_RFC` (a role with the pipeline-engine objects); `sap_ape_check_authorizations()`
asks the SAP system per object and returns a `verdict` (see `erpl-ape-cds`). The full package for the
Basis team is produced by `/erpl:basis-handout` (background in `erpl-deploy`).

## RFC backend: SAP SDK or erpl-proto

| `erpl_rfc_backend` | Implementation | When |
|---|---|---|
| `'nwrfc'` (default) | SAP NetWeaver RFC SDK, bundled by the trampoline | Default; full SNC/CommonCryptoLib support (`tls_client_pse`) |
| `'proto'` | erpl-proto, a pure-Rust RFC implementation | Hosts where the SDK libraries cannot be loaded; wsRFC with PEM files (`ws_client_cert`/`ws_client_key`) |

```sql
-- check:offline
SET erpl_rfc_backend = 'proto';   -- before the first SAP call of the process
SELECT current_setting('erpl_rfc_backend');
```

The environment variable `ERPL_RFC_BACKEND` sets the same thing for processes you do not control.

## Editions and licence

erpl is source-available under the Business Source License 1.1 (see `LICENSE`). Reading SAP ERP tables is
covered by the Community Edition; BW (BICS) and replication (ODP) are Enterprise Edition capabilities —
contact https://erpl.io for licensing. Using erpl inside a commercial product or service needs a commercial
licence.

## Telemetry

On by default, anonymous, and never includes SAP hosts, users, table or function names, SQL or data
(TELEMETRY.md lists every field). Turn it off with either:

```sql
-- check:offline
SET erpl_telemetry_enabled = false;
```

```text
export DATAZOO_DISABLE_TELEMETRY=1
```

## Canonical first session

Save as `session.sql` and run `duckdb -unsigned < session.sql` (after the one-time `INSTALL` above):

```sql
-- check:skip-live (example host; the live check supplies its own secret)
LOAD erpl;
CREATE SECRET my_sap (
    TYPE sap_rfc,
    ASHOST 'sap.example.com', SYSNR '00', CLIENT '100',
    USER 'ERPL_USER', PASSWD 'change-me', LANG 'EN'
);
PRAGMA sap_rfc_ping;
SELECT * FROM sap_show_tables(TABLENAME => 'T00*') LIMIT 5;
```

The same session against an existing secret, as an assistant would run it to check access:

```sql
PRAGMA sap_rfc_ping;
SELECT * FROM sap_show_tables(TABLENAME => 'T00*') LIMIT 5;
SELECT sap_rfc_live_connections();
```

See also: `erpl-choose-interface` (which function for which SAP object), `erpl-deploy` (servers,
containers, secrets in production), `erpl-operate` (tracing and troubleshooting).
