# ERPL Extension Suite — API Reference

> Complete function reference for SAP data integration in DuckDB

**DuckDB Version:** >= v1.2.0
**Extensions:** erpl_rfc, erpl_bics, erpl_odp, erpl_tunnel

---

## Overview

The ERPL extension suite brings SAP data integration directly into DuckDB. It enables analysts, data engineers, and scientists to query SAP ERP tables, SAP BW cubes, and replicate data using familiar SQL — no middleware, no data movement.

**Key Benefits:**
- **SQL-native**: All operations expressed as SQL functions, pragmas, and secrets
- **Zero middleware**: Connect directly to SAP via RFC and BICS protocols
- **Parallel extraction**: Multi-threaded reads for large tables
- **Virtual catalog**: ATTACH SAP systems as DuckDB databases

| Extension | Purpose |
|-----------|---------|
| **erpl_rfc** | Core SAP RFC connectivity — table reads, function calls, metadata |
| **erpl_bics** | SAP BW queries via BICS — cubes, hierarchies, lineage |
| **erpl_odp** | SAP ODP data extraction and replication |
| **erpl_tunnel** | SSH tunneling for secure SAP connections |

---

## Quick Start

```sql
-- 1. Install and load
INSTALL 'erpl' FROM 'http://get.erpl.io';
LOAD 'erpl';

-- 2. Create SAP connection secret
CREATE SECRET my_sap (
    TYPE sap_rfc,
    ASHOST 'sap-server.example.com',
    SYSNR '00',
    CLIENT '100',
    USER 'DEVELOPER',
    PASSWD 'secret',
    LANG 'EN'
);

-- 3. Read a table
SELECT * FROM sap_read_table('SFLIGHT');

-- 4. Attach SAP as a virtual database
ATTACH '' AS sap (TYPE sap_rfc);
SELECT * FROM sap."SFLIGHT" WHERE CARRID = 'LH';

-- 5. Query SAP BW
SELECT * FROM sap_bics_show_cubes();
```

---

## Quick Reference

| Function | Purpose | Example |
|----------|---------|---------|
| `sap_read_table` | Read SAP table data | `SELECT * FROM sap_read_table('SFLIGHT')` |
| `sap_rfc_invoke` | Call any RFC function | `SELECT * FROM sap_rfc_invoke('STFC_CONNECTION', {'REQUTEXT': 'Hi'})` |
| `sap_show_tables` | Search SAP tables | `SELECT * FROM sap_show_tables(TABLENAME='*FLIGHT*')` |
| `sap_describe_fields` | Get table field metadata | `SELECT * FROM sap_describe_fields('SFLIGHT')` |
| `sap_bics_show_cubes` | List BW cubes | `SELECT * FROM sap_bics_show_cubes()` |
| `sap_bics_hierarchy` | Extract BW hierarchy | `SELECT * FROM sap_bics_hierarchy('MY_HIER')` |
| `sap_odp_read_full` | Extract ODP data | `SELECT * FROM sap_odp_read_full('BW', 'MY_ODP')` |
| `ATTACH` | Mount SAP as database | `ATTACH '' AS sap (TYPE sap_rfc)` |

---

## erpl_rfc — SAP RFC Connectivity

### Data Access

#### `sap_read_table(table_name [, THREADS, COLUMNS, FILTER, ...])`

Read data from an SAP table or CDS view. Supports projection pushdown, filter pushdown, and parallel reads.

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `table_name` | VARCHAR | *required* | SAP table or CDS view name |
| `THREADS` | UINTEGER | 0 | Number of parallel read threads |
| `COLUMNS` | LIST(VARCHAR) | all | Columns to retrieve |
| `FILTER` | VARCHAR | — | SAP WHERE clause filter |
| `MAX_ROWS` | UINTEGER | 0 (all) | Maximum rows to return |
| `READ_TABLE_FUNCTION` | VARCHAR | `'RFC_READ_TABLE'` | RFC function to use (see note) |
| `READ_TABLE_DELIMITER` | VARCHAR | — | Delimiter for TABLE2 variants |
| `SECRET` | VARCHAR | — | Named secret to use |

**Supported `READ_TABLE_FUNCTION` values:** `RFC_READ_TABLE`, `/BODS/RFC_READ_TABLE`, `/SAPDS/RFC_READ_TABLE`, `/BODS/RFC_READ_TABLE2`, `/SAPDS/RFC_READ_TABLE2`

```sql
-- Basic read
SELECT * FROM sap_read_table('SFLIGHT');

-- With DuckDB filter pushdown (automatically pushed to SAP)
SELECT * FROM sap_read_table('SFLIGHT') WHERE CARRID = 'LH';

-- SAP-side filter, column selection, parallel threads, row limit
SELECT * FROM sap_read_table('SFLIGHT',
    COLUMNS=['CARRID', 'CONNID', 'FLDATE'],
    FILTER='CARRID = ''LH''',
    THREADS=4,
    MAX_ROWS=1000
);

-- Using a named secret
SELECT * FROM sap_read_table('SFLIGHT', SECRET='my_sap');
```

---

#### `sap_rfc_invoke(function_name, ...args [, path, secret])`

Invoke any SAP RFC function module. Accepts variable arguments as STRUCT or scalar values.

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `function_name` | VARCHAR | *required* | RFC function module name |
| `...args` | ANY (varargs) | — | Function parameters as structs/values |
| `path` | VARCHAR | — | Path to select specific output (e.g. `'/RFCTABLE'`) |
| `secret` | VARCHAR | — | Named secret to use |

```sql
-- Simple function call
SELECT * FROM sap_rfc_invoke('STFC_CONNECTION', {'REQUTEXT': 'Hello SAP'});

-- Select specific output table via path
SELECT * FROM sap_rfc_invoke('BAPI_FLIGHT_GETLIST',
    {'AIRLINE': 'LH'},
    path='/FLIGHT_LIST'
);
```

---

### Discovery & Metadata

#### `sap_show_tables([TABLENAME, TEXT, THREADS])`

Search for SAP tables and views. No positional arguments.

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `TABLENAME` | VARCHAR | `'%'` | Table name pattern (supports `*` and `%` wildcards) |
| `TEXT` | VARCHAR | `'%'` | Description text pattern |
| `THREADS` | UINTEGER | 0 | Parallel read threads |

**Returns:** `table_name`, `text`, `class` (VIEW, TRANSP, POOL, CLUSTER)

```sql
SELECT * FROM sap_show_tables(TABLENAME='*FLIGHT*');
SELECT * FROM sap_show_tables(TEXT='%booking%');
```

---

#### `sap_describe_fields(table_name [, LANGUAGE])`

Get field metadata for an SAP table or view.

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `table_name` | VARCHAR | *required* | Table/view name |
| `LANGUAGE` | VARCHAR | `'E'` | Language for descriptions |

**Returns:** `pos`, `is_key`, `field`, `text`, `sap_type`, `length`, `decimals`, `check_table`, `ref_table`, `ref_field`, `language`

```sql
SELECT field, is_key, sap_type, length FROM sap_describe_fields('SFLIGHT');
```

---

#### `sap_rfc_show_function([FUNCNAME, GROUPNAME, LANGUAGE])`

Search for RFC function modules by name or group.

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `FUNCNAME` | VARCHAR | `'*'` | Function name pattern (wildcard `*`) |
| `GROUPNAME` | VARCHAR | — | Function group name pattern |
| `LANGUAGE` | VARCHAR | — | Language for descriptions |

```sql
SELECT * FROM sap_rfc_show_function(FUNCNAME='BAPI_FLIGHT*');
SELECT * FROM sap_rfc_show_function(GROUPNAME='RSBOLAP_BICS');
```

---

#### `sap_rfc_show_groups([GROUPNAME, LANGUAGE])`

Search for RFC function groups.

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `GROUPNAME` | VARCHAR | `'*'` | Group name pattern (wildcard `*`) |
| `LANGUAGE` | VARCHAR | — | Language for descriptions |

**Returns:** `name`, `text`

```sql
SELECT * FROM sap_rfc_show_groups(GROUPNAME='RSBOLAP*');
```

---

#### `sap_rfc_describe_function(function_name)`

Get detailed metadata about an RFC function module (parameters, types, structures).

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `function_name` | VARCHAR | *required* | Function module name |

```sql
SELECT * FROM sap_rfc_describe_function('STFC_CONNECTION');
```

---

### Connection & Diagnostics

#### `PRAGMA sap_rfc_ping([secret])`

Test SAP RFC connection. Returns `'PONG'` on success.

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `secret` | VARCHAR | — | Named secret to use |

```sql
PRAGMA sap_rfc_ping;
PRAGMA sap_rfc_ping(secret='my_sap');
```

---

#### `PRAGMA sap_rfc_set_trace_level(level)`

Set SAP NetWeaver RFC SDK trace level.

| Parameter | Type | Description |
|-----------|------|-------------|
| `level` | INTEGER | 0=Off, 1=Brief, 2=Verbose, 3=Detailed, 4=Full |

```sql
PRAGMA sap_rfc_set_trace_level(2);
```

---

#### `PRAGMA sap_rfc_set_trace_dir(directory)`

Set directory for SAP RFC SDK trace files.

```sql
PRAGMA sap_rfc_set_trace_dir('/tmp/sap_traces');
```

---

#### `PRAGMA sap_rfc_set_maximum_trace_file_size(size, unit)`

Set maximum size for SAP RFC trace files.

| Parameter | Type | Description |
|-----------|------|-------------|
| `size` | INTEGER | File size value |
| `unit` | ENUM | `'M'` (megabytes) or `'G'` (gigabytes) |

```sql
PRAGMA sap_rfc_set_maximum_trace_file_size(100, 'M');
```

---

#### `PRAGMA sap_rfc_set_maximum_stored_trace_files(count)`

Set maximum number of trace files to keep.

```sql
PRAGMA sap_rfc_set_maximum_stored_trace_files(10);
```

---

#### `PRAGMA sap_rfc_set_ini_path(path)`

Set path to SAP RFC INI configuration file (`sapnwrfc.ini`).

```sql
PRAGMA sap_rfc_set_ini_path('/etc/sap/sapnwrfc.ini');
```

---

#### `PRAGMA sap_rfc_reload_ini_file`

Reload the SAP RFC INI configuration file.

```sql
PRAGMA sap_rfc_reload_ini_file;
```

---

### ATTACH — Virtual SAP Catalog

Attach an SAP system as a virtual DuckDB database. Tables appear as views backed by `sap_read_table()`.

```sql
-- Attach with default secret
ATTACH '' AS sap (TYPE sap_rfc);
SELECT * FROM sap."SFLIGHT";

-- Attach with named secret
ATTACH '' AS sap (TYPE sap_rfc, SECRET 'my_sap');

-- Restrict to specific tables
ATTACH '' AS sap (TYPE sap_rfc, TABLES 'SFLIGHT,SPFLI,SCARR');

-- Detach
DETACH sap;
```

| Option | Type | Description |
|--------|------|-------------|
| `TYPE` | — | Must be `sap_rfc` |
| `SECRET` | VARCHAR | Named secret for SAP connection |
| `TABLES` | VARCHAR | Comma-separated list of tables to expose (empty = on-demand) |

---

## erpl_bics — SAP Business Warehouse

### Discovery

#### `sap_bics_show([obj_type, search, search_in_key, search_in_text, fetch_levels, secret])`

List InfoProviders, cubes, queries, or info areas.

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `obj_type` | ENUM | — | `'INFOPROVIDER'`, `'QUERY'`, `'CUBE'`, `'INFOAREA'` |
| `search` | VARCHAR | — | Search pattern (supports wildcards) |
| `search_in_key` | BOOLEAN | — | Search in technical names |
| `search_in_text` | BOOLEAN | — | Search in descriptions |
| `fetch_levels` | UINTEGER | — | Number of hierarchy levels to fetch |
| `secret` | VARCHAR | — | Named secret to use |

**Returns:** `technical_name`, `text`, `type`, `cube_name`, `is_folder`, `last_changed`, `last_changed_by`, `level`

```sql
SELECT * FROM sap_bics_show();
SELECT * FROM sap_bics_show(obj_type='QUERY', search='*SALES*');
```

---

#### `sap_bics_show_cubes([search, search_in_key, search_in_text, secret])`

Convenience function to list only BW cubes. Same parameters as `sap_bics_show` minus `obj_type`.

```sql
SELECT * FROM sap_bics_show_cubes();
SELECT * FROM sap_bics_show_cubes(search='*SALES*');
```

---

#### `sap_bics_show_queries([search, search_in_key, search_in_text, secret])`

Convenience function to list only BW queries.

```sql
SELECT * FROM sap_bics_show_queries();
```

---

#### `sap_bics_show_hierarchies([search, info_object, secret])`

List available BW hierarchies.

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `search` | VARCHAR | — | Search pattern for hierarchy names |
| `info_object` | VARCHAR | — | Filter by InfoObject |
| `secret` | VARCHAR | — | Named secret |

**Returns:** `technical_name`, `text`, `version`, `valid_to_date`

```sql
SELECT * FROM sap_bics_show_hierarchies();
SELECT * FROM sap_bics_show_hierarchies(info_object='0COSTCENTER');
```

---

### Describe

#### `sap_bics_describe(cube_name [, query_name, id, version, secret])`

Retrieve metadata structure for cubes or queries.

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `cube_name` | VARCHAR | *required* | Cube technical name |
| `query_name` | VARCHAR | — | Query technical name |
| `id` | VARCHAR | — | State ID for persisted session |
| `version` | UINTEGER | — | State version |
| `secret` | VARCHAR | — | Named secret |

```sql
-- Describe a cube
SELECT * FROM sap_bics_describe('MY_CUBE');

-- Describe a query
SELECT * FROM sap_bics_describe('MY_CUBE', 'MY_QUERY');
```

---

#### `sap_bics_describe_infoobject(info_object_name [, secret])`

Get technical details about a BW InfoObject.

**Returns:** `info_object`, `data_type`, `conv_exit`, `output_length`, `length`, `decimals`

```sql
SELECT * FROM sap_bics_describe_infoobject('0COSTCENTER');
```

---

### Hierarchy Extraction

#### `sap_bics_hierarchy(hierarchy_name [, version, date_to, secret])`

Extract hierarchy structure with node relationships and paths.

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `hierarchy_name` | VARCHAR | *required* | Hierarchy technical name |
| `version` | VARCHAR | `'A'` | Hierarchy version |
| `date_to` | VARCHAR | — | Valid-to date (`YYYYMMDD`) |
| `secret` | VARCHAR | — | Named secret |

**Returns:** `node_id`, `parent_id`, `child_id`, `next_id`, `info_object`, `node_name`, `node_value`, `date_from`, `date_to`, `level`, `path`

```sql
SELECT * FROM sap_bics_hierarchy('ZCOSTCENTER_H01');
SELECT * FROM sap_bics_hierarchy('ZCOSTCENTER_H01', version='A', date_to='20251231');
```

---

### Query Workflow (Stateful)

BICS queries use a stateful workflow: initialize a session, configure axes and filters, then fetch results.

#### Step 1: `sap_bics_begin(cube_name [, id, return, secret])`

Initialize a BICS query session.

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `cube_name` | VARCHAR | *required* | Cube technical name |
| `id` | VARCHAR | — | User-defined state ID |
| `return` | BICS_RETURN | `'DESCRIBE'` | `'DESCRIBE'` for metadata, `'RESULT'` for data |
| `secret` | VARCHAR | — | Named secret |

```sql
SELECT * FROM sap_bics_begin('MY_CUBE', id='my_session');
```

#### Step 2: `sap_bics_rows(state_id, char1 [, char2, ..., op, return])`

Configure row axis characteristics.

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `state_id` | VARCHAR | *required* | State ID from `sap_bics_begin` |
| `char1, ...` | VARCHAR | *required* | Characteristics to add/set |
| `op` | BICS_OPERATION | `'ADD'` | `'SET'`, `'ADD'`, or `'REMOVE'` |
| `return` | BICS_RETURN | `'DESCRIBE'` | Return format |

```sql
SELECT * FROM sap_bics_rows('my_session', '0CALDAY', '0MATERIAL');
```

#### Step 2b: `sap_bics_columns(state_id, char1 [, char2, ..., op, return])`

Configure column axis characteristics. Same signature as `sap_bics_rows`.

```sql
SELECT * FROM sap_bics_columns('my_session', '0AMOUNT', op='SET');
```

#### Step 3: `sap_bics_filter(state_id, filters [, op, return])`

Apply filters to query.

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `state_id` | VARCHAR | *required* | State ID |
| `filters` | varies | *required* | Filter definitions |
| `op` | BICS_OPERATION | — | Operation type |
| `return` | BICS_RETURN | `'DESCRIBE'` | Return format |

#### Step 4: `sap_bics_result(state_id)`

Fetch result set from configured query state.

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `state_id` | VARCHAR | *required* | State ID |

```sql
-- Complete workflow
SELECT * FROM sap_bics_begin('MY_CUBE', id='q1');
SELECT * FROM sap_bics_rows('q1', '0CALDAY', '0MATERIAL');
SELECT * FROM sap_bics_columns('q1', '0AMOUNT');
SELECT * FROM sap_bics_result('q1');
```

---

### Metadata Views

Functions for extracting BW system metadata. All accept an optional `secret` parameter.

| Function | Description |
|----------|-------------|
| `sap_bics_meta_providers([provider_type, secret])` | List BW InfoProviders by type (CUBE, ADSO, HCPR, ODSO, ODSVIEW) |
| `sap_bics_meta_provider_fields(provider_name [, secret])` | Fields/characteristics for a provider |
| `sap_bics_meta_datasources([secret])` | List all DataSources |
| `sap_bics_meta_datasource_fields(datasource_name [, secret])` | Fields for a DataSource |
| `sap_bics_meta_transformations([secret])` | List all transformations |
| `sap_bics_meta_transform_fields(transformation_name [, secret])` | Field mappings and rules |
| `sap_bics_meta_hcpr([secret])` | List all CompositeProviders |
| `sap_bics_meta_hcpr_mapping(hcpr_name [, secret])` | Field mappings for CompositeProviders |
| `sap_bics_meta_queries([secret])` | List all BW queries |
| `sap_bics_meta_query_usage(query_name [, secret])` | Query usage and dependencies |
| `sap_bics_meta_query_elements(query_name [, secret])` | Detailed query structure elements |
| `sap_bics_meta_query_stats([secret])` | Query performance and usage statistics |
| `sap_bics_meta_objxref([secret])` | Dependencies between BW objects |
| `sap_bics_meta_infoobjects([secret])` | List all InfoObjects |

```sql
SELECT * FROM sap_bics_meta_providers(provider_type='CUBE');
SELECT * FROM sap_bics_meta_provider_fields('MY_CUBE');
SELECT * FROM sap_bics_meta_queries();
```

---

### Lineage & Impact Analysis

Functions for BW data lineage extraction. All accept an optional `secret` parameter.

#### `sap_bics_lineage_edges([scope, secret])`

Extract lineage edges between BW objects.

**Returns:** `edge_type`, `src_kind`, `src_name`, `src_field`, `tgt_kind`, `tgt_name`, `tgt_field`

```sql
SELECT * FROM sap_bics_lineage_edges();
```

---

#### `sap_bics_lineage_graph_json([scope, secret])`

Full lineage graph in JSON format.

```sql
SELECT * FROM sap_bics_lineage_graph_json();
```

---

#### `sap_bics_lineage_trace(object_name [, secret])`

Trace lineage from a specific object.

```sql
SELECT * FROM sap_bics_lineage_trace('MY_CUBE');
```

---

#### `sap_bics_query_lineage(query_name [, secret])`

Complete field-level lineage for a specific BW query.

```sql
SELECT * FROM sap_bics_query_lineage('MY_QUERY');
```

---

## erpl_odp — SAP Operational Data Provisioning

### Discovery

#### `sap_odp_show_contexts([secret])`

List available ODP contexts.

**Returns:** `technical_name`, `text`, `release`

```sql
SELECT * FROM sap_odp_show_contexts();
-- Typical contexts: BW, ABAP_CDS, SAPI, SLT, HANA
```

---

#### `sap_odp_show(odp_context [, search, secret])`

List ODP providers/sources within a context.

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `odp_context` | VARCHAR | *required* | ODP context (e.g. `'BW'`, `'ABAP_CDS'`, `'SAPI'`) |
| `search` | VARCHAR | — | Search pattern (wildcards) |
| `secret` | VARCHAR | — | Named secret |

**Returns:** `technical_name`, `text`, `semantics`, `semantics_text`

```sql
SELECT * FROM sap_odp_show('BW');
SELECT * FROM sap_odp_show('ABAP_CDS', search='*FLIGHT*');
```

---

#### `sap_odp_describe(odp_context, odp_name [, secret])`

Get detailed metadata about an ODP provider.

```sql
SELECT * FROM sap_odp_describe('BW', 'MY_ODP_SOURCE');
```

---

### Data Extraction

#### `sap_odp_preview(odp_context, odp_name [, secret])`

Preview a small sample of data from an ODP provider.

```sql
SELECT * FROM sap_odp_preview('BW', 'MY_ODP_SOURCE');
```

---

#### `sap_odp_read_full(odp_context, odp_name [, threads, columns, filters, secret])`

Full extraction of ODP provider data with parallel processing.

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `odp_context` | VARCHAR | *required* | ODP context |
| `odp_name` | VARCHAR | *required* | ODP provider name |
| `threads` | UINTEGER | 5 | Parallel read threads |
| `columns` | LIST(VARCHAR) | all | Columns to extract |
| `filters` | LIST | — | Selection filters |
| `secret` | VARCHAR | — | Named secret |

```sql
SELECT * FROM sap_odp_read_full('BW', 'MY_ODP_SOURCE');
SELECT * FROM sap_odp_read_full('ABAP_CDS', 'MY_CDS_VIEW', threads=8);
```

---

### Subscription Management

#### `sap_odp_show_subscriptions([secret])`

List active ODP subscriptions.

```sql
SELECT * FROM sap_odp_show_subscriptions();
```

---

#### `sap_odp_show_cursors([secret])`

List ODP extraction cursors for delta tracking.

```sql
SELECT * FROM sap_odp_show_cursors();
```

---

#### `PRAGMA odp_drop(subscription_id)`

Drop/delete an ODP subscription.

```sql
PRAGMA odp_drop('SUB_12345');
```

---

## erpl_tunnel — SSH Tunneling

### Tunnel Management

#### `PRAGMA tunnel_create([secret, remote_host, remote_port, local_port, timeout])`

Create a new SSH tunnel for port forwarding.

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `secret` | VARCHAR | — | SSH tunnel secret name |
| `remote_host` | VARCHAR | *required* | Target host to tunnel to |
| `remote_port` | INTEGER | *required* | Target port |
| `local_port` | INTEGER | *required* | Local port to bind |
| `timeout` | INTEGER | 60 | Connection timeout in seconds |

**Returns:** `tunnel_id`, `message`

```sql
PRAGMA tunnel_create(
    secret='my_ssh',
    remote_host='sap-internal.corp',
    remote_port=3300,
    local_port=13300
);
```

---

#### `PRAGMA tunnel_close(tunnel_id)`

Close a specific tunnel by ID.

| Parameter | Type | Description |
|-----------|------|-------------|
| `tunnel_id` | INTEGER | Tunnel ID to close |

```sql
PRAGMA tunnel_close(1);
```

---

#### `PRAGMA tunnel_close_all`

Close all active tunnels.

```sql
PRAGMA tunnel_close_all;
```

---

#### `tunnels()`

List all active SSH tunnels.

**Returns:** `tunnel_id` (BIGINT), `ssh_host` (VARCHAR), `ssh_port` (INTEGER), `ssh_user` (VARCHAR), `remote_host` (VARCHAR), `remote_port` (INTEGER), `local_port` (INTEGER), `status` (VARCHAR)

```sql
SELECT * FROM tunnels();
```

---

## Reference

### Secret Types

#### `sap_rfc` — SAP RFC Connection

All parameters are VARCHAR. Choose either direct connection or load-balanced parameters.

| Parameter | Description | Direct | Load-Balanced |
|-----------|-------------|:------:|:-------------:|
| `ashost` | Application server host | required | — |
| `sysnr` | System number | required | — |
| `mshost` | Message server host | — | required |
| `msserv` | Message server service | — | optional |
| `sysid` | System ID | — | required |
| `group` | Logon group | — | required |
| `client` | SAP client number | required | required |
| `user` | Username | required | required |
| `passwd` | Password (redacted) | required | required |
| `lang` | Language (e.g. `'EN'`) | optional | optional |
| `snc_qop` | SNC quality of protection | optional | optional |
| `snc_myname` | SNC client name | optional | optional |
| `snc_partnername` | SNC server name | optional | optional |
| `snc_lib` | SNC library path | optional | optional |
| `mysapsso2` | SSO2 ticket | optional | optional |

```sql
-- Direct connection
CREATE SECRET my_sap (
    TYPE sap_rfc,
    ASHOST 'sap.example.com', SYSNR '00', CLIENT '100',
    USER 'sapuser', PASSWD 'password', LANG 'EN'
);

-- Load-balanced connection
CREATE SECRET my_sap_lb (
    TYPE sap_rfc,
    MSHOST 'sapms.example.com', SYSID 'PRD', GROUP 'PUBLIC',
    CLIENT '100', USER 'sapuser', PASSWD 'password'
);
```

---

#### `ssh_tunnel` — SSH Tunnel Connection

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `ssh_host` | VARCHAR | *required* | SSH jump host |
| `ssh_port` | INTEGER | 22 | SSH port |
| `ssh_user` | VARCHAR | *required* | SSH username |
| `password` | VARCHAR | — | SSH password (redacted) |
| `private_key_path` | VARCHAR | — | Path to SSH private key (redacted) |
| `passphrase` | VARCHAR | — | Private key passphrase (redacted) |
| `auth_method` | VARCHAR | auto | `'password'`, `'key'`, or `'agent'` (auto-detected if omitted) |

```sql
-- Password authentication
CREATE SECRET my_ssh (
    TYPE ssh_tunnel,
    ssh_host 'jumphost.example.com', ssh_user 'myuser',
    password 'secret'
);

-- Key-based authentication
CREATE SECRET my_ssh_key (
    TYPE ssh_tunnel,
    ssh_host 'jumphost.example.com', ssh_user 'myuser',
    private_key_path '/home/user/.ssh/id_rsa'
);
```

---

### Custom Types

| Type | Values | Used By |
|------|--------|---------|
| `BICS_RETURN` | `'DESCRIBE'`, `'RESULT'` | BICS query workflow functions |
| `BICS_OPERATION` | `'SET'`, `'ADD'`, `'REMOVE'` | BICS axis/filter configuration |
| `ODP_REPLICATION_MODE` | `'FULL'`, `'DELTA'`, `'RECOVER'` | ODP extraction mode |

---

### SAP ↔ DuckDB Type Mapping

| SAP RFC Type | DuckDB Type | Notes |
|-------------|-------------|-------|
| CHAR | VARCHAR | Fixed-length, right-trimmed |
| NUM (NUMC) | VARCHAR | Numeric characters, preserves leading zeros |
| INT | INTEGER | 4-byte integer |
| INT1 | TINYINT | 1-byte integer |
| INT2 | SMALLINT | 2-byte integer |
| INT8 | INTEGER | 8-byte mapped to INTEGER |
| FLOAT | DOUBLE | Floating point |
| BCD | DECIMAL | Binary coded decimal |
| DECF16 | DECIMAL | Decimal floating point 16 |
| DECF34 | DECIMAL | Decimal floating point 34 |
| STRING | VARCHAR | Variable-length string |
| XSTRING | VARCHAR | Hex string |
| XMLDATA | VARCHAR | XML data |
| BYTE | BLOB | Raw bytes |
| DATE | DATE | Format YYYYMMDD |
| TIME | TIME | Format HHMMSS |
| UTCLONG | TIMESTAMP | UTC timestamp (long) |
| UTCSECOND | TIMESTAMP | UTC timestamp (seconds) |
| UTCMINUTE | TIMESTAMP | UTC timestamp (minutes) |
| DTDAY | INTEGER | Date duration in days |
| DTWEEK | INTEGER | Date duration in weeks |
| DTMONTH | INTEGER | Date duration in months |
| TSECOND | INTEGER | Time duration in seconds |
| TMINUTE | INTEGER | Time duration in minutes |
| CDAY | INTEGER | Calendar day |
| STRUCTURE | STRUCT | Nested structure |
| TABLE | LIST | Table parameter |

---

### Configuration Options

#### erpl_rfc

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `erpl_telemetry_enabled` | BOOLEAN | `true` | Enable telemetry ([erpl.io/telemetry](https://erpl.io/telemetry)) |
| `erpl_telemetry_key` | VARCHAR | *(built-in)* | Telemetry API key |
| `erpl_trace_enabled` | BOOLEAN | `false` | Enable ERPL tracing |
| `erpl_trace_level` | VARCHAR | `'INFO'` | Trace level: TRACE, DEBUG, INFO, WARN, ERROR, NONE |
| `erpl_trace_output` | VARCHAR | `'console'` | Output: console, file, both |
| `erpl_trace_file_path` | VARCHAR | `'trace'` | Trace file directory |
| `erpl_trace_max_file_size` | BIGINT | 0 (unlimited) | Max trace file size in bytes |
| `erpl_trace_rotation` | BOOLEAN | `false` | Enable trace file rotation |
| `erpl_rfc_strict_type_check` | BOOLEAN | `false` | When true, throw an error on unsupported SAP RFC types instead of falling back to VARCHAR |

```sql
SET erpl_trace_enabled = TRUE;
SET erpl_trace_level = 'DEBUG';
SET erpl_trace_output = 'both';
```

#### erpl_bics

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `erpl_bics_trace` | BOOLEAN | `false` | Enable BICS trace logging |
| `erpl_bics_trace_dir` | VARCHAR | `'./trace'` | BICS trace directory |

#### erpl_tunnel

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `erpl_telemetry_enabled` | BOOLEAN | `true` | Enable telemetry |
| `erpl_telemetry_key` | VARCHAR | *(built-in)* | Telemetry API key |

---

## Notes

### Filter Pushdown

`sap_read_table` supports both SAP-side and DuckDB-side filter pushdown:

```sql
-- SAP-side filter (via FILTER parameter, pushed to RFC_READ_TABLE)
SELECT * FROM sap_read_table('SFLIGHT', FILTER='CARRID = ''LH''');

-- DuckDB-side filter pushdown (automatically pushed to SAP when possible)
SELECT * FROM sap_read_table('SFLIGHT') WHERE CARRID = 'LH';
```

### Parallel Reads

Use the `THREADS` parameter on `sap_read_table` and `sap_odp_read_full` for large tables:

```sql
SELECT * FROM sap_read_table('LARGE_TABLE', THREADS=8);
```

### SSH Tunnel + SAP Connection

Complete workflow for connecting through an SSH jump host:

```sql
-- 1. Create SSH tunnel secret
CREATE SECRET my_ssh (TYPE ssh_tunnel, ssh_host 'jump.example.com', ssh_user 'user', password 'pw');

-- 2. Create tunnel
PRAGMA tunnel_create(secret='my_ssh', remote_host='sap.internal', remote_port=3300, local_port=13300);

-- 3. Create SAP connection pointing to local tunnel port
CREATE SECRET my_sap (TYPE sap_rfc, ASHOST 'localhost', SYSNR '00', CLIENT '100', USER 'sap', PASSWD 'pw');

-- 4. Query through tunnel
SELECT * FROM sap_read_table('SFLIGHT');

-- 5. Cleanup
PRAGMA tunnel_close_all;
```

---

**Last Updated:** 2026-02-11
