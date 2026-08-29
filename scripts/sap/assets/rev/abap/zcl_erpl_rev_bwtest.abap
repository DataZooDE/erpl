CLASS zcl_erpl_rev_bwtest DEFINITION PUBLIC FINAL CREATE PUBLIC.
  PUBLIC SECTION.
    INTERFACES if_oo_adt_classrun.
  PRIVATE SECTION.
    DATA: mv_pass TYPE i, mv_fail TYPE i, mo TYPE REF TO if_oo_adt_classrun_out.
    METHODS ok IMPORTING cond TYPE abap_bool what TYPE string detail TYPE string DEFAULT ''.
ENDCLASS.

CLASS zcl_erpl_rev_bwtest IMPLEMENTATION.
  METHOD ok.
    IF cond = abap_true. mv_pass = mv_pass + 1.
    ELSE. mv_fail = mv_fail + 1. mo->write( |FAIL { what }: { detail }| ). ENDIF.
  ENDMETHOD.

  METHOD if_oo_adt_classrun~main.
    mo = out.
    " BW/native (ADBC) source path. A real BW _SYS_BIC calc view can't exist on this
    " AS-ABAP box, so a HANA VIEW created via native SQL stands in for one — it
    " exercises the SAME replicate_native code path (ADBC read -> result metadata ->
    " BXML ingest -> DuckDB). Native SQL sees ALL clients (no MANDT filter), so parity
    " is taken against the native count, not the Open SQL one.
    TRY.
        NEW cl_sql_statement( )->execute_ddl(
          `CREATE OR REPLACE VIEW ZERPL_HV AS SELECT CARRID, CONNID, FLDATE, SEATSOCC FROM SFLIGHT` ).
        DATA(lo_r) = NEW cl_sql_statement( )->execute_query(
          `SELECT COUNT(*) AS N, SUM(SEATSOCC) AS S FROM ZERPL_HV` ).
        DATA: BEGIN OF ls, n TYPE i, s TYPE i, END OF ls.
        GET REFERENCE OF ls INTO DATA(lr).
        lo_r->set_param_struct( lr ). lo_r->next( ). lo_r->close( ).

        DATA(r) = zcl_erpl_rev_util=>replicate_native(
          iv_from = 'ZERPL_HV' iv_target = 'bw_flights' iv_init = 'SET threads TO 1;' ).
        DATA(qc) = zcl_erpl_rev_util=>query( |SELECT count(*) AS c FROM bw_flights| ).
        ok( cond = xsdbool( r-error IS INITIAL AND r-rows_affected = ls-n AND qc-rows CS |"c":{ ls-n }| )
            what = 'native_count_parity' detail = |err=[{ r-error }] rep={ r-rows_affected } native={ ls-n } duck={ qc-rows }| ).

        DATA(qs) = zcl_erpl_rev_util=>query( |SELECT sum(seatsocc) AS s FROM bw_flights| ).
        ok( cond = xsdbool( qs-rows CS |"s":{ ls-s }| )
            what = 'native_value_parity' detail = |native_sum={ ls-s } duck={ qs-rows }| ).

        DATA(p) = zcl_erpl_rev_util=>publish(
          iv_source = 'bw_flights' iv_kind = 'PARQUET' iv_dest = '/tmp/erpl_bw.parquet' ).
        DATA(cp) = zcl_erpl_rev_util=>query( |SELECT count(*) AS c FROM read_parquet('/tmp/erpl_bw.parquet')| ).
        ok( cond = xsdbool( p-error IS INITIAL AND cp-rows CS |"c":{ ls-n }| )
            what = 'native_to_parquet' detail = |perr=[{ p-error }] cnt={ cp-rows }| ).

        " (4) parameterized HANA SQLScript table function — a scripted-calc-view
        "     analogue with an input parameter (the closest creatable proxy for a BW
        "     calc view here). replicate_native reads `SELECT * FROM ZERPL_TF('LH')`.
        TRY. NEW cl_sql_statement( )->execute_ddl( `DROP FUNCTION ZERPL_TF` ). CATCH cx_root ##NO_HANDLER. ENDTRY.
        NEW cl_sql_statement( )->execute_ddl(
          `CREATE FUNCTION ZERPL_TF (IN P_CARRID NVARCHAR(3))` &&
          ` RETURNS TABLE (CARRID NVARCHAR(3), CONNID NVARCHAR(4), FLDATE NVARCHAR(8), SEATSOCC INTEGER)` &&
          ` LANGUAGE SQLSCRIPT READS SQL DATA AS BEGIN` &&
          ` RETURN SELECT CARRID, CONNID, FLDATE, SEATSOCC FROM SFLIGHT WHERE CARRID = :P_CARRID; END;` ).
        DATA(lo_f) = NEW cl_sql_statement( )->execute_query(
          `SELECT COUNT(*) AS N, SUM(SEATSOCC) AS S FROM ZERPL_TF('LH')` ).
        DATA: BEGIN OF lf, n TYPE i, s TYPE i, END OF lf.
        GET REFERENCE OF lf INTO DATA(lrf).
        lo_f->set_param_struct( lrf ). lo_f->next( ). lo_f->close( ).

        DATA(rf) = zcl_erpl_rev_util=>replicate_native(
          iv_from = |ZERPL_TF('LH')| iv_target = 'bw_tf' iv_init = 'SET threads TO 1;' ).
        DATA(qf) = zcl_erpl_rev_util=>query( |SELECT count(*) AS c, sum(seatsocc) AS s FROM bw_tf| ).
        ok( cond = xsdbool( rf-error IS INITIAL AND rf-rows_affected = lf-n AND qf-rows CS |"c":{ lf-n }| )
            what = 'table_function_count_parity' detail = |err=[{ rf-error }] rep={ rf-rows_affected } hana={ lf-n } duck={ qf-rows }| ).
        ok( cond = xsdbool( qf-rows CS |"s":{ lf-s }| )
            what = 'table_function_value_parity' detail = |hana_sum={ lf-s } duck={ qf-rows }| ).

        zcl_erpl_rev_util=>query( |DROP TABLE IF EXISTS bw_flights; DROP TABLE IF EXISTS bw_tf| ).
        NEW cl_sql_statement( )->execute_ddl( `DROP VIEW ZERPL_HV` ).
        TRY. NEW cl_sql_statement( )->execute_ddl( `DROP FUNCTION ZERPL_TF` ). CATCH cx_root ##NO_HANDLER. ENDTRY.
        out->write( |BW native_rows={ ls-n }| ).
        out->write( |BW RESULT pass={ mv_pass } fail={ mv_fail }| ).
      CATCH cx_root INTO DATA(lx).
        out->write( |DUMP: { lx->get_text( ) }| ).
    ENDTRY.
  ENDMETHOD.
ENDCLASS.
