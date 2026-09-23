#include "duckdb.hpp"
#include "scanner_rfc_authorizations.hpp"
#include "telemetry.hpp"

#include <algorithm>

namespace duckdb
{

namespace
{
struct AuthRow {
    const char *extension;
    const char *duckdb_function;
    const char *rfc_function_module;
    const char *invocation;
    const char *purpose;
};

// Which SAP RFC function modules each ERPL DuckDB function invokes — derived by
// call-stack analysis of the rfc / bics / odp / ape scanners (issue #71). `invocation`:
//   always         — called on every execution
//   fallback       — one of a runtime-selected chain (capability-dependent)
//   optional       — attempted, gracefully skipped on failure
//   metadata       — secondary DDIC/describe call enriching the result
//   user-specified — the FM is supplied by the caller at runtime
// Reads-table-backed functions may use any of the RFC_READ_TABLE fallback
// variants documented under sap_read_table; they are listed once there.
const AuthRow AUTH_ROWS[] = {
    // ---- erpl_rfc ----------------------------------------------------------
    {"erpl_rfc", "sap_read_table", "RFC_READ_TABLE", "always", "Primary table / CDS-view read (default reader)."},
    {"erpl_rfc", "sap_read_table", "<custom read table function>", "optional", "User-configured RFC read table function module via READ_TABLE_FUNCTION, secret, or erpl_rfc_read_table_function."},
    {"erpl_rfc", "sap_read_table", "DDIF_FIELDINFO_GET", "metadata", "Field names / types / lengths for the requested table at bind time."},
    {"erpl_rfc", "sap_rfc_invoke", "<user-specified>", "user-specified", "Invokes the function module passed as the first argument; grant S_RFC for whatever you call."},
    {"erpl_rfc", "sap_rfc_show_function", "RFC_FUNCTION_SEARCH", "always", "Search RFC-enabled function modules by name/group pattern."},
    {"erpl_rfc", "sap_rfc_show_groups", "RFC_GROUP_SEARCH", "always", "List SAP function groups."},
    {"erpl_rfc", "sap_rfc_describe_function", "RPY_FUNCTIONMODULE_READ", "optional", "ABAP source / short text; skipped on FL180, falls back to SDK introspection."},
    {"erpl_rfc", "sap_show_tables", "RFC_READ_TABLE", "always", "Resolves the table catalog from DDIC via RFC_READ_TABLE (default reader)."},
    {"erpl_rfc", "sap_show_tables", "<custom read table function>", "optional", "User-configured RFC read table function module via READ_TABLE_FUNCTION, secret, or erpl_rfc_read_table_function."},
    {"erpl_rfc", "sap_show_tables", "DDIF_FIELDINFO_GET", "metadata", "Field metadata for listed tables."},
    {"erpl_rfc", "sap_describe_fields", "DDIF_FIELDINFO_GET", "always", "Field metadata (names, ABAP types, lengths, texts) for a table/structure."},
    {"erpl_rfc", "sap_rfc_ping", "<none>", "always", "Uses the SDK RfcPing() call — no RFC function module is invoked."},

    // ---- erpl_bics: stateful query execution -------------------------------
    {"erpl_bics", "sap_bics_begin", "BICS_CONS_CREATE_DATA_AREA", "always", "Create the BICS application data area on the BW server."},
    {"erpl_bics", "sap_bics_begin", "BICS_PROV_OPEN", "always", "Open the data provider (cube or BEx query)."},
    {"erpl_bics", "sap_bics_begin", "BICS_PROV_GET_INITIAL_STATE", "always", "Fetch the initial query state (axes, characteristics, measures)."},
    {"erpl_bics", "sap_bics_begin", "BICS_PROV_VAR_GET_VARIABLES", "optional", "Read BEx variables when the query defines them."},
    {"erpl_bics", "sap_bics_begin", "RFC_READ_TABLE", "metadata", "Probe RSRREPDIR to detect the BEx query type (default reader)."},
    {"erpl_bics", "sap_bics_begin", "<custom read table function>", "optional", "User-configured RFC read table function module via secret or erpl_rfc_read_table_function."},
    {"erpl_bics", "sap_bics_columns", "BICS_PROV_SET_STATE", "always", "Submit column-axis changes."},
    {"erpl_bics", "sap_bics_columns", "BICS_PROV_GET_RESULT_SET", "optional", "Fetch the updated result set when requested."},
    {"erpl_bics", "sap_bics_rows", "BICS_PROV_SET_STATE", "always", "Submit row-axis changes."},
    {"erpl_bics", "sap_bics_rows", "BICS_PROV_GET_RESULT_SET", "optional", "Fetch the updated result set when requested."},
    {"erpl_bics", "sap_bics_filter", "BICS_PROV_SET_STATE", "always", "Submit filter / restriction changes."},
    {"erpl_bics", "sap_bics_filter", "BICS_PROV_GET_RESULT_SET", "optional", "Fetch the updated result set when requested."},
    {"erpl_bics", "sap_bics_set_char_prop", "BICS_PROV_SET_STATE", "always", "Submit characteristic display / sort / totals properties."},
    {"erpl_bics", "sap_bics_result", "BICS_PROV_GET_RESULT_SET", "always", "Fetch the flattened result set (members + cells)."},
    {"erpl_bics", "sap_bics_result", "BICS_PROV_CLOSE", "always", "Close the data provider when the session ends."},
    {"erpl_bics", "sap_bics_describe", "BICS_PROV_GET_DESIGN_TIME_INFO", "always", "Design-time metadata (dimensions/measures) without executing the query."},
    {"erpl_bics", "sap_bics_describe", "BAPI_IOBJ_GETDETAIL", "optional", "InfoObject attribute / hierarchy / text details."},
    {"erpl_bics", "sap_bics_describe_infoobject", "BAPI_IOBJ_GETDETAIL", "always", "InfoObject type / length / decimals / conversion exit."},

    // ---- erpl_bics: catalog / metadata / lineage (DDIC reads) --------------
    {"erpl_bics", "sap_bics_show", "RSOBJS_GET_NODES", "always", "Browse InfoProviders / queries / InfoAreas."},
    {"erpl_bics", "sap_bics_show_cubes", "RSOBJS_GET_NODES", "always", "List InfoCubes and MultiProviders."},
    {"erpl_bics", "sap_bics_show_queries", "RSOBJS_GET_NODES", "always", "List BEx queries."},
    {"erpl_bics", "sap_bics_show_hierarchies", "RFC_READ_TABLE", "always", "Read RSHIEDIR (hierarchy directory)."},
    {"erpl_bics", "sap_bics_hierarchy", "RFC_READ_TABLE", "always", "Read RSHIEDIR hierarchy node structure."},
    {"erpl_bics", "sap_bics_hierarchy", "RSNDI_SHIE_STRUCTURE_GET3", "optional", "Hierarchy structure with attributes (fallback)."},
    {"erpl_bics", "sap_bics_meta_*", "RFC_READ_TABLE", "always", "All sap_bics_meta_* functions read BW dictionary tables (RSD*, RSO*, RSTRAN*, RSRREPDIR, RSZ*, ROOS*) via RFC_READ_TABLE."},
    {"erpl_bics", "sap_bics_meta_*", "DDIF_FIELDINFO_GET", "metadata", "Field metadata for the BW dictionary tables read above."},
    {"erpl_bics", "sap_bics_lineage_*", "RFC_READ_TABLE", "always", "All sap_bics_lineage_* functions trace lineage by reading RSTRAN/RSTRANFIELD/RSOOBJXREF via RFC_READ_TABLE."},

    // ---- erpl_odp ----------------------------------------------------------
    {"erpl_odp", "sap_odp_show_contexts", "RODPS_REPL_CONTEXT_GET_LIST", "always", "Enumerate ODP contexts (ABAP_CDS, BW, SAPI, ODQF)."},
    {"erpl_odp", "sap_odp_show", "RODPS_REPL_ODP_GET_LIST", "always", "List ODP objects within a context."},
    {"erpl_odp", "sap_odp_describe", "RODPS_REPL_ODP_GET_DETAIL", "always", "Field structure / metadata for an ODP object."},
    {"erpl_odp", "sap_odp_preview", "RODPS_REPL_ODP_GET_DETAIL", "always", "Metadata for the preview."},
    {"erpl_odp", "sap_odp_preview", "RODPS_REPL_ODP_READ_DIRECT_XML", "always", "Direct read of the first rows without a subscription."},
    {"erpl_odp", "sap_odp_show_subscriptions", "RFC_READ_TABLE", "always", "Read the RODPS_REPL_SUBSC view of active subscriptions (default reader)."},
    {"erpl_odp", "sap_odp_show_subscriptions", "<custom read table function>", "optional", "User-configured RFC read table function module via READ_TABLE_FUNCTION, secret, or erpl_rfc_read_table_function."},
    {"erpl_odp", "sap_odp_show_subscriptions", "DDIF_FIELDINFO_GET", "metadata", "Schema of RODPS_REPL_SUBSC."},
    {"erpl_odp", "sap_odp_show_cursors", "RODPS_REPL_CURSOR_GET_LIST", "always", "List delta extraction cursors and status."},
    {"erpl_odp", "sap_odp_read_full", "RODPS_REPL_ODP_GET_DETAIL", "always", "Metadata before full extraction."},
    {"erpl_odp", "sap_odp_read_full", "RODPS_REPL_ODP_OPEN", "always", "Open a full-extraction cursor."},
    {"erpl_odp", "sap_odp_read_full", "RODPS_REPL_ODP_FETCH_XML", "always", "Fetch data packages."},
    {"erpl_odp", "sap_odp_read_full", "RODPS_REPL_ODP_CLOSE", "always", "Close the full-extraction cursor when done."},
    {"erpl_odp", "sap_odp_read_delta", "RODPS_REPL_ODP_GET_DETAIL", "always", "Metadata before delta extraction."},
    {"erpl_odp", "sap_odp_read_delta", "RODPS_REPL_ODP_OPEN", "always", "Open a delta cursor (auto delta-init for a fresh subscriber)."},
    {"erpl_odp", "sap_odp_read_delta", "RODPS_REPL_ODP_FETCH_XML", "always", "Fetch delta packages."},
    {"erpl_odp", "sap_odp_get_last_modified", "RODPS_REPL_ODP_GET_LAST_MODIF", "always", "Probe the latest modification timestamp without opening a cursor."},
    {"erpl_odp", "sap_odp_get_subscriptions", "RODPS_REPL_ODP_GET_SUBSCR", "always", "List all subscriptions for an ODP source."},
    {"erpl_odp", "sap_odp_drop", "RODPS_REPL_ODP_RESET", "always", "Reset a subscription so a full extraction can re-initialize."},
    {"erpl_odp", "sap_odp_close_delta_cursor", "RODPS_REPL_CURSOR_GET_LIST", "always", "Look up the cursor to close."},
    {"erpl_odp", "sap_odp_close_delta_cursor", "RODPS_REPL_ODP_CLOSE", "always", "Gracefully close the delta cursor (idempotent)."},

    // ---- erpl_ape ----------------------------------------------------------
    // Derived from ape/src: the DHAMB_SERVICE_* calls go through
    // ApeCallDhambService (ape/src/ape_metadata.cpp), the DHAPE_GRAPH_* ones
    // through ApeSession (ape/src/ape_session.cpp). Note that the extraction
    // functions reach SAP through only two modules regardless of what the graph
    // does: the pipeline is driven entirely by the graph JSON handed to
    // DHAPE_GRAPH_MANAGER, so S_RFC alone says nothing about which operators a
    // user may run. That is gated per operator by S_DHAPEOPR, which this table
    // cannot express -- use sap_ape_check_authorizations() for the object-level
    // requirements and ape/docs/security.md for the role.
    {"erpl_ape", "sap_ape_show", "DHAMB_SERVICE_DSET_BROWSE", "always", "Walk the metadata browser's CDS folder tree; called once per folder level."},
    {"erpl_ape", "sap_ape_describe", "DHAMB_SERVICE_DSET_BROWSE", "metadata", "Resolve a bare entity name to its browser path before describing it."},
    {"erpl_ape", "sap_ape_describe", "DHAMB_SERVICE_DSET_DEFINITION", "always", "Field structure of a CDS entity (ABAP types, lengths, decimals)."},
    {"erpl_ape", "sap_ape_preview", "DHAMB_SERVICE_DSET_BROWSE", "metadata", "Resolve a bare entity name to its browser path."},
    {"erpl_ape", "sap_ape_preview", "DHAMB_SERVICE_DSET_PREVIEW", "always", "Read a bounded sample directly from the browser, bypassing the pipeline."},
    {"erpl_ape", "sap_ape_read_full", "DHAMB_SERVICE_DSET_DEFINITION", "metadata", "Result schema at bind time, before any graph exists."},
    {"erpl_ape", "sap_ape_read_full", "DHAPE_GRAPH_MANAGER", "always", "Create, start and stop the extraction graph (protocol v6 starts it on create)."},
    {"erpl_ape", "sap_ape_read_full", "DHAPE_GRAPH_ROUNDTRIP", "always", "The only data path: fetch one package per call until the last batch."},
    {"erpl_ape", "sap_ape_read_delta", "DHAMB_SERVICE_DSET_DEFINITION", "metadata", "Result schema at bind time; skipped entirely when recover => true."},
    {"erpl_ape", "sap_ape_read_delta", "DHAPE_GRAPH_MANAGER", "always", "Create, start and stop the delta graph."},
    {"erpl_ape", "sap_ape_read_delta", "DHAPE_GRAPH_ROUNDTRIP", "always", "Fetch delta packages; each handover is committed SAP-side on receipt."},
    {"erpl_ape", "sap_ape_show_subscriptions", "DHAPE_GRAPH_MANAGER", "always", "Run an admin graph built around the subscription reader operator."},
    {"erpl_ape", "sap_ape_show_subscriptions", "DHAPE_GRAPH_ROUNDTRIP", "always", "Collect the subscription list from that graph's outport."},
    {"erpl_ape", "sap_ape_drop", "DHAPE_GRAPH_MANAGER", "always", "Run an admin graph built around the subscription eraser operator."},
    {"erpl_ape", "sap_ape_drop", "DHAPE_GRAPH_ROUNDTRIP", "always", "Drive the eraser and read back its result."},
    {"erpl_ape", "sap_ape_ping", "<none>", "always", "Logon check uses the SDK RfcPing() call -- no function module is invoked."},
    {"erpl_ape", "sap_ape_ping", "DHAPE_GRAPH_VERSION", "always", "A logon proves nothing about the engine; this is the cheapest call that fails when the DHAPE group is unreachable."},
    {"erpl_ape", "sap_ape_system_info", "DHAMB_SERVICE_SYSTEM", "always", "System, release and metadata-browser API version."},
    {"erpl_ape", "sap_ape_check_authorizations", "AUTHORITY_CHECK", "always", "Probe each authorization object erpl_ape needs; reports through its exceptions, once per requirement."},
};

struct RfcAuthorizationsBindData : public TableFunctionData {
    idx_t offset = 0;
};
} // namespace

static unique_ptr<FunctionData> RfcAuthorizationsBind(ClientContext &context,
                                                      TableFunctionBindInput &input,
                                                      vector<LogicalType> &return_types,
                                                      vector<string> &names)
{
    PostHogTelemetry::Instance().RecordFunctionCall("sap_rfc_authorizations");

    names = {"extension", "duckdb_function", "rfc_function_module", "invocation", "purpose"};
    return_types = {LogicalType::VARCHAR, LogicalType::VARCHAR, LogicalType::VARCHAR,
                    LogicalType::VARCHAR, LogicalType::VARCHAR};

    return make_uniq<RfcAuthorizationsBindData>();
}

static void RfcAuthorizationsScan(ClientContext &context, TableFunctionInput &data, DataChunk &output)
{
    auto &bind_data = data.bind_data->CastNoConst<RfcAuthorizationsBindData>();
    const idx_t total = sizeof(AUTH_ROWS) / sizeof(AUTH_ROWS[0]);
    if (bind_data.offset >= total) {
        return;
    }

    idx_t count = std::min<idx_t>(total - bind_data.offset, STANDARD_VECTOR_SIZE);
    for (idx_t i = 0; i < count; i++) {
        const auto &row = AUTH_ROWS[bind_data.offset + i];
        output.SetValue(0, i, Value(row.extension));
        output.SetValue(1, i, Value(row.duckdb_function));
        output.SetValue(2, i, Value(row.rfc_function_module));
        output.SetValue(3, i, Value(row.invocation));
        output.SetValue(4, i, Value(row.purpose));
    }
    output.SetCardinality(count);
    bind_data.offset += count;
}

TableFunction CreateRfcAuthorizationsScanFunction()
{
    return TableFunction("sap_rfc_authorizations", {}, RfcAuthorizationsScan, RfcAuthorizationsBind);
}

} // namespace duckdb
