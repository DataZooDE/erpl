# ERPL Telemetry

ERPL collects **anonymous, privacy-preserving usage telemetry** so we can see
which capabilities are used, on which platforms, and where they fail — and
prioritise accordingly. It is **on by default** and **trivial to turn off**.

Telemetry is emitted through the shared
[`DataZooDE/posthog-telemetry`](https://github.com/DataZooDE/posthog-telemetry)
library and follows the cross-product **`telemetry_schema: 2`** envelope
(`third_party/posthog-telemetry/TELEMETRY-SCHEMA.md`). Ingestion is the EU
PostHog cloud.

## How to turn it off

Any one of these fully short-circuits telemetry — when disabled, **nothing
leaves the machine** (the opt-out is enforced at the transport, not just at the
call sites):

```sql
SET erpl_telemetry_enabled = false;   -- DuckDB setting (per session)
```

```bash
export DATAZOO_DISABLE_TELEMETRY=1     # environment (1|true|yes)
```

## The guarantee: bounded, enumerated, non-PII

Every property we send is **either** a constant drawn from a small, code-controlled
enumeration **or** a pure number (durations, counts). The library additionally
clamps every outgoing string to 512 bytes as a backstop.

We **never** send: SAP host names, system numbers, client numbers, user names,
RFC/BAPI/function-module names or their arguments, table names, column names,
`FILTER`/`WHERE` clauses, SQL text, row/result data, or SAP/RFC **error
messages**. Error reporting sends only an *enumerated error class* (see below).

The instrumentation lives in one small, auditable header —
`rfc/src/include/erpl_telemetry.hpp` — and every capture site carries a comment
listing exactly what it sends.

## What is collected

### Envelope (attached to every event)

`product` (`erpl`), `product_version`, `product_edition` (`oss`),
`telemetry_schema` (`2`), `duckdb_version`, `os`, `arch`, `platform`, `is_ci`,
`is_container`, a per-process `$session_id`, and — once associated — the
`deployment` group. `distinct_id` is the SHA-256 of a machine id: a **stable,
pseudonymous** identifier, not tied to any personal data.

### Events

| Event | When | Properties (beyond the envelope) |
|---|---|---|
| `extension_loaded` | an ERPL extension loads (`erpl_rfc`, `erpl_bics`, `erpl_odp`) | — |
| `feature_used` | a named capability is exercised | `feature` (enum below), plus the per-feature props below |
| `function_executed` | a DuckDB function runs — **aggregated** per function per session (not per row) | `function_name`, `call_count`, `duration_ms_p50` |
| `$exception` | a caught error in an instrumented path | `error_class` (enum), `feature` (enum), `phase` (enum) |

### `feature` values and their properties

| `feature` | Fires at | Extra props |
|---|---|---|
| `connection_opened` | an RFC connection is opened (`RfcAuthParams::Connect`) | `auth_kind` ∈ {`basic`, `sso`, `snc`} |
| `sap_rfc` | an RFC function module is invoked (`sap_rfc_invoke`) | `duration_ms` |
| `bapi_call` | as `sap_rfc`, when the module name has the `BAPI_` prefix | `duration_ms` |
| `rfc_table_read` | `sap_read_table` bind | `duration_ms` |
| `odp_extract` | `sap_odp_read_full` / `sap_odp_read_delta` bind | `mode` ∈ {`full`, `delta`}, `duration_ms` |

`odata_read` and `cds_read` are reserved in the shared schema for the OData/CDS
extensions and are **not** emitted by this repository.

`auth_kind` is derived purely from *which* credential fields are set — never
their values. The `BAPI_` classification inspects only the name prefix to pick
an enum constant; the function name itself is not sent.

### `error_class` values (for `$exception`)

`connection_failed`, `auth_error`, `timeout`, `rfc_error` — mapped from the SAP
`RFC_RC` return-code enum. The SAP error **message** is never included.

## Enterprise / account analytics

OSS ERPL associates only the `deployment` group. Enterprise builds with a
license key additionally associate an `account` group keyed on
`sha256(license_id)` (the raw key is hashed, never sent). OSS ERPL has no
license key, so this is not active here.

## Function-call aggregation

DuckDB function calls are recorded via `RecordFunctionCall(function_name)`, which
aggregates in-process into a single `function_executed` event per function per
session (carrying `call_count` and `duration_ms_p50`). This is never done on a
per-row path, so a million-row scan produces O(1) telemetry rows, not a
firehose.
