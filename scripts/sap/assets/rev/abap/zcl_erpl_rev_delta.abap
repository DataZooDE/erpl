CLASS zcl_erpl_rev_delta DEFINITION PUBLIC FINAL CREATE PUBLIC.
  PUBLIC SECTION.
    " Delta (incremental) extraction engine. The ABAP side stays a thin reader:
    " it selects the changed rows with plain Open SQL and streams them through the
    " existing replicate()/MERGE path; the merge, the snapshot diff and ALL delta
    " state live in the C++/DuckDB server (_erpl_rev_delta_state, read/written via
    " Z_DUCKDB_QUERY — no new SAP Z table). Four methods, one merge engine:
    "   WATERMARK   - chg_col > wm (numeric high-water): keyed upsert.
    "   INSERT_ONLY - append-only source driven by CDHDR change numbers (2-step):
    "                 keyed upsert (the DDIC key dedups re-delivered rows).
    "   CHANGEDOC   - CDHDR(objectclas) feed -> business keys -> re-read source by
    "                 key -> keyed upsert (catches every change path; e.g. MARA/MAKT).
    "   SNAPSHOT    - full reload into <target>__snap + server-side anti-join merge
    "                 (the only path that reflects PHYSICAL deletes).
    " Every cycle is idempotent (key-based merge) and re-runnable; a per-target
    " lease prevents overlapping ticks. See docs/delta.md and HLD §3-§7.

    CONSTANTS c_lease_ttl TYPE i VALUE 600.   " a RUNNING lease older than this is stale

    TYPES: BEGIN OF ty_state,
             target      TYPE string,
             method      TYPE string,
             source_from TYPE string,
             keys        TYPE string,
             chg_col     TYPE string,
             wm_kind     TYPE string,   " NUMTS | DATETIME | CHANGENR | INT | DATE
             wm_value    TYPE string,
             safety_secs TYPE i,
             cadence     TYPE string,   " micro:<sec> | hourly | nightly | manual
             extra       TYPE string,   " JSON, e.g. {"objectclas":"MATERIAL"}
             status      TYPE string,
           END OF ty_state.

    TYPES: BEGIN OF ty_run,
             target  TYPE string,
             method  TYPE string,
             rows    TYPE i,
             ins     TYPE i,
             upd     TYPE i,
             del     TYPE i,
             wm      TYPE string,
             skipped TYPE abap_bool,
             error   TYPE string,
           END OF ty_run.
    TYPES tt_run TYPE STANDARD TABLE OF ty_run WITH EMPTY KEY.

    "! Register (or idempotently update) one delta target in _erpl_rev_delta_state.
    "! Enforces the granularity gate: a date-only change column (wm_kind=DATE)
    "! cannot run at micro cadence (set rv_error). Returns '' on success.
    CLASS-METHODS register
      IMPORTING is_state        TYPE ty_state
      RETURNING VALUE(rv_error) TYPE string.

    "! Read the full config+state row for a target ('' target => not registered).
    CLASS-METHODS state
      IMPORTING iv_target    TYPE csequence
      RETURNING VALUE(rs)    TYPE ty_state.

    "! Advance the watermark + rows + last_run_ts and set status back to IDLE.
    CLASS-METHODS commit
      IMPORTING iv_target TYPE csequence
                iv_wm     TYPE string
                iv_rows   TYPE i.

    "! Try to take the per-target lease. False if another cycle holds a FRESH
    "! RUNNING lease (< c_lease_ttl old); a stale lease is reclaimed.
    CLASS-METHODS try_lease
      IMPORTING iv_target    TYPE csequence
      RETURNING VALUE(rv_ok) TYPE abap_bool.

    "! Release the lease, recording final status + last_error.
    CLASS-METHODS release
      IMPORTING iv_target TYPE csequence
                iv_status TYPE string DEFAULT 'IDLE'
                iv_error  TYPE string DEFAULT ''.

    "! Run ONE delta cycle for a target: lease -> dispatch by method -> commit ->
    "! release. rs-skipped=X when the lease is held by another cycle.
    CLASS-METHODS run
      IMPORTING iv_target TYPE csequence
      RETURNING VALUE(rs) TYPE ty_run.

    "! Targets currently DUE (cadence elapsed since last_run_ts, lease free).
    "! cadence='manual' is never due. Used by the Z_ERPL_REV_DELTA job loop.
    CLASS-METHODS due
      RETURNING VALUE(rt) TYPE string_table.

    "! Run one tick: every due target, in order. Returns one ty_run per target.
    CLASS-METHODS run_due
      RETURNING VALUE(rt) TYPE tt_run.

    "! Current CDHDR high-water for an object class as 14-char YYYYMMDDHHMMSS
    "! (latest UDATE+UTIME). Use it to seed a CHANGEDOC/INSERT_ONLY watermark so the
    "! first cycle only picks up changes made AFTER registration.
    CLASS-METHODS cdhdr_highwater
      IMPORTING iv_cls    TYPE csequence
      RETURNING VALUE(rv) TYPE string.

    "! Evaluate a single-cell numeric DuckDB query (e.g. SELECT count(*) ...) and
    "! return the integer value. (query()'s row_count is the #result-rows, not the
    "! cell value, so a count needs the cell parsed out of the rows JSON.)
    CLASS-METHODS scalar
      IMPORTING iv_sql    TYPE string
      RETURNING VALUE(rv) TYPE i.

    "! Thin wrapper over the Z_DUCKDB_SNAPSHOT_MERGE FM (server anti-join merge).
    CLASS-METHODS snapshot_merge
      IMPORTING iv_target TYPE csequence
                iv_staging TYPE csequence
                iv_keys    TYPE string
      EXPORTING ev_ins     TYPE i
                ev_upd     TYPE i
                ev_del     TYPE i
                ev_error   TYPE string.

    "! Install (or remove) the periodic SAP background job that drives the sync —
    "! the supported way to run delta on a cron. It schedules report Z_ERPL_REV_DELTA
    "! (one tick = run_due, i.e. every DUE target) to start now and repeat every
    "! iv_minutes minutes; one tick at the finest period gates each target by its own
    "! `cadence`. Any existing job of the same name is removed first, so calling it
    "! again just re-times it. iv_remove=X removes it and does not reschedule.
    "! NB: a SAP background-job period is >= 1 minute; for sub-minute cadence use the
    "! report's loop mode (p_loop) or an external trigger. Returns a status line.
    CLASS-METHODS schedule
      IMPORTING iv_minutes TYPE i DEFAULT 1
                iv_remove  TYPE abap_bool DEFAULT abap_false
      RETURNING VALUE(rv_msg) TYPE string.

    "! Period in MINUTES implied by a cadence string (micro:<sec>->sec/60, hourly->60,
    "! nightly->1440, manual/unknown->0). Used to derive the heartbeat-job period from
    "! a target's chosen refresh interval, so one setting drives both.
    CLASS-METHODS cadence_minutes
      IMPORTING iv_cadence TYPE csequence
      RETURNING VALUE(rv)  TYPE i.

  PRIVATE SECTION.
    CONSTANTS c_dest TYPE rfcdest VALUE 'ERPL_REV'.

    "! SQL string-literal escaping (single quotes doubled).
    CLASS-METHODS q IMPORTING iv TYPE csequence RETURNING VALUE(rv) TYPE string.
    "! Parse the extra-JSON objectclas (CHANGEDOC/INSERT_ONLY driver class).
    CLASS-METHODS objectclas IMPORTING iv_extra TYPE string RETURNING VALUE(rv) TYPE string.
    "! Current max(chg_col) of a (numeric-watermark) source, as text.
    CLASS-METHODS source_max
      IMPORTING is_state     TYPE ty_state
      RETURNING VALUE(rv)    TYPE string.
    "! The four dispatch implementations (each returns rows/ins/upd/del/wm/error).
    CLASS-METHODS run_watermark   IMPORTING is_state TYPE ty_state RETURNING VALUE(rs) TYPE ty_run.
    CLASS-METHODS run_changedoc   IMPORTING is_state TYPE ty_state RETURNING VALUE(rs) TYPE ty_run.
    CLASS-METHODS run_insert_only IMPORTING is_state TYPE ty_state RETURNING VALUE(rs) TYPE ty_run.
    CLASS-METHODS run_snapshot    IMPORTING is_state TYPE ty_state RETURNING VALUE(rs) TYPE ty_run.
    "! CDHDR change feed since a 14-char YYYYMMDDHHMMSS watermark: change numbers,
    "! object ids, and the new high-water (max udate+utime). cls = OBJECTCLAS.
    CLASS-METHODS cdhdr_feed
      IMPORTING iv_cls     TYPE csequence
                iv_wm      TYPE string
      EXPORTING et_changenr TYPE string_table
                et_objectid TYPE string_table
                ev_new_wm   TYPE string.
ENDCLASS.

CLASS zcl_erpl_rev_delta IMPLEMENTATION.

  METHOD q.
    rv = iv.
    REPLACE ALL OCCURRENCES OF `'` IN rv WITH `''`.
  ENDMETHOD.

  METHOD objectclas.
    IF iv_extra IS INITIAL. RETURN. ENDIF.
    TYPES: BEGIN OF ty_x, objectclas TYPE string, END OF ty_x.
    DATA ls_x TYPE ty_x.
    TRY.
        /ui2/cl_json=>deserialize( EXPORTING json = iv_extra CHANGING data = ls_x ).
      CATCH cx_root ##NO_HANDLER.
    ENDTRY.
    rv = ls_x-objectclas.
  ENDMETHOD.

  METHOD register.
    " Granularity gate: a date-only change column cannot be sub-hourly (HLD §7).
    IF is_state-wm_kind = 'DATE' AND is_state-cadence CP 'micro:*'.
      rv_error = |granularity gate: wm_kind=DATE cannot use cadence { is_state-cadence }|.
      RETURN.
    ENDIF.
    DATA(lv_sql) =
      |INSERT INTO _erpl_rev_delta_state | &&
      |(target,method,source_from,keys,chg_col,wm_kind,wm_value,safety_secs,cadence,extra,status) VALUES (| &&
      |'{ q( is_state-target ) }','{ q( is_state-method ) }','{ q( is_state-source_from ) }',| &&
      |'{ q( is_state-keys ) }','{ q( is_state-chg_col ) }','{ q( is_state-wm_kind ) }',| &&
      |{ COND string( WHEN is_state-wm_value IS INITIAL THEN 'NULL' ELSE |'{ q( is_state-wm_value ) }'| ) },| &&
      |{ is_state-safety_secs },'{ q( is_state-cadence ) }',| &&
      |{ COND string( WHEN is_state-extra IS INITIAL THEN 'NULL' ELSE |'{ q( is_state-extra ) }'| ) },'IDLE') | &&
      |ON CONFLICT (target) DO UPDATE SET method=excluded.method, source_from=excluded.source_from, | &&
      |keys=excluded.keys, chg_col=excluded.chg_col, wm_kind=excluded.wm_kind, wm_value=excluded.wm_value, | &&
      |safety_secs=excluded.safety_secs, cadence=excluded.cadence, extra=excluded.extra|.
    DATA(ls) = zcl_erpl_rev_util=>query( lv_sql ).
    rv_error = ls-error.
  ENDMETHOD.

  METHOD state.
    DATA(ls) = zcl_erpl_rev_util=>query(
      |SELECT target, method, source_from, keys, coalesce(chg_col,'') AS chg_col, | &&
      |coalesce(wm_kind,'') AS wm_kind, coalesce(wm_value,'') AS wm_value, | &&
      |coalesce(safety_secs,0) AS safety_secs, coalesce(cadence,'manual') AS cadence, | &&
      |coalesce(extra,'') AS extra, coalesce(status,'IDLE') AS status | &&
      |FROM _erpl_rev_delta_state WHERE target='{ q( iv_target ) }'| ).
    IF ls-error IS NOT INITIAL OR ls-row_count = 0. RETURN. ENDIF.
    DATA lt TYPE STANDARD TABLE OF ty_state WITH EMPTY KEY.
    /ui2/cl_json=>deserialize( EXPORTING json = ls-rows CHANGING data = lt ).
    READ TABLE lt INTO rs INDEX 1.
  ENDMETHOD.

  METHOD commit.
    zcl_erpl_rev_util=>query(
      |UPDATE _erpl_rev_delta_state SET | &&
      |wm_value={ COND string( WHEN iv_wm IS INITIAL THEN 'wm_value' ELSE |'{ q( iv_wm ) }'| ) }, | &&
      |rows_applied={ iv_rows }, last_run_ts=now(), status='IDLE', last_error=NULL | &&
      |WHERE target='{ q( iv_target ) }'| ).
  ENDMETHOD.

  METHOD try_lease.
    " Check-then-set: free unless another cycle holds a fresh RUNNING lease.
    DATA(ls) = zcl_erpl_rev_util=>query(
      |SELECT count(*) AS c FROM _erpl_rev_delta_state | &&
      |WHERE target='{ q( iv_target ) }' AND status='RUNNING' | &&
      |AND lease_ts >= now() - INTERVAL '{ c_lease_ttl }' SECOND| ).
    IF ls-error IS NOT INITIAL. RETURN. ENDIF.
    DATA(held) = ls-rows.
    CONDENSE held.
    IF held CS '"c":1' OR held CS '"c": 1'. rv_ok = abap_false. RETURN. ENDIF.
    zcl_erpl_rev_util=>query(
      |UPDATE _erpl_rev_delta_state SET status='RUNNING', lease_ts=now() | &&
      |WHERE target='{ q( iv_target ) }'| ).
    rv_ok = abap_true.
  ENDMETHOD.

  METHOD release.
    zcl_erpl_rev_util=>query(
      |UPDATE _erpl_rev_delta_state SET status='{ q( iv_status ) }', | &&
      |last_error={ COND string( WHEN iv_error IS INITIAL THEN 'NULL' ELSE |'{ q( iv_error ) }'| ) } | &&
      |WHERE target='{ q( iv_target ) }'| ).
  ENDMETHOD.

  METHOD run.
    DATA(ls_state) = state( iv_target ).
    rs-target = iv_target.
    rs-method = ls_state-method.
    IF ls_state-target IS INITIAL.
      rs-error = |no delta registration for { iv_target }|.
      RETURN.
    ENDIF.
    IF try_lease( iv_target ) = abap_false.
      rs-skipped = abap_true.
      RETURN.
    ENDIF.
    GET TIME STAMP FIELD DATA(lv_t0).
    CASE ls_state-method.
      WHEN 'WATERMARK'.   rs = run_watermark( ls_state ).
      WHEN 'INSERT_ONLY'. rs = run_insert_only( ls_state ).
      WHEN 'CHANGEDOC'.   rs = run_changedoc( ls_state ).
      WHEN 'SNAPSHOT'.    rs = run_snapshot( ls_state ).
      WHEN OTHERS.        rs-error = |unknown delta method { ls_state-method }|.
    ENDCASE.
    rs-target = iv_target.
    rs-method = ls_state-method.
    GET TIME STAMP FIELD DATA(lv_t1).
    IF rs-error IS INITIAL.
      commit( iv_target = iv_target iv_wm = rs-wm iv_rows = rs-rows ).
      release( iv_target = iv_target iv_status = 'IDLE' ).
    ELSE.
      release( iv_target = iv_target iv_status = 'ERROR' iv_error = rs-error ).
    ENDIF.
    " Dashboard stats: one DELTA run row per cycle (every method). WATERMARK/CHANGEDOC/
    " INSERT_ONLY report a row count but not an I/U/D split, so attribute it to ins so
    " rows_applied is meaningful; SNAPSHOT carries the real ins/upd/del.
    zcl_erpl_rev_util=>record_run(
      iv_target   = iv_target
      iv_source   = ls_state-source_from
      iv_run_type = 'DELTA'
      iv_method   = ls_state-method
      iv_status   = COND #( WHEN rs-error IS INITIAL THEN 'SUCCESS' ELSE 'ERROR' )
      iv_ms       = CONV i( cl_abap_tstmp=>subtract( tstmp1 = lv_t1 tstmp2 = lv_t0 ) * 1000 )
      iv_read     = rs-rows
      iv_ins      = COND i( WHEN rs-ins + rs-upd + rs-del = 0 THEN rs-rows ELSE rs-ins )
      iv_upd      = rs-upd
      iv_del      = rs-del
      iv_wm_from  = ls_state-wm_value
      iv_wm_to    = rs-wm
      iv_error    = rs-error ).
  ENDMETHOD.

  METHOD source_max.
    " Numeric high-water: max(chg_col) over the source, as plain text. DEC(21,7)
    " holds a TIMESTAMPL (sub-second) precisely and an integer/CHANGENR acceptably.
    DATA lv_max TYPE p LENGTH 11 DECIMALS 7.
    DATA lt_sel TYPE string_table.
    APPEND |max( { is_state-chg_col } )| TO lt_sel.
    TRY.
        SELECT SINGLE (lt_sel) FROM (is_state-source_from) INTO @lv_max.
      CATCH cx_root ##NO_HANDLER.
    ENDTRY.
    rv = condense( |{ lv_max }| ).
  ENDMETHOD.

  METHOD run_watermark.
    " Read chg_col > wm (numeric) and keyed-upsert via MERGE. New wm = source max.
    DATA lv_where TYPE string.
    IF is_state-wm_value IS NOT INITIAL.
      " safety overlap is expressed at cadence/value level by the caller; the
      " numeric watermark compares strictly greater-than the stored high-water.
      " Quote the literal: a DEC(21,7) TIMESTAMPL carries a decimal point, which the
      " ABAP SQL parser rejects as a bare numeric literal in a dynamic condition.
      lv_where = |{ is_state-chg_col } > '{ is_state-wm_value }'|.
    ENDIF.
    DATA(r) = zcl_erpl_rev_util=>replicate(
      iv_tab      = is_state-source_from
      iv_target   = is_state-target
      iv_mode     = 'MERGE'
      iv_truncate = abap_false
      iv_where    = lv_where
      iv_record   = abap_false ).   " the cycle is recorded by run() as one DELTA row
    rs-rows  = r-rows_affected.
    rs-error = r-error.
    IF r-error IS INITIAL.
      rs-wm = source_max( is_state ).
      IF rs-wm IS INITIAL OR rs-wm = '0'. rs-wm = is_state-wm_value. ENDIF.
    ENDIF.
  ENDMETHOD.

  METHOD cdhdr_feed.
    " The canonical incremental CDHDR read (HLD §5.2 / research): watermark on
    " UDATE+UTIME (CHANGENR is buffered/non-monotonic — never `changenr > wm`).
    DATA lv_d TYPE d.
    DATA lv_t TYPE t.
    IF iv_wm IS NOT INITIAL AND strlen( iv_wm ) >= 14.
      lv_d = iv_wm(8).
      lv_t = iv_wm+8(6).
    ENDIF.
    DATA lv_cls TYPE cdobjectcl.
    lv_cls = iv_cls.
    TYPES: BEGIN OF ty_h, changenr TYPE cdchangenr, objectid TYPE cdobjectv,
                          udate TYPE cddatum, utime TYPE cduzeit, END OF ty_h.
    DATA lt_h TYPE STANDARD TABLE OF ty_h.
    IF lv_d IS INITIAL.
      SELECT changenr objectid udate utime FROM cdhdr
        INTO TABLE lt_h
        WHERE objectclas = lv_cls
        ORDER BY udate utime changenr.
    ELSE.
      SELECT changenr objectid udate utime FROM cdhdr
        INTO TABLE lt_h
        WHERE objectclas = lv_cls
          AND ( udate > lv_d OR ( udate = lv_d AND utime > lv_t ) )
        ORDER BY udate utime changenr.
    ENDIF.
    DATA lv_maxd TYPE d.
    DATA lv_maxt TYPE t.
    lv_maxd = lv_d.
    lv_maxt = lv_t.
    LOOP AT lt_h INTO DATA(ls_h).
      APPEND |{ ls_h-changenr }| TO et_changenr.
      APPEND condense( |{ ls_h-objectid }| ) TO et_objectid.
      IF ls_h-udate > lv_maxd OR ( ls_h-udate = lv_maxd AND ls_h-utime > lv_maxt ).
        lv_maxd = ls_h-udate.
        lv_maxt = ls_h-utime.
      ENDIF.
    ENDLOOP.
    IF lv_maxd IS NOT INITIAL.
      ev_new_wm = |{ lv_maxd }{ lv_maxt }|.
    ELSE.
      ev_new_wm = iv_wm.
    ENDIF.
  ENDMETHOD.

  METHOD run_changedoc.
    " CDHDR(objectclas) feed -> distinct business keys -> re-read current rows from
    " the real source by key -> keyed upsert (op I/U; deletes ride the nightly
    " SNAPSHOT). source_from = the table to re-read (e.g. MARA/MAKT); the key field
    " is the first DDIC key (e.g. MATNR), matched against CDHDR.OBJECTID.
    DATA lt_chg TYPE string_table.
    DATA lt_oid TYPE string_table.
    cdhdr_feed( EXPORTING iv_cls = objectclas( is_state-extra ) iv_wm = is_state-wm_value
                IMPORTING et_changenr = lt_chg et_objectid = lt_oid ev_new_wm = rs-wm ).
    SORT lt_oid.
    DELETE ADJACENT DUPLICATES FROM lt_oid.
    IF lt_oid IS INITIAL.
      rs-wm = is_state-wm_value.
      RETURN.
    ENDIF.
    " Business key field = the first NON-client key column (CDHDR.OBJECTID is the
    " business key without the client, e.g. MATNR for MANDT,MATNR,SPRAS).
    SPLIT is_state-keys AT ',' INTO TABLE DATA(lt_keys).
    DATA lv_keyf TYPE string.
    LOOP AT lt_keys INTO DATA(lv_k).
      DATA(lv_ku) = to_upper( condense( lv_k ) ).
      IF lv_ku = 'MANDT' OR lv_ku = 'CLIENT' OR lv_ku = 'MANDANT'. CONTINUE. ENDIF.
      lv_keyf = lv_ku.
      EXIT.
    ENDLOOP.
    IF lv_keyf IS INITIAL. lv_keyf = to_upper( VALUE string( lt_keys[ 1 ] OPTIONAL ) ). ENDIF.
    DATA lt_in TYPE string_table.
    LOOP AT lt_oid INTO DATA(lv_oid).
      APPEND |'{ q( lv_oid ) }'| TO lt_in.
    ENDLOOP.
    DATA(lv_where) = |{ lv_keyf } IN ( { concat_lines_of( table = lt_in sep = `,` ) } )|.
    DATA(r) = zcl_erpl_rev_util=>replicate(
      iv_tab      = is_state-source_from
      iv_target   = is_state-target
      iv_mode     = 'MERGE'
      iv_truncate = abap_false
      iv_where    = lv_where
      iv_record   = abap_false ).   " the cycle is recorded by run() as one DELTA row
    rs-rows  = r-rows_affected.
    rs-error = r-error.
    IF r-error IS NOT INITIAL. rs-wm = is_state-wm_value. ENDIF.
  ENDMETHOD.

  METHOD run_insert_only.
    " Append-only source driven by CDHDR change numbers (2-step, portable across
    " ECC cluster / S4 transparent CDPOS): CDHDR feed -> CHANGENR list -> re-read
    " by CHANGENR -> keyed upsert (DDIC key dedups re-delivered rows). chg_col is
    " informational; the high-water is the CDHDR UDATE+UTIME pair.
    DATA lt_chg TYPE string_table.
    DATA lt_oid TYPE string_table.
    cdhdr_feed( EXPORTING iv_cls = objectclas( is_state-extra ) iv_wm = is_state-wm_value
                IMPORTING et_changenr = lt_chg et_objectid = lt_oid ev_new_wm = rs-wm ).
    SORT lt_chg.
    DELETE ADJACENT DUPLICATES FROM lt_chg.
    IF lt_chg IS INITIAL.
      rs-wm = is_state-wm_value.
      RETURN.
    ENDIF.
    DATA lt_in TYPE string_table.
    LOOP AT lt_chg INTO DATA(lv_c).
      APPEND |'{ q( lv_c ) }'| TO lt_in.
    ENDLOOP.
    DATA(lv_where) = |changenr IN ( { concat_lines_of( table = lt_in sep = `,` ) } )|.
    DATA(r) = zcl_erpl_rev_util=>replicate(
      iv_tab      = is_state-source_from
      iv_target   = is_state-target
      iv_mode     = 'MERGE'
      iv_truncate = abap_false
      iv_where    = lv_where
      iv_record   = abap_false ).   " the cycle is recorded by run() as one DELTA row
    rs-rows  = r-rows_affected.
    rs-error = r-error.
    IF r-error IS NOT INITIAL. rs-wm = is_state-wm_value. ENDIF.
  ENDMETHOD.

  METHOD run_snapshot.
    " Full reload into <target>__snap, then a server-side anti-join merge onto the
    " target (upsert all + delete keys absent from the snapshot). The only path
    " that reflects PHYSICAL deletes. Self-seeds the target (CREATE IF NOT EXISTS
    " + PK) so the first snapshot cycle also works.
    DATA(lv_stg) = |{ is_state-target }__snap|.
    DATA(ld) = zcl_erpl_rev_util=>describe_table(
      iv_tab = is_state-source_from iv_target = is_state-target ).
    IF ld-error IS NOT INITIAL. rs-error = ld-error. RETURN. ENDIF.
    DATA(lc) = zcl_erpl_rev_util=>query( ld-ddl ).   " ensure target exists (+PK)
    IF lc-error IS NOT INITIAL. rs-error = lc-error. RETURN. ENDIF.

    " Optional parallel reload: extra may carry {"jobs":N,"part_col":"X"} (e.g. set by
    " Z_ERPL_REV_REPLICATE when the seed used the Parallel tab). With jobs>1 and a
    " numeric partition column, the per-cycle full reload runs across N background
    " workers via the proven parallel full-load engine; otherwise it's a serial read.
    TYPES: BEGIN OF ty_par, jobs TYPE i, part_col TYPE string, END OF ty_par.
    DATA ls_par TYPE ty_par.
    IF is_state-extra IS NOT INITIAL.
      TRY.
          /ui2/cl_json=>deserialize( EXPORTING json = is_state-extra CHANGING data = ls_par ).
        CATCH cx_root ##NO_HANDLER.
      ENDTRY.
    ENDIF.

    DATA r TYPE zcl_erpl_rev_util=>ty_repl.
    DATA(lv_pcol) = ls_par-part_col.
    IF ls_par-jobs > 1 AND lv_pcol IS INITIAL.
      lv_pcol = zcl_erpl_rev_util=>pick_partition_col( ld-fields ).
    ENDIF.
    IF ls_par-jobs > 1 AND lv_pcol IS NOT INITIAL.
      r = zcl_erpl_rev_util=>replicate_parallel(
            iv_tab      = is_state-source_from
            iv_target   = lv_stg
            iv_part_col = lv_pcol
            iv_jobs     = ls_par-jobs
            iv_record   = abap_false ).   " staging reload; the cycle is one DELTA row
    ENDIF.
    " Serial reload when not parallel, or as a safe fallback if the parallel reload
    " could not run (e.g. no suitable numeric partition column / no free batch WPs).
    IF NOT ( ls_par-jobs > 1 AND lv_pcol IS NOT INITIAL ) OR r-error IS NOT INITIAL.
      r = zcl_erpl_rev_util=>replicate(
            iv_tab      = is_state-source_from
            iv_target   = lv_stg
            iv_truncate = abap_true
            iv_record   = abap_false ).
    ENDIF.
    IF r-error IS NOT INITIAL. rs-error = r-error. RETURN. ENDIF.
    snapshot_merge( EXPORTING iv_target = is_state-target iv_staging = lv_stg iv_keys = is_state-keys
                    IMPORTING ev_ins = rs-ins ev_upd = rs-upd ev_del = rs-del ev_error = rs-error ).
    rs-rows = rs-ins + rs-upd + rs-del.
    " Bookkeeping watermark = current run timestamp (snapshot is full each cycle).
    GET TIME STAMP FIELD DATA(lv_ts).
    rs-wm = condense( |{ lv_ts }| ).
  ENDMETHOD.

  METHOD cdhdr_highwater.
    DATA lv_cls TYPE cdobjectcl.
    lv_cls = iv_cls.
    DATA lv_maxd TYPE cddatum.
    DATA lv_maxt TYPE cduzeit.
    SELECT MAX( udate ) FROM cdhdr WHERE objectclas = @lv_cls INTO @lv_maxd.
    IF lv_maxd IS INITIAL. RETURN. ENDIF.
    SELECT MAX( utime ) FROM cdhdr
      WHERE objectclas = @lv_cls AND udate = @lv_maxd INTO @lv_maxt.
    rv = |{ lv_maxd }{ lv_maxt }|.
  ENDMETHOD.

  METHOD scalar.
    DATA(ls) = zcl_erpl_rev_util=>query( iv_sql ).
    IF ls-error IS NOT INITIAL. RETURN. ENDIF.
    FIND PCRE ':\s*(-?[0-9]+)' IN ls-rows SUBMATCHES DATA(lv).
    IF sy-subrc = 0. rv = CONV i( lv ). ENDIF.
  ENDMETHOD.

  METHOD snapshot_merge.
    DATA: lv_ins TYPE string, lv_upd TYPE string, lv_del TYPE string.
    DATA lv_msg TYPE c LENGTH 255.
    CALL FUNCTION 'Z_DUCKDB_SNAPSHOT_MERGE' DESTINATION c_dest
      EXPORTING  iv_target  = CONV string( iv_target )
                 iv_staging = CONV string( iv_staging )
                 iv_keys    = iv_keys
      IMPORTING  ev_ins     = lv_ins
                 ev_upd     = lv_upd
                 ev_del     = lv_del
                 ev_error   = ev_error
      EXCEPTIONS system_failure = 1 MESSAGE lv_msg
                 communication_failure = 2 MESSAGE lv_msg
                 OTHERS = 3.
    IF sy-subrc <> 0.
      ev_error = |RFC subrc={ sy-subrc } { lv_msg }|.
      RETURN.
    ENDIF.
    IF lv_ins CO ' 0123456789'. ev_ins = CONV i( lv_ins ). ENDIF.
    IF lv_upd CO ' 0123456789'. ev_upd = CONV i( lv_upd ). ENDIF.
    IF lv_del CO ' 0123456789'. ev_del = CONV i( lv_del ). ENDIF.
  ENDMETHOD.

  METHOD due.
    " Due = cadence elapsed since last_run_ts (or never run) and lease free.
    " micro:<sec> -> <sec>; hourly -> 3600; nightly -> 86400; manual -> never.
    DATA(ls) = zcl_erpl_rev_util=>query(
      |SELECT target, cadence, status, | &&
      |coalesce(epoch(now()) - epoch(last_run_ts), 9.0e18) AS age, | &&
      |coalesce(epoch(now()) - epoch(lease_ts), 9.0e18) AS lease_age | &&
      |FROM _erpl_rev_delta_state WHERE cadence <> 'manual'| ).
    IF ls-error IS NOT INITIAL OR ls-row_count = 0. RETURN. ENDIF.
    TYPES: BEGIN OF ty_d, target TYPE string, cadence TYPE string, status TYPE string,
                          age TYPE f, lease_age TYPE f, END OF ty_d.
    DATA lt TYPE STANDARD TABLE OF ty_d WITH EMPTY KEY.
    /ui2/cl_json=>deserialize( EXPORTING json = ls-rows CHANGING data = lt ).
    LOOP AT lt INTO DATA(ls_d).
      DATA(lv_int) = COND f( WHEN ls_d-cadence CP 'micro:*'
                               THEN CONV f( substring_after( val = ls_d-cadence sub = ':' ) )
                             WHEN ls_d-cadence = 'hourly'  THEN 3600
                             WHEN ls_d-cadence = 'nightly' THEN 86400
                             ELSE 86400 ).
      DATA(lv_busy) = xsdbool( ls_d-status = 'RUNNING' AND ls_d-lease_age < c_lease_ttl ).
      IF lv_busy = abap_false AND ls_d-age >= lv_int.
        APPEND ls_d-target TO rt.
      ENDIF.
    ENDLOOP.
  ENDMETHOD.

  METHOD run_due.
    LOOP AT due( ) INTO DATA(lv_t).
      APPEND run( lv_t ) TO rt.
    ENDLOOP.
  ENDMETHOD.

  METHOD schedule.
    CONSTANTS lc_job TYPE btcjob VALUE 'ERPL_REV_DELTA'.
    " Remove any existing scheduled/released/ready periodic job of this name first
    " (status S=scheduled, R=released/ready, Y=ready, P=planned), so a re-schedule
    " just re-times it instead of stacking duplicates.
    SELECT jobname, jobcount FROM tbtco
      WHERE jobname = @lc_job AND status IN ( 'S', 'R', 'Y', 'P' )
      INTO TABLE @DATA(lt_old).
    LOOP AT lt_old INTO DATA(ls_old).
      CALL FUNCTION 'BP_JOB_DELETE'
        EXPORTING jobcount = ls_old-jobcount jobname = ls_old-jobname
                  forcedmode = abap_true commit_flag = abap_true
        EXCEPTIONS OTHERS = 0.
    ENDLOOP.
    DATA(lv_removed) = lines( lt_old ).

    IF iv_remove = abap_true.
      rv_msg = |unscheduled: removed { lv_removed } '{ lc_job }' job(s).|.
      RETURN.
    ENDIF.

    DATA(lv_min) = COND i( WHEN iv_minutes > 0 THEN iv_minutes ELSE 1 ).
    DATA lv_jc TYPE btcjobcnt.
    CALL FUNCTION 'JOB_OPEN'
      EXPORTING jobname = lc_job IMPORTING jobcount = lv_jc EXCEPTIONS OTHERS = 1.
    IF sy-subrc <> 0. rv_msg = |ERROR: JOB_OPEN subrc { sy-subrc }|. RETURN. ENDIF.

    " The job step: report Z_ERPL_REV_DELTA with its defaults (p_once -> one tick over
    " every DUE target). JOB_SUBMIT (an FM) is used rather than `SUBMIT … VIA JOB` so
    " it also works when scheduled from a non-dialog context.
    CALL FUNCTION 'JOB_SUBMIT'
      EXPORTING authcknam = sy-uname jobcount = lv_jc jobname = lc_job report = 'Z_ERPL_REV_DELTA'
      EXCEPTIONS OTHERS = 1.
    IF sy-subrc <> 0. rv_msg = |ERROR: JOB_SUBMIT subrc { sy-subrc }|. RETURN. ENDIF.

    " Schedule it to start now and repeat every lv_min minutes (the cron). An
    " explicit start date/time + PRDMINS is the canonical way to create a PERIODIC
    " job (more reliable than STRTIMMED, which is geared to a one-off immediate run).
    CALL FUNCTION 'JOB_CLOSE'
      EXPORTING jobcount = lv_jc jobname = lc_job
                sdlstrtdt = sy-datum sdlstrttm = sy-uzeit prdmins = lv_min
      EXCEPTIONS OTHERS = 1.
    IF sy-subrc <> 0. rv_msg = |ERROR: JOB_CLOSE subrc { sy-subrc }|. RETURN. ENDIF.

    rv_msg = |scheduled '{ lc_job }' to run every { lv_min } min| &&
             COND string( WHEN lv_removed > 0 THEN | (replaced { lv_removed } old)| ELSE `` ) &&
             |; monitor/stop in SM37.|.
  ENDMETHOD.

  METHOD cadence_minutes.
    IF iv_cadence CP 'micro:*'.
      DATA(lv_sec) = CONV i( condense( substring_after( val = iv_cadence sub = ':' ) ) ).
      rv = COND #( WHEN lv_sec >= 60 THEN lv_sec / 60 ELSE 1 ).
    ELSEIF iv_cadence = 'hourly'.
      rv = 60.
    ELSEIF iv_cadence = 'nightly'.
      rv = 1440.
    ELSE.
      rv = 0.   " manual / unknown -> not scheduled
    ENDIF.
  ENDMETHOD.

ENDCLASS.
