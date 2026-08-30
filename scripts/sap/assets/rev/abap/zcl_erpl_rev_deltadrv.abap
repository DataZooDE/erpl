CLASS zcl_erpl_rev_deltadrv DEFINITION PUBLIC FINAL CREATE PUBLIC.
  PUBLIC SECTION.
    " Change-injection driver for delta E2E + the interactive simulator. It makes
    " REAL, committed SAP changes so a delta cycle has something to pick up:
    "   * ZDELTA_WM  - direct Open SQL insert/update/delete on a test table whose
    "                  CHANGED_AT is a numeric (DEC15 YYYYMMDDHHMMSS) watermark.
    "   * CDHDR/CDPOS - synth_cd writes a genuine change document (header + item)
    "                  under a customer object class, which the CHANGEDOC /
    "                  INSERT_ONLY readers consume (A4H has no MM, so this stands in
    "                  for the BAPI_MATERIAL_SAVEDATA / MM02 change-document path).
    "   * SFLIGHT    - sflight_change / sflight_mass for the SNAPSHOT demo.
    " Shared by zcl_erpl_rev_deltatest (automated proof) and the GUI demo report
    " Z_ERPL_REV_DELTA_SFLIGHT (run it by hand in SAP GUI to demo to others).

    "! Current UTC time as a high-resolution TIMESTAMPL (DEC21,7) watermark value —
    "! sub-second so a seed and a same-second change still compare strictly greater.
    CLASS-METHODS now_ts RETURNING VALUE(rv) TYPE timestampl.

    "! (Re)seed ZDELTA_WM with iv_rows rows (id = 0000000001..). All get the
    "! current timestamp. Returns the high-water just written (max changed_at).
    CLASS-METHODS seed_wm
      IMPORTING iv_rows  TYPE i DEFAULT 10
      RETURNING VALUE(rv) TYPE timestampl.

    "! Update one ZDELTA_WM row (bumps VAL + NAME + CHANGED_AT = now).
    CLASS-METHODS touch_wm IMPORTING iv_id TYPE csequence.
    "! Insert one new ZDELTA_WM row (CHANGED_AT = now).
    CLASS-METHODS insert_wm IMPORTING iv_id TYPE csequence.
    "! Physically delete one ZDELTA_WM row.
    CLASS-METHODS delete_wm IMPORTING iv_id TYPE csequence.

    "! Make a REAL, committed change to the SFLIGHT demo table (the SNAPSHOT delta
    "! demo). iv_kind: 'U' bumps PRICE+100 / SEATSOCC+1 of one flight; 'I' clones a
    "! flight of (carrid,connid) at iv_fldate; 'D' deletes one flight. Returns a
    "! human-readable line for the demo log (prefixed 'ERROR:' on failure).
    CLASS-METHODS sflight_change
      IMPORTING iv_kind   TYPE c
                iv_carrid TYPE s_carr_id
                iv_connid TYPE s_conn_id
                iv_fldate TYPE s_date
      RETURNING VALUE(rv_msg) TYPE string.

    "! Delete ALL demo flights (FLDATE >= 2099-01-01) — full cleanup of anything the
    "! mass/single demo ops left behind, so a run starts from a clean SFLIGHT.
    CLASS-METHODS sflight_purge_demo.

    "! First SFLIGHT flight in key order — sensible defaults for the demo screen.
    CLASS-METHODS sflight_default
      EXPORTING ev_carrid TYPE s_carr_id
                ev_connid TYPE s_conn_id
                ev_fldate TYPE s_date.

    "! MASS change to SFLIGHT for the demo: bulk insert / update / delete iv_count
    "! "demo flights" for (carrid,connid). Demo flights live in a far-future FLDATE
    "! range (>= 2099-01-01) so they never collide with real data and are trivially
    "! targeted/cleaned. 'I' bulk-inserts iv_count new dates after the current max;
    "! 'U' bumps PRICE+100 on up to iv_count of them; 'D' deletes up to iv_count.
    "! One COMMIT per call (bulk DML). Returns a log line.
    CLASS-METHODS sflight_mass
      IMPORTING iv_kind   TYPE c
                iv_carrid TYPE s_carr_id
                iv_connid TYPE s_conn_id
                iv_count  TYPE i
      RETURNING VALUE(rv_msg) TYPE string.

    "! Write a REAL change document (one CDHDR header + one CDPOS item) for a
    "! customer-owned object class, committed. A4H has no Materials Management, so
    "! this stands in for BAPI_MATERIAL_SAVEDATA: it produces genuine CDHDR/CDPOS
    "! rows that the CHANGEDOC / INSERT_ONLY readers consume unchanged. CHANGENR is
    "! a fresh GLOBAL high-water+1 so it can never collide with an existing change
    "! number (run_insert_only re-reads CDPOS by CHANGENR alone). OBJECTID is the
    "! business key the CHANGEDOC re-read matches against the source's first key.
    CLASS-METHODS synth_cd
      IMPORTING iv_objectclas TYPE cdhdr-objectclas
                iv_objectid   TYPE cdhdr-objectid
                iv_tabname    TYPE cdpos-tabname    DEFAULT space
                iv_tabkey     TYPE cdpos-tabkey     DEFAULT space
                iv_fname      TYPE cdpos-fname       DEFAULT space
                iv_value      TYPE cdpos-value_new   DEFAULT space
      RETURNING VALUE(rv_changenr) TYPE cdhdr-changenr.

    "! Delete all synthetic change documents (CDHDR + CDPOS) for an object class —
    "! a clean baseline before/after the change-doc scenario.
    CLASS-METHODS synth_cd_purge IMPORTING iv_objectclas TYPE cdhdr-objectclas.

ENDCLASS.

CLASS zcl_erpl_rev_deltadrv IMPLEMENTATION.

  METHOD now_ts.
    GET TIME STAMP FIELD rv.
  ENDMETHOD.

  METHOD seed_wm.
    DELETE FROM zdelta_wm.                                  "#EC CI_NOFIRST
    DATA lt TYPE STANDARD TABLE OF zdelta_wm.
    DATA(lv_ts) = now_ts( ).
    DATA lv_id TYPE n LENGTH 10.
    DO iv_rows TIMES.
      lv_id = sy-index.
      APPEND VALUE zdelta_wm( id = lv_id
                             name = |row { sy-index }|
                             val = sy-index
                             changed_at = lv_ts ) TO lt.
    ENDDO.
    INSERT zdelta_wm FROM TABLE @lt.
    COMMIT WORK AND WAIT.
    rv = lv_ts.
  ENDMETHOD.

  METHOD touch_wm.
    DATA(lv_ts) = now_ts( ).
    DATA lv_id TYPE zdelta_wm-id.
    lv_id = iv_id.
    UPDATE zdelta_wm
      SET name = @( |touched { lv_ts }| ),
          val = val + 1,
          changed_at = @lv_ts
      WHERE id = @lv_id.
    COMMIT WORK AND WAIT.
  ENDMETHOD.

  METHOD insert_wm.
    DATA ls TYPE zdelta_wm.
    ls-id = iv_id.
    ls-name = |inserted|.
    ls-val = 1.
    ls-changed_at = now_ts( ).
    INSERT zdelta_wm FROM @ls.
    COMMIT WORK AND WAIT.
  ENDMETHOD.

  METHOD delete_wm.
    DATA lv_id TYPE zdelta_wm-id.
    lv_id = iv_id.
    DELETE FROM zdelta_wm WHERE id = @lv_id.
    COMMIT WORK AND WAIT.
  ENDMETHOD.

  METHOD sflight_purge_demo.
    DELETE FROM sflight WHERE fldate >= '20990101'.       "#EC CI_NOFIRST
    COMMIT WORK AND WAIT.
  ENDMETHOD.

  METHOD sflight_default.
    SELECT carrid, connid, fldate FROM sflight
      ORDER BY carrid, connid, fldate
      INTO @DATA(ls) UP TO 1 ROWS.
    ENDSELECT.
    ev_carrid = ls-carrid.
    ev_connid = ls-connid.
    ev_fldate = ls-fldate.
  ENDMETHOD.

  METHOD sflight_change.
    DATA ls TYPE sflight.
    CASE iv_kind.
      WHEN 'U'.
        SELECT SINGLE * FROM sflight
          WHERE carrid = @iv_carrid AND connid = @iv_connid AND fldate = @iv_fldate
          INTO @ls.
        IF sy-subrc <> 0.
          rv_msg = |ERROR: flight { iv_carrid } { iv_connid } { iv_fldate } not found|.
          RETURN.
        ENDIF.
        DATA(lv_old) = ls-price.
        ls-price    = ls-price + 100.
        ls-seatsocc = ls-seatsocc + 1.
        UPDATE sflight FROM @ls.
        COMMIT WORK AND WAIT.
        rv_msg = |UPDATE { iv_carrid } { iv_connid } { iv_fldate }: price { lv_old } -> { ls-price }|.
      WHEN 'I'.
        SELECT SINGLE * FROM sflight
          WHERE carrid = @iv_carrid AND connid = @iv_connid
          INTO @ls.
        IF sy-subrc <> 0.
          rv_msg = |ERROR: no template flight for { iv_carrid } { iv_connid }|.
          RETURN.
        ENDIF.
        ls-fldate   = iv_fldate.
        ls-price    = ls-price + 100.
        ls-seatsocc = 0.
        INSERT sflight FROM @ls.
        IF sy-subrc <> 0.
          rv_msg = |ERROR: insert { iv_carrid } { iv_connid } { iv_fldate } failed (already exists?)|.
          RETURN.
        ENDIF.
        COMMIT WORK AND WAIT.
        rv_msg = |INSERT { iv_carrid } { iv_connid } { iv_fldate } (price { ls-price })|.
      WHEN 'D'.
        DELETE FROM sflight
          WHERE carrid = @iv_carrid AND connid = @iv_connid AND fldate = @iv_fldate.
        IF sy-subrc <> 0.
          rv_msg = |ERROR: delete { iv_carrid } { iv_connid } { iv_fldate } matched nothing|.
          RETURN.
        ENDIF.
        COMMIT WORK AND WAIT.
        rv_msg = |DELETE { iv_carrid } { iv_connid } { iv_fldate }|.
      WHEN OTHERS.
        rv_msg = |ERROR: unknown change kind { iv_kind }|.
    ENDCASE.
  ENDMETHOD.

  METHOD sflight_mass.
    CONSTANTS lc_base TYPE d VALUE '20990101'.   " demo-flight marker: FLDATE >= this
    DATA lv_n TYPE i.
    lv_n = COND #( WHEN iv_count > 0 THEN iv_count ELSE 1000 ).

    CASE iv_kind.
      WHEN 'I'.
        " Template: any existing flight of this connection (else any flight at all).
        DATA ls_t TYPE sflight.
        SELECT SINGLE * FROM sflight
          WHERE carrid = @iv_carrid AND connid = @iv_connid INTO @ls_t.
        IF sy-subrc <> 0.
          SELECT SINGLE * FROM sflight INTO @ls_t.
          IF sy-subrc <> 0. rv_msg = 'ERROR: SFLIGHT is empty (no template)'. RETURN. ENDIF.
        ENDIF.
        " Start the new dates after the current max demo date (so repeated presses
        " keep appending without key collisions).
        SELECT MAX( fldate ) FROM sflight
          WHERE carrid = @iv_carrid AND connid = @iv_connid AND fldate >= @lc_base
          INTO @DATA(lv_max).
        DATA lv_start TYPE d.
        lv_start = COND #( WHEN lv_max IS INITIAL THEN lc_base ELSE lv_max + 1 ).
        DATA lt_ins TYPE STANDARD TABLE OF sflight.
        DO lv_n TIMES.
          DATA(ls) = ls_t.
          ls-carrid   = iv_carrid.
          ls-connid   = iv_connid.
          ls-fldate   = lv_start + ( sy-index - 1 ).
          ls-price    = ls_t-price + sy-index.
          ls-seatsocc = 0.
          APPEND ls TO lt_ins.
        ENDDO.
        INSERT sflight FROM TABLE @lt_ins ACCEPTING DUPLICATE KEYS.
        COMMIT WORK AND WAIT.
        rv_msg = |MASS INSERT { lines( lt_ins ) } flights ({ iv_carrid } { iv_connid }, from { lv_start })|.
      WHEN 'U'.
        DATA lt_upd TYPE STANDARD TABLE OF sflight.
        SELECT * FROM sflight
          WHERE carrid = @iv_carrid AND connid = @iv_connid AND fldate >= @lc_base
          ORDER BY fldate
          INTO TABLE @lt_upd UP TO @lv_n ROWS.
        LOOP AT lt_upd ASSIGNING FIELD-SYMBOL(<f>).
          <f>-price    = <f>-price + 100.
          <f>-seatsocc = <f>-seatsocc + 1.
        ENDLOOP.
        UPDATE sflight FROM TABLE @lt_upd.
        COMMIT WORK AND WAIT.
        rv_msg = |MASS UPDATE { lines( lt_upd ) } demo flights (price +100)|.
      WHEN 'D'.
        DATA lt_del TYPE STANDARD TABLE OF sflight.
        SELECT * FROM sflight
          WHERE carrid = @iv_carrid AND connid = @iv_connid AND fldate >= @lc_base
          ORDER BY fldate
          INTO TABLE @lt_del UP TO @lv_n ROWS.
        DELETE sflight FROM TABLE @lt_del.
        COMMIT WORK AND WAIT.
        rv_msg = |MASS DELETE { lines( lt_del ) } demo flights|.
      WHEN OTHERS.
        rv_msg = |ERROR: unknown mass kind { iv_kind }|.
    ENDCASE.
  ENDMETHOD.

  METHOD synth_cd.
    " Fresh GLOBAL change number (CDCHANGENR is one number range; a value above the
    " current max can't collide with any existing CDPOS row).
    SELECT MAX( changenr ) FROM cdhdr INTO @DATA(lv_maxc).
    DATA lv_num TYPE n LENGTH 10.
    lv_num = lv_maxc.
    lv_num = lv_num + 1.
    rv_changenr = lv_num.

    DATA ls_h TYPE cdhdr.
    ls_h-mandant    = sy-mandt.
    ls_h-objectclas = iv_objectclas.
    ls_h-objectid   = iv_objectid.
    ls_h-changenr   = rv_changenr.
    ls_h-username   = sy-uname.
    ls_h-udate      = sy-datum.
    ls_h-utime      = sy-uzeit.
    ls_h-change_ind = 'U'.
    INSERT cdhdr FROM @ls_h.

    DATA ls_p TYPE cdpos.
    ls_p-mandant    = sy-mandt.
    ls_p-objectclas = iv_objectclas.
    ls_p-objectid   = iv_objectid.
    ls_p-changenr   = rv_changenr.
    ls_p-tabname    = iv_tabname.
    ls_p-tabkey     = iv_tabkey.
    ls_p-fname      = iv_fname.
    ls_p-chngind    = 'U'.
    ls_p-value_new  = iv_value.
    INSERT cdpos FROM @ls_p.

    COMMIT WORK AND WAIT.
  ENDMETHOD.

  METHOD synth_cd_purge.
    DELETE FROM cdhdr WHERE objectclas = @iv_objectclas.   "#EC CI_NOFIRST
    DELETE FROM cdpos WHERE objectclas = @iv_objectclas.   "#EC CI_NOFIRST
    COMMIT WORK AND WAIT.
  ENDMETHOD.

ENDCLASS.
