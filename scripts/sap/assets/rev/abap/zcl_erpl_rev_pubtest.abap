CLASS zcl_erpl_rev_pubtest DEFINITION PUBLIC FINAL CREATE PUBLIC.
  PUBLIC SECTION.
    INTERFACES if_oo_adt_classrun.
  PRIVATE SECTION.
    DATA: mv_pass TYPE i, mv_fail TYPE i, mo TYPE REF TO if_oo_adt_classrun_out.
    METHODS ok IMPORTING cond TYPE abap_bool what TYPE string detail TYPE string DEFAULT ''.
ENDCLASS.

CLASS zcl_erpl_rev_pubtest IMPLEMENTATION.
  METHOD ok.
    IF cond = abap_true. mv_pass = mv_pass + 1.
    ELSE. mv_fail = mv_fail + 1. mo->write( |FAIL { what }: { detail }| ). ENDIF.
  ENDMETHOD.

  METHOD if_oo_adt_classrun~main.
    mo = out.
    " Stage-then-publish: replicate a SAP slice into a LOCAL DuckDB holding table,
    " then publish it to external targets (parquet single file, partitioned parquet
    " dataset, and a table in an ATTACHed catalog) and read each back for parity.
    " The attached-catalog path (a second DuckDB file here) is the SAME SQL used for
    " postgres/ducklake/bigquery/iceberg — only the ATTACH/secret differs.
    TRY.
        DATA(lv_where) = |belnr between '0000000001' and '0000000020'|.
        SELECT COUNT(*) FROM zwide_bseg INTO @DATA(lv_sap)
          WHERE belnr BETWEEN '0000000001' AND '0000000020'.

        DATA(lr) = zcl_erpl_rev_util=>replicate(
          iv_tab = 'ZWIDE_BSEG' iv_target = 'pubhold' iv_where = lv_where
          iv_init = 'SET threads TO 1;' iv_build_pk = abap_false ).
        ok( cond = xsdbool( lr-error IS INITIAL AND lr-rows_affected = lv_sap )
            what = 'staged_local' detail = |err=[{ lr-error }] rows={ lr-rows_affected } sap={ lv_sap }| ).

        " (1) single parquet file
        DATA(p1) = zcl_erpl_rev_util=>publish(
          iv_source = 'pubhold' iv_kind = 'PARQUET' iv_dest = '/tmp/erpl_pubtest.parquet' ).
        DATA(c1) = zcl_erpl_rev_util=>query(
          |SELECT count(*) AS c FROM read_parquet('/tmp/erpl_pubtest.parquet')| ).
        ok( cond = xsdbool( p1-error IS INITIAL AND c1-rows CS |"c":{ lv_sap }| )
            what = 'parquet_file' detail = |perr=[{ p1-error }] cnt={ c1-rows }| ).

        " (2) partitioned parquet dataset (by fiscal year)
        DATA(p2) = zcl_erpl_rev_util=>publish(
          iv_source = 'pubhold' iv_kind = 'PARQUET' iv_dest = '/tmp/erpl_pubtest_ds'
          iv_partition_by = 'gjahr' ).
        DATA(c2) = zcl_erpl_rev_util=>query(
          |SELECT count(*) AS c FROM read_parquet('/tmp/erpl_pubtest_ds/**/*.parquet')| ).
        ok( cond = xsdbool( p2-error IS INITIAL AND c2-rows CS |"c":{ lv_sap }| )
            what = 'parquet_dataset' detail = |perr=[{ p2-error }] cnt={ c2-rows }| ).

        " (3) table in an attached catalog (proxy for postgres/ducklake/bigquery)
        zcl_erpl_rev_util=>query( |ATTACH IF NOT EXISTS '/tmp/erpl_pubext.duckdb' AS extdb| ).
        DATA(p3) = zcl_erpl_rev_util=>publish(
          iv_source = 'pubhold' iv_kind = 'TABLE' iv_dest = 'extdb.main.pubtgt' iv_mode = 'FULL' ).
        DATA(c3) = zcl_erpl_rev_util=>query( |SELECT count(*) AS c FROM extdb.main.pubtgt| ).
        ok( cond = xsdbool( p3-error IS INITIAL AND c3-rows CS |"c":{ lv_sap }| )
            what = 'attached_table_full' detail = |perr=[{ p3-error }] cnt={ c3-rows }| ).

        " APPEND doubles it
        DATA(p4) = zcl_erpl_rev_util=>publish(
          iv_source = 'pubhold' iv_kind = 'TABLE' iv_dest = 'extdb.main.pubtgt' iv_mode = 'APPEND' ).
        DATA(c4) = zcl_erpl_rev_util=>query( |SELECT count(*) AS c FROM extdb.main.pubtgt| ).
        ok( cond = xsdbool( p4-error IS INITIAL AND c4-rows CS |"c":{ lv_sap * 2 }| )
            what = 'attached_table_append' detail = |perr=[{ p4-error }] cnt={ c4-rows }| ).

        " (6) the actual end-user report writing to parquet (stage-then-publish via
        "     Z_ERPL_REV_REPLICATE): SUBMIT it, read its list, and re-read the file.
        SUBMIT z_erpl_rev_replicate
          WITH p_tab    = 'ZWIDE_BSEG'
          WITH p_target = 'pubrun'
          WITH p_where  = lv_where
          WITH p_init   = 'SET threads TO 1;'
          WITH r_kd     = ' '
          WITH r_kp     = 'X'
          WITH p_dest   = '/tmp/erpl_pubrun.parquet'
          WITH p_verify = 'X'
          EXPORTING LIST TO MEMORY AND RETURN.
        DATA lt_rlist TYPE STANDARD TABLE OF abaplist.
        CALL FUNCTION 'LIST_FROM_MEMORY' TABLES listobject = lt_rlist EXCEPTIONS OTHERS = 1.
        DATA lt_rtxt TYPE STANDARD TABLE OF char1024.
        CALL FUNCTION 'LIST_TO_ASCI'
          EXPORTING list_index = -1 TABLES listasci = lt_rtxt listobject = lt_rlist
          EXCEPTIONS OTHERS = 1.
        CALL FUNCTION 'LIST_FREE_MEMORY' EXCEPTIONS OTHERS = 0.
        DATA(lv_rep) = concat_lines_of( table = lt_rtxt sep = ` ` ).
        DATA(c6) = zcl_erpl_rev_util=>query(
          |SELECT count(*) AS c FROM read_parquet('/tmp/erpl_pubrun.parquet')| ).
        ok( cond = xsdbool( lv_rep CS 'parquet ->' AND lv_rep CS 'Verify'
                            AND lv_rep CS 'OK' AND c6-rows CS |"c":{ lv_sap }| )
            what = 'report_to_parquet' detail = |cnt={ c6-rows } list=[{ lv_rep }]| ).

        zcl_erpl_rev_util=>query(
          |DROP TABLE IF EXISTS extdb.main.pubtgt; DETACH extdb; DROP TABLE IF EXISTS pubhold| ).
        out->write( |PUBTEST sap_rows={ lv_sap }| ).
        out->write( |PUBTEST RESULT pass={ mv_pass } fail={ mv_fail }| ).
      CATCH cx_root INTO DATA(lx).
        out->write( |DUMP: { lx->get_text( ) }| ).
    ENDTRY.
  ENDMETHOD.
ENDCLASS.
