CLASS zcl_erpl_rev_slttest DEFINITION PUBLIC FINAL CREATE PUBLIC.
  PUBLIC SECTION.
    INTERFACES if_oo_adt_classrun.
  PRIVATE SECTION.
    DATA: mv_pass TYPE i, mv_fail TYPE i, mo TYPE REF TO if_oo_adt_classrun_out.
    METHODS ok IMPORTING cond TYPE abap_bool what TYPE string detail TYPE string DEFAULT ''.
    "! count(*) of a DuckDB DESCRIBE subquery (number of columns in a table).
    METHODS col_count IMPORTING iv_target TYPE string RETURNING VALUE(rv) TYPE i.
    "! does a DuckDB table have a column (DuckDB folds unquoted names to lower)?
    METHODS has_col IMPORTING iv_target TYPE string iv_col TYPE string
                    RETURNING VALUE(rv) TYPE abap_bool.
    "! count(*) of a DuckDB table.
    METHODS row_count IMPORTING iv_target TYPE string RETURNING VALUE(rv) TYPE i.
ENDCLASS.

CLASS zcl_erpl_rev_slttest IMPLEMENTATION.
  METHOD ok.
    IF cond = abap_true. mv_pass = mv_pass + 1.
    ELSE. mv_fail = mv_fail + 1. mo->write( |FAIL { what }: { detail }| ). ENDIF.
  ENDMETHOD.

  METHOD col_count.
    DATA(q) = zcl_erpl_rev_util=>query(
      |SELECT count(*) AS c FROM (DESCRIBE { iv_target })| ).
    FIND PCRE '"c":(\d+)' IN q-rows SUBMATCHES DATA(lv_n).
    IF lv_n CO '0123456789'. rv = CONV i( lv_n ). ENDIF.
  ENDMETHOD.

  METHOD has_col.
    DATA(q) = zcl_erpl_rev_util=>query(
      |SELECT count(*) AS c FROM (DESCRIBE { iv_target }) | &&
      |WHERE lower(column_name) = '{ to_lower( iv_col ) }'| ).
    rv = xsdbool( q-rows CS '"c":1' ).
  ENDMETHOD.

  METHOD row_count.
    DATA(q) = zcl_erpl_rev_util=>query( |SELECT count(*) AS c FROM { iv_target }| ).
    FIND PCRE '"c":(\d+)' IN q-rows SUBMATCHES DATA(lv_n).
    IF lv_n CO '0123456789'. rv = CONV i( lv_n ). ENDIF.
  ENDMETHOD.

  METHOD if_oo_adt_classrun~main.
    mo = out.

    " ============================================================ "
    " 1) FIELD SELECTION (column projection). Keys are auto-kept,   "
    "    so SFLIGHT (key MANDT CARRID CONNID FLDATE) projected to   "
    "    CARRID CONNID FLDATE PRICE yields those 5 columns, not the "
    "    full ~10. PLANETYPE (not selected, not key) must be gone.  "
    " ============================================================ "
    DATA(d1) = zcl_erpl_rev_util=>describe_table(
                 iv_tab = 'SFLIGHT' iv_target = 'slt_proj'
                 iv_columns = 'CARRID CONNID FLDATE PRICE' ).
    ok( cond = xsdbool( d1-error IS INITIAL ) what = 'proj_describe_noerr' detail = d1-error ).
    ok( cond = xsdbool( d1-ddl CS 'FLDATE DATE' ) what = 'proj_fldate_date' detail = d1-ddl ).
    ok( cond = xsdbool( d1-ddl CS 'DECIMAL' ) what = 'proj_price_decimal' detail = d1-ddl ).
    ok( cond = xsdbool( d1-ddl NS 'PLANETYPE' ) what = 'proj_ddl_no_planetype' detail = d1-ddl ).
    ok( cond = xsdbool( d1-added_keys CS 'MANDT' ) what = 'proj_added_mandt' detail = d1-added_keys ).

    DATA(r1) = zcl_erpl_rev_util=>replicate(
                 iv_tab = 'SFLIGHT' iv_target = 'slt_proj'
                 iv_init = 'SET threads TO 1;'
                 iv_columns = 'CARRID CONNID FLDATE PRICE' iv_maxrows = 5000 ).
    ok( cond = xsdbool( r1-error IS INITIAL ) what = 'proj_repl_noerr' detail = r1-error ).
    ok( cond = xsdbool( col_count( 'slt_proj' ) = 5 )
        what = 'proj_5_cols' detail = |cols={ col_count( 'slt_proj' ) }| ).
    ok( cond = xsdbool( has_col( iv_target = 'slt_proj' iv_col = 'PRICE' ) = abap_true
                    AND has_col( iv_target = 'slt_proj' iv_col = 'CARRID' ) = abap_true
                    AND has_col( iv_target = 'slt_proj' iv_col = 'PLANETYPE' ) = abap_false )
        what = 'proj_col_membership' ).

    " ============================================================ "
    " 2) FILTER at source. Only CARRID='LH' rows transferred.       "
    "    Parity vs SAP (data-independent), distinct carrid = 1,     "
    "    and literal case preserved ('LH' not lower-cased).         "
    " ============================================================ "
    SELECT COUNT(*) FROM sflight WHERE carrid = 'LH' INTO @DATA(lv_lh) UP TO 5000 ROWS.
    ok( cond = xsdbool( lv_lh > 0 ) what = 'filter_has_lh_data' detail = |sap_lh={ lv_lh }| ).
    DATA(r2) = zcl_erpl_rev_util=>replicate(
                 iv_tab = 'SFLIGHT' iv_target = 'slt_lh'
                 iv_init = 'SET threads TO 1;'
                 iv_where = `CARRID = 'LH'` iv_maxrows = 5000 ).
    ok( cond = xsdbool( r2-error IS INITIAL ) what = 'filter_repl_noerr' detail = r2-error ).
    ok( cond = xsdbool( row_count( 'slt_lh' ) = lv_lh )
        what = 'filter_count_parity' detail = |sap={ lv_lh } duck={ row_count( 'slt_lh' ) }| ).
    DATA(c2) = zcl_erpl_rev_util=>query( `SELECT count(DISTINCT carrid) AS c FROM slt_lh` ).
    ok( cond = xsdbool( c2-rows CS '"c":1' ) what = 'filter_one_carrid' detail = c2-rows ).

    " ============================================================ "
    " 3) PROJECTION + FILTER combined.                             "
    " ============================================================ "
    DATA(r3) = zcl_erpl_rev_util=>replicate(
                 iv_tab = 'SFLIGHT' iv_target = 'slt_pf'
                 iv_init = 'SET threads TO 1;'
                 iv_columns = 'CARRID PRICE' iv_where = `CARRID = 'LH'`
                 iv_maxrows = 5000 ).
    ok( cond = xsdbool( r3-error IS INITIAL ) what = 'pf_repl_noerr' detail = r3-error ).
    ok( cond = xsdbool( row_count( 'slt_pf' ) = lv_lh )
        what = 'pf_count_parity' detail = |sap={ lv_lh } duck={ row_count( 'slt_pf' ) }| ).
    " keys auto-kept: MANDT CARRID CONNID FLDATE + selected PRICE = 5 cols.
    ok( cond = xsdbool( col_count( 'slt_pf' ) = 5 )
        what = 'pf_5_cols' detail = |cols={ col_count( 'slt_pf' ) }| ).
    ok( cond = xsdbool( has_col( iv_target = 'slt_pf' iv_col = 'PLANETYPE' ) = abap_false )
        what = 'pf_no_planetype' ).

    " ============================================================ "
    " 4) KEY RETENTION when projecting ONLY a non-key column.      "
    "    Selecting just PRICE must still carry the full key, so     "
    "    UPSERT dedups: replicate twice -> row count unchanged.     "
    " ============================================================ "
    DATA(d4) = zcl_erpl_rev_util=>describe_table(
                 iv_tab = 'SFLIGHT' iv_target = 'slt_keyret' iv_columns = 'PRICE' ).
    ok( cond = xsdbool( d4-keys CS 'CARRID' AND d4-keys CS 'CONNID'
                    AND d4-keys CS 'FLDATE' AND d4-keys CS 'MANDT' )
        what = 'keyret_full_key' detail = d4-keys ).
    DATA(r4a) = zcl_erpl_rev_util=>replicate(
                  iv_tab = 'SFLIGHT' iv_target = 'slt_keyret'
                  iv_init = 'SET threads TO 1;' iv_columns = 'PRICE' iv_maxrows = 2000 ).
    ok( cond = xsdbool( r4a-error IS INITIAL ) what = 'keyret_repl1_noerr' detail = r4a-error ).
    DATA(lv_after1) = row_count( 'slt_keyret' ).
    DATA(r4b) = zcl_erpl_rev_util=>replicate(
                  iv_tab = 'SFLIGHT' iv_target = 'slt_keyret'
                  iv_columns = 'PRICE' iv_maxrows = 2000 ).
    ok( cond = xsdbool( r4b-error IS INITIAL ) what = 'keyret_repl2_noerr' detail = r4b-error ).
    DATA(lv_after2) = row_count( 'slt_keyret' ).
    ok( cond = xsdbool( lv_after1 = lv_after2 AND lv_after1 > 0 )
        what = 'keyret_upsert_dedup' detail = |after1={ lv_after1 } after2={ lv_after2 }| ).

    " ============================================================ "
    " 5) BAD COLUMN -> clean error, NO dump.                       "
    " ============================================================ "
    DATA(r5) = zcl_erpl_rev_util=>replicate(
                 iv_tab = 'SFLIGHT' iv_target = 'slt_bad'
                 iv_columns = 'CARRID NOTACOL' iv_maxrows = 10 ).
    ok( cond = xsdbool( r5-error IS NOT INITIAL ) what = 'badcol_error' detail = r5-error ).

    " ============================================================ "
    " 6) BAD WHERE -> clean error, NO dump.                        "
    " ============================================================ "
    DATA(r6) = zcl_erpl_rev_util=>replicate(
                 iv_tab = 'SFLIGHT' iv_target = 'slt_badw'
                 iv_where = `CARRID ==== 'LH'` iv_maxrows = 10 ).
    ok( cond = xsdbool( r6-error IS NOT INITIAL ) what = 'badwhere_error' detail = r6-error ).

    " ============================================================ "
    " 7) BROAD / REALISTIC: AND + NUMC compare, mixed-case &        "
    "    comma/space column list, literal case preserved.          "
    " ============================================================ "
    SELECT COUNT(*) FROM sflight
      WHERE carrid = 'LH' AND connid >= '0400' INTO @DATA(lv_lh4) UP TO 5000 ROWS.
    DATA(r7) = zcl_erpl_rev_util=>replicate(
                 iv_tab = 'SFLIGHT' iv_target = 'slt_real'
                 iv_init = 'SET threads TO 1;'
                 iv_columns = `carrid, Connid , FLDATE,price`
                 iv_where = `CARRID = 'LH' AND CONNID >= '0400'`
                 iv_maxrows = 5000 ).
    ok( cond = xsdbool( r7-error IS INITIAL ) what = 'real_repl_noerr' detail = r7-error ).
    ok( cond = xsdbool( row_count( 'slt_real' ) = lv_lh4 )
        what = 'real_count_parity' detail = |sap={ lv_lh4 } duck={ row_count( 'slt_real' ) }| ).
    ok( cond = xsdbool( col_count( 'slt_real' ) = 5 )
        what = 'real_5_cols' detail = |cols={ col_count( 'slt_real' ) }| ).
    " literal case preserved: lower-casing 'LH' would have matched 0 rows.
    ok( cond = xsdbool( row_count( 'slt_real' ) > 0 )
        what = 'real_literal_case' detail = |duck={ row_count( 'slt_real' ) }| ).

    " ============================================================ "
    " 8) VALUE HELP backing methods (feed the report's F4 popups).  "
    " ============================================================ "
    DATA(t_exact) = zcl_erpl_rev_util=>search_tables( iv_pattern = 'SFLIGHT' ).
    ok( cond = xsdbool( line_exists( t_exact[ tabname = 'SFLIGHT' ] ) )
        what = 'search_exact' detail = |n={ lines( t_exact ) }| ).
    DATA(t_pref) = zcl_erpl_rev_util=>search_tables( iv_pattern = 'SFL*' ).
    ok( cond = xsdbool( line_exists( t_pref[ tabname = 'SFLIGHT' ] ) AND lines( t_pref ) >= 1 )
        what = 'search_prefix' detail = |n={ lines( t_pref ) }| ).
    DATA(t_scarr) = zcl_erpl_rev_util=>search_tables( iv_pattern = 'SCARR' ).
    ok( cond = xsdbool( line_exists( t_scarr[ tabname = 'SCARR' ] ) )
        what = 'search_scarr' detail = |n={ lines( t_scarr ) }| ).
    " a hit should carry a human description.
    READ TABLE t_exact INTO DATA(ls_hit) WITH KEY tabname = 'SFLIGHT'.
    ok( cond = xsdbool( ls_hit-text IS NOT INITIAL ) what = 'search_has_text' detail = ls_hit-text ).
    " capped sample is non-empty and respects iv_max.
    DATA(t_any) = zcl_erpl_rev_util=>search_tables( iv_pattern = '' iv_max = 25 ).
    ok( cond = xsdbool( lines( t_any ) > 0 AND lines( t_any ) <= 25 )
        what = 'search_capped' detail = |n={ lines( t_any ) }| ).

    DATA(cols) = zcl_erpl_rev_util=>list_columns( 'SFLIGHT' ).
    ok( cond = xsdbool( lines( cols ) > 5 AND line_exists( cols[ name = 'FLDATE' ] ) )
        what = 'list_columns_full' detail = |n={ lines( cols ) }| ).
    READ TABLE cols INTO DATA(ls_col) WITH KEY name = 'FLDATE'.
    ok( cond = xsdbool( ls_col-is_key = abap_true AND ls_col-duckdb_type CS 'DATE'
                    AND ls_col-text IS NOT INITIAL )
        what = 'list_columns_fldate' detail = |key={ ls_col-is_key } t={ ls_col-text }| ).

    " ============================================================ "
    " 9) MAPPING DISPLAY formatting (the report's column list).     "
    "    Field names are STRINGs; formatting must handle short ones "
    "    (e.g. PRICE, len 5) without STRING_LENGTH_TOO_LARGE — the  "
    "    bug that dumped the live report. This exercises that exact "
    "    code path headlessly.                                      "
    " ============================================================ "
    DATA(lines_) = zcl_erpl_rev_util=>format_fields(
                     zcl_erpl_rev_util=>list_columns( 'SFLIGHT' ) ).
    DATA(lv_has_price) = abap_false.
    LOOP AT lines_ INTO DATA(lv_l).
      IF lv_l CS 'PRICE'. lv_has_price = abap_true. ENDIF.
    ENDLOOP.
    ok( cond = xsdbool( lines( lines_ ) > 5 )
        what = 'format_fields_count' detail = |n={ lines( lines_ ) }| ).
    " short name (PRICE, len 5) formatted with no STRING_LENGTH_TOO_LARGE dump.
    ok( cond = lv_has_price
        what = 'format_fields_short_name' detail = |first={ VALUE #( lines_[ 1 ] OPTIONAL ) }| ).

    " ============================================================ "
    " 10) ASYNC PIPELINE: a tiny batch forces several packages      "
    "     (1 sync + N async, ingested out of order while the next   "
    "     package is read) — every row must still land exactly.     "
    " ============================================================ "
    SELECT COUNT(*) FROM sflight INTO @DATA(lv_sfall).
    DATA(ra) = zcl_erpl_rev_util=>replicate(
                 iv_tab = 'SFLIGHT' iv_target = 'slt_async'
                 iv_init = 'SET threads TO 1;' iv_batch = 25 ).
    ok( cond = xsdbool( ra-error IS INITIAL ) what = 'async_repl_noerr' detail = ra-error ).
    ok( cond = xsdbool( lv_sfall > 25 AND row_count( 'slt_async' ) = lv_sfall )
        what = 'async_count_parity'
        detail = |sap={ lv_sfall } duck={ row_count( 'slt_async' ) }| ).

    out->write( |SLT RESULT pass={ mv_pass } fail={ mv_fail }| ).
  ENDMETHOD.
ENDCLASS.
