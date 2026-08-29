CLASS zcl_erpl_rev_repltest DEFINITION PUBLIC FINAL CREATE PUBLIC.
  PUBLIC SECTION.
    INTERFACES if_oo_adt_classrun.
  PRIVATE SECTION.
    DATA: mv_pass TYPE i, mv_fail TYPE i, mo TYPE REF TO if_oo_adt_classrun_out.
    METHODS ok IMPORTING cond TYPE abap_bool what TYPE string detail TYPE string DEFAULT ''.
ENDCLASS.

CLASS zcl_erpl_rev_repltest IMPLEMENTATION.
  METHOD ok.
    IF cond = abap_true. mv_pass = mv_pass + 1.
    ELSE. mv_fail = mv_fail + 1. mo->write( |FAIL { what }: { detail }| ). ENDIF.
  ENDMETHOD.

  METHOD if_oo_adt_classrun~main.
    mo = out.

    " describe SFLIGHT: must map typed columns (DATE / DECIMAL), not all VARCHAR.
    DATA(d) = zcl_erpl_rev_util=>describe_table( iv_tab = 'SFLIGHT' iv_target = 'sflight_rt' ).
    ok( cond = xsdbool( d-ddl CS 'FLDATE DATE' ) what = 'sflight_fldate_date' detail = d-ddl ).
    ok( cond = xsdbool( d-ddl CS 'DECIMAL' )     what = 'sflight_has_decimal' detail = d-ddl ).
    ok( cond = xsdbool( d-keys CS 'CARRID' AND d-keys CS 'CONNID' AND d-keys CS 'FLDATE' )
        what = 'sflight_keys' detail = d-keys ).

    " replicate (capped) and verify count parity SAP vs DuckDB.
    DATA(r) = zcl_erpl_rev_util=>replicate(
                iv_tab = 'SFLIGHT' iv_target = 'sflight_rt'
                iv_init = 'SET threads TO 1;' iv_maxrows = 5000 ).
    ok( cond = xsdbool( r-error IS INITIAL ) what = 'repl_noerr' detail = r-error ).

    SELECT COUNT(*) FROM sflight INTO @DATA(lv_sap) UP TO 5000 ROWS.
    ok( cond = xsdbool( r-rows_affected = lv_sap )
        what = 'repl_count' detail = |sap={ lv_sap } got={ r-rows_affected }| ).

    DATA(c) = zcl_erpl_rev_util=>query( `SELECT count(*) AS c FROM sflight_rt` ).
    ok( cond = xsdbool( c-rows CS |"c":{ lv_sap }| ) what = 'duckdb_count' detail = c-rows ).

    " typed round-trip: FLDATE stored as a real DATE.
    DATA(s) = zcl_erpl_rev_util=>query(
      `SELECT typeof(fldate) AS t_date FROM sflight_rt LIMIT 1` ).
    ok( cond = xsdbool( s-rows CS '"t_date":"DATE"' ) what = 'fldate_is_date' detail = s-rows ).

    out->write( |REPL RESULT pass={ mv_pass } fail={ mv_fail }| ).
  ENDMETHOD.
ENDCLASS.
