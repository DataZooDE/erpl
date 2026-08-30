CLASS zcl_erpl_rev_cdc DEFINITION PUBLIC FINAL CREATE PUBLIC.
  PUBLIC SECTION.
    " Thin executor for the opt-in trigger-CDC delta tier (epic #17 / ADR-0004).
    " It makes ZERO CDC decisions: the server (Z_DUCKDB_CDC_PLAN / Z_DUCKDB_CDC_APPLY)
    " generates every piece of SQL and owns all state; this class only
    "   (a) runs the opaque DDL the server returns on the SAP DB (cl_sql_statement),
    "   (b) stages the new log rows (replicate_native = ADBC read -> DuckDB), and
    "   (c) calls the server apply, then runs the opaque prune statement.
    " The triggers + log table are customer-owned, in-namespace objects created with
    " the customer's own DDL on the customer's own table (no SAP-proprietary CDC).

    TYPES: BEGIN OF ty_result,
             ins     TYPE i,
             upd     TYPE i,
             del     TYPE i,
             prune   TYPE i,
             applied TYPE abap_bool,
             error   TYPE string,
           END OF ty_result.

    "! Provision CDC for a target: fetch the plan, run the log-table + trigger DDL on
    "! the SAP DB, mark SEEDED. The DuckDB target must already be seeded (a full-load
    "! replicate) before the first run. iv_mode: DELETE_ONLY (default) | FULL_IUD.
    CLASS-METHODS provision
      IMPORTING iv_target TYPE string
                iv_source TYPE string
                iv_keys   TYPE string
                iv_mode   TYPE string DEFAULT 'DELETE_ONLY'
      RETURNING VALUE(rv_error) TYPE string.

    "! Run one CDC cycle: stage the new log rows (seq > position), apply them in the
    "! server (coalesce -> MERGE -> advance position), then prune the SAP log.
    CLASS-METHODS run
      IMPORTING iv_target TYPE string
      RETURNING VALUE(rs) TYPE ty_result.

    "! Tear down: drop the triggers + log table + sequence; mark DISABLED.
    CLASS-METHODS teardown
      IMPORTING iv_target TYPE string
      RETURNING VALUE(rv_error) TYPE string.

    "! Run one cycle for every provisioned (SEEDED/ACTIVE) CDC target — the heartbeat
    "! entry point, called from a periodic job alongside the watermark/snapshot tiers.
    "! Returns the targets it ran.
    CLASS-METHODS run_due
      RETURNING VALUE(rt_targets) TYPE string_table.

  PRIVATE SECTION.
    CONSTANTS c_dest TYPE rfcdest VALUE 'ERPL_REV'.

    TYPES: BEGIN OF ty_plan,
             log_table     TYPE string,
             seq_name      TYPE string,
             op_col        TYPE string,
             seq_col       TYPE string,
             position      TYPE i,
             key_cols      TYPE string_table,
             provision_ddl TYPE string_table,
             teardown_ddl  TYPE string_table,
             read_sql      TYPE string,
             read_from     TYPE string,
             prune_sql     TYPE string,
           END OF ty_plan.

    "! Ask the server for the plan (and drive the state transition for the action).
    CLASS-METHODS plan
      IMPORTING iv_target TYPE string
                iv_action TYPE string
                iv_source TYPE string DEFAULT ''
                iv_keys   TYPE string DEFAULT ''
                iv_mode   TYPE string DEFAULT ''
      EXPORTING es_plan   TYPE ty_plan
                ev_error  TYPE string.

    "! Apply one staged log batch in the server (Z_DUCKDB_CDC_APPLY).
    CLASS-METHODS apply
      IMPORTING iv_target  TYPE string
                iv_staging TYPE string
                iv_keys    TYPE string
      RETURNING VALUE(rs)  TYPE ty_result.

    "! Run one opaque SQL string on the SAP DB. iv_ddl=true -> execute_ddl (CREATE/
    "! DROP), false -> execute_update (the prune DELETE).
    CLASS-METHODS exec_native
      IMPORTING iv_sql   TYPE string
                iv_ddl   TYPE abap_bool DEFAULT abap_true
      RETURNING VALUE(rv_error) TYPE string.

    CLASS-METHODS to_int IMPORTING iv TYPE string RETURNING VALUE(rv) TYPE i.
ENDCLASS.


CLASS zcl_erpl_rev_cdc IMPLEMENTATION.

  METHOD plan.
    DATA lv_plan TYPE string.
    DATA lv_err  TYPE string.
    DATA lv_msg  TYPE c LENGTH 255.
    CALL FUNCTION 'Z_DUCKDB_CDC_PLAN' DESTINATION c_dest
      EXPORTING iv_target   = iv_target
                iv_source   = iv_source
                iv_keys     = iv_keys
                iv_mode     = iv_mode
                iv_action   = iv_action
      IMPORTING ev_plan     = lv_plan
                ev_error    = lv_err
      EXCEPTIONS system_failure        = 1 MESSAGE lv_msg
                 communication_failure = 2 MESSAGE lv_msg
                 OTHERS                = 3.
    IF sy-subrc <> 0.
      ev_error = |RFC subrc={ sy-subrc } { lv_msg }|.
      RETURN.
    ENDIF.
    IF lv_err IS NOT INITIAL.
      ev_error = lv_err.
      RETURN.
    ENDIF.
    IF lv_plan IS NOT INITIAL.
      /ui2/cl_json=>deserialize( EXPORTING json = lv_plan CHANGING data = es_plan ).
    ENDIF.
  ENDMETHOD.

  METHOD apply.
    DATA: lv_ins TYPE string, lv_upd TYPE string, lv_del TYPE string,
          lv_pru TYPE string, lv_app TYPE string.
    DATA lv_msg TYPE c LENGTH 255.
    CALL FUNCTION 'Z_DUCKDB_CDC_APPLY' DESTINATION c_dest
      EXPORTING iv_target  = iv_target
                iv_staging = iv_staging
                iv_keys    = iv_keys
      IMPORTING ev_ins     = lv_ins
                ev_upd     = lv_upd
                ev_del     = lv_del
                ev_prune   = lv_pru
                ev_applied = lv_app
                ev_error   = rs-error
      EXCEPTIONS system_failure        = 1 MESSAGE lv_msg
                 communication_failure = 2 MESSAGE lv_msg
                 OTHERS                = 3.
    IF sy-subrc <> 0.
      rs-error = |RFC subrc={ sy-subrc } { lv_msg }|.
      RETURN.
    ENDIF.
    rs-ins     = to_int( lv_ins ).
    rs-upd     = to_int( lv_upd ).
    rs-del     = to_int( lv_del ).
    rs-prune   = to_int( lv_pru ).
    rs-applied = xsdbool( lv_app = 'X' ).
  ENDMETHOD.

  METHOD exec_native.
    TRY.
        IF iv_ddl = abap_true.
          NEW cl_sql_statement( )->execute_ddl( iv_sql ).
        ELSE.
          NEW cl_sql_statement( )->execute_update( iv_sql ).
        ENDIF.
      CATCH cx_root INTO DATA(lx).
        rv_error = lx->get_text( ).
    ENDTRY.
  ENDMETHOD.

  METHOD provision.
    " Safety gate (ADR-0004): triggers can only go on a TRANSPARENT table. Pool/cluster
    " tables, views and activation-request (ADSO) objects are not trigger-trackable —
    " refuse with guidance to use SNAPSHOT instead, rather than failing obscurely.
    DATA lv_src TYPE tabname.
    lv_src = to_upper( iv_source ).
    SELECT SINGLE tabclass FROM dd02l INTO @DATA(lv_class) WHERE tabname = @lv_src.
    IF sy-subrc = 0 AND lv_class <> 'TRANSP'.
      rv_error = |CDC: source { iv_source } is { lv_class }; trigger-CDC needs a | &&
                 |transparent table (pool/cluster/view/ADSO are not trigger-trackable — | &&
                 |use the SNAPSHOT delta method instead)|.
      RETURN.
    ENDIF.

    DATA ls TYPE ty_plan.
    plan( EXPORTING iv_target = iv_target iv_action = 'PROVISION'
                    iv_source = iv_source iv_keys = iv_keys iv_mode = iv_mode
          IMPORTING es_plan = ls ev_error = rv_error ).
    IF rv_error IS NOT INITIAL. RETURN. ENDIF.
    " Drop any leftover objects from a prior run first (best-effort) so provisioning
    " is idempotent even when the server's DuckDB state was reset but the SAP-side
    " trigger/log objects still exist. The trigger is dropped first (teardown order).
    LOOP AT ls-teardown_ddl INTO DATA(lv_drop).
      exec_native( iv_sql = lv_drop iv_ddl = abap_true ).
    ENDLOOP.
    LOOP AT ls-provision_ddl INTO DATA(lv_ddl).
      rv_error = exec_native( iv_sql = lv_ddl iv_ddl = abap_true ).
      IF rv_error IS NOT INITIAL. RETURN. ENDIF.
    ENDLOOP.
    " triggers + log are live and the log starts empty -> mark SEEDED (position 0).
    plan( EXPORTING iv_target = iv_target iv_action = 'SEED' IMPORTING ev_error = rv_error ).
  ENDMETHOD.

  METHOD run.
    DATA ls TYPE ty_plan.
    plan( EXPORTING iv_target = iv_target iv_action = 'CYCLE'
          IMPORTING es_plan = ls ev_error = rs-error ).
    IF rs-error IS NOT INITIAL. RETURN. ENDIF.

    " Stage the new log rows (seq > current position) into a DuckDB staging table
    " via the ADBC native read — the server then coalesces + applies them.
    DATA(lv_stg) = |{ iv_target }__cdclog|.
    DATA(lr) = zcl_erpl_rev_util=>replicate_native(
      iv_from   = ls-read_from
      iv_target = lv_stg
      iv_where  = |"{ ls-seq_col }" > { ls-position }| ).
    IF lr-error IS NOT INITIAL. rs-error = lr-error. RETURN. ENDIF.

    DATA(lv_keys) = concat_lines_of( table = ls-key_cols sep = `,` ).
    rs = apply( iv_target = iv_target iv_staging = lv_stg iv_keys = lv_keys ).
    IF rs-error IS NOT INITIAL. RETURN. ENDIF.

    " Prune the SAP log up to the server-confirmed position (watermark-driven, never
    " destructive-on-read). Only when something was actually applied.
    IF rs-applied = abap_true.
      DATA(lv_prune) = replace( val = ls-prune_sql sub = `%CONF%` with = |{ rs-prune }| occ = 0 ).
      rs-error = exec_native( iv_sql = lv_prune iv_ddl = abap_false ).
    ENDIF.
  ENDMETHOD.

  METHOD teardown.
    DATA ls TYPE ty_plan.
    plan( EXPORTING iv_target = iv_target iv_action = 'DISABLE'
          IMPORTING es_plan = ls ev_error = rv_error ).
    IF rv_error IS NOT INITIAL. RETURN. ENDIF.
    " Best-effort: drop everything, keep the first error (so a missing object doesn't
    " abort the rest of the cleanup).
    LOOP AT ls-teardown_ddl INTO DATA(lv_ddl).
      DATA(lv_e) = exec_native( iv_sql = lv_ddl iv_ddl = abap_true ).
      IF lv_e IS NOT INITIAL AND rv_error IS INITIAL. rv_error = lv_e. ENDIF.
    ENDLOOP.
  ENDMETHOD.

  METHOD run_due.
    DATA(ls) = zcl_erpl_rev_util=>query(
      |SELECT target FROM _erpl_rev_cdc WHERE status IN ('SEEDED','ACTIVE')| ).
    IF ls-error IS NOT INITIAL OR ls-row_count = 0. RETURN. ENDIF.
    TYPES: BEGIN OF ty_t, target TYPE string, END OF ty_t.
    DATA lt TYPE STANDARD TABLE OF ty_t WITH EMPTY KEY.
    /ui2/cl_json=>deserialize( EXPORTING json = ls-rows CHANGING data = lt ).
    LOOP AT lt INTO DATA(ls_t).
      run( ls_t-target ).
      APPEND ls_t-target TO rt_targets.
    ENDLOOP.
  ENDMETHOD.

  METHOD to_int.
    IF iv CO ` 0123456789-`. rv = CONV i( iv ). ENDIF.
  ENDMETHOD.

ENDCLASS.
