CLASS zcl_erpl_rev_cdctest DEFINITION PUBLIC FINAL CREATE PUBLIC.
  PUBLIC SECTION.
    INTERFACES if_oo_adt_classrun.
  PRIVATE SECTION.
    DATA: mv_pass TYPE i, mv_fail TYPE i, mo TYPE REF TO if_oo_adt_classrun_out.
    METHODS ok IMPORTING cond TYPE abap_bool what TYPE string detail TYPE string DEFAULT ''.
    METHODS cnt IMPORTING iv_sql TYPE string RETURNING VALUE(rv) TYPE i.
    METHODS has IMPORTING iv_sql TYPE string iv_sub TYPE string RETURNING VALUE(rv) TYPE abap_bool.
    METHODS m1_delete_only.
    METHODS m2_full_iud.
    METHODS m3_sflight.
ENDCLASS.

CLASS zcl_erpl_rev_cdctest IMPLEMENTATION.

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
        m1_delete_only( ).
        m2_full_iud( ).
        m3_sflight( ).
      CATCH cx_root INTO DATA(lx).
        mv_fail = mv_fail + 1.
        out->write( |DUMP: { lx->get_text( ) }| ).
    ENDTRY.
    out->write( |CDC RESULT pass={ mv_pass } fail={ mv_fail }| ).
  ENDMETHOD.

  METHOD m1_delete_only.
    " Trigger-CDC delete-only on ZDELTA_WM (string keys CLIENT,ID — no type casting):
    " provision real HANA triggers on the source, physically delete rows, run one CDC
    " cycle, and assert the physical deletes are reflected in the DuckDB target — the
    " gap the watermark tier cannot see. Idempotent re-run is a no-op; teardown drops
    " the trigger/log/sequence (leaving ZDELTA_WM clean for the other suites).

    " Seed the source + full-load the DuckDB target.
    zcl_erpl_rev_deltadrv=>seed_wm( 10 ).
    zcl_erpl_rev_util=>replicate( iv_tab = 'ZDELTA_WM' iv_target = 'cdc_wm' ).
    ok( cond = xsdbool( cnt( |SELECT count(*) AS c FROM cdc_wm| ) = 10 ) what = 'CDC baseline=10' ).

    " Provision delete-only triggers (self-cleans any leftovers from a prior run).
    DATA(lv_pe) = zcl_erpl_rev_cdc=>provision(
      iv_target = 'cdc_wm' iv_source = 'ZDELTA_WM' iv_keys = 'CLIENT,ID' iv_mode = 'DELETE_ONLY' ).
    ok( cond = xsdbool( lv_pe IS INITIAL ) what = 'CDC provision ok' detail = lv_pe ).

    " Physically delete two rows -> the AFTER DELETE trigger logs them.
    zcl_erpl_rev_deltadrv=>delete_wm( '0000000003' ).
    zcl_erpl_rev_deltadrv=>delete_wm( '0000000005' ).

    " One CDC cycle: read the log, apply, prune.
    DATA(r1) = zcl_erpl_rev_cdc=>run( 'cdc_wm' ).
    ok( cond = xsdbool( r1-error IS INITIAL ) what = 'CDC cycle ok' detail = r1-error ).
    ok( cond = xsdbool( r1-del = 2 ) what = 'CDC two physical deletes captured' detail = |{ r1-del }| ).
    ok( cond = xsdbool( cnt( |SELECT count(*) AS c FROM cdc_wm| ) = 8 ) what = 'CDC count 10->8' ).
    ok( cond = xsdbool( cnt( |SELECT count(*) AS c FROM cdc_wm WHERE id='0000000003'| ) = 0 )
        what = 'CDC deleted row 3 absent from target' ).
    ok( cond = xsdbool( cnt( |SELECT count(*) AS c FROM cdc_wm WHERE id='0000000005'| ) = 0 )
        what = 'CDC deleted row 5 absent from target' ).

    " Idempotent re-run: no new log rows -> nothing applied, target unchanged.
    DATA(r2) = zcl_erpl_rev_cdc=>run( 'cdc_wm' ).
    ok( cond = xsdbool( r2-applied = abap_false ) what = 'CDC idempotent re-run is a no-op'
        detail = |applied={ r2-applied } del={ r2-del }| ).
    ok( cond = xsdbool( cnt( |SELECT count(*) AS c FROM cdc_wm| ) = 8 ) what = 'CDC count still 8' ).

    " Heartbeat: run_due runs every provisioned (SEEDED/ACTIVE) CDC target.
    DATA(lt_due) = zcl_erpl_rev_cdc=>run_due( ).
    ok( cond = xsdbool( line_exists( lt_due[ table_line = 'cdc_wm' ] ) )
        what = 'CDC run_due runs the active target' ).

    " Teardown: drop trigger + log + sequence (trigger first, so ZDELTA_WM stays usable).
    DATA(lv_te) = zcl_erpl_rev_cdc=>teardown( 'cdc_wm' ).
    ok( cond = xsdbool( lv_te IS INITIAL ) what = 'CDC teardown ok (no orphan objects)' detail = lv_te ).
  ENDMETHOD.

  METHOD m2_full_iud.
    " FULL_IUD on ZDELTA_WM (a source without a usable change column): provision
    " AFTER INSERT/UPDATE/DELETE triggers that log the full row image, then make all
    " three kinds of change and prove one cycle reflects insert + update + delete in
    " the DuckDB target — the server upserts the I/U row images and deletes by key.
    zcl_erpl_rev_deltadrv=>seed_wm( 10 ).
    zcl_erpl_rev_util=>replicate( iv_tab = 'ZDELTA_WM' iv_target = 'cdc_iud' ).
    DATA(lv_pe) = zcl_erpl_rev_cdc=>provision(
      iv_target = 'cdc_iud' iv_source = 'ZDELTA_WM' iv_keys = 'CLIENT,ID' iv_mode = 'FULL_IUD' ).
    ok( cond = xsdbool( lv_pe IS INITIAL ) what = 'CDC(iud) provision ok' detail = lv_pe ).
    DATA(lv_n0) = cnt( |SELECT count(*) AS c FROM cdc_iud| ).

    " one insert, one update, one delete -> three trigger rows (I/U/D).
    zcl_erpl_rev_deltadrv=>insert_wm( '0000000011' ).
    zcl_erpl_rev_deltadrv=>touch_wm( '0000000002' ).
    zcl_erpl_rev_deltadrv=>delete_wm( '0000000004' ).

    DATA(r1) = zcl_erpl_rev_cdc=>run( 'cdc_iud' ).
    ok( cond = xsdbool( r1-error IS INITIAL ) what = 'CDC(iud) cycle ok' detail = r1-error ).
    ok( cond = xsdbool( r1-ins = 1 ) what = 'CDC(iud) one insert' detail = |{ r1-ins }| ).
    ok( cond = xsdbool( r1-upd = 1 ) what = 'CDC(iud) one update' detail = |{ r1-upd }| ).
    ok( cond = xsdbool( r1-del = 1 ) what = 'CDC(iud) one delete' detail = |{ r1-del }| ).
    ok( cond = xsdbool( cnt( |SELECT count(*) AS c FROM cdc_iud| ) = lv_n0 + 1 - 1 )
        what = 'CDC(iud) net count (+1 insert, -1 delete)' ).
    ok( cond = xsdbool( cnt( |SELECT count(*) AS c FROM cdc_iud WHERE id='0000000011'| ) = 1 )
        what = 'CDC(iud) inserted row 11 present' ).
    ok( cond = has( iv_sql = |SELECT name FROM cdc_iud WHERE id='0000000002'| iv_sub = 'touched' )
        what = 'CDC(iud) updated row 2 carries the new value' ).
    ok( cond = xsdbool( cnt( |SELECT count(*) AS c FROM cdc_iud WHERE id='0000000004'| ) = 0 )
        what = 'CDC(iud) deleted row 4 absent' ).

    DATA(r2) = zcl_erpl_rev_cdc=>run( 'cdc_iud' ).
    ok( cond = xsdbool( r2-applied = abap_false ) what = 'CDC(iud) idempotent re-run is a no-op'
        detail = |applied={ r2-applied }| ).

    DATA(lv_te) = zcl_erpl_rev_cdc=>teardown( 'cdc_iud' ).
    ok( cond = xsdbool( lv_te IS INITIAL ) what = 'CDC(iud) teardown ok' detail = lv_te ).
  ENDMETHOD.

  METHOD m3_sflight.
    " The flight-booking demo wired to trigger-CDC: provision delete-only triggers on
    " SFLIGHT (which has a DATE key FLDATE and a NUMC key CONNID) and prove a PHYSICAL
    " flight delete is captured into the DuckDB target — using a far-future demo flight
    " so real data is never touched. Exercises the type-aware key casting end to end.
    DATA: lv_c TYPE s_carr_id, lv_n TYPE s_conn_id, lv_d TYPE s_date.
    zcl_erpl_rev_deltadrv=>sflight_default(
      IMPORTING ev_carrid = lv_c ev_connid = lv_n ev_fldate = lv_d ).
    ok( cond = xsdbool( lv_c IS NOT INITIAL ) what = 'CDC(sflight) demo data present' ).
    IF lv_c IS INITIAL. RETURN. ENDIF.
    DATA(lv_demo) = CONV s_date( '20991231' ).

    " seed the target WITH a demo flight present, then provision the triggers.
    zcl_erpl_rev_deltadrv=>sflight_purge_demo( ).
    zcl_erpl_rev_deltadrv=>sflight_change( iv_kind = 'I' iv_carrid = lv_c iv_connid = lv_n iv_fldate = lv_demo ).
    zcl_erpl_rev_util=>replicate( iv_tab = 'SFLIGHT' iv_target = 'sflight_cdc' ).
    DATA(lv_n0) = cnt( |SELECT count(*) AS c FROM sflight_cdc| ).
    ok( cond = xsdbool( cnt( |SELECT count(*) AS c FROM sflight_cdc WHERE fldate='2099-12-31'| ) = 1 )
        what = 'CDC(sflight) demo flight seeded' ).

    DATA(lv_pe) = zcl_erpl_rev_cdc=>provision(
      iv_target = 'sflight_cdc' iv_source = 'SFLIGHT'
      iv_keys = 'MANDT,CARRID,CONNID,FLDATE' iv_mode = 'DELETE_ONLY' ).
    ok( cond = xsdbool( lv_pe IS INITIAL ) what = 'CDC(sflight) provision ok' detail = lv_pe ).

    " physically delete the demo flight -> the AFTER DELETE trigger logs it.
    zcl_erpl_rev_deltadrv=>sflight_change( iv_kind = 'D' iv_carrid = lv_c iv_connid = lv_n iv_fldate = lv_demo ).
    DATA(r1) = zcl_erpl_rev_cdc=>run( 'sflight_cdc' ).
    ok( cond = xsdbool( r1-error IS INITIAL ) what = 'CDC(sflight) cycle ok' detail = r1-error ).
    ok( cond = xsdbool( r1-del = 1 ) what = 'CDC(sflight) physical delete captured (DATE+NUMC keys)'
        detail = |{ r1-del }| ).
    ok( cond = xsdbool( cnt( |SELECT count(*) AS c FROM sflight_cdc WHERE fldate='2099-12-31'| ) = 0 )
        what = 'CDC(sflight) deleted flight absent from target' ).
    ok( cond = xsdbool( cnt( |SELECT count(*) AS c FROM sflight_cdc| ) = lv_n0 - 1 )
        what = 'CDC(sflight) count -1' ).

    DATA(r2) = zcl_erpl_rev_cdc=>run( 'sflight_cdc' ).
    ok( cond = xsdbool( r2-applied = abap_false ) what = 'CDC(sflight) idempotent re-run no-op' ).

    DATA(lv_te) = zcl_erpl_rev_cdc=>teardown( 'sflight_cdc' ).
    ok( cond = xsdbool( lv_te IS INITIAL ) what = 'CDC(sflight) teardown ok' detail = lv_te ).
    zcl_erpl_rev_deltadrv=>sflight_purge_demo( ).
  ENDMETHOD.

ENDCLASS.
