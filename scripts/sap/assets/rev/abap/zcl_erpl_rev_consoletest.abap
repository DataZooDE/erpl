CLASS zcl_erpl_rev_consoletest DEFINITION PUBLIC FINAL CREATE PUBLIC.
  PUBLIC SECTION.
    INTERFACES if_oo_adt_classrun.
  PRIVATE SECTION.
    DATA: mv_pass TYPE i, mv_fail TYPE i, mo TYPE REF TO if_oo_adt_classrun_out.
    METHODS ok IMPORTING cond TYPE abap_bool what TYPE string detail TYPE string DEFAULT ''.
    "! Run a console-style query through the full streaming path and return the
    "! first row's first cell as text (empty if no rows). Captures any dump as a
    "! caught exception text in cv_err so a bad column name fails the assertion
    "! instead of terminating the program.
    METHODS run
      IMPORTING iv_sql   TYPE string
      EXPORTING ev_err   TYPE string
                ev_rows  TYPE i
                ev_cell1 TYPE string
                ev_cols  TYPE string.
ENDCLASS.

CLASS zcl_erpl_rev_consoletest IMPLEMENTATION.
  METHOD ok.
    IF cond = abap_true. mv_pass = mv_pass + 1.
    ELSE. mv_fail = mv_fail + 1. mo->write( |FAIL { what }: { detail }| ). ENDIF.
  ENDMETHOD.

  METHOD run.
    TRY.
        DATA(s) = zcl_erpl_rev_util=>query_stream( iv_sql = iv_sql ).
        ev_err  = s-error.
        ev_rows = s-row_count.
        ev_cols = s-columns.
        IF s-data IS BOUND.
          FIELD-SYMBOLS <t> TYPE STANDARD TABLE.
          ASSIGN s-data->* TO <t>.
          FIELD-SYMBOLS <r> TYPE any.
          READ TABLE <t> INDEX 1 ASSIGNING <r>.
          IF sy-subrc = 0.
            FIELD-SYMBOLS <f> TYPE any.
            ASSIGN COMPONENT 1 OF STRUCTURE <r> TO <f>.
            IF sy-subrc = 0. ev_cell1 = condense( CONV string( <f> ) ). ENDIF.
          ENDIF.
        ENDIF.
      CATCH cx_root INTO DATA(lx).
        " A structure-build dump (e.g. CX_SY_STRUCT_COMP_NAME) would land here.
        ev_err = |DUMP { lx->get_text( ) }|.
    ENDTRY.
  ENDMETHOD.

  METHOD if_oo_adt_classrun~main.
    mo = out.
    DATA: lv_err TYPE string, lv_rows TYPE i, lv_c1 TYPE string, lv_cols TYPE string.

    " The console accepts ARBITRARY SQL, so the result column names are arbitrary.
    " Each case must come back without an error/dump and with the right value.

    " 1) count(*) — DuckDB names it "count_star()": parens + star are invalid ABAP
    "    component chars (this is the case that dumped in production).
    run( EXPORTING iv_sql = `SELECT count(*) FROM range(2025)`
         IMPORTING ev_err = lv_err ev_rows = lv_rows ev_cell1 = lv_c1 ev_cols = lv_cols ).
    ok( cond = xsdbool( lv_err IS INITIAL AND lv_c1 = '2025' )
        what = 'count_star' detail = |err={ lv_err } c1={ lv_c1 } cols={ lv_cols }| ).

    " 2) Unaliased arithmetic — DuckDB names it "(1 + 2)".
    run( EXPORTING iv_sql = `SELECT 1 + 2` IMPORTING ev_err = lv_err ev_cell1 = lv_c1 ).
    ok( cond = xsdbool( lv_err IS INITIAL AND lv_c1 = '3' )
        what = 'arith_unaliased' detail = |err={ lv_err } c1={ lv_c1 }| ).

    " 3) Unaliased function — DuckDB names it "upper('a')".
    run( EXPORTING iv_sql = `SELECT upper('abc')` IMPORTING ev_err = lv_err ev_cell1 = lv_c1 ).
    ok( cond = xsdbool( lv_err IS INITIAL AND lv_c1 = 'ABC' )
        what = 'func_unaliased' detail = |err={ lv_err } c1={ lv_c1 }| ).

    " 4) CASE expression, unaliased.
    run( EXPORTING iv_sql = `SELECT CASE WHEN 1=1 THEN 'yes' ELSE 'no' END`
         IMPORTING ev_err = lv_err ev_cell1 = lv_c1 ).
    ok( cond = xsdbool( lv_err IS INITIAL AND lv_c1 = 'yes' )
        what = 'case_unaliased' detail = |err={ lv_err } c1={ lv_c1 }| ).

    " 5) A very long expression -> column name well over ABAP's 30-char limit.
    run( EXPORTING iv_sql =
           `SELECT length('the quick brown fox jumps over the lazy dog') * 1000 + 7`
         IMPORTING ev_err = lv_err ev_cell1 = lv_c1 ).
    ok( cond = xsdbool( lv_err IS INITIAL AND lv_c1 = '43007' )
        what = 'long_name' detail = |err={ lv_err } c1={ lv_c1 }| ).

    " 6) Two expressions that sanitize toward the same base -> must not collide.
    run( EXPORTING iv_sql = `SELECT 1+1 AS "a(*)", 2+2 AS "a[*]"`
         IMPORTING ev_err = lv_err ev_rows = lv_rows ).
    ok( cond = xsdbool( lv_err IS INITIAL AND lv_rows = 1 )
        what = 'dup_sanitized' detail = |err={ lv_err }| ).

    " 7) NULL value in an unaliased expression.
    run( EXPORTING iv_sql = `SELECT NULLIF(5,5)` IMPORTING ev_err = lv_err ev_rows = lv_rows ).
    ok( cond = xsdbool( lv_err IS INITIAL AND lv_rows = 1 )
        what = 'null_value' detail = |err={ lv_err }| ).

    " 8) Mixed real types (date/decimal/varchar) with messy unaliased names.
    run( EXPORTING iv_sql =
           `SELECT DATE '2024-01-15', CAST(12.5 AS DECIMAL(10,2)), 'x' || 'y'`
         IMPORTING ev_err = lv_err ev_rows = lv_rows ev_cols = lv_cols ).
    ok( cond = xsdbool( lv_err IS INITIAL AND lv_rows = 1 )
        what = 'mixed_types' detail = |err={ lv_err } cols={ lv_cols }| ).

    " 9) Unicode data round-trips.
    run( EXPORTING iv_sql = `SELECT 'grün' AS txt` IMPORTING ev_err = lv_err ev_cell1 = lv_c1 ).
    ok( cond = xsdbool( lv_err IS INITIAL AND lv_c1 = 'grün' )
        what = 'unicode' detail = |err={ lv_err } c1={ lv_c1 }| ).

    out->write( |CONSOLE RESULT pass={ mv_pass } fail={ mv_fail }| ).
  ENDMETHOD.
ENDCLASS.
