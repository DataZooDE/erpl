CLASS zcl_erpl_rev_deltatest DEFINITION PUBLIC FINAL CREATE PUBLIC.
  PUBLIC SECTION.
    INTERFACES if_oo_adt_classrun.
  PRIVATE SECTION.
    DATA: mv_pass TYPE i, mv_fail TYPE i, mo TYPE REF TO if_oo_adt_classrun_out.
    METHODS ok IMPORTING cond TYPE abap_bool what TYPE string detail TYPE string DEFAULT ''.
    "! Integer value of a single-cell DuckDB query.
    METHODS cnt IMPORTING iv_sql TYPE string RETURNING VALUE(rv) TYPE i.
    "! True if a query's rows JSON contains a substring (cheap value assertion).
    METHODS has IMPORTING iv_sql TYPE string iv_sub TYPE string RETURNING VALUE(rv) TYPE abap_bool.
    METHODS m1_watermark.
    METHODS m2_snapshot.
    METHODS m3_changedoc.
    METHODS m4_orchestration.
    METHODS m5_sflight.
    METHODS m6_stats.
ENDCLASS.

CLASS zcl_erpl_rev_deltatest IMPLEMENTATION.

  METHOD ok.
    IF cond = abap_true. mv_pass = mv_pass + 1.
    ELSE. mv_fail = mv_fail + 1. mo->write( |FAIL { what }: { detail }| ). ENDIF.
  ENDMETHOD.

  METHOD cnt.
    rv = zcl_erpl_rev_delta=>scalar( iv_sql ).
  ENDMETHOD.

  METHOD has.
    DATA(ls) = zcl_erpl_rev_util=>query( iv_sql ).
    rv = xsdbool( ls-error IS INITIAL AND ls-rows CS iv_sub ).
  ENDMETHOD.

  METHOD if_oo_adt_classrun~main.
    mo = out.
    TRY.
        m1_watermark( ).
        m2_snapshot( ).
        m3_changedoc( ).
        m4_orchestration( ).
        m5_sflight( ).
        m6_stats( ).
      CATCH cx_root INTO DATA(lx).
        mv_fail = mv_fail + 1.
        out->write( |DUMP: { lx->get_text( ) }| ).
    ENDTRY.
    out->write( |DELTA RESULT pass={ mv_pass } fail={ mv_fail }| ).
  ENDMETHOD.

  METHOD m1_watermark.
    " WATERMARK: a real Open SQL change to ZDELTA_WM is merged by chg_col > wm; a
    " re-run with no new change is a no-op (idempotent); the watermark advances.
    DATA(lv_seed) = zcl_erpl_rev_deltadrv=>seed_wm( 10 ).            " 10 rows @ seed ts
    zcl_erpl_rev_util=>replicate( iv_tab = 'ZDELTA_WM' iv_target = 'delta_wm' ). " baseline (+PK)
    zcl_erpl_rev_delta=>register( VALUE #(
      target = 'delta_wm' method = 'WATERMARK' source_from = 'ZDELTA_WM'
      keys = 'CLIENT,ID' chg_col = 'CHANGED_AT' wm_kind = 'NUMTS'
      wm_value = condense( |{ lv_seed }| ) safety_secs = 0 cadence = 'manual' ) ).
    ok( cond = xsdbool( cnt( |SELECT count(*) AS c FROM delta_wm| ) = 10 ) what = 'M1 baseline=10' ).

    " Inject: update 3 rows + insert 1 (4 rows now have chg_col > wm).
    zcl_erpl_rev_deltadrv=>touch_wm( '0000000001' ).
    zcl_erpl_rev_deltadrv=>touch_wm( '0000000002' ).
    zcl_erpl_rev_deltadrv=>touch_wm( '0000000003' ).
    zcl_erpl_rev_deltadrv=>insert_wm( '0000000011' ).

    DATA(r1) = zcl_erpl_rev_delta=>run( 'delta_wm' ).
    ok( cond = xsdbool( r1-error IS INITIAL ) what = 'M1 cycle ok' detail = r1-error ).
    ok( cond = xsdbool( r1-rows = 4 ) what = 'M1 applied=4' detail = |{ r1-rows }| ).
    ok( cond = xsdbool( cnt( |SELECT count(*) AS c FROM delta_wm| ) = 11 ) what = 'M1 count=11' ).
    ok( cond = has( iv_sql = |SELECT name FROM delta_wm WHERE id='0000000001'| iv_sub = 'touched' )
        what = 'M1 updated row carries new value' ).

    " Idempotency: nothing changed in SAP -> next cycle applies 0, data identical.
    DATA(r2) = zcl_erpl_rev_delta=>run( 'delta_wm' ).
    ok( cond = xsdbool( r2-rows = 0 ) what = 'M1 idempotent re-run applies 0' detail = |{ r2-rows }| ).
    ok( cond = xsdbool( cnt( |SELECT count(*) AS c FROM delta_wm| ) = 11 ) what = 'M1 count still 11' ).
  ENDMETHOD.

  METHOD m2_snapshot.
    " SNAPSHOT: insert + update + PHYSICAL DELETE in SAP; one cycle reflects all
    " three, and the deleted row is gone from the target (the watermark path can't
    " see physical deletes — only the snapshot anti-join can).
    zcl_erpl_rev_util=>replicate( iv_tab = 'ZDELTA_WM' iv_target = 'delta_snap' ). " baseline
    zcl_erpl_rev_delta=>register( VALUE #(
      target = 'delta_snap' method = 'SNAPSHOT' source_from = 'ZDELTA_WM'
      keys = 'CLIENT,ID' cadence = 'manual' ) ).
    DATA(lv_before) = cnt( |SELECT count(*) AS c FROM delta_snap| ).

    zcl_erpl_rev_deltadrv=>insert_wm( '0000000012' ).     " new
    zcl_erpl_rev_deltadrv=>touch_wm( '0000000004' ).      " changed
    zcl_erpl_rev_deltadrv=>delete_wm( '0000000005' ).     " hard delete

    DATA(r) = zcl_erpl_rev_delta=>run( 'delta_snap' ).
    ok( cond = xsdbool( r-error IS INITIAL ) what = 'M2 cycle ok' detail = r-error ).
    ok( cond = xsdbool( r-del = 1 ) what = 'M2 one delete detected' detail = |{ r-del }| ).
    ok( cond = xsdbool( cnt( |SELECT count(*) AS c FROM delta_snap| ) = lv_before )
        what = 'M2 net count (+1 -1)' ).
    ok( cond = xsdbool( cnt( |SELECT count(*) AS c FROM delta_snap WHERE id='0000000005'| ) = 0 )
        what = 'M2 hard-deleted row absent from target' ).
    ok( cond = xsdbool( cnt( |SELECT count(*) AS c FROM delta_snap WHERE id='0000000012'| ) = 1 )
        what = 'M2 inserted row present in target' ).
  ENDMETHOD.

  METHOD m3_changedoc.
    " CHANGEDOC + INSERT_ONLY proven against REAL CDHDR/CDPOS change documents.
    " A4H is a bare ABAP Platform with no Materials Management, so instead of
    " BAPI_MATERIAL_SAVEDATA (the MM02 path) we write genuine CDHDR/CDPOS rows under
    " customer-owned object classes and let the readers consume them through the
    " exact CDHDR-feed -> business-key/CHANGENR -> re-read -> MERGE path. Same engine
    " an MM-equipped (S/4) system would drive via material change documents.
    zcl_erpl_rev_deltadrv=>synth_cd_purge( 'ZERPLCD' ).
    zcl_erpl_rev_deltadrv=>synth_cd_purge( 'ZERPLIO' ).

    " ---- CHANGEDOC: CDHDR(ZERPLCD).OBJECTID -> re-read ZDELTA_WM by ID -> MERGE ----
    zcl_erpl_rev_util=>replicate( iv_tab = 'ZDELTA_WM' iv_target = 'cd_wm' ).   " baseline (+PK)
    DATA(lv_hw_cd) = zcl_erpl_rev_delta=>cdhdr_highwater( 'ZERPLCD' ).          " '' after purge
    zcl_erpl_rev_deltadrv=>touch_wm( '0000000007' ).                            " real source change
    zcl_erpl_rev_deltadrv=>synth_cd(
      iv_objectclas = 'ZERPLCD' iv_objectid = '0000000007'
      iv_tabname = 'ZDELTA_WM' iv_fname = 'NAME' ).
    zcl_erpl_rev_delta=>register( VALUE #(
      target = 'cd_wm' method = 'CHANGEDOC' source_from = 'ZDELTA_WM'
      keys = 'CLIENT,ID' wm_kind = 'DATETIME' wm_value = lv_hw_cd
      extra = '{"objectclas":"ZERPLCD"}' cadence = 'manual' ) ).
    DATA(rc) = zcl_erpl_rev_delta=>run( 'cd_wm' ).
    ok( cond = xsdbool( rc-error IS INITIAL ) what = 'M3 changedoc cycle ok' detail = rc-error ).
    ok( cond = xsdbool( rc-rows >= 1 ) what = 'M3 changedoc applied >=1' detail = |{ rc-rows }| ).
    ok( cond = has( iv_sql = |SELECT name FROM cd_wm WHERE id='0000000007'| iv_sub = 'touched' )
        what = 'M3 changedoc re-read landed the updated row in the target' ).
    DATA(rc2) = zcl_erpl_rev_delta=>run( 'cd_wm' ).
    ok( cond = xsdbool( rc2-rows = 0 ) what = 'M3 changedoc idempotent re-run applies 0'
        detail = |{ rc2-rows }| ).

    " ---- INSERT_ONLY: CDHDR(ZERPLIO) -> CHANGENR list -> re-read CDPOS -> append ----
    zcl_erpl_rev_util=>query( |DROP TABLE IF EXISTS cd_pos| ).   " deterministic across runs
    DATA(ld_pos) = zcl_erpl_rev_util=>describe_table( iv_tab = 'CDPOS' iv_target = 'cd_pos' ).
    ok( cond = xsdbool( ld_pos-error IS INITIAL ) what = 'M3 cd_pos describe ok' detail = ld_pos-error ).
    zcl_erpl_rev_util=>query( ld_pos-ddl ).                      " create cd_pos (+PK) empty
    DATA(lv_hw_io) = zcl_erpl_rev_delta=>cdhdr_highwater( 'ZERPLIO' ).
    zcl_erpl_rev_deltadrv=>synth_cd( iv_objectclas = 'ZERPLIO' iv_objectid = 'OBJ1'
      iv_tabname = 'ZDELTA_WM' iv_tabkey = 'K1' iv_fname = 'VAL' iv_value = '111' ).
    zcl_erpl_rev_deltadrv=>synth_cd( iv_objectclas = 'ZERPLIO' iv_objectid = 'OBJ2'
      iv_tabname = 'ZDELTA_WM' iv_tabkey = 'K2' iv_fname = 'VAL' iv_value = '222' ).
    zcl_erpl_rev_delta=>register( VALUE #(
      target = 'cd_pos' method = 'INSERT_ONLY' source_from = 'CDPOS'
      keys = 'MANDANT,OBJECTCLAS,OBJECTID,CHANGENR,TABNAME,TABKEY,FNAME,CHNGIND'
      wm_kind = 'DATETIME' wm_value = lv_hw_io
      extra = '{"objectclas":"ZERPLIO"}' cadence = 'manual' ) ).
    DATA(rio) = zcl_erpl_rev_delta=>run( 'cd_pos' ).
    ok( cond = xsdbool( rio-error IS INITIAL ) what = 'M3 insert_only cycle ok' detail = rio-error ).
    ok( cond = xsdbool( cnt( |SELECT count(*) AS c FROM cd_pos| ) = 2 )
        what = 'M3 insert_only appended 2 CDPOS rows' detail = |{ rio-rows }| ).
    DATA(rio2) = zcl_erpl_rev_delta=>run( 'cd_pos' ).
    ok( cond = xsdbool( rio2-rows = 0 ) what = 'M3 insert_only idempotent re-run applies 0'
        detail = |{ rio2-rows }| ).

    zcl_erpl_rev_deltadrv=>synth_cd_purge( 'ZERPLCD' ).
    zcl_erpl_rev_deltadrv=>synth_cd_purge( 'ZERPLIO' ).
  ENDMETHOD.

  METHOD m4_orchestration.
    " Lease blocks an overlapping cycle; the granularity gate rejects sub-hourly on
    " a date-only column; due-detection + run_due drive a catch-up cycle.
    zcl_erpl_rev_util=>query(
      |UPDATE _erpl_rev_delta_state SET status='RUNNING', lease_ts=now() WHERE target='delta_wm'| ).
    DATA(rl) = zcl_erpl_rev_delta=>run( 'delta_wm' ).
    ok( cond = xsdbool( rl-skipped = abap_true ) what = 'M4 lease blocks overlapping cycle' ).
    zcl_erpl_rev_util=>query(
      |UPDATE _erpl_rev_delta_state SET status='IDLE' WHERE target='delta_wm'| ).

    " Granularity gate: micro cadence on a date-only watermark is rejected.
    DATA(lv_gate) = zcl_erpl_rev_delta=>register( VALUE #(
      target = 'gate_t' method = 'WATERMARK' source_from = 'ZDELTA_WM'
      keys = 'CLIENT,ID' chg_col = 'CHANGED_AT' wm_kind = 'DATE' cadence = 'micro:60' ) ).
    ok( cond = xsdbool( lv_gate IS NOT INITIAL )
        what = 'M4 granularity gate rejects micro on DATE' detail = lv_gate ).

    " Due + run_due catch-up: make delta_wm due (micro:1, last run 10s ago), change a
    " row, and let the orchestrator pick it up.
    zcl_erpl_rev_util=>query(
      |UPDATE _erpl_rev_delta_state SET cadence='micro:1', | &&
      |last_run_ts = now() - INTERVAL '10' SECOND WHERE target='delta_wm'| ).
    DATA(lt_due) = zcl_erpl_rev_delta=>due( ).
    ok( cond = xsdbool( line_exists( lt_due[ table_line = 'delta_wm' ] ) )
        what = 'M4 delta_wm reported due' ).

    zcl_erpl_rev_deltadrv=>touch_wm( '0000000006' ).
    DATA(lt_run) = zcl_erpl_rev_delta=>run_due( ).
    DATA(lv_hit) = abap_false.
    LOOP AT lt_run INTO DATA(ls) WHERE target = 'delta_wm'.
      IF ls-error IS INITIAL AND ls-rows >= 1. lv_hit = abap_true. ENDIF.
    ENDLOOP.
    ok( cond = lv_hit what = 'M4 run_due executes the due catch-up cycle' ).
  ENDMETHOD.

  METHOD m5_sflight.
    " The SFLIGHT demo scenario behind Z_ERPL_REV_DELTA_SFLIGHT: SNAPSHOT delta on
    " the flight-booking demo table — insert + update + physical delete, each
    " reflected after one cycle, on a recognizable standard SAP table. Uses a fixed
    " far-future flight date (2099-12-31) so the asserts are deterministic and the
    " demo flight is added then removed (SFLIGHT is left as it was).
    DATA: lv_c TYPE s_carr_id, lv_n TYPE s_conn_id, lv_d TYPE s_date.
    zcl_erpl_rev_deltadrv=>sflight_default(
      IMPORTING ev_carrid = lv_c ev_connid = lv_n ev_fldate = lv_d ).
    ok( cond = xsdbool( lv_c IS NOT INITIAL ) what = 'M5 SFLIGHT demo data present' ).
    IF lv_c IS INITIAL. RETURN. ENDIF.
    DATA(lv_demo) = CONV s_date( '20991231' ).

    " purge any leftover demo flights (FLDATE >= 2099-01-01) for a clean baseline,
    " then full-load + register SNAPSHOT
    zcl_erpl_rev_deltadrv=>sflight_purge_demo( ).
    zcl_erpl_rev_util=>replicate( iv_tab = 'SFLIGHT' iv_target = 'sflight' ).
    zcl_erpl_rev_delta=>register( VALUE #(
      target = 'sflight' method = 'SNAPSHOT' source_from = 'SFLIGHT'
      keys = 'MANDT,CARRID,CONNID,FLDATE' cadence = 'manual' ) ).
    DATA(lv_n0) = cnt( |SELECT count(*) AS c FROM sflight| ).

    " INSERT a demo flight -> one cycle -> present in DuckDB
    zcl_erpl_rev_deltadrv=>sflight_change( iv_kind = 'I' iv_carrid = lv_c iv_connid = lv_n iv_fldate = lv_demo ).
    DATA(ri) = zcl_erpl_rev_delta=>run( 'sflight' ).
    ok( cond = xsdbool( ri-error IS INITIAL ) what = 'M5 insert cycle ok' detail = ri-error ).
    ok( cond = xsdbool( cnt( |SELECT count(*) AS c FROM sflight| ) = lv_n0 + 1 )
        what = 'M5 inserted flight reflected (count +1)' ).
    ok( cond = xsdbool( cnt( |SELECT count(*) AS c FROM sflight WHERE fldate='2099-12-31'| ) = 1 )
        what = 'M5 inserted flight present in DuckDB' ).

    " UPDATE the demo flight's price (+100) -> one cycle -> new price in DuckDB
    DATA(lv_p0) = cnt( |SELECT CAST(price AS INTEGER) AS c FROM sflight WHERE fldate='2099-12-31'| ).
    zcl_erpl_rev_deltadrv=>sflight_change( iv_kind = 'U' iv_carrid = lv_c iv_connid = lv_n iv_fldate = lv_demo ).
    zcl_erpl_rev_delta=>run( 'sflight' ).
    ok( cond = xsdbool( cnt( |SELECT CAST(price AS INTEGER) AS c FROM sflight WHERE fldate='2099-12-31'| ) = lv_p0 + 100 )
        what = 'M5 updated price reflected (+100)' detail = |{ lv_p0 }| ).

    " DELETE the demo flight -> one cycle -> gone from DuckDB (snapshot anti-join)
    zcl_erpl_rev_deltadrv=>sflight_change( iv_kind = 'D' iv_carrid = lv_c iv_connid = lv_n iv_fldate = lv_demo ).
    DATA(rd) = zcl_erpl_rev_delta=>run( 'sflight' ).
    ok( cond = xsdbool( rd-del = 1 ) what = 'M5 delete detected (del=1)' detail = |{ rd-del }| ).
    ok( cond = xsdbool( cnt( |SELECT count(*) AS c FROM sflight WHERE fldate='2099-12-31'| ) = 0 )
        what = 'M5 deleted flight absent from DuckDB' ).
    ok( cond = xsdbool( cnt( |SELECT count(*) AS c FROM sflight| ) = lv_n0 )
        what = 'M5 count restored to baseline' ).

    " MASS operations (bulk demo flights, FLDATE >= 2099-01-01). N=25 for a fast test.
    DATA(lv_m) = 25.
    zcl_erpl_rev_deltadrv=>sflight_mass( iv_kind = 'I' iv_carrid = lv_c iv_connid = lv_n iv_count = lv_m ).
    zcl_erpl_rev_delta=>run( 'sflight' ).
    ok( cond = xsdbool( cnt( |SELECT count(*) AS c FROM sflight| ) = lv_n0 + lv_m )
        what = 'M5 mass insert reflected (+25)' ).
    ok( cond = xsdbool( cnt( |SELECT count(*) AS c FROM sflight WHERE fldate >= '2099-01-01'| ) = lv_m )
        what = 'M5 25 demo flights present in DuckDB' ).

    DATA(lv_s0) = cnt( |SELECT CAST(sum(price) AS INTEGER) AS c FROM sflight WHERE fldate >= '2099-01-01'| ).
    zcl_erpl_rev_deltadrv=>sflight_mass( iv_kind = 'U' iv_carrid = lv_c iv_connid = lv_n iv_count = lv_m ).
    zcl_erpl_rev_delta=>run( 'sflight' ).
    ok( cond = xsdbool( cnt( |SELECT CAST(sum(price) AS INTEGER) AS c FROM sflight WHERE fldate >= '2099-01-01'| )
                        = lv_s0 + lv_m * 100 )
        what = 'M5 mass update reflected (+100 x 25)' detail = |{ lv_s0 }| ).

    zcl_erpl_rev_deltadrv=>sflight_mass( iv_kind = 'D' iv_carrid = lv_c iv_connid = lv_n iv_count = lv_m ).
    DATA(rmdel) = zcl_erpl_rev_delta=>run( 'sflight' ).
    ok( cond = xsdbool( rmdel-del = lv_m ) what = 'M5 mass delete detected (del=25)' detail = |{ rmdel-del }| ).
    ok( cond = xsdbool( cnt( |SELECT count(*) AS c FROM sflight WHERE fldate >= '2099-01-01'| ) = 0 )
        what = 'M5 all demo flights gone' ).
    ok( cond = xsdbool( cnt( |SELECT count(*) AS c FROM sflight| ) = lv_n0 )
        what = 'M5 mass delete restored baseline' ).
  ENDMETHOD.

  METHOD m6_stats.
    " C15: every full + incremental run is recorded in _erpl_rev_run_stats so a
    " replication dashboard can be built straight from DuckDB. The baselines above
    " wrote FULL rows; each delta cycle wrote a DELTA row; the SFLIGHT/snapshot
    " cycles recorded physical deletes. The erpl_rev_run_stats view derives the
    " dashboard columns (rows_applied / rows_per_sec / is_success / started_at).
    ok( cond = xsdbool( cnt( |SELECT count(*) AS c FROM _erpl_rev_run_stats WHERE run_type='FULL'| ) >= 1 )
        what = 'C15 FULL runs recorded' ).
    ok( cond = xsdbool( cnt( |SELECT count(*) AS c FROM _erpl_rev_run_stats WHERE run_type='DELTA'| ) >= 1 )
        what = 'C15 DELTA runs recorded' ).
    ok( cond = xsdbool( cnt( |SELECT count(*) AS c FROM _erpl_rev_run_stats | &&
                             |WHERE method='SNAPSHOT' AND rows_del >= 1| ) >= 1 )
        what = 'C15 a snapshot cycle recorded a physical delete' ).
    ok( cond = xsdbool( cnt( |SELECT count(*) AS c FROM _erpl_rev_run_stats | &&
                             |WHERE method IN ('WATERMARK','CHANGEDOC','INSERT_ONLY')| ) >= 1 )
        what = 'C15 watermark/changedoc/insert_only cycles recorded' ).
    ok( cond = xsdbool( cnt( |SELECT count(*) AS c FROM erpl_rev_run_stats | &&
                             |WHERE is_success AND rows_applied > 0| ) >= 1 )
        what = 'C15 dashboard view derives rows_applied / is_success' ).
  ENDMETHOD.

ENDCLASS.
