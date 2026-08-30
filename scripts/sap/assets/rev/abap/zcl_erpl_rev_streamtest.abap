CLASS zcl_erpl_rev_streamtest DEFINITION PUBLIC FINAL CREATE PUBLIC.
  PUBLIC SECTION.
    INTERFACES if_oo_adt_classrun.
  PRIVATE SECTION.
    DATA: mv_pass TYPE i, mv_fail TYPE i, mo TYPE REF TO if_oo_adt_classrun_out.
    METHODS ok IMPORTING cond TYPE abap_bool what TYPE string detail TYPE string DEFAULT ''.
ENDCLASS.

CLASS zcl_erpl_rev_streamtest IMPLEMENTATION.
  METHOD ok.
    IF cond = abap_true. mv_pass = mv_pass + 1.
    ELSE. mv_fail = mv_fail + 1. mo->write( |FAIL { what }: { detail }| ). ENDIF.
  ENDMETHOD.

  METHOD if_oo_adt_classrun~main.
    mo = out.
    CONSTANTS c_dest TYPE rfcdest VALUE 'ERPL_REV'.

    " A dynamic result table (ID:int, V:string) — mirrors result_to_alv's RTTS
    " build; the BXML pages deserialize straight into it via CALL TRANSFORMATION.
    DATA(lo_s) = cl_abap_structdescr=>get( VALUE #(
      ( name = 'ID' type = cl_abap_elemdescr=>get_i( ) )
      ( name = 'V'  type = cl_abap_elemdescr=>get_string( ) ) ) ).
    DATA(lo_t) = cl_abap_tabledescr=>create( lo_s ).
    DATA lr_all TYPE REF TO data.
    CREATE DATA lr_all TYPE HANDLE lo_t.
    FIELD-SYMBOLS <all> TYPE STANDARD TABLE.
    ASSIGN lr_all->* TO <all>.

    " 1) OPEN a cursor over 5000 rows.
    DATA: lv_handle TYPE string, lv_cols TYPE string,
          lv_err TYPE string, lv_msg TYPE c LENGTH 255.
    CALL FUNCTION 'Z_DUCKDB_OPEN' DESTINATION c_dest
      EXPORTING iv_sql = `SELECT i AS id, 'row_' || i AS v FROM range(5000) t(i)`
      IMPORTING ev_handle = lv_handle ev_columns = lv_cols ev_error = lv_err
      EXCEPTIONS system_failure = 1 MESSAGE lv_msg
                 communication_failure = 2 MESSAGE lv_msg OTHERS = 3.
    ok( cond = xsdbool( sy-subrc = 0 AND lv_err IS INITIAL AND lv_handle IS NOT INITIAL )
        what = 'open' detail = |subrc={ sy-subrc } err={ lv_err }{ lv_msg } h={ lv_handle }| ).

    " 2) FETCH pages until done; decode each BXML page into the dynamic table.
    DATA: lv_pages TYPE i, lv_done TYPE string, lv_fetched TYPE string,
          lv_xdata TYPE xstring.
    DO 100 TIMES.                       " safety bound
      CLEAR: lv_xdata, lv_done, lv_fetched, lv_err.
      CALL FUNCTION 'Z_DUCKDB_FETCH' DESTINATION c_dest
        EXPORTING iv_handle = lv_handle iv_page_rows = '2048'
        IMPORTING ev_xdata = lv_xdata ev_fetched = lv_fetched
                  ev_done = lv_done ev_error = lv_err
        EXCEPTIONS system_failure = 1 MESSAGE lv_msg
                   communication_failure = 2 MESSAGE lv_msg OTHERS = 3.
      IF sy-subrc <> 0 OR lv_err IS NOT INITIAL.
        ok( cond = abap_false what = 'fetch' detail = |subrc={ sy-subrc } err={ lv_err }{ lv_msg }| ).
        EXIT.
      ENDIF.
      lv_pages = lv_pages + 1.
      IF lv_xdata IS NOT INITIAL.
        " The slim part: standard binary-sXML reader + identity transformation
        " deserialize the page straight into a fresh dynamic page table.
        DATA lr_page TYPE REF TO data.
        CREATE DATA lr_page TYPE HANDLE lo_t.
        FIELD-SYMBOLS <page> TYPE STANDARD TABLE.
        ASSIGN lr_page->* TO <page>.
        DATA(lo_rd) = cl_sxml_string_reader=>create( input = lv_xdata ).
        CALL TRANSFORMATION id SOURCE XML lo_rd RESULT data = <page>.
        INSERT LINES OF <page> INTO TABLE <all>.
      ENDIF.
      IF lv_done = 'X'. EXIT. ENDIF.
    ENDDO.

    " 3) CLOSE.
    CALL FUNCTION 'Z_DUCKDB_CLOSE' DESTINATION c_dest
      EXPORTING iv_handle = lv_handle IMPORTING ev_error = lv_err
      EXCEPTIONS system_failure = 1 MESSAGE lv_msg
                 communication_failure = 2 MESSAGE lv_msg OTHERS = 3.
    ok( cond = xsdbool( sy-subrc = 0 ) what = 'close' detail = |subrc={ sy-subrc }{ lv_msg }| ).

    " 4) Assert: all 5000 rows arrived, paged (2048 each -> 3 pages), typed.
    ok( cond = xsdbool( lines( <all> ) = 5000 ) what = 'rowcount'
        detail = |got { lines( <all> ) }| ).
    ok( cond = xsdbool( lv_pages >= 3 ) what = 'paged' detail = |{ lv_pages } pages| ).

    FIELD-SYMBOLS <first> TYPE any.
    READ TABLE <all> INDEX 1 ASSIGNING <first>.
    FIELD-SYMBOLS <id> TYPE any.
    FIELD-SYMBOLS <v>  TYPE any.
    ASSIGN COMPONENT 'ID' OF STRUCTURE <first> TO <id>.
    ASSIGN COMPONENT 'V'  OF STRUCTURE <first> TO <v>.
    ok( cond = xsdbool( <id> = 0 AND <v> = 'row_0' ) what = 'firstrow'
        detail = |id={ <id> } v={ <v> }| ).
    FIELD-SYMBOLS <last> TYPE any.
    READ TABLE <all> INDEX 5000 ASSIGNING <last>.
    ASSIGN COMPONENT 'V'  OF STRUCTURE <last> TO <v>.
    ASSIGN COMPONENT 'ID' OF STRUCTURE <last> TO <id>.
    " Assert the LAST row's id too: an unread integer stays 0, so checking only
    " row 0's id would false-pass even if nothing bound.
    ok( cond = xsdbool( <id> = 4999 AND <v> = 'row_4999' ) what = 'lastrow'
        detail = |id={ <id> } v={ <v> }| ).

    " 5) The slim util wrapper: query_stream does the same in one call.
    DATA(s) = zcl_erpl_rev_util=>query_stream(
                iv_sql = `SELECT i AS id, 'row_' || i AS v FROM range(5000) t(i)` ).
    ok( cond = xsdbool( s-error IS INITIAL ) what = 'qs_noerr' detail = s-error ).
    ok( cond = xsdbool( s-row_count = 5000 ) what = 'qs_count' detail = |{ s-row_count }| ).
    ok( cond = xsdbool( s-truncated = abap_false ) what = 'qs_nottrunc' detail = '' ).

    " 6) Display cap: query_stream with iv_maxrows stops early and flags it.
    DATA(s2) = zcl_erpl_rev_util=>query_stream(
                 iv_sql = `SELECT i AS id FROM range(50000) t(i)` iv_maxrows = 10000 ).
    ok( cond = xsdbool( s2-error IS INITIAL ) what = 'qs2_noerr' detail = s2-error ).
    ok( cond = xsdbool( s2-row_count >= 10000 AND s2-truncated = abap_true )
        what = 'qs2_capped' detail = |rows={ s2-row_count } trunc={ s2-truncated }| ).

    out->write( |STREAM RESULT pass={ mv_pass } fail={ mv_fail }| ).
  ENDMETHOD.
ENDCLASS.
