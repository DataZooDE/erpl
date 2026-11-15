---
description: Write the SAP Basis handout for an erpl deployment — RFC user, S_RFC scope, APE role objects, network — from what will actually be used
argument-hint: "[interfaces or functions to be used, e.g. 'sap_read_table, ODP, APE on I_PRODUCT']"
---

Write a Basis handout for this erpl use: $ARGUMENTS

Use the `erpl-deploy` skill (SAP-side package) and `erpl-setup` (secret types, network).

**How to run SQL.** DuckDB CLI with `-unsigned`, script through stdin, `LOAD erpl;` first. Step 1 needs no
SAP connection.

1. Pin down which erpl functions will be used (ask if the argument is vague). Then derive the RFC modules
   with `SELECT duckdb_function, rfc_function_module, invocation, purpose FROM sap_rfc_authorizations()
   WHERE duckdb_function IN (…) ORDER BY ALL;` — static, no connection needed. Modules shown as
   `<user-specified>` are the BAPIs the user will call with `sap_rfc_invoke`; list them explicitly.
2. If APE is used and a connection exists, get the authorization objects and their current state from
   `SELECT auth_object, fields, checked_values, must_be_granted, verdict, purpose FROM
   sap_ape_check_authorizations('<entity>');`. Explain that `S_DHAPEOP` must *not* be granted.
3. Write `erpl-basis-handout.md` in the current directory with these sections: purpose and scope
   (read-only extraction); the technical user (type System/Communication, logon language, client); `S_RFC`
   (function groups and modules from step 1, `ACTVT 16`); table authorizations for tables read with
   `sap_read_table` (`S_TABU_NAM`); BW analysis authorizations if BICS is used; the APE objects from step 2;
   ODP/ODQ access if ODP is used; network (application server port 33NN, message server, SAProuter, or
   wsRFC/UCON for S/4HANA Cloud); expected load (parallel connections) and how to verify
   (`PRAGMA sap_rfc_ping`, `sap_ape_check_authorizations`).
4. Mark every item as *derived from erpl* or *to be confirmed by Basis*. Do not invent authorization
   objects or field values beyond what the two functions return; say where a value is the customer's choice.
