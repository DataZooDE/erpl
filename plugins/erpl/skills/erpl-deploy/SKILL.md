---
name: erpl-deploy
description: >
  Running erpl in production: pinning DuckDB and erpl versions, installing on servers, containers and CI
  (INSTALL erpl FROM get.erpl.io or an offline release archive, the -unsigned / allow_unsigned_extensions
  setting, the extension directory the trampoline writes to), Python/Node/R hosts, choosing the RFC backend
  (erpl_rfc_backend nwrfc vs proto, ERPL_RFC_BACKEND), handling secrets on servers (persistent secret
  directory, secrets from the environment, SNC and certificates), the SAP-side package for the Basis team
  (dedicated RFC user, S_RFC scoped with sap_rfc_authorizations(), the APE role checked with
  sap_ape_check_authorizations(), WebSocket RFC/UCON), network paths (SAProuter, erpl_tunnel), and
  telemetry policy (DATAZOO_DISABLE_TELEMETRY). Use when moving an erpl workload from a laptop to a server,
  container, scheduler or CI job, or when preparing the SAP user and authorizations for go-live.
user-invocable: false
---

# erpl — Deployment Cheat Sheet

## Critical gotchas

1. **Pin the DuckDB version.** erpl binaries are built per DuckDB release; a client upgrade without a
   matching erpl build makes `INSTALL`/`LOAD` fail. Upgrade DuckDB and erpl together.
2. **Install once at build time, not at run time.** `INSTALL` downloads from get.erpl.io; production jobs
   should only `LOAD`. Bake the extension into the image or install it in a provisioning step.
3. **The trampoline writes into the extension directory** (`~/.duckdb/extensions/<duckdb>/<platform>/`, or
   `SET extension_directory`): the first `LOAD` extracts the SAP SDK libraries and sub-extensions there, so
   that directory must be writable by the service user at least once.
4. **A dedicated SAP user** (type *System* or *Communication*), not a person's dialog user — repeated failed
   logons from a job lock it for everyone.
5. **Persistent secrets are stored unencrypted** in the secret directory. On servers either restrict that
   directory to the service user or create the secret per run from the environment / a secret manager.
6. **Check with Basis before parallel extraction** against production (`threads`, `partitions`, several
   processes): every RFC connection holds a work process.

## Installing

```sql
-- check:skip-live (network install)
INSTALL erpl FROM 'http://get.erpl.io';
LOAD erpl;
SELECT extension_name, extension_version FROM duckdb_extensions() WHERE extension_name LIKE 'erpl%';
```

```text
# Container (sketch): pin DuckDB, install erpl at build time
FROM python:3.12-slim
RUN pip install duckdb==1.5.6
RUN python -c "import duckdb; c = duckdb.connect(config={'allow_unsigned_extensions': 'true'}); \
               c.execute(\"INSTALL erpl FROM 'http://get.erpl.io'\"); c.execute('LOAD erpl')"
ENV DATAZOO_DISABLE_TELEMETRY=1
```

Air-gapped: download `erpl-<duckdb-version>-extension-<os>-<arch>.tar.gz` from the GitHub release, unpack,
`INSTALL '/opt/erpl/erpl.duckdb_extension';`. Python, Node and R hosts need `allow_unsigned_extensions` in
the connection config (see `erpl-setup`).

## RFC backend

| | `nwrfc` (default) | `proto` |
|---|---|---|
| Implementation | SAP NetWeaver RFC SDK, bundled | erpl-proto, pure Rust |
| Choose | default | `SET erpl_rfc_backend = 'proto';` before the first SAP call, or `ERPL_RFC_BACKEND=proto` |
| wsRFC client certificate | PSE (`tls_client_pse`) | PEM/DER files (`ws_client_cert`, `ws_client_key`) |
| SNC | yes (with an SNC library) | see the erpl-proto docs |

Verify with `SELECT sap_rfc_backend();`. The choice is fixed per process; there is no silent fallback.

## Secrets on servers

```sql
-- check:skip-live (illustrates a per-run secret from the environment)
CREATE SECRET prod_sap (
    TYPE sap_rfc,
    ASHOST getenv('SAP_ASHOST'), SYSNR getenv('SAP_SYSNR'), CLIENT getenv('SAP_CLIENT'),
    USER getenv('SAP_USER'), PASSWD getenv('SAP_PASSWORD'), LANG 'EN'
);
```

`getenv` is available in the DuckDB CLI; in Python/Node pass the values as parameters from your secret
store instead. Alternatives: `CREATE PERSISTENT SECRET` in a secret directory only the service user can
read (`SET secret_directory = '/etc/erpl/secrets'`), SNC/Kerberos (no password at all), or X.509/SSO2
tickets. Always name the secret in production SQL (`secret => 'prod_sap'`).

## The SAP-side package (Basis)

| Need | Source of truth |
|---|---|
| `S_RFC` function groups/modules erpl calls | `SELECT DISTINCT rfc_function_module FROM sap_rfc_authorizations() WHERE …` (static, no connection) |
| APE authorization objects and whether the user has them | `sap_ape_check_authorizations('<entity>')` — read `verdict` |
| Table reads | `S_TABU_DIS`/`S_TABU_NAM` for the tables read with `sap_read_table` |
| BW data | BW analysis authorizations of the RFC user (BICS returns layout without data otherwise) |
| ODP | Authorizations for the ODP context, e.g. `S_RO_OSOA` for DataSources |
| S/4HANA Cloud | Communication arrangement + wsRFC; UCON may need to release the RFC function modules |

`/erpl:basis-handout` turns this into a document for the Basis team from the functions you actually use.

## Network

| Situation | Approach |
|---|---|
| Direct reach to the application server (port 33NN) | `ashost` + `sysnr` |
| Through a SAProuter | `saprouter` field in the secret |
| Behind a bastion, VPN or mesh | the `erpl_tunnel` extension (skill `erpl-tunnel-import`; `/plugin marketplace add DataZooDE/erpl-tunnel` → `/plugin install erpl-tunnel@erpl-tunnel`) |
| S/4HANA Cloud | wsRFC (`wshost`, `wsport` 443) |

## Telemetry

Anonymous usage telemetry is on by default (TELEMETRY.md lists every field; no SAP names, data or SQL). For
production policy set `DATAZOO_DISABLE_TELEMETRY=1` in the service environment or
`SET erpl_telemetry_enabled = false;` in every session.

## Go-live checklist

1. DuckDB and erpl versions pinned; `LOAD erpl` works as the service user without network.
2. Dedicated SAP user; `PRAGMA sap_rfc_ping(secret = 'prod_sap')` returns PONG.
3. `S_RFC` scoped from `sap_rfc_authorizations()`; APE `verdict` all `ok` where used.
4. Persistent database for pipelines (`erpl-pipelines`), backups for it and for `erpl_ape.delta_spill`.
5. Fixed `subscriber_process` names documented; one scheduler entry per subscriber.
6. Monitoring: `sap_rfc_live_connections()` returns 0 after jobs; logs kept (`erpl-operate`).
7. Parallelism agreed with Basis (`erpl-performance`).

See also: `erpl-setup`, `erpl-pipelines`, `erpl-operate`, `erpl-performance`. To serve landed data as an
API, the flapi plugin (`flapi-deploy`, `flapi-rest`).
