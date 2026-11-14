---
name: erpl-choose-interface
description: >
  Choosing the right erpl interface for an SAP object or task before writing SQL: transparent table or
  DDIC view → sap_read_table / ATTACH (erpl_rfc); BAPI or function module → sap_rfc_invoke; CDS view or
  entity → sap_ape_read_full / sap_ape_read_delta (erpl_ape) or the ODP context ABAP_CDS; extractor
  (DataSource, 2LIS_*, 0FI_*), SLT or BW InfoProvider replication → sap_odp_read_full / sap_odp_read_delta
  (erpl_odp); SAP BW query or cube with its business logic → sap_bics_begin / sap_bics_result (erpl_bics);
  BW modelling metadata and lineage → sap_bics_meta_* / sap_bics_lineage_*. Covers full vs delta
  (including deletes), discovery functions per interface, and editions. Use when a user names an SAP
  object (table, CDS view, extractor, query) and asks how to get it into DuckDB, or when deciding between
  ODP, APE and RFC for a recurring or incremental load.
user-invocable: false
---

# erpl — Which Interface for Which SAP Object

erpl reaches SAP through four sub-extensions. Several can read the same object, but they differ in what
they deliver: a one-off full read, a delta with deletes, or a BW query result with its business logic.
Pick by the object *and* by whether the load will recur.

## Decision table

| You have… | Use | Delta? | Discover with | Skill |
|---|---|---|---|---|
| Transparent table, DDIC database view (MARA, BKPF, SFLIGHT, Z-table) | `sap_read_table('T')` or `ATTACH '' AS sap (TYPE sap_rfc)` | No — full reads; filter with `WHERE` | `sap_show_tables`, `sap_describe_fields` | `erpl-rfc-tables` |
| BAPI / RFC-enabled function module | `sap_rfc_invoke('BAPI_…', …)` | — | `sap_rfc_show_function`, `sap_rfc_describe_function` | `erpl-rfc-functions` |
| CDS view or entity (`I_*`, `C_*`, `/DMO/*`, `Z*`), recurring or large load | `sap_ape_read_full('ENTITY')` | Yes, with deletes: `sap_ape_read_delta` (needs the CDC annotation) | `sap_ape_show`, `sap_ape_describe`, `sap_ape_preview` | `erpl-ape-cds` |
| CDS view, quick ad-hoc look | `sap_read_table('/DMO/R_BOOKING_D')` also works | No | `sap_describe_fields` | `erpl-rfc-tables` |
| Extraction-enabled CDS view via ODP | `sap_odp_read_full('ABAP_CDS', '<name from sap_odp_show>')` | Yes: `sap_odp_read_delta` (deletes only with CDC delta) | `sap_odp_show('ABAP_CDS')` | `erpl-odp-replication` |
| Extractor / DataSource (`2LIS_11_VAITM`, `0FI_GL_14`, …) | `sap_odp_read_full('SAPI', 'NAME')` | Yes, if `sap_odp_describe(…).supports_delta` | `sap_odp_show('SAPI')` | `erpl-odp-replication` |
| BW InfoProvider data (aDSO, CompositeProvider, InfoObject) as raw rows | `sap_odp_read_full('BW', 'NAME')` | Yes, for delta-capable providers | `sap_odp_show('BW')` | `erpl-odp-replication` |
| SLT replication | `sap_odp_read_delta('<SLT context>', 'TABLE', 'my_pipeline')` | Yes | `sap_odp_show_contexts()` lists the SLT context | `erpl-odp-replication` |
| BW **query** or cube *with* its business logic (variables, calculated key figures, hierarchies, BW authorizations) | `sap_bics_begin` → `sap_bics_rows`/`filter` → `sap_bics_result` | No — a query result is a snapshot | `sap_bics_show_queries`, `sap_bics_describe` | `erpl-bics-queries` |
| BW modelling metadata, data flow, lineage, impact analysis | `sap_bics_meta_*`, `sap_bics_lineage_*`, `sap_bics_query_lineage` | — | `sap_bics_meta_providers` | `erpl-bics-metadata` |

## How to tell what an object is

- **Is it a table?** `sap_show_tables(TABLENAME => 'NAME')` lists DDIC tables and views with their
  `class` (`TRANSP`, `VIEW`, …). CDS entities do not appear there.
- **Can `sap_read_table` read it?** `sap_describe_fields('NAME')` returns its fields if so (tables, DDIC
  views and CDS views); it fails with `NOT_FOUND` otherwise.
- **Is it a CDS entity?** `sap_ape_show(search => 'NAME%')` finds it. Released entities
  (`is_released = true`) are the stable choice for production.
- **Is it an ODP provider?** `sap_odp_show_contexts()` lists the contexts this system offers, then
  `sap_odp_show('<context>', search => '*NAME*')`. ODP names carry a suffix for their `semantics`
  (e.g. `…$P` master data/attributes), so copy the `technical_name` it returns.
- **Is it a BW query?** `sap_bics_show_queries(search => 'NAME')`; a cube or aDSO is listed by
  `sap_bics_show_cubes`.

## Choosing between overlapping options

| Question | Prefer | Because |
|---|---|---|
| CDS view: APE or ODP `ABAP_CDS`? | APE when the system has the ABAP Pipeline Engine and the view carries `@Analytics.dataExtraction.delta.changeDataCapture.automatic`; ODP when the view is exposed via ODP and APE is unavailable | APE delivers deletes as `/1DH/OPERATION = 'D'`; ODP on a byElement-delta CDS source cannot report physical deletes |
| Table or CDS view: `sap_read_table` or ODP/SLT/APE? | `sap_read_table` for occasional reads and small/medium tables; ODP/SLT for recurring incremental loads | `sap_read_table` has no change tracking; every run is a full (filtered) read |
| BW: BICS or ODP `BW`? | BICS when the answer must match what users see in the BW query; ODP when you want the provider's raw records | BICS executes the query (variables, restrictions, conditions); ODP bypasses query logic |
| BW query too large for one result? | Narrow it with `sap_bics_filter`, or read the underlying provider via ODP | BW cannot page a query result |
| Need a business object with logic (e.g. create/read a sales order with checks)? | `sap_rfc_invoke` on the BAPI | Tables bypass the application logic |

## Discover first, then read

```sql
-- What kinds of objects does this system offer?
SELECT * FROM sap_odp_show_contexts();
SELECT cds_name, description, is_released FROM sap_ape_show(search => '/DMO/R_BOOKING%') LIMIT 20;
SELECT * FROM sap_show_tables(TABLENAME => 'SFLIGHT');
SELECT technical_name, text, cube_name FROM sap_bics_show_queries(search => 'SALES');
```

## Editions

The Community Edition covers RFC table reads; BW access (BICS) and replication (ODP) are Enterprise
Edition capabilities, and CDS extraction via APE needs an SAP-side role that is set up per engagement
(`ape/docs/security.md`). See `erpl-setup` for licensing pointers.

See also: `erpl-setup` (install and connect), `erpl-rfc-tables`, `erpl-rfc-functions`,
`erpl-odp-replication`, `erpl-ape-cds`, `erpl-bics-queries`, `erpl-bics-metadata`, `erpl-pipelines`
(landing the data incrementally).
