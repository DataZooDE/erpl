CLASS zcl_erpl_rev_cdstest DEFINITION PUBLIC FINAL CREATE PUBLIC.
  PUBLIC SECTION.
    INTERFACES if_oo_adt_classrun.
  PRIVATE SECTION.
    DATA: mv_pass TYPE i, mv_fail TYPE i, mo TYPE REF TO if_oo_adt_classrun_out.
    METHODS ok IMPORTING cond TYPE abap_bool what TYPE string detail TYPE string DEFAULT ''.
ENDCLASS.

CLASS zcl_erpl_rev_cdstest IMPLEMENTATION.
  METHOD ok.
    IF cond = abap_true. mv_pass = mv_pass + 1.
    ELSE. mv_fail = mv_fail + 1. mo->write( |FAIL { what }: { detail }| ). ENDIF.
  ENDMETHOD.

  METHOD if_oo_adt_classrun~main.
    mo = out.
    " Replicate a CDS VIEW ENTITY (ZERPL_C_FLIGHTS over SFLIGHT) through the normal
    " path. DDIF_FIELDINFO_GET resolves the CDS with correct datatypes + key flags;
    " the only CDS-specific handling is filtering the synthetic .NODE pseudo-field.
    TRY.
        " (1) describe: scalar columns only (NODE filtered), keys auto-detected.
        DATA(d) = zcl_erpl_rev_util=>describe_table( iv_tab = 'ZERPL_C_FLIGHTS' iv_target = 'cds_flights' ).
        DATA(lv_hasnode) = abap_false.
        LOOP AT d-fields INTO DATA(f) WHERE name CP '.*' OR duckdb_type = 'NODE'.
          lv_hasnode = abap_true.
        ENDLOOP.
        ok( cond = xsdbool( d-error IS INITIAL AND lines( d-fields ) = 7 AND lv_hasnode = abap_false )
            what = 'describe_no_node' detail = |fields={ lines( d-fields ) } node={ lv_hasnode } err=[{ d-error }]| ).
        ok( cond = xsdbool( d-keys = 'CARRID,CONNID,FLDATE' )
            what = 'keys_auto_detected' detail = |keys=[{ d-keys }]| ).

        " (2) serial replicate -> DuckDB, full count parity vs the CDS.
        SELECT COUNT(*) FROM zerpl_c_flights INTO @DATA(lv_sap).
        DATA(r) = zcl_erpl_rev_util=>replicate(
          iv_tab = 'ZERPL_C_FLIGHTS' iv_target = 'cds_flights' iv_init = 'SET threads TO 1;' ).
        DATA(qc) = zcl_erpl_rev_util=>query( |SELECT count(*) AS c FROM cds_flights| ).
        ok( cond = xsdbool( r-error IS INITIAL AND r-rows_affected = lv_sap AND qc-rows CS |"c":{ lv_sap }| )
            what = 'replicate_count_parity' detail = |err=[{ r-error }] rep={ r-rows_affected } sap={ lv_sap } duck={ qc-rows }| ).

        " (3) value parity: an integer aggregate must match exactly on both sides.
        SELECT SUM( seatsocc ) FROM zerpl_c_flights INTO @DATA(lv_sum).
        DATA(qs) = zcl_erpl_rev_util=>query( |SELECT sum(seatsocc) AS s FROM cds_flights| ).
        ok( cond = xsdbool( qs-rows CS |"s":{ lv_sum }| )
            what = 'value_parity_sum_seatsocc' detail = |sap={ lv_sum } duck={ qs-rows }| ).

        " (4) CDS -> external target (parquet) via stage-then-publish.
        DATA(p) = zcl_erpl_rev_util=>publish(
          iv_source = 'cds_flights' iv_kind = 'PARQUET' iv_dest = '/tmp/erpl_cds.parquet' ).
        DATA(cp) = zcl_erpl_rev_util=>query(
          |SELECT count(*) AS c FROM read_parquet('/tmp/erpl_cds.parquet')| ).
        ok( cond = xsdbool( p-error IS INITIAL AND cp-rows CS |"c":{ lv_sap }| )
            what = 'cds_to_parquet' detail = |perr=[{ p-error }] cnt={ cp-rows }| ).

        " (5) CDS WITH PARAMETERS: replicate a parameterized view, parity for p_carrid=LH.
        SELECT COUNT(*) FROM zerpl_cp_flights( p_carrid = 'LH' ) INTO @DATA(lv_lh).
        DATA(rp) = zcl_erpl_rev_util=>replicate(
          iv_tab = 'ZERPL_CP_FLIGHTS' iv_target = 'cds_lh'
          iv_params = |p_carrid = 'LH'| iv_init = 'SET threads TO 1;' ).
        DATA(ql) = zcl_erpl_rev_util=>query( |SELECT count(*) AS c FROM cds_lh| ).
        ok( cond = xsdbool( rp-error IS INITIAL AND rp-rows_affected = lv_lh AND ql-rows CS |"c":{ lv_lh }| )
            what = 'cds_with_parameters' detail = |err=[{ rp-error }] rep={ rp-rows_affected } sap={ lv_lh } duck={ ql-rows }| ).

        " (6) F4 discovery: search_tables surfaces CDS entities (from DDLS/TADIR).
        DATA(lt_hits) = zcl_erpl_rev_util=>search_tables( iv_pattern = 'ZERPL_C' ).
        DATA(lv_found) = abap_false.
        LOOP AT lt_hits INTO DATA(h) WHERE tabname = 'ZERPL_C_FLIGHTS'. lv_found = abap_true. ENDLOOP.
        ok( cond = lv_found what = 'search_finds_cds' detail = |hits={ lines( lt_hits ) }| ).

        " (7) is_cds drives the report's conditional CDS-field show/hide.
        ok( cond = xsdbool( zcl_erpl_rev_util=>is_cds( 'ZERPL_C_FLIGHTS' ) = abap_true
                            AND zcl_erpl_rev_util=>is_cds( 'SFLIGHT' ) = abap_false )
            what = 'is_cds_detection'
            detail = |cds=[{ zcl_erpl_rev_util=>is_cds( 'ZERPL_C_FLIGHTS' ) }] tab=[{ zcl_erpl_rev_util=>is_cds( 'SFLIGHT' ) }]| ).

        zcl_erpl_rev_util=>query( |DROP TABLE IF EXISTS cds_flights; DROP TABLE IF EXISTS cds_lh| ).
        out->write( |CDS sap_rows={ lv_sap }| ).
        out->write( |CDS RESULT pass={ mv_pass } fail={ mv_fail }| ).
      CATCH cx_root INTO DATA(lx).
        out->write( |DUMP: { lx->get_text( ) }| ).
    ENDTRY.
  ENDMETHOD.
ENDCLASS.
