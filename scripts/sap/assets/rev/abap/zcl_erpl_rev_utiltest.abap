CLASS zcl_erpl_rev_utiltest DEFINITION PUBLIC FINAL CREATE PUBLIC.
  PUBLIC SECTION.
    INTERFACES if_oo_adt_classrun.
  PRIVATE SECTION.
    DATA: mv_pass TYPE i, mv_fail TYPE i, mo TYPE REF TO if_oo_adt_classrun_out.
    METHODS ok IMPORTING cond TYPE abap_bool what TYPE string detail TYPE string DEFAULT ''.
ENDCLASS.

CLASS zcl_erpl_rev_utiltest IMPLEMENTATION.

  METHOD ok.
    IF cond = abap_true.
      mv_pass = mv_pass + 1.
    ELSE.
      mv_fail = mv_fail + 1.
      mo->write( |FAIL { what }: { detail }| ).
    ENDIF.
  ENDMETHOD.

  METHOD if_oo_adt_classrun~main.
    mo = out.

    " 1) query: simple scalar SELECT
    DATA(q) = zcl_erpl_rev_util=>query( `SELECT 42 AS answer` ).
    ok( cond = xsdbool( q-error IS INITIAL ) what = 'query_noerr' detail = q-error ).
    ok( cond = xsdbool( q-row_count = 1 ) what = 'query_rowcount' detail = |{ q-row_count }| ).
    ok( cond = xsdbool( q-rows CS '"answer":42' ) what = 'query_value' detail = q-rows ).

    " 2) describe_table T000 -> DDL with MANDT VARCHAR + PRIMARY KEY (MANDT)
    DATA(d) = zcl_erpl_rev_util=>describe_table( iv_tab = 'T000' iv_target = 'zt000_util' ).
    ok( cond = xsdbool( d-ddl CS 'MANDT VARCHAR' ) what = 'ddl_mandt' detail = d-ddl ).
    ok( cond = xsdbool( d-ddl CS 'PRIMARY KEY (MANDT' ) what = 'ddl_pk' detail = d-keys ).

    " 3) replicate T000 1:1 -> DuckDB, then count matches SAP
    SELECT COUNT(*) FROM t000 INTO @DATA(lv_sap).
    DATA(r) = zcl_erpl_rev_util=>replicate( iv_tab = 'T000' iv_target = 'zt000_util' ).
    ok( cond = xsdbool( r-error IS INITIAL ) what = 'repl_noerr' detail = r-error ).
    ok( cond = xsdbool( r-rows_affected = lv_sap )
        what = 'repl_count' detail = |sap={ lv_sap } affected={ r-rows_affected }| ).

    DATA(c) = zcl_erpl_rev_util=>query( `SELECT count(*) AS c FROM zt000_util` ).
    ok( cond = xsdbool( c-rows CS |"c":{ lv_sap }| )
        what = 'duckdb_count' detail = c-rows ).

    " 4) result_to_alv builds a dynamic table with the right number of rows
    DATA(rr) = zcl_erpl_rev_util=>result_to_alv(
      zcl_erpl_rev_util=>query( `SELECT mandt, mtext FROM zt000_util ORDER BY mandt` ) ).
    FIELD-SYMBOLS <lt> TYPE STANDARD TABLE.
    IF rr IS BOUND.
      ASSIGN rr->* TO <lt>.
      ok( cond = xsdbool( lines( <lt> ) = lv_sap )
          what = 'alv_rows' detail = |{ lines( <lt> ) }| ).
    ELSE.
      ok( cond = abap_false what = 'alv_rows' detail = 'no table' ).
    ENDIF.

    out->write( |UTIL RESULT pass={ mv_pass } fail={ mv_fail }| ).
  ENDMETHOD.

ENDCLASS.
