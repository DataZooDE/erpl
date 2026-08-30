CLASS zcl_erpl_rev_widetest DEFINITION PUBLIC FINAL CREATE PUBLIC.
  PUBLIC SECTION.
    INTERFACES if_oo_adt_classrun.
  PRIVATE SECTION.
    DATA: mv_pass TYPE i, mv_fail TYPE i, mo TYPE REF TO if_oo_adt_classrun_out.
    METHODS ok IMPORTING cond TYPE abap_bool what TYPE string detail TYPE string DEFAULT ''.
ENDCLASS.

CLASS zcl_erpl_rev_widetest IMPLEMENTATION.
  METHOD ok.
    IF cond = abap_true. mv_pass = mv_pass + 1.
    ELSE. mv_fail = mv_fail + 1. mo->write( |FAIL { what }: { detail }| ). ENDIF.
  ENDMETHOD.

  METHOD if_oo_adt_classrun~main.
    mo = out.

    " describe the wide (420-col) table: typed columns, 5-part key.
    DATA(d) = zcl_erpl_rev_util=>describe_table( iv_tab = 'ZWIDE_BSEG' iv_target = 'zwide_bseg' ).
    ok( cond = xsdbool( lines( d-fields ) > 200 ) what = 'wide_colcount'
        detail = |{ lines( d-fields ) }| ).
    ok( cond = xsdbool( d-ddl CS 'WRBTR0000 DECIMAL(13,2)' ) what = 'wide_dec_typed' detail = d-ddl ).
    ok( cond = xsdbool( d-keys CS 'BUKRS' AND d-keys CS 'BELNR'
                        AND d-keys CS 'GJAHR' AND d-keys CS 'BUZEI' )
        what = 'wide_keys' detail = d-keys ).

    " Replicate a capped slice 1:1 and verify count parity SAP vs DuckDB.
    " 420 columns x N rows of JSON over RFC is heavy; 500 rows keeps the test
    " inside the ADT timeout while still exercising the full wide path. (The
    " replicator report can pull more via p_maxrow.)
    DATA(r) = zcl_erpl_rev_util=>replicate(
                iv_tab = 'ZWIDE_BSEG' iv_target = 'zwide_bseg'
                iv_init = 'SET threads TO 1;' iv_maxrows = 500 ).
    ok( cond = xsdbool( r-error IS INITIAL ) what = 'wide_repl_noerr' detail = r-error ).

    SELECT COUNT(*) FROM zwide_bseg INTO @DATA(lv_sap) UP TO 500 ROWS.
    ok( cond = xsdbool( r-rows_affected = lv_sap )
        what = 'wide_repl_count' detail = |sap={ lv_sap } got={ r-rows_affected }| ).

    DATA(c) = zcl_erpl_rev_util=>query( `SELECT count(*) AS c FROM zwide_bseg` ).
    ok( cond = xsdbool( c-rows CS |"c":{ lv_sap }| ) what = 'wide_duckdb_count' detail = c-rows ).

    " typed round-trip: a DEC column lands as DECIMAL in DuckDB.
    DATA(s) = zcl_erpl_rev_util=>query(
      `SELECT typeof(wrbtr0000) AS t FROM zwide_bseg LIMIT 1` ).
    ok( cond = xsdbool( s-rows CS '"t":"DECIMAL' ) what = 'wide_dec_roundtrip' detail = s-rows ).

    " ============================================================ "
    " CONTENT CHECK: the data must actually be WRITTEN, not just    "
    " counted. Replicate exactly ONE known row (source filter) and  "
    " compare DuckDB values against the SAP source across every     "
    " type family — CHAR/NUMC/STRING/SSTRING/DEC/INT/DATE/TIME/BLOB."
    " ============================================================ "
    DATA(ro) = zcl_erpl_rev_util=>replicate(
                 iv_tab = 'ZWIDE_BSEG' iv_target = 'zwide_one'
                 iv_init = 'SET threads TO 1;'
                 iv_where = `BELNR = '0000000001' AND BUZEI = '001'` ).
    ok( cond = xsdbool( ro-error IS INITIAL ) what = 'one_repl_noerr' detail = ro-error ).

    " the SAP source row (ground truth)
    SELECT SINGLE * FROM zwide_bseg INTO @DATA(sap)
      WHERE bukrs = '1000' AND belnr = '0000000001'
        AND gjahr = '2026' AND buzei = '001'.
    ok( cond = xsdbool( sy-subrc = 0 ) what = 'one_sap_row_exists' detail = |subrc={ sy-subrc }| ).

    " the same row back out of DuckDB
    DATA(q) = zcl_erpl_rev_util=>query(
      |SELECT ckey0000, sgtxt0000, ssht0000, note0000, monat0000, n100000, | &&
      |waers0000, flag0000, wrbtr0000, num80000, byte0000, | &&
      |augdt0000, uztim0000, hex(blob0000) AS blobhex | &&
      |FROM zwide_one WHERE belnr = '0000000001' AND buzei = '001'| ).
    DATA(j) = q-rows.
    ok( cond = xsdbool( q-error IS INITIAL AND q-row_count = 1 )
        what = 'one_duck_row' detail = |err={ q-error } rc={ q-row_count }| ).

    " --- value-by-value: DuckDB must equal the SAP source ---
    DATA lv TYPE string.
    ok( cond = xsdbool( j CS |"ckey0000":"{ condense( CONV string( sap-ckey0000 ) ) }"| )
        what = 'val_char_ckey' detail = j ).
    ok( cond = xsdbool( j CS |"sgtxt0000":"{ condense( CONV string( sap-sgtxt0000 ) ) }"| )
        what = 'val_char_sgtxt' detail = j ).
    ok( cond = xsdbool( j CS |"ssht0000":"{ condense( CONV string( sap-ssht0000 ) ) }"| )
        what = 'val_sstring_ssht' detail = j ).
    ok( cond = xsdbool( j CS |"note0000":"{ sap-note0000 }"| )
        what = 'val_string_note' detail = j ).
    ok( cond = xsdbool( j CS |"monat0000":"{ sap-monat0000 }"| )
        what = 'val_numc_monat' detail = j ).
    ok( cond = xsdbool( j CS |"n100000":"{ sap-n100000 }"| )
        what = 'val_numc_n100000' detail = j ).
    ok( cond = xsdbool( j CS |"waers0000":"{ condense( CONV string( sap-waers0000 ) ) }"| )
        what = 'val_char_waers' detail = j ).
    ok( cond = xsdbool( j CS |"flag0000":"{ sap-flag0000 }"| )
        what = 'val_char_flag' detail = j ).
    lv = sap-wrbtr0000. CONDENSE lv NO-GAPS.
    ok( cond = xsdbool( j CS |"wrbtr0000":{ lv }| )
        what = 'val_dec_wrbtr' detail = |sap={ lv } j={ j }| ).
    ok( cond = xsdbool( j CS |"num80000":{ sap-num80000 }| )
        what = 'val_int8_num8' detail = j ).
    ok( cond = xsdbool( j CS |"byte0000":{ sap-byte0000 }| )
        what = 'val_int1_byte' detail = j ).
    DATA(dt) = CONV string( sap-augdt0000 ).
    ok( cond = xsdbool( j CS |"augdt0000":"{ dt(4) }-{ dt+4(2) }-{ dt+6(2) }"| )
        what = 'val_date_augdt' detail = j ).
    DATA(tm) = CONV string( sap-uztim0000 ).
    ok( cond = xsdbool( j CS |"uztim0000":"{ tm(2) }:{ tm+2(2) }:{ tm+4(2) }"| )
        what = 'val_time_uztim' detail = j ).
    " BLOB bytes: SAP xstring as upper-hex (string template renders hex) must
    " equal DuckDB hex(blob) — proves the raw bytes round-tripped exactly.
    DATA(bh) = CONV string( |{ sap-blob0000 }| ).
    ok( cond = xsdbool( j CS |"blobhex":"{ bh }"| )
        what = 'val_blob_hex' detail = |sap_hex={ bh } j={ j }| ).

    out->write( |WIDE RESULT pass={ mv_pass } fail={ mv_fail }| ).
  ENDMETHOD.
ENDCLASS.
