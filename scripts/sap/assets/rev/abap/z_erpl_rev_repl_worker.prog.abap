*&---------------------------------------------------------------------*
*& Report  Z_ERPL_REV_REPL_WORKER
*&---------------------------------------------------------------------*
*& Partitioned-replication WORKER: replicate ONE key-range partition into a
*& pre-existing heap target (the coordinator zcl_erpl_rev_util=>replicate_parallel
*& created the heap and will build the PRIMARY KEY once all workers finish). Runs
*& as a background job; raises MESSAGE TYPE 'E' on error so the job aborts and the
*& coordinator (polling TBTCO) sees the failure.
*&
*& Progress: each ingested package emits a MESSAGE TYPE 'S', plus a start and a
*& done line. In a background job those land in the JOB LOG (SM37 -> Job log), so
*& an operator can watch a worker's rows/s climb instead of seeing only "started".
*&---------------------------------------------------------------------*
REPORT z_erpl_rev_repl_worker.

PARAMETERS:
  p_tab   TYPE string,            " source table
  p_tgt   TYPE string,            " target (already created as a heap by coordinator)
  p_where TYPE string,            " this partition's OpenSQL WHERE (range predicate)
  p_cols  TYPE string,            " projected columns (blank = all)
  p_init  TYPE string,            " DuckDB init SQL
  p_parm  TYPE string,            " CDS source parameters (FROM name( ... ))
  p_batch TYPE i DEFAULT 50000.   " package size

" Progress sink -> job log. tick fires per ingested package; note carries any
" free-text status. MESSAGE TYPE 'S' in a background job is captured to the job log.
CLASS lcl_log DEFINITION.
  PUBLIC SECTION.
    INTERFACES zif_erpl_rev_progress.
ENDCLASS.
CLASS lcl_log IMPLEMENTATION.
  METHOD zif_erpl_rev_progress~tick.
    DATA lv_rps TYPE i.
    IF iv_seconds > 0. lv_rps = iv_rows / iv_seconds. ENDIF.
    MESSAGE |loaded { iv_rows } rows @ { lv_rps } rows/s, { iv_seconds }s| TYPE 'S'.
  ENDMETHOD.
  METHOD zif_erpl_rev_progress~note.
    MESSAGE iv_text TYPE 'S'.
  ENDMETHOD.
ENDCLASS.

START-OF-SELECTION.
  MESSAGE |replicate { p_tab } -> { p_tgt }, package { p_batch } rows, filter [{ p_where }]| TYPE 'S'.
  DATA(lo_log) = NEW lcl_log( ).
  DATA(r) = zcl_erpl_rev_util=>replicate(
    iv_tab      = p_tab
    iv_target   = p_tgt
    iv_columns  = p_cols
    iv_where    = p_where
    iv_init     = p_init
    iv_params   = p_parm
    iv_batch    = p_batch
    iv_truncate = abap_true        " INSERT mode + heap semantics, but...
    iv_create   = abap_false       " ...coordinator already created the heap, and
    iv_build_pk = abap_false       " ...coordinator builds the PK once at the end.
    iv_record   = abap_false       " ...coordinator records ONE FULL stats row.
    ii_progress = lo_log ).
  IF r-error IS NOT INITIAL.
    MESSAGE |worker error: { r-error }| TYPE 'E'.
  ENDIF.
  MESSAGE |done: { r-rows_affected } rows in { r-seconds }s| TYPE 'S'.
  WRITE: / |worker ok rows={ r-rows_affected } secs={ r-seconds }|.
