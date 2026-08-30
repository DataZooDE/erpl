*&---------------------------------------------------------------------*
*& Report  Z_ERPL_REV_DELTA
*&---------------------------------------------------------------------*
*& Delta orchestration loop. On each tick it asks the server which targets are
*& DUE (cadence elapsed since last_run_ts, lease free) and runs one delta cycle
*& per target (lease -> dispatch by method -> commit -> release). All delta state
*& lives in the DuckDB table _erpl_rev_delta_state (no SAP Z table).
*&
*& Two framed blocks on the screen:
*&   "Run delta now"  - run every due target once (or a single named target), as a
*&                      one-shot job step, or as a foreground watch loop.
*&   "Schedule ..."   - install / remove ONE periodic background job that ticks this
*&                      report (each tick runs all due targets, so one job at the
*&                      finest period drives every per-target cadence). SM37-monitorable.
*&---------------------------------------------------------------------*
REPORT z_erpl_rev_delta LINE-SIZE 120.
TYPE-POOLS icon.   " icon_led_green / _red / _yellow, icon_alarm, icon_delete for the list

" ── Run delta now ─────────────────────────────────────────────────────────────
SELECTION-SCREEN BEGIN OF BLOCK run WITH FRAME TITLE t_run.
  SELECTION-SCREEN BEGIN OF LINE.
  SELECTION-SCREEN COMMENT 1(28) c_tgt FOR FIELD p_tgt.
  PARAMETERS p_tgt TYPE string.
  SELECTION-SCREEN END OF LINE.
  SELECTION-SCREEN COMMENT /3(75) c_tgt2.
  SELECTION-SCREEN BEGIN OF LINE.
  PARAMETERS p_once RADIOBUTTON GROUP m DEFAULT 'X' USER-COMMAND md.
  SELECTION-SCREEN COMMENT 4(60) c_once FOR FIELD p_once.
  SELECTION-SCREEN END OF LINE.
  SELECTION-SCREEN BEGIN OF LINE.
  PARAMETERS p_loop RADIOBUTTON GROUP m.
  SELECTION-SCREEN COMMENT 4(60) c_loop FOR FIELD p_loop.
  SELECTION-SCREEN END OF LINE.
  SELECTION-SCREEN BEGIN OF LINE.
  SELECTION-SCREEN COMMENT 6(26) c_secs FOR FIELD p_secs MODIF ID lop.
  PARAMETERS p_secs TYPE i DEFAULT 60 MODIF ID lop.
  SELECTION-SCREEN END OF LINE.
  SELECTION-SCREEN BEGIN OF LINE.
  SELECTION-SCREEN COMMENT 6(26) c_dur FOR FIELD p_dur MODIF ID lop.
  PARAMETERS p_dur TYPE i DEFAULT 600 MODIF ID lop.
  SELECTION-SCREEN END OF LINE.
SELECTION-SCREEN END OF BLOCK run.

" ── Schedule a periodic background job ────────────────────────────────────────
SELECTION-SCREEN BEGIN OF BLOCK sch WITH FRAME TITLE t_sch.
  SELECTION-SCREEN COMMENT /1(79) c_sch1.
  SELECTION-SCREEN BEGIN OF LINE.
  PARAMETERS p_sched AS CHECKBOX.
  SELECTION-SCREEN COMMENT 4(40) c_sched FOR FIELD p_sched.
  SELECTION-SCREEN END OF LINE.
  SELECTION-SCREEN BEGIN OF LINE.
  SELECTION-SCREEN COMMENT 6(26) c_min FOR FIELD p_min.
  PARAMETERS p_min TYPE i DEFAULT 1.
  SELECTION-SCREEN END OF LINE.
  SELECTION-SCREEN BEGIN OF LINE.
  PARAMETERS p_unsch AS CHECKBOX.
  SELECTION-SCREEN COMMENT 4(40) c_unsch FOR FIELD p_unsch.
  SELECTION-SCREEN END OF LINE.
SELECTION-SCREEN END OF BLOCK sch.

INITIALIZATION.
  t_run   = 'Run delta now'.
  c_tgt   = 'Target (one table)'.
  c_tgt2  = 'Blank = run every DUE target; or name one to run it now (ignores cadence).'.
  c_once  = 'One tick, then stop  (the job step for periodic delta)'.
  c_loop  = 'Keep ticking  (foreground watch / sub-minute micro-batch)'.
  c_secs  = 'Tick every (seconds)'.
  c_dur   = 'Stop after (seconds)'.
  t_sch   = 'Schedule a periodic background job (the recommended way to run delta)'.
  c_sch1  = 'ONE periodic job; each tick runs every DUE target. One job drives all cadences.'.
  c_sched = 'Install / re-time the job'.
  c_min   = 'Every (minutes, min 1)'.
  c_unsch = 'Remove the job'.

" The loop interval/duration only apply to "Keep ticking".
AT SELECTION-SCREEN OUTPUT.
  LOOP AT SCREEN.
    IF screen-group1 = 'LOP'.
      screen-input = COND i( WHEN p_loop = abap_true THEN 1 ELSE 0 ).
      MODIFY SCREEN.
    ENDIF.
  ENDLOOP.

AT SELECTION-SCREEN ON HELP-REQUEST FOR p_tgt.
  MESSAGE 'DuckDB target table to run now (e.g. mara). Blank = run every target whose cadence is due.' TYPE 'I'.
AT SELECTION-SCREEN ON HELP-REQUEST FOR p_secs.
  MESSAGE 'In "Keep ticking" mode: seconds between ticks (e.g. 10 for a fast demo).' TYPE 'I'.
AT SELECTION-SCREEN ON HELP-REQUEST FOR p_dur.
  MESSAGE 'In "Keep ticking" mode: stop the loop after this many seconds.' TYPE 'I'.
AT SELECTION-SCREEN ON HELP-REQUEST FOR p_min.
  MESSAGE 'Background-job period in minutes (minimum 1). For sub-minute, use "Keep ticking" instead.' TYPE 'I'.

START-OF-SELECTION.
  " Scheduling actions are setup, not a run: do them and stop.
  IF p_unsch = abap_true.
    WRITE: / icon_delete AS ICON, zcl_erpl_rev_delta=>schedule( iv_remove = abap_true ).
    RETURN.
  ENDIF.
  IF p_sched = abap_true.
    WRITE: / icon_alarm AS ICON, zcl_erpl_rev_delta=>schedule( iv_minutes = p_min ).
    RETURN.
  ENDIF.

  DATA lt_run TYPE zcl_erpl_rev_delta=>tt_run.

  IF p_tgt IS NOT INITIAL.
    APPEND zcl_erpl_rev_delta=>run( p_tgt ) TO lt_run.
    PERFORM show USING lt_run.
    RETURN.
  ENDIF.

  IF p_loop = abap_true.
    GET TIME STAMP FIELD DATA(lv_t0).
    DO.
      lt_run = zcl_erpl_rev_delta=>run_due( ).
      PERFORM show USING lt_run.
      GET TIME STAMP FIELD DATA(lv_tn).
      IF cl_abap_tstmp=>subtract( tstmp1 = lv_tn tstmp2 = lv_t0 ) >= p_dur. EXIT. ENDIF.
      WAIT UP TO p_secs SECONDS.
    ENDDO.
  ELSE.
    lt_run = zcl_erpl_rev_delta=>run_due( ).
    PERFORM show USING lt_run.
  ENDIF.

*&---------------------------------------------------------------------*
*&  Show one tick's results as a compact, coloured table.
*&---------------------------------------------------------------------*
FORM show USING it_run TYPE zcl_erpl_rev_delta=>tt_run.
  WRITE: / |{ sy-datum DATE = USER } { sy-uzeit TIME = USER }| COLOR COL_GROUP, '— delta tick'.
  IF it_run IS INITIAL.
    WRITE: / icon_led_yellow AS ICON, 'No targets due this tick.'.
    ULINE.
    RETURN.
  ENDIF.

  FORMAT COLOR COL_HEADING.
  WRITE: /(22) 'Target', (12) 'Method', (8) 'rows', (6) 'ins', (6) 'upd', (6) 'del', 'Watermark'.
  FORMAT COLOR OFF.
  LOOP AT it_run INTO DATA(ls).
    IF ls-skipped = abap_true.
      WRITE: / icon_led_inactive AS ICON, (22) ls-target, 'skipped — another cycle holds the lease' COLOR COL_TOTAL.
    ELSEIF ls-error IS NOT INITIAL.
      WRITE: / icon_led_red AS ICON, (22) ls-target, (12) ls-method, 'ERROR:' COLOR COL_NEGATIVE, ls-error.
    ELSE.
      WRITE: / icon_led_green AS ICON, (22) ls-target, (12) ls-method,
               (8) ls-rows, (6) ls-ins, (6) ls-upd, (6) ls-del, ls-wm COLOR COL_NORMAL.
    ENDIF.
  ENDLOOP.
  ULINE.
ENDFORM.
