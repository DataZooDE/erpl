---
name: erpl-bics-metadata
description: >
  Exploring SAP BW models, data flows and lineage with erpl_bics metadata functions: InfoProviders and their
  fields (sap_bics_meta_providers, sap_bics_meta_provider_fields), DataSources (sap_bics_meta_datasources,
  sap_bics_meta_datasource_fields), transformations and rules (sap_bics_meta_transformations,
  sap_bics_meta_transform_fields), CompositeProviders (sap_bics_meta_hcpr_components,
  sap_bics_meta_hcpr_mapping), queries, their elements, usage and runtime statistics (sap_bics_meta_queries,
  sap_bics_meta_query_elements, sap_bics_meta_query_usage, sap_bics_meta_query_stats), InfoObjects
  (sap_bics_meta_infoobjects, sap_bics_describe_infoobject), the cross reference sap_bics_meta_objxref, and
  lineage / impact analysis (sap_bics_lineage_edges, sap_bics_lineage_graph_json, sap_bics_lineage_trace,
  sap_bics_query_lineage). Use when documenting or auditing a BW system, answering "where does this key
  figure come from" or "what breaks if I change this InfoObject/DataSource", finding unused queries, or
  preparing a BW migration.
user-invocable: false
---

# erpl — BW Metadata, Lineage & Impact Analysis Cheat Sheet

These functions read BW's repository tables (over `RFC_READ_TABLE`) and return them as tables, so a whole
BW landscape can be queried, joined and versioned with SQL. Nothing is inferred: every row comes from
repository data.

## Critical gotchas

1. **Read-only repository access**: this needs `S_RFC` for `RFC_READ_TABLE` plus table authorizations
   (`S_TABU_*`) for the RS* tables — not BW analysis authorizations.
2. **CompositeProvider field mappings are not available.** They live in the CompositeProvider's XML
   definition, which `RFC_READ_TABLE` cannot read: `sap_bics_meta_hcpr_mapping.source_field` is always
   empty, and HCPR "fields" are the InfoObjects it requires.
3. **Object types a release lacks are skipped** with a WARN trace (e.g. no aDSO tables on an old BW): an
   empty result can mean "type does not exist here". A field missing in the DDIC is an error instead.
4. **Texts follow the secret's logon language**, falling back to English.
5. **Unfiltered calls read whole repository tables.** `sap_bics_meta_query_stats()` without a query, or
   `sap_bics_lineage_edges()` without `scope`, can be large on a production BW — scope them.
6. **Materialise before exploring**: every call is a round-trip; `CREATE TABLE … AS` once, then query
   locally.

## Inventory

```sql
SELECT provider_type, count(*) FROM sap_bics_meta_providers() GROUP BY ALL;
SELECT provider, provider_type, txtlg, changed_on FROM sap_bics_meta_providers(type => 'CUBE');
SELECT field_name, infoobject, is_key, data_type, text FROM sap_bics_meta_provider_fields('0D_NW_C01');
SELECT iobjnm, iobjtp, datatp, txtlg FROM sap_bics_meta_infoobjects(iobjnm => '0D_NW_*');
SELECT * FROM sap_bics_describe_infoobject('0D_NW_CNTRY');
```

| Function | Rows |
|---|---|
| `sap_bics_meta_providers([type])` | InfoProviders; `type` `CUBE`, `ADSO`, `HCPR`, `ODSO` |
| `sap_bics_meta_provider_fields(provider [, provider_type])` | Fields with InfoObject, key flag, type, text |
| `sap_bics_meta_datasources([appcomp])` / `sap_bics_meta_datasource_fields(ds)` | DataSources and their fields |
| `sap_bics_meta_transformations([active_only])` / `sap_bics_meta_transform_fields(transid)` | Transformations and their rules |
| `sap_bics_meta_hcpr_components(hcpr)` / `sap_bics_meta_hcpr_mapping([hcpr])` | CompositeProvider parts |
| `sap_bics_meta_queries()` / `sap_bics_meta_query_usage(q)` / `sap_bics_meta_query_elements(q)` | Queries, their provider, their elements (restricted/calculated key figures with `selection_info`, `formula`) |
| `sap_bics_meta_query_stats([query_name, from_date, to_date])` | Executions, runtime and users per query and day (`YYYYMMDD` dates) |
| `sap_bics_meta_objxref([tlogo, objnm, tlogo_dep])` | Raw BW object cross reference |

## Query anatomy

```sql
SELECT element_type, element_name, iobjnm, description, selection_info, formula
FROM sap_bics_meta_query_elements('0D_FC_NW_C01_Q0001')
ORDER BY element_type;
```

`element_type`: `CHA` characteristic, `KYF` plain key figure, `SEL` restricted key figure/selection, `CAL`
calculated key figure/formula, else BW's own type (`STR`, `VAR`, …).

## Lineage

```sql
-- Edges around one object (transformations in/out, queries on it)
SELECT edge_type, src_kind, src_name, src_field, tgt_kind, tgt_name, tgt_field
FROM sap_bics_lineage_edges(scope => '0D_NW_C01');

-- Everything upstream of a query: provider, InfoObjects, transformations, sources, source systems
SELECT level, object_type, object_name, field_name, edge_type, path
FROM sap_bics_query_lineage('0D_FC_NW_C01_Q0001') ORDER BY level;

-- Downstream impact of an object or one field
SELECT hop, target_object, target_field, edge_type, path
FROM sap_bics_lineage_trace(source_object => '0D_NW_C01');

-- The object graph as JSON {nodes, edges} for a diagram tool
SELECT lineage_graph_json FROM sap_bics_lineage_graph_json(scope => '0D_NW_C01');
```

## Typical analyses

```sql
-- Queries nobody ran in the statistics window (statistics must be switched on in BW)
SELECT q.* FROM sap_bics_meta_queries() q
ANTI JOIN (SELECT DISTINCT query_name FROM sap_bics_meta_query_stats(from_date => '20260101')) s
ON q.compuid = s.query_name;
```

The column names of `sap_bics_meta_queries` depend on RSRREPDIR — `DESCRIBE SELECT * FROM
sap_bics_meta_queries()` before joining. Also useful:

| Question | Approach |
|---|---|
| Which queries use InfoObject X? | `sap_bics_lineage_trace(source_object => '<provider>', source_field => 'X')` filtered to `edge_type = 'QUERY'` |
| What feeds this aDSO? | `sap_bics_lineage_edges(scope => '<adso>')` where `tgt_name = '<adso>'` |
| Slowest queries | `sap_bics_meta_query_stats(...)` ordered by `avg_duration_sec` |
| Document a model | Materialise providers, fields, transformations; render with the visualization plugin |

To *change* BW models (create, activate, transport), use the erpl-adt plugin (`erpl-adt-bw`;
`/plugin marketplace add DataZooDE/erpl-adt` → `/plugin install erpl-adt@erpl-adt`). For diagrams or
dashboards over these tables, the anofox-visualization plugin (`anofox-visualization-charts`).

See also: `erpl-bics-queries` (executing queries), `erpl-choose-interface`.
