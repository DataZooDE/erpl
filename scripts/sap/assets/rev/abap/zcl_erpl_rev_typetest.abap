CLASS zcl_erpl_rev_typetest DEFINITION PUBLIC FINAL CREATE PUBLIC.
  PUBLIC SECTION.
    INTERFACES if_oo_adt_classrun.
  PRIVATE SECTION.
    DATA: mv_pass TYPE i,
          mv_fail TYPE i,
          mo_out  TYPE REF TO if_oo_adt_classrun_out.
    METHODS eq IMPORTING got TYPE string exp TYPE string what TYPE string.
ENDCLASS.

CLASS zcl_erpl_rev_typetest IMPLEMENTATION.

  METHOD eq.
    IF got = exp.
      mv_pass = mv_pass + 1.
    ELSE.
      mv_fail = mv_fail + 1.
      mo_out->write( |FAIL { what }: got=[{ got }] exp=[{ exp }]| ).
    ENDIF.
  ENDMETHOD.

  METHOD if_oo_adt_classrun~main.
    mo_out = out.

    " --- forward: DDIC -> DuckDB ---
    eq( what = 'CHAR'  exp = 'VARCHAR'
        got = zcl_erpl_rev_typemap=>ddic_to_duckdb( 'CHAR' ) ).
    eq( what = 'NUMC'  exp = 'VARCHAR'
        got = zcl_erpl_rev_typemap=>ddic_to_duckdb( 'NUMC' ) ).
    eq( what = 'INT4'  exp = 'INTEGER'
        got = zcl_erpl_rev_typemap=>ddic_to_duckdb( 'INT4' ) ).
    eq( what = 'INT8'  exp = 'BIGINT'
        got = zcl_erpl_rev_typemap=>ddic_to_duckdb( 'INT8' ) ).
    eq( what = 'DEC'   exp = 'DECIMAL(10,2)'
        got = zcl_erpl_rev_typemap=>ddic_to_duckdb( iv_datatype = 'DEC' iv_length = 10 iv_decimals = 2 ) ).
    eq( what = 'CURR'  exp = 'DECIMAL(15,2)'
        got = zcl_erpl_rev_typemap=>ddic_to_duckdb( iv_datatype = 'CURR' iv_length = 15 iv_decimals = 2 ) ).
    eq( what = 'FLTP'  exp = 'DOUBLE'
        got = zcl_erpl_rev_typemap=>ddic_to_duckdb( 'FLTP' ) ).
    eq( what = 'DATS'  exp = 'DATE'
        got = zcl_erpl_rev_typemap=>ddic_to_duckdb( 'DATS' ) ).
    eq( what = 'TIMS'  exp = 'TIME'
        got = zcl_erpl_rev_typemap=>ddic_to_duckdb( 'TIMS' ) ).
    eq( what = 'RAW'   exp = 'BLOB'
        got = zcl_erpl_rev_typemap=>ddic_to_duckdb( 'RAW' ) ).

    " --- reverse: DuckDB -> DDIC (datatype only) ---
    eq( what = 'r_INTEGER' exp = 'INT4'
        got = CONV string( zcl_erpl_rev_typemap=>duckdb_to_ddic( 'INTEGER' )-datatype ) ).
    eq( what = 'r_BIGINT'  exp = 'INT8'
        got = CONV string( zcl_erpl_rev_typemap=>duckdb_to_ddic( 'BIGINT' )-datatype ) ).
    eq( what = 'r_DATE'    exp = 'DATS'
        got = CONV string( zcl_erpl_rev_typemap=>duckdb_to_ddic( 'DATE' )-datatype ) ).
    eq( what = 'r_TIME'    exp = 'TIMS'
        got = CONV string( zcl_erpl_rev_typemap=>duckdb_to_ddic( 'TIME' )-datatype ) ).
    eq( what = 'r_VARCHAR' exp = 'CHAR'
        got = CONV string( zcl_erpl_rev_typemap=>duckdb_to_ddic( 'VARCHAR' )-datatype ) ).

    " reverse DECIMAL(10,2) keeps precision/scale
    DATA(ls) = zcl_erpl_rev_typemap=>duckdb_to_ddic( 'DECIMAL(10,2)' ).
    eq( what = 'r_DEC_dt'  exp = 'DEC' got = CONV string( ls-datatype ) ).
    eq( what = 'r_DEC_len' exp = '10'  got = |{ ls-length }| ).
    eq( what = 'r_DEC_dec' exp = '2'   got = |{ ls-decimals }| ).

    " --- round-trip stability on the DuckDB side ---
    DATA(lt_rt) = VALUE string_table( ( `INTEGER` ) ( `BIGINT` ) ( `DATE` )
                                      ( `TIME` ) ( `DECIMAL(10,2)` ) ( `DOUBLE` ) ).
    LOOP AT lt_rt INTO DATA(lv_t).
      DATA(d) = zcl_erpl_rev_typemap=>duckdb_to_ddic( lv_t ).
      DATA(round) = zcl_erpl_rev_typemap=>ddic_to_duckdb(
        iv_datatype = d-datatype iv_length = d-length iv_decimals = d-decimals ).
      eq( what = |rt_{ lv_t }| exp = lv_t got = round ).
    ENDLOOP.

    out->write( |TYPEMAP RESULT pass={ mv_pass } fail={ mv_fail }| ).
  ENDMETHOD.

ENDCLASS.
