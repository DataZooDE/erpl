CLASS zcl_erpl_rev_difftest DEFINITION PUBLIC FINAL CREATE PUBLIC.
  PUBLIC SECTION.
    INTERFACES if_oo_adt_classrun.
  PRIVATE SECTION.
    DATA: mv_pass TYPE i, mv_fail TYPE i, mo TYPE REF TO if_oo_adt_classrun_out.
    METHODS ok IMPORTING cond TYPE abap_bool what TYPE string detail TYPE string DEFAULT ''.

    "! Canonical text for one SAP cell — MUST match duck_expr() so identical data
    "! yields identical fingerprints.
    METHODS sap_cell IMPORTING is_field TYPE zcl_erpl_rev_util=>ty_field
                               iv_val   TYPE any
                     RETURNING VALUE(rv) TYPE string.
    "! DuckDB SQL expression rendering one column to the same canonical text.
    METHODS duck_expr IMPORTING is_field TYPE zcl_erpl_rev_util=>ty_field
                      RETURNING VALUE(rv) TYPE string.
    "! lowercase MD5 hex of a string's UTF-8 bytes (matches DuckDB md5()).
    METHODS md5 IMPORTING iv TYPE string RETURNING VALUE(rv) TYPE string.

    "! Compare SAP source vs the (already replicated) DuckDB target, row by row in
    "! key order, EVERY column (FLTP excluded — float text isn't engine-stable).
    "! ev_diff empty => byte-for-byte identical.
    "! iv_hash=X compares a per-row MD5 of the fingerprint instead of the raw
    "! fingerprint — for wide tables whose full row text is too large to ship.
    METHODS compare
      IMPORTING iv_tab    TYPE csequence
                iv_target TYPE csequence
                iv_cap    TYPE i DEFAULT 0
                iv_hash   TYPE abap_bool DEFAULT abap_false
      EXPORTING ev_rows   TYPE i
                ev_cols   TYPE i
                ev_diff   TYPE string.
    "! replicate (full-load) then compare.
    METHODS verify
      IMPORTING iv_tab    TYPE csequence
                iv_target TYPE csequence
                iv_cap    TYPE i DEFAULT 0
                iv_hash   TYPE abap_bool DEFAULT abap_false
      EXPORTING ev_rows   TYPE i
                ev_cols   TYPE i
                ev_diff   TYPE string.
ENDCLASS.

CLASS zcl_erpl_rev_difftest IMPLEMENTATION.
  METHOD ok.
    IF cond = abap_true. mv_pass = mv_pass + 1.
    ELSE. mv_fail = mv_fail + 1. mo->write( |FAIL { what }: { detail }| ). ENDIF.
  ENDMETHOD.

  METHOD sap_cell.
    FIELD-SYMBOLS <v> TYPE any.
    ASSIGN iv_val TO <v>.
    CASE is_field-datatype.
      WHEN 'DATS'.
        DATA(d) = CONV string( <v> ).
        rv = COND #( WHEN d = '00000000' OR d IS INITIAL THEN ``
                     ELSE |{ d(4) }-{ d+4(2) }-{ d+6(2) }| ).
      WHEN 'TIMS'.
        DATA(t) = CONV string( <v> ).
        IF t IS INITIAL. t = '000000'. ENDIF.
        rv = |{ t(2) }:{ t+2(2) }:{ t+4(2) }|.
      WHEN 'INT1' OR 'INT2' OR 'INT4' OR 'INT8'
           OR 'DEC' OR 'CURR' OR 'QUAN'.
        DATA n TYPE string.
        n = <v>.
        CONDENSE n NO-GAPS.
        rv = n.
      WHEN 'RAW' OR 'LRAW' OR 'RSTR'.
        rv = |{ <v> }|.                       " xstring -> uppercase hex, whole
        " Compared byte for byte, trailing zeros included. Both sides used to
        " trim '(00)+$' because the pipeline dropped them; trimming
        " "symmetrically" sounds harmless and is not. ZWIDE_BSEG's
        " blob0001..blob0004 are all-zero RAW(16) on every row, so both sides
        " reduced to empty and a column replicated as an EMPTY BLOB compared
        " equal to sixteen zero bytes -- 3000 rows x 390 columns of it.
      WHEN OTHERS.                             " CHAR/CLNT/NUMC/CUKY/UNIT/LANG/...
        rv = CONV string( <v> ).
        REPLACE PCRE '\s+$' IN rv WITH ``.     " right-trim (CHAR blank-pad)
    ENDCASE.
  ENDMETHOD.

  METHOD md5.
    TRY.
        DATA(lx) = cl_abap_codepage=>convert_to( source = iv codepage = `UTF-8` ).
        cl_abap_message_digest=>calculate_hash_for_raw(
          EXPORTING if_algorithm  = `MD5`
                    if_data       = lx
          IMPORTING ef_hashstring = DATA(hs) ).
        rv = to_lower( hs ).
      CATCH cx_root.
        rv = `<md5-error>`.
    ENDTRY.
  ENDMETHOD.

  METHOD duck_expr.
    DATA(c) = to_lower( is_field-name ).
    CASE is_field-datatype.
      WHEN 'DATS' OR 'TIMS'.
        rv = |coalesce(cast({ c } as varchar),'')|.       " 'YYYY-MM-DD' / 'HH:MM:SS'
      WHEN 'INT1' OR 'INT2' OR 'INT4' OR 'INT8'
           OR 'DEC' OR 'CURR' OR 'QUAN'.
        rv = |coalesce(cast({ c } as varchar),'')|.
      WHEN 'RAW' OR 'LRAW' OR 'RSTR'.
        rv = |coalesce(hex({ c }),'')|.       " full hex; see sap_cell
      WHEN OTHERS.
        rv = |coalesce(rtrim({ c }),'')|.
    ENDCASE.
  ENDMETHOD.

  METHOD compare.
    DATA(ls_desc) = zcl_erpl_rev_util=>describe_table( iv_tab = iv_tab iv_target = iv_target ).

    " comparable columns (skip FLTP) + key order (skip the client field).
    DATA lt_cmp   TYPE zcl_erpl_rev_util=>tt_field.
    DATA lt_order TYPE string_table.
    DATA lv_dexpr TYPE string.
    DATA lv_sord  TYPE string.
    LOOP AT ls_desc-fields INTO DATA(f).
      IF f-datatype = 'FLTP'. CONTINUE. ENDIF.
      APPEND f TO lt_cmp.
      lv_dexpr = COND #( WHEN lv_dexpr IS INITIAL THEN duck_expr( f )
                         ELSE |{ lv_dexpr } \|\| '\|' \|\| { duck_expr( f ) }| ).
    ENDLOOP.
    ev_cols = lines( lt_cmp ).
    DATA lv_dord TYPE string.   " DuckDB ORDER BY (plain lowercased names; ASC default)
    LOOP AT ls_desc-fields INTO DATA(k) WHERE is_key = abap_true AND datatype <> 'CLNT'.
      lv_sord = COND #( WHEN lv_sord IS INITIAL THEN |{ k-name } ASCENDING|
                        ELSE |{ lv_sord }, { k-name } ASCENDING| ).
      lv_dord = COND #( WHEN lv_dord IS INITIAL THEN to_lower( k-name )
                        ELSE |{ lv_dord }, { to_lower( k-name ) }| ).
    ENDLOOP.
    APPEND lv_sord TO lt_order.

    " DuckDB fingerprints (one '|'-joined canonical string per row, key order).
    " Wide tables hash the fingerprint so only a short md5 crosses the wire.
    DATA(lv_fpsel) = COND string( WHEN iv_hash = abap_true THEN |md5({ lv_dexpr })|
                                  ELSE lv_dexpr ).
    DATA(lv_sql) = |SELECT { lv_fpsel } AS fp FROM { iv_target } | &&
                   |ORDER BY { lv_dord }|.
    DATA(s) = zcl_erpl_rev_util=>query_stream( iv_sql = lv_sql iv_maxrows = iv_cap ).
    IF s-error IS NOT INITIAL. ev_diff = |duck query: { s-error }|. RETURN. ENDIF.
    DATA lt_duck TYPE string_table.
    FIELD-SYMBOLS <dt> TYPE STANDARD TABLE.
    ASSIGN s-data->* TO <dt>.
    LOOP AT <dt> ASSIGNING FIELD-SYMBOL(<dr>).
      ASSIGN COMPONENT 1 OF STRUCTURE <dr> TO FIELD-SYMBOL(<fp>).
      APPEND CONV string( <fp> ) TO lt_duck.
    ENDLOOP.

    " SAP fingerprints (same formatting, same key order).
    DATA lt_select TYPE string_table.
    APPEND ls_desc-col_list TO lt_select.
    DATA lr TYPE REF TO data.
    DATA(lo_s) = CAST cl_abap_structdescr(
      cl_abap_typedescr=>describe_by_name( CONV string( iv_tab ) ) ).
    DATA(lo_t) = cl_abap_tabledescr=>create( lo_s ).
    CREATE DATA lr TYPE HANDLE lo_t.
    FIELD-SYMBOLS <st> TYPE STANDARD TABLE.
    ASSIGN lr->* TO <st>.
    IF iv_cap > 0.
      SELECT (lt_select) FROM (iv_tab) ORDER BY (lt_order)
        INTO CORRESPONDING FIELDS OF TABLE @<st> UP TO @iv_cap ROWS.
    ELSE.
      SELECT (lt_select) FROM (iv_tab) ORDER BY (lt_order)
        INTO CORRESPONDING FIELDS OF TABLE @<st>.
    ENDIF.

    DATA lt_sap TYPE string_table.
    LOOP AT <st> ASSIGNING FIELD-SYMBOL(<sr>).
      DATA lt_p TYPE string_table.
      CLEAR lt_p.
      LOOP AT lt_cmp INTO DATA(cf).
        ASSIGN COMPONENT cf-name OF STRUCTURE <sr> TO FIELD-SYMBOL(<cv>).
        APPEND sap_cell( is_field = cf iv_val = <cv> ) TO lt_p.
      ENDLOOP.
      DATA(lv_fp) = concat_lines_of( table = lt_p sep = `|` ).
      APPEND COND string( WHEN iv_hash = abap_true THEN md5( lv_fp ) ELSE lv_fp ) TO lt_sap.
    ENDLOOP.

    " compare counts + every row.
    ev_rows = lines( lt_sap ).
    IF lines( lt_sap ) <> lines( lt_duck ).
      ev_diff = |row count sap={ lines( lt_sap ) } duck={ lines( lt_duck ) }|.
      RETURN.
    ENDIF.
    LOOP AT lt_sap INTO DATA(lv_s).
      DATA(lv_idx) = sy-tabix.
      READ TABLE lt_duck INTO DATA(lv_dk) INDEX lv_idx.
      IF lv_s <> lv_dk.
        ev_diff = |row { lv_idx }: SAP=[{ lv_s }] DUCK=[{ lv_dk }]|.
        RETURN.
      ENDIF.
    ENDLOOP.
  ENDMETHOD.

  METHOD verify.
    DATA(r) = zcl_erpl_rev_util=>replicate(
                iv_tab = iv_tab iv_target = iv_target
                iv_init = 'SET threads TO 1;' iv_maxrows = iv_cap ).
    IF r-error IS NOT INITIAL. ev_diff = |replicate: { r-error }|. RETURN. ENDIF.
    compare( EXPORTING iv_tab = iv_tab iv_target = iv_target iv_cap = iv_cap iv_hash = iv_hash
             IMPORTING ev_rows = ev_rows ev_cols = ev_cols ev_diff = ev_diff ).
  ENDMETHOD.

  METHOD if_oo_adt_classrun~main.
    mo = out.
    DATA: lv_rows TYPE i, lv_cols TYPE i, lv_diff TYPE string.
    TRY.

    " ---- SFLIGHT: EXHAUSTIVE (every row, every column) ----
    verify( EXPORTING iv_tab = 'SFLIGHT' iv_target = 'diff_sflight'
            IMPORTING ev_rows = lv_rows ev_cols = lv_cols ev_diff = lv_diff ).
    ok( cond = xsdbool( lv_diff IS INITIAL AND lv_rows > 0 )
        what = 'sflight_identical'
        detail = |rows={ lv_rows } cols={ lv_cols } diff={ lv_diff }| ).
    out->write( |SFLIGHT: { lv_rows } rows x { lv_cols } cols compared, identical={ xsdbool( lv_diff IS INITIAL ) }| ).

    " ---- NEGATIVE CONTROL: corrupt one DuckDB column, expect compare to CATCH it ----
    zcl_erpl_rev_util=>query( |UPDATE diff_sflight SET price = price + 1| ).
    CLEAR lv_diff.
    compare( EXPORTING iv_tab = 'SFLIGHT' iv_target = 'diff_sflight'
             IMPORTING ev_diff = lv_diff ).
    ok( cond = xsdbool( lv_diff IS NOT INITIAL )
        what = 'neg_control_detects_diff' detail = |diff={ lv_diff }| ).

    " ---- ZWIDE_BSEG: large sample, every column incl DATE/DEC/NUMC/INT8/BLOB ----
    CLEAR: lv_rows, lv_cols, lv_diff.
    verify( EXPORTING iv_tab = 'ZWIDE_BSEG' iv_target = 'diff_wide' iv_cap = 3000 iv_hash = abap_true
            IMPORTING ev_rows = lv_rows ev_cols = lv_cols ev_diff = lv_diff ).
    ok( cond = xsdbool( lv_diff IS INITIAL AND lv_rows = 3000 )
        what = 'zwide_identical'
        detail = |rows={ lv_rows } cols={ lv_cols } diff={ lv_diff }| ).
    out->write( |ZWIDE_BSEG: { lv_rows } rows x { lv_cols } cols compared, identical={ xsdbool( lv_diff IS INITIAL ) }| ).

    " ---- REPOSRC: large RSTR DATA column (the BXML multi-chunk decode path) ----
    " REPOSRC's DATA is a multi-KB raw string serialized as repeated binary BXML
    " chunks; this guards the long-value decode fix (regression: "sXML: truncated
    " content"). hex(DATA) per row, md5'd, must match SAP byte-for-byte.
    CLEAR: lv_rows, lv_cols, lv_diff.
    verify( EXPORTING iv_tab = 'REPOSRC' iv_target = 'diff_reposrc' iv_cap = 200 iv_hash = abap_true
            IMPORTING ev_rows = lv_rows ev_cols = lv_cols ev_diff = lv_diff ).
    ok( cond = xsdbool( lv_diff IS INITIAL AND lv_rows = 200 )
        what = 'reposrc_identical'
        detail = |rows={ lv_rows } cols={ lv_cols } diff={ lv_diff }| ).
    out->write( |REPOSRC: { lv_rows } rows x { lv_cols } cols compared, identical={ xsdbool( lv_diff IS INITIAL ) }| ).

    out->write( |DIFF RESULT pass={ mv_pass } fail={ mv_fail }| ).
    CATCH cx_root INTO DATA(lx).
      out->write( |DUMP: { lx->get_text( ) }| ).
    ENDTRY.
  ENDMETHOD.
ENDCLASS.
