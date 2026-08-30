" Calls a function with STRING exports through the ABI test destination.
"
" A reply encodes a STRING differently from a CHAR (spec SRV-7), and getting it
" wrong truncates every STRING export to one character while leaving one-character
" values looking perfectly correct. Non-ASCII text is included deliberately: it is
" what separates UTF-8 from a single-byte codepage, which the ASCII-only capture
" could not.
CLASS zcl_erpl_abi_string DEFINITION PUBLIC FINAL CREATE PUBLIC.
  PUBLIC SECTION.
    INTERFACES if_oo_adt_classrun.
ENDCLASS.
CLASS zcl_erpl_abi_string IMPLEMENTATION.
  METHOD if_oo_adt_classrun~main.
    DATA lv_cols TYPE string.
    DATA lv_rows TYPE string.
    DATA lv_cnt  TYPE string.
    DATA lv_err  TYPE string.
    DATA lv_msg  TYPE c LENGTH 255.
    CALL FUNCTION 'Z_DUCKDB_QUERY' DESTINATION 'ZERPL_ABI_TEST'
      EXPORTING iv_sql       = `SELECT 1`
      IMPORTING ev_columns   = lv_cols
                ev_rows      = lv_rows
                ev_row_count = lv_cnt
                ev_error     = lv_err
      EXCEPTIONS system_failure        = 1 MESSAGE lv_msg
                 communication_failure = 2 MESSAGE lv_msg
                 OTHERS                = 3.
    out->write( |rc = { sy-subrc }| ).
    out->write( |cols = { lv_cols }| ).
    out->write( |len = { strlen( lv_cols ) }| ).
    out->write( |msg = { lv_msg }| ).
  ENDMETHOD.
ENDCLASS.
