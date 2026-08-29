CLASS zcl_erpl_rev_partest DEFINITION PUBLIC FINAL CREATE PUBLIC.
  PUBLIC SECTION.
    INTERFACES if_oo_adt_classrun.
  PRIVATE SECTION.
    DATA: mv_pass TYPE i, mv_fail TYPE i, mo TYPE REF TO if_oo_adt_classrun_out.
    METHODS ok IMPORTING cond TYPE abap_bool what TYPE string detail TYPE string DEFAULT ''.
ENDCLASS.

CLASS zcl_erpl_rev_partest IMPLEMENTATION.
  METHOD ok.
    IF cond = abap_true. mv_pass = mv_pass + 1.
    ELSE. mv_fail = mv_fail + 1. mo->write( |FAIL { what }: { detail }| ). ENDIF.
  ENDMETHOD.

  METHOD if_oo_adt_classrun~main.
    mo = out.
    " Partitioned full-load: one COORDINATOR creates the heap target (no PK) and
    " builds the PRIMARY KEY once at the end; N WORKERS each replicate a DISJOINT
    " key range into the shared heap with iv_create=false + iv_build_pk=false (the
    " enabler for parallel background-job replication). Here the two workers run
    " synchronously — this proves correctness of the merge + deferred-PK contract;
    " M2's per-connection model is what lets real background jobs run them in
    " parallel on the server.
    TRY.
        DATA(d) = zcl_erpl_rev_util=>describe_table( iv_tab = 'ZWIDE_BSEG' iv_target = 'par_tgt' ).
        zcl_erpl_rev_util=>query( |DROP TABLE IF EXISTS par_tgt; { d-ddl_nopk }| ).
        DATA(ra) = zcl_erpl_rev_util=>replicate(
          iv_tab = 'ZWIDE_BSEG' iv_target = 'par_tgt' iv_init = 'SET threads TO 1;'
          iv_where = |belnr BETWEEN '0000000001' AND '0000000020'|
          iv_truncate = abap_true iv_create = abap_false iv_build_pk = abap_false ).
        DATA(rb) = zcl_erpl_rev_util=>replicate(
          iv_tab = 'ZWIDE_BSEG' iv_target = 'par_tgt'
          iv_where = |belnr BETWEEN '0000000021' AND '0000000040'|
          iv_truncate = abap_true iv_create = abap_false iv_build_pk = abap_false ).
        DATA(pk) = zcl_erpl_rev_util=>query( |ALTER TABLE par_tgt ADD PRIMARY KEY ({ d-keys })| ).
        DATA(dup) = zcl_erpl_rev_util=>query( |INSERT INTO par_tgt SELECT * FROM par_tgt LIMIT 1| ).
        DATA(qc) = zcl_erpl_rev_util=>query( |SELECT count(*) AS c FROM par_tgt| ).
        SELECT COUNT(*) FROM zwide_bseg INTO @DATA(lv_sap)
          WHERE belnr BETWEEN '0000000001' AND '0000000040'.

        ok( cond = xsdbool( ra-error IS INITIAL AND rb-error IS INITIAL )
            what = 'workers_no_error' detail = |A=[{ ra-error }] B=[{ rb-error }]| ).
        ok( cond = xsdbool( pk-error IS INITIAL )
            what = 'pk_built' detail = pk-error ).
        ok( cond = xsdbool( dup-error CS 'primary key' )
            what = 'pk_enforced' detail = |dup_err=[{ dup-error }]| ).
        DATA(lv_total) = ra-rows_affected + rb-rows_affected.
        ok( cond = xsdbool( qc-rows CS |"c":{ lv_total }| AND lv_total = lv_sap )
            what = 'count_parity' detail = |duck={ qc-rows } sap={ lv_sap } workers={ lv_total }| ).

        " --- auto partition column + auto job count (what Z_ERPL_REV_REPLICATE
        "     prefills/determines for the user) ---
        DATA(lv_pcol) = zcl_erpl_rev_util=>pick_partition_col( d-fields ).
        ok( cond = xsdbool( lv_pcol = 'BELNR' )
            what = 'pick_partition_col' detail = |picked=[{ lv_pcol }] (widest numeric key)| ).
        ok( cond = xsdbool( zcl_erpl_rev_util=>recommend_jobs( iv_rows = 50000 ) = 1 )
            what = 'recommend_jobs_small' detail = |{ zcl_erpl_rev_util=>recommend_jobs( iv_rows = 50000 ) }| ).
        ok( cond = xsdbool( zcl_erpl_rev_util=>recommend_jobs( iv_rows = 120000 ) = 2 )
            what = 'recommend_jobs_min2' detail = |{ zcl_erpl_rev_util=>recommend_jobs( iv_rows = 120000 ) }| ).
        ok( cond = xsdbool( zcl_erpl_rev_util=>recommend_jobs( iv_rows = 1000000 iv_cap = 8 ) = 4 )
            what = 'recommend_jobs_target' detail = |{ zcl_erpl_rev_util=>recommend_jobs( iv_rows = 1000000 iv_cap = 8 ) }| ).
        ok( cond = xsdbool( zcl_erpl_rev_util=>recommend_jobs( iv_rows = 10000000 iv_cap = 8 ) = 8 )
            what = 'recommend_jobs_cap' detail = |{ zcl_erpl_rev_util=>recommend_jobs( iv_rows = 10000000 iv_cap = 8 ) }| ).

        zcl_erpl_rev_util=>query( |DROP TABLE IF EXISTS par_tgt| ).
        out->write( |PARTITION RESULT pass={ mv_pass } fail={ mv_fail }| ).
      CATCH cx_root INTO DATA(lx).
        out->write( |DUMP: { lx->get_text( ) }| ).
    ENDTRY.
  ENDMETHOD.
ENDCLASS.
