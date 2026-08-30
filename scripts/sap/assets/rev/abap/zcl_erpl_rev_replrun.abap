CLASS zcl_erpl_rev_replrun DEFINITION PUBLIC FINAL CREATE PUBLIC.
  PUBLIC SECTION.
    INTERFACES if_oo_adt_classrun.
  PRIVATE SECTION.
    DATA: mv_pass TYPE i, mv_fail TYPE i, mo TYPE REF TO if_oo_adt_classrun_out.
    METHODS ok IMPORTING cond TYPE abap_bool what TYPE string detail TYPE string DEFAULT ''.
ENDCLASS.

CLASS zcl_erpl_rev_replrun IMPLEMENTATION.
  METHOD ok.
    IF cond = abap_true. mv_pass = mv_pass + 1.
    ELSE. mv_fail = mv_fail + 1. mo->write( |FAIL { what }: { detail }| ). ENDIF.
  ENDMETHOD.

  METHOD if_oo_adt_classrun~main.
    mo = out.
    " End-to-end of the Z_ERPL_REV_REPLICATE *parallel* branch as a user runs it:
    " SUBMIT the report with the parallel checkbox set (p_jobs=4 forces parallel at
    " 50k; the auto job-count would pick serial below 100k — that path is unit-
    " tested in PARTEST). Partition column left blank so the report auto-resolves
    " it (-> BELNR). Capture the report's list from memory and assert it ran the
    " parallel coordinator and verified row parity.
    TRY.
        DATA(lv_where) = |belnr between '0000000001' and '0000000100'|.
        SUBMIT z_erpl_rev_replicate
          WITH p_tab    = 'ZWIDE_BSEG'
          WITH p_target = 'pp_rep'
          WITH p_where  = lv_where
          WITH p_init   = 'SET threads TO 1;'
          WITH p_par    = abap_true
          WITH p_jobs   = 4
          WITH p_verify = abap_true
          EXPORTING LIST TO MEMORY AND RETURN.

        DATA lt_list TYPE STANDARD TABLE OF abaplist.
        CALL FUNCTION 'LIST_FROM_MEMORY' TABLES listobject = lt_list EXCEPTIONS OTHERS = 1.
        DATA lt_txt TYPE STANDARD TABLE OF char1024.
        CALL FUNCTION 'LIST_TO_ASCI'
          EXPORTING list_index = -1
          TABLES    listasci   = lt_txt listobject = lt_list
          EXCEPTIONS OTHERS    = 1.
        CALL FUNCTION 'LIST_FREE_MEMORY' EXCEPTIONS OTHERS = 0.

        DATA(lv_all) = concat_lines_of( table = lt_txt sep = |{ cl_abap_char_utilities=>newline }| ).

        SELECT COUNT(*) FROM zwide_bseg INTO @DATA(lv_sap)
          WHERE belnr BETWEEN '0000000001' AND '0000000100'.

        ok( cond = xsdbool( lv_all CS 'parallel full-load' AND lv_all CS 'on BELNR' )
            what = 'report_chose_parallel_on_BELNR' detail = lv_all ).
        ok( cond = xsdbool( lv_all CS '4 jobs' )
            what = 'report_used_4_jobs' ).
        ok( cond = xsdbool( lv_all CS |Rows replicated| AND lv_all CS |{ lv_sap }| )
            what = 'report_replicated_all_rows' detail = |sap={ lv_sap }| ).
        ok( cond = xsdbool( lv_all CS 'Verify' AND lv_all CS 'OK' )
            what = 'report_verify_ok' ).
        " independent re-count of the target the report built
        DATA(qc) = zcl_erpl_rev_util=>query( |SELECT count(*) AS c FROM pp_rep| ).
        ok( cond = xsdbool( qc-rows CS |"c":{ lv_sap }| )
            what = 'target_parity' detail = |duck={ qc-rows } sap={ lv_sap }| ).

        " worker progress reaches the JOB LOG (operator visibility in SM37) — the
        " newest finished ERPLR worker is from this run; read its log and assert
        " the per-package + done lines are there, not just "started".
        SELECT jobname, jobcount FROM tbtco
          WHERE jobname LIKE 'ERPLR%' AND status = 'F'
          ORDER BY strtdate DESCENDING, strttime DESCENDING
          INTO TABLE @DATA(lt_j) UP TO 1 ROWS.
        DATA lv_jlog TYPE string.
        IF lt_j IS NOT INITIAL.
          READ TABLE lt_j INTO DATA(ls_j) INDEX 1.
          DATA lt_log TYPE STANDARD TABLE OF tbtc5.
          CALL FUNCTION 'BP_JOBLOG_READ'
            EXPORTING jobcount = ls_j-jobcount jobname = ls_j-jobname
            TABLES    joblogtbl = lt_log
            EXCEPTIONS OTHERS = 1.
          LOOP AT lt_log INTO DATA(lg). lv_jlog = |{ lv_jlog } { lg-text }|. ENDLOOP.
        ENDIF.
        ok( cond = xsdbool( lv_jlog CS 'replicate ZWIDE_BSEG'
                            AND ( lv_jlog CS 'loaded' OR lv_jlog CS 'done:' ) )
            what = 'worker_joblog_progress' detail = lv_jlog ).

        zcl_erpl_rev_util=>query( |DROP TABLE IF EXISTS pp_rep| ).
        out->write( |REPLRUN RESULT pass={ mv_pass } fail={ mv_fail }| ).
      CATCH cx_root INTO DATA(lx).
        out->write( |DUMP: { lx->get_text( ) }| ).
    ENDTRY.
  ENDMETHOD.
ENDCLASS.
