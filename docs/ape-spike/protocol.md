# erpl_ape — APE protocol note (S1 spike)

Status: **in progress.** Everything below marked ✅ was verified live against the `a4h`
ABAP Platform Trial (APE version **2.7.0**). Items marked ❓ are open.

Method: ABAP source read with `uvx erpl-adt … source read <OBJ> --type CLAS|INTF [--section all|testclasses]`;
RFCs driven from SQL with `sap_rfc_invoke` in the existing debug build; multi-call sequences driven
from an ABAP `IF_OO_ADT_CLASSRUN` probe (`ZCL_ERPL_APE_PROBE`) so all calls share one session.

## 1. Availability on a4h ✅

| Package | Contents |
|---|---|
| `LT_DHAPE_REMOTE` | `DHAPE_GRAPH_{MANAGER,ROUNDTRIP,VERSION}`, `…_{POLL,PUSH,UNBLOCK}_PORT`, `DHAPE_FEDERATION_{OPEN,FETCH,CLOSE,CAPABILITIES}` |
| `LT_DHAMB_REMOTE` | `DHAMB_SERVICE_DSET_{BROWSE,DEFINITION,PREVIEW,LINEAGE}`, `…_{DYN_PROP,HEALTH,SYSTEM}` |
| `LT_DHCDC_*` | `DHCDC_API_REMOTE`, `DHCDC_REMOTE`, `DHCDC_RT_REPLICATION`, `DHCDC_REC_REMOTE` |

`POLL_PORT` / `PUSH_PORT` are **deprecated stubs** (`return-message = 'deprecated'`).
`ROUNDTRIP` is the only data path — it is bidirectional (`it_port` in, `et_port` out).

## 2. Operator registry — the BRD's operator does not exist here ✅

`cl_dhape_operator_registry=>get_operator_with_descr( )` on a4h returns, among others:

```
com.sap.abap.cds.reader.v1 / .v2 / com.sap.abap.cds_reader (V0)
com.sap.abap.reader            :: Read Data From SAP System     <- gen2
com.sap.abap.subscr.eraser.v1  :: Resilience Subscription Eraser V1
com.sap.abap.subscr.reader.v1  :: ABAP Subscription Reader V1
internal.agentOperator, internal.inport, internal.outport
```

**`com.sap.abap.cds.reader.v3` is NOT registered**, even though `CL_DHAPE_OPER_CDS_READER_V3`
exists as a class. Passing it yields `CX_SY_REF_IS_INITIAL` in
`CL_DHAPE_GRAPH_FACTORY->CREATE_PROCESS` (registry lookup returns an unbound reference).

**Consequence — decision 5 is settled by evidence: use `com.sap.abap.reader` (gen2).**
It was already preferred on defensibility grounds (`LT_DHAPE_OPER_PUBLIC`, `CREATE PUBLIC`,
public abstract base).

Revised **L1 allow-list** (was 3 entries in the BRD):

```
com.sap.abap.reader            (container pinned to CDS / CDS_EXTRACTION — see §5)
com.sap.abap.subscr.eraser.v1
com.sap.abap.subscr.reader.v1  (note: operator.json says "internal": true)
internal.agentOperator         (mandatory for v7 — see §6)
internal.outport               (synthesised by the factory, see §4)
```

`com.sap.abap.subscr.reader.v1` settles the open question of how subscriptions are listed:
config is `{"reader": "<reader operator name>"}`, outport `out` type `string`.

## 3. RFC signatures ✅

```abap
DHAPE_GRAPH_MANAGER   IMPORTING iv_mode TYPE dhape_graph_manager_mode, iv_appid TYPE char128,
                                iv_graph_uuid TYPE char32 OPT, iv_graph TYPE string OPT
                      EXPORTING ev_graph_uuid TYPE char32, et_msg TYPE dhape_t_graph_msg

DHAPE_GRAPH_ROUNDTRIP IMPORTING iv_graph_uuid, it_port TYPE dhape_t_graph_port, it_msg,
                                is_rucksack_md_id OPT
                      EXPORTING et_port, et_msg, ev_reconnect_json, ev_rucksack_md

DHAPE_GRAPH_VERSION   IMPORTING iv_version OPT
                      EXPORTING ev_version TYPE char30, et_msg, ev_capabilities TYPE string
```

`iv_mode` (`IF_DHAPE_APE_FACADE=>gc_graph_manager_mode`): `C` create, `R` start, `S` stop, `O` reconnect.

`DHAPE_PORT` (= `DHAPE_T_GRAPH_PORT`): `mandt, graph_uuid CHAR32, port_number INT2,
direction CHAR1, port_status CHAR1, port_data abap.string`. **Packages are strings.**

`DHAPE_GRAPH_VERSION` live result: `ev_version = 2.7.0`, `ev_capabilities` is JSON
(`cbSerializationEnabled`, `sessionStatistics{totalSessions,activeSessions,totalDialogProcesses,…}`).

## 4. Graph description format ✅

JSON, parsed by `CL_DHAPE_JSON`, consumed by `CL_DHAPE_GRAPH_FACTORY`:

```json
{ "Attributes": { "graphid": "...", "protocol": "v6"|"v7",
                  "multiplicity": "...", "graphkind": "user"|"rms", "tracelevel": "debug" },
  "snapshotConfig": { "enabled": "true"|"false" },
  "autoRestartId": "...",
  "Processes":   { "<name>": { "Component": "<operator>", "Metadata": { ... } } },
  "Connections": { "<name>": { "Src": {"Process":"..","Port":".."},
                               "Tgt": {"Process":"..","Port":".."} } },
  "Outports":    { "<k>": { "Process":"..", "Port":"..",
                            "Metadata": {"portNumber":0, "type":"table"} } },
  "Inports":     { ... },
  "vTypes":      { "table": {...}, "structure": {...}, "scalar": {...} } }
```

- A process's `conf` is the **detached `/Metadata` object**, so the operator reads
  `/Config/...` *within* it.
- `Connections` may be an array (`[]`) or an object — the factory only iterates members.
- For each `Outports` entry the factory **synthesises** an extra process
  `name = 'to_main_engine' && <key>`, `comp = 'internal.outport'`,
  conf `{Process, Port, Type, PortNr}`, plus a connection into its `in` port. That is why
  `internal.outport` must be allow-listed even though we never name it.
- `Outports/*/Metadata/portNumber` is the `DHAPE_PORT-PORT_NUMBER` seen on `et_port`.
- `/Attributes/graphid` (not `IV_APPID`) becomes `DHAPE_GRAPH-APPID`.

**`protocol` is load-bearing.** From `CL_DHAPE_APE_FACADE`:
`" A start will happen during create for protocol 6, for protocol 7 do it here"`.
With no/`v6` protocol, `create` auto-starts and an explicit `R` then fails with
`Unexpected graph status (A)`. The gen2 reader is subengine **v7**, so declare `"protocol":"v7"`.

## 5. Gen2 reader config ✅

Canonical, from `CL_DHAPE_PROC_GEN2_READER` `gc_path` + the operator's own schema:

```
/Config/transferMode                                          "Initial Load"|"Replication"|"Delta Load"
/Config/objectName/remoteObjectReference/qualifiedName         = DHAMB OBJECTPATH, e.g. /CDS/TMP/ZERPL_APE_FLIGHT
/Config/objectName/remoteObjectReference/nativeQualifiedName   (ODP only)
/Config/objectName/filter/selectOptions                        -> F11 filters
/Config/objectName/schema/tableBasedRepresentation/attributes   -> F9 projection + types
/AdditionalOutPorts/1/vtype-ID                                 (in Metadata, NOT Config)
```

`qualifiedName` is split on `/`; element **2** is the container type
(`CDS`, `CDS_EXTRACTION`, `SLT`, `ODP_SAPI`, `ODP_BW`), the last element is the object name.
`CDS`/`CDS_EXTRACTION` ⇒ `adapter_type = cdc`, `objtype = cds_view`. **This is where BRD L3 is
enforced: pin the container to CDS/CDS_EXTRACTION, or gen2 will happily read SLT and ODP.**

Filter and projection shapes, verbatim from `CL_DHAPE_PROC_GEN2_READER`'s test class:

```json
"filter": { "selectOptions": [ { "name": "COLUMN1",
                                 "elements": [ { "comparison": "=", "low": "100" } ] } ] },
"schema": { "tableBasedRepresentation": { "attributes": [
   { "name":"COLUMN1", "datatype":"DECIMAL", "nativeDatatype":"DEC", "precision":"10", "scale":"5",
     "properties":[{"name":"abapDecimals","namespace":"com.sap.abap","value":"000015"}] },
   { "name":"COLUMN2", "datatype":"STRING", "nativeDatatype":"CHAR",
     "properties":[{"name":"caseSensitive","namespace":"com.sap.abap","value":"X"}] } ] } },
"AdditionalOutPorts": [ { "name":"outData", "type":"table",
                          "vtype-ID":"$GRAPH.generated.<proc>_outData" } ]
```

`selectOptions` maps onto `ty_t_filters(fieldname, sign, opt, low)` — so **erpl_odp's
`{FIELDNAME, SIGN, OP, LOW, HIGH}` filter struct maps almost 1:1** and should be reused.
`nativeDatatype` takes DHAMB's `ABAPTYPE` directly.

## 6. Graph lifecycle — verified as far as it goes

State machine (`IF_DHAPE_DB_FACADE=>gc_status`): `C` created, `A` ready, `R` running, `S` stopped, `E` expired.
`CL_DHAPE_GRAPH`: `on_start` requires `created` → sets `ready`; **`on_resume` is what sets `running`**.

Verified with the single-session probe, protocol v7:

| Step | Result |
|---|---|
| `C` create | ✅ uuid returned, `et_msg` empty, `DHAPE_GRAPH.STATUS = C` |
| `R` start | ✅ `et_msg` empty, `STATUS = A` |
| `ROUNDTRIP` | ❌ `Failed to resume graph (failed to resume process reader) / An error occurred while setting up the replication application / Invalid subscriber ID` |
| `S` stop | ✅ `STATUS = S` |

`internal.agentOperator` **must** be present as a process: at start, `CL_DHAPE_GRAPH` scans
`mt_procs` for one `IS INSTANCE OF if_dhape_oper_agent` and assigns `mo_agent_process`. Without it,
`CL_DHAPE_PROC_GEN2_READER->GET_ADAPTER_PARAMETERS:21` null-dereferences on
`mo_graph->get_agent_operator( )->get_state_manager( )->read_state_uuid( )`.
Its config schema is empty (`{}`); inport `in`/outport `out`, both `scalar`, subengine v7.

### ❓ The one open blocker

`DHAPE_GRAPH-STATE_UUID` stays **empty**, so `read_state_uuid( )` returns blank and the CDC
adapter rejects it as `Invalid subscriber ID`. `CL_DHAPE_GRAPH_FACTORY` only calls
`set_graph_external_ids( graph_id, recovering_handle, primary_graph_uuid )` — it never writes
`STATE_UUID`. Setting `snapshotConfig.enabled = "true"` did **not** populate it.

Next reads, in order: `CL_DHAPE_OPER_AGENT` and its process class,
`CL_DHAPE_GRAPH_PROC_SNAPSHOT` / `CL_DHAPE_OPER_SNAPSHOTABLE`, and whatever implements
`get_state_manager( )` / `create_state( )` — to find who is expected to write `STATE_UUID`, and
whether a graph must be registered as a consumer first (`DHAPE_CONSUMER_ID` is documented as
"Graph ID or Replication ID", `DHAPE_CONSUMER_TYPE` ∈ `RMS`/`GEN2`/`GEN1`).

## 7. DHAMB metadata — REST over RFC ✅

All `DHAMB_SERVICE_*` FMs take `iv_path` + `it_parameters` (`dhbas_t_name_value_string`) and return
`ev_result_json`, `ev_http_status`, `ev_status`. OData-style parameters: `path`, `children`,
`$search`, `$top`, `$skip`, `$orderby`, `$rms`.

- `DHAMB_SERVICE_SYSTEM` → `{"ARRAY":{"DBHOST","DBSYS","MSGSRVHOST","MSGSRVNO","IDENTIFICATIONKEY","APIVERSION","APIVERSIONSUPPORT"}}`.
  Backs `sap_ape_system_info()`.
- `DHAMB_SERVICE_DSET_BROWSE` with `iv_path='/'` → folders `/CDS`, `/TABLES`, `/VIEWS`, `/ODP_BW`, `/ODP_SAPI`.
  **`sap_ape_show` must browse only `/CDS`** — browsing the root or `/ODP_*` would put ODP in our
  call path and break BRD L3.
  `/CDS` is a package/component tree; leaves have `OBJECTTYPE = "CDS"`.
  Row schema: `OBJECTNAME, OBJECTDESCR, OBJECTTYPE, OBJECTPATH, OBJECTPARENT, CONTFLAG, TABART,
  CONTENTTYPE, DATACLASS, PACKAGE, PACKAGEDESCR, CLIDEP, COMPONENT_ID, COMPONENT, COMPDESCR,
  CDS_PUBLISHED, CDS_TYPE, BASETABLE, LASTCHANGE`. `CDS_PUBLISHED` is the release flag for BRD L2.
- `DHAMB_SERVICE_DSET_DEFINITION` with `iv_path=<OBJECTPATH>` → the browse row plus `COLUMNS[]`:
  `COLUMNNAME, COLUMNDESC, IS_KEY ('X'|''), DATAELEMENT, DOMAIN, ABAPTYPE, ABAPLEN, ABAPDEC,
  OUTPUTLEN, REFTABLE, REFFIELD, VALUETABLE`.
  `ABAPLEN`/`ABAPDEC` are zero-padded strings. Feeds
  `RfcType::FromTypeName(ABAPTYPE, ABAPLEN, ABAPDEC).CreateDuckDbType()` unchanged;
  `REFTABLE`/`REFFIELD` is the currency/unit reference.

## 8. Payload encoding ❓ (not yet observed)

From source only: the reader emits a vType message via `cl_dhape_vtype_factory=>new_vtype_message(
iv_encoding = gc_encoding-table )`, body header `{id, type, size, schema}`, headers
`com.sap.headers.{body,batch,cdc,snapshot}`. `IF_DHAPE_VTYPE_CONSTANTS=>gc_type_id-cdc_mode =
'com.sap.headers.cdc.mode'` — so the **change mode is a typed row component**, which supports
decision 2 (surface engine-native names rather than inventing `_ape_*`).

`/vTypes` shape (`CL_DHAPE_VTYPE_REPOSITORY->add_definitions`):
`/table/<id>/rows/components/<k>/<field>/{description, template, vflow.type, name,
value.length, value.precision, value.scale}`; also `/structure/...` and `/scalar/...`.
Base vTypes: `com.sap.core.string`, `com.sap.core.int64`, `com.sap.core.boolean`.

Wire formats are selected by readable name; `CL_DHBAS_CONV_WIRE_LIGHT_ISO=>cv_my_readable_name =
'Required Conversions Plus Time Format and Currency'`. `CL_DHBAS_CONV_WIRE_CLASSIC` defines
invalid-value sentinels the decoder must handle: `9999-99-99`, `99:99:99.999`,
`9999-99-99T99:99:99.9999999`, `NaN`, `?`.

## 9. Session affinity ✅ — and it changes the design

`CL_DHAPE_APE_FACADE->validate_graph` requires `iv_graph_uuid` to equal the **session context's**
graph uuid, and if the in-memory graph object is unbound it re-reads from the DB and demands
status `stopped`, else raises `graph_no_longer_valid`.

So: graph *metadata* is DB-backed (create in one process, read the row in another — verified), but a
**running** graph cannot be driven from another session. `create`/`start`/`roundtrip`/`stop` must all
run on **one pinned RFC connection**.

⇒ `ApeSession` must hold a single `shared_ptr<RfcConnection>` for the graph's whole lifetime, the
`erpl_odp` `odp_fetch.cpp` pattern. It cannot be stateless, and every cleanup path is load-bearing.

## 10. Fixture

`docs/ape-spike/fixtures/zerpl_ape_flight.ddl` — `ZERPL_APE_FLIGHT` over `SFLIGHT` in `$TMP`, with
`@Analytics.dataExtraction.enabled: true`. Activated on a4h; DHAMB lists it at
`/CDS/TMP/ZERPL_APE_FLIGHT` with `BASETABLE = ZERPLAPEFLGT` and `CDS_PUBLISHED = ''` (unreleased,
so it also exercises the L2 gate). Column types cover CHAR/NUMC/DATS/CURR/CUKY/INT4.

`docs/ape-spike/fixtures/graph_initial_load_v7.json` — the graph that reaches §6's blocker.

Probe classes on a4h (`$TMP`, delete when done): `ZCL_ERPL_APE_PROBE`, `ZCL_ERPL_APE_PROBE2`.

## 11. The v6 / `cds.reader.v2` path — graph runs, data does not flow ✅❓

Because the gen2 (v7) reader needs the agent-operator state protocol (§6), the **v6 readers are
the pragmatic path**, and they are registered on a4h. `com.sap.abap.cds.reader.v2` operator.json /
schema:

- subengine **v6**, outport **`outMessageData`** type `message`, `internal: false`
- required config: `subscriptionType`, `cdsname`, `action`, `chunkSize`
- `subscriptionType` ∈ `New` | `Existing`; with `New` supply `subscriptionName`, with `Existing`
  supply `subscriptionID`
- `action` ∈ `Initial Load` | `Replication` | `Delta Load`
- `wireformat` ∈ `Enhanced Format Conversions` | `Required Conversions` |
  `Required Conversions Plus Currency` | `Required Conversions Plus Time Format and Currency`
- `cdsname` is a **plain string** (the CDS entity name) — no `objectName` object, no
  `schema/attributes`, no vType declaration, no agent operator. Far simpler than gen2.

`com.sap.abap.cds.reader.v1` is the same minus `wireformat`, outport `outData` type `abap.*`.

Verified live (`docs/ape-spike/fixtures/graph_v6_initial_load.json`):

| Step | Result |
|---|---|
| `C` create | ✅ `DHAPE_GRAPH.STATUS = A` — **v6 auto-starts during create; do NOT send `R`** |
| `ROUNDTRIP` | ✅ `STATUS = R` (running), `et_port` returns 1 row, `direction = O` |
| client marks port ready | ✅ send `it_port = [{graph_uuid, port_number=0, direction='O', port_status='R'}]`; the engine echoes `pst=R` |
| `S` stop | ✅ |
| data | ❓ `port_data` is always empty; `ev_rucksack_md` and `ev_reconnect_json` also empty |

`IF_DHAPE_PORT=>gc_port_status`: `' '` initial, `'C'` closed, `'B'` blocked, `'R'` ready.
`gc_port_dir`: `'I'` incoming, `'O'` outgoing.

### Subscriptions are real ✅

`DHAPE_SUBSCR` (view `DHAPE_SUBSCR_V`) is the inventory behind `sap_ape_show_subscriptions`:

```
SUBSCRIPTION_ID  READER                      FIELD1            FIELD2         GRAPH_UUID CREATED_BY CREATED_AT
<uuid>           com.sap.abap.cds.reader     ZERPL_APE_FLIGHT  Initial Load   <uuid>     DEVELOPER  2026...
```

`FIELD1` = CDS name, `FIELD2` = transfer mode, `READER` = the reader **family**
(`com.sap.abap.cds.reader`), which is exactly the key the eraser's `cdsSubscrID` value help uses.
So `PRAGMA sap_ape_drop` runs `com.sap.abap.subscr.eraser.v1` with
`{"readerObj":"CDS Views","cdsSubscrID":"<SUBSCRIPTION_ID>"}`.

Re-using a `subscriptionName` fails with `Subscription name ERPL_APE_S1 already exists`, so tests
must generate unique names (or pass `subscriptionType: Existing` + `subscriptionID`).

The CDC engine also **generated runtime objects**: function groups `/1DH/A4H_001000000000N`
(description = our graph UUID) and `/1DH/A4H_OLI_001000000000N` (OLI = initial load). So generation
succeeds; only the data hand-off does not happen.

### ❓ Remaining blocker, restated

Two candidate readers, two different walls:

1. **gen2 `com.sap.abap.reader` (v7)** — `DHAPE_GRAPH-STATE_UUID` is never populated, so
   `read_state_uuid( )` returns blank and the CDC adapter answers `Invalid subscriber ID`.
   `CL_DHAPE_STATE_MANAGER->read_state_uuid` only *reads* the DB; nothing in the factory writes it.
   The agent operator owns an inport and `ROUNDTRIP` accepts `it_msg`, so the client is evidently
   expected to drive SAP's agent message protocol to establish state. That is a substantial
   additional protocol, not a graph-JSON field.
2. **`cds.reader.v2` (v6)** — everything negotiates correctly, the subscription and its `/1DH/`
   runtime objects are generated, the port handshake is accepted, and the reader still emits
   nothing.

For (2) the most likely cause is that a CDC initial load must be **queued and executed by a
background job** before the reader can stream it, and the a4h trial has no such job scheduled —
i.e. the BRD's own risk *"A4H trial lacks a working CDC engine"* materialising. Next checks:
`DHCDC_SUBSREG` / the `DHCDC_RT_*` function groups for the load-request state, `SM37`-equivalent
job inspection, and whether `action: Replication` behaves differently from `Initial Load`.

**Until one of these is resolved, `sap_ape_read_full` / `_read_delta` cannot be implemented.**
Everything in §7 (discovery, describe, preview, system info) is unaffected and fully specified.
