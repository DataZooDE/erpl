CLASS zcl_erpl_rev_pardemo DEFINITION PUBLIC FINAL CREATE PUBLIC.
  PUBLIC SECTION.
    INTERFACES if_oo_adt_classrun.
  PRIVATE SECTION.
    DATA: mv_pass TYPE i, mv_fail TYPE i, mo TYPE REF TO if_oo_adt_classrun_out.
    METHODS ok IMPORTING cond TYPE abap_bool what TYPE string detail TYPE string DEFAULT ''.
ENDCLASS.

CLASS zcl_erpl_rev_pardemo IMPLEMENTATION.
  METHOD ok.
    IF cond = abap_true. mv_pass = mv_pass + 1.
    ELSE. mv_fail = mv_fail + 1. mo->write( |FAIL { what }: { detail }| ). ENDIF.
  ENDMETHOD.

  METHOD if_oo_adt_classrun~main.
    mo = out.
    " Live parallel partitioned full-load: replicate_parallel splits belnr 1..100
    " into 4 disjoint ranges, SUBMITs 4 background worker jobs (real parallelism: the
    " A4H profile has rdisp/wp_no_btc=5), waits, then builds the PK once. Compared
    " against a single synchronous replicate() of the SAME range for a wall-clock
    " number and an exact-parity check against the SAP source.
    DATA(lv_where) = |belnr BETWEEN '0000000001' AND '0000000100'|.
    TRY.
        SELECT COUNT(*) FROM zwide_bseg INTO @DATA(lv_sap) WHERE belnr BETWEEN '0000000001' AND '0000000100'.

        " --- parallel (4 background jobs) ---
        DATA(rp) = zcl_erpl_rev_util=>replicate_parallel(
          iv_tab = 'ZWIDE_BSEG' iv_target = 'pp_par' iv_part_col = 'BELNR'
          iv_jobs = 4 iv_where = lv_where iv_init = 'SET threads TO 1;' ).
        DATA(qp) = zcl_erpl_rev_util=>query( |SELECT count(*) AS c FROM pp_par| ).
        DATA(dup) = zcl_erpl_rev_util=>query( |INSERT INTO pp_par SELECT * FROM pp_par LIMIT 1| ).

        " --- single job (synchronous baseline) ---
        DATA(rs) = zcl_erpl_rev_util=>replicate(
          iv_tab = 'ZWIDE_BSEG' iv_target = 'pp_one' iv_where = lv_where iv_init = 'SET threads TO 1;' ).

        ok( cond = xsdbool( rp-error IS INITIAL ) what = 'parallel_no_error' detail = rp-error ).
        ok( cond = xsdbool( qp-rows CS |"c":{ lv_sap }| AND rp-rows_affected = lv_sap )
            what = 'parallel_parity' detail = |duck={ qp-rows } sap={ lv_sap } rp={ rp-rows_affected }| ).
        ok( cond = xsdbool( dup-error CS 'primary key' ) what = 'pk_enforced' detail = |dup=[{ dup-error }]| ).
        ok( cond = xsdbool( rs-error IS INITIAL AND rs-rows_affected = lv_sap )
            what = 'single_parity' detail = |rs={ rs-rows_affected } err=[{ rs-error }]| ).

        out->write( |PARDEMO sap_rows={ lv_sap }| ).
        out->write( |PARDEMO parallel_secs={ rp-seconds } single_secs={ rs-seconds }| ).
        out->write( |PARDEMO RESULT pass={ mv_pass } fail={ mv_fail }| ).

        zcl_erpl_rev_util=>query( |DROP TABLE IF EXISTS pp_par; DROP TABLE IF EXISTS pp_one;| ).
      CATCH cx_root INTO DATA(lx).
        out->write( |DUMP: { lx->get_text( ) }| ).
    ENDTRY.
  ENDMETHOD.
ENDCLASS.
