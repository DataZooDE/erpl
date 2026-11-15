---
description: Check an erpl SAP connection end to end — versions, backend, logon, required authorizations — and say what to fix
argument-hint: "[secret name] [CDS entity to check APE access for]"
---

Check the erpl connection to SAP: $ARGUMENTS

Use the `erpl-setup` skill (secrets, errors) and `erpl-deploy` (authorizations).

**How to run SQL.** Use the DuckDB CLI the user works with (ask if unclear) with `-unsigned`, and feed a
script through stdin (`duckdb -unsigned [db] <<'SQL' … SQL`). Never put several statements into one
`duckdb -c` string: a `LOAD` or `CREATE SECRET` there is invisible to the statements after it. Start every
script with `LOAD erpl;` (skip it for a statically linked development build). Never print or log passwords;
if no secret exists, ask the user to create one themselves (`erpl-setup`).

1. Environment, without SAP:
   `SELECT version();`, `SELECT extension_name, extension_version, loaded FROM duckdb_extensions() WHERE
   extension_name LIKE 'erpl%';`, `SELECT sap_rfc_backend();`, and the secrets in scope:
   `SELECT name, persistent, storage FROM duckdb_secrets() WHERE type = 'sap_rfc';`.
   More than one and no name given: ask which one.
2. Logon: `PRAGMA sap_rfc_ping(secret = '<name>');` — map an error to its cause with the table in
   `erpl-setup` (communication failure, logon failure, locked user).
3. Leak check: `SELECT sap_rfc_live_connections();` must be 0 afterwards.
4. RFC authorizations erpl needs: `SELECT extension, count(DISTINCT rfc_function_module) FROM
   sap_rfc_authorizations() GROUP BY ALL;` — a quick read confirms the common ones:
   `SELECT count(*) FROM sap_show_tables(TABLENAME => 'T000', secret => '<name>');`.
5. If APE matters (or an entity was given): `PRAGMA sap_ape_ping(secret = '<name>');` and
   `SELECT auth_object, verdict, note FROM sap_ape_check_authorizations('<entity>', secret => '<name>')
   WHERE verdict <> 'ok';`. Read `verdict`, not `present`.
6. Report a table — check, result, next step — and stop at the first blocking failure with its fix.
   For missing authorizations offer `/erpl:basis-handout`.
