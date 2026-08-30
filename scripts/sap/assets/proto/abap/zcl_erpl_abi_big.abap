" Sends an XSTRING of a size given in ZERPL_ABI_SIZE (an ABAP memory id),
" through the ABI test destination, and reports how it went.
"
" A large import is not merely a big field: above the server's threshold ABAP
" compresses it, and above the record size it fragments it. Both are things a
" server has to undo, and neither shows up until the payload is big enough --
" so this exists to find the exact size at which a server stops coping.
CLASS zcl_erpl_abi_big DEFINITION PUBLIC FINAL CREATE PUBLIC.
  PUBLIC SECTION.
    INTERFACES if_oo_adt_classrun.
  PRIVATE SECTION.
    METHODS try IMPORTING iv_bytes TYPE i
                          io_out   TYPE REF TO if_oo_adt_classrun_out.
ENDCLASS.
CLASS zcl_erpl_abi_big IMPLEMENTATION.
  METHOD try.
    DATA lv_x    TYPE xstring.
    DATA lv_msg  TYPE c LENGTH 255.
    DATA lv_aff  TYPE string.
    DATA lv_err  TYPE string.
    DATA lv_unit TYPE xstring VALUE '00112233445566778899AABBCCDDEEFF'.

    " Build iv_bytes of payload by repeating a 16-byte unit. Compressible
    " enough that the compression path is taken when the size crosses the
    " threshold, which is the point.
    CLEAR lv_x.
    WHILE xstrlen( lv_x ) < iv_bytes.
      CONCATENATE lv_x lv_unit INTO lv_x IN BYTE MODE.
    ENDWHILE.

    CALL FUNCTION 'Z_DUCKDB_INGEST' DESTINATION 'ZERPL_ABI_TEST'
      EXPORTING  iv_target        = 'probe'
                 iv_xdata         = lv_x
      IMPORTING  ev_rows_affected = lv_aff
                 ev_error         = lv_err
      EXCEPTIONS system_failure        = 1 MESSAGE lv_msg
                 communication_failure = 2 MESSAGE lv_msg
                 OTHERS                = 3.
    io_out->write( |size={ xstrlen( lv_x ) } rc={ sy-subrc } got=[{ lv_aff }] err=[{ lv_err }] msg=[{ lv_msg }]| ).
  ENDMETHOD.

  METHOD if_oo_adt_classrun~main.
    " A ladder either side of the 8192-byte compression threshold and the
    " record size, so the first failing rung names the boundary.
    try( iv_bytes = 1024      io_out = out ).
    try( iv_bytes = 8000      io_out = out ).
    try( iv_bytes = 8192      io_out = out ).
    try( iv_bytes = 16384     io_out = out ).
    try( iv_bytes = 65536     io_out = out ).
    try( iv_bytes = 262144    io_out = out ).
    try( iv_bytes = 1048576   io_out = out ).
  ENDMETHOD.
ENDCLASS.
