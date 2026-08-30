" Calls Z_DUCKDB_FETCH through the ABI test destination and reports how many
" bytes of EV_XDATA came back.
"
" A reply larger than one record has to be split, and the rule for splitting a
" *server's* reply is not the one a client uses to send, nor the one a client
" uses to read a reply. This is what produces one big enough to find out.
CLASS zcl_erpl_abi_fetch DEFINITION PUBLIC FINAL CREATE PUBLIC.
  PUBLIC SECTION.
    INTERFACES if_oo_adt_classrun.
ENDCLASS.
CLASS zcl_erpl_abi_fetch IMPLEMENTATION.
  METHOD if_oo_adt_classrun~main.
    DATA lv_x    TYPE xstring.
    DATA lv_done TYPE string.
    DATA lv_err  TYPE string.
    DATA lv_msg  TYPE c LENGTH 255.
    CALL FUNCTION 'Z_DUCKDB_FETCH' DESTINATION 'ZERPL_ABI_TEST'
      EXPORTING  iv_handle = 'probe'
      IMPORTING  ev_xdata  = lv_x
                 ev_done   = lv_done
                 ev_error  = lv_err
      EXCEPTIONS system_failure        = 1 MESSAGE lv_msg
                 communication_failure = 2 MESSAGE lv_msg
                 OTHERS                = 3.
    out->write( |fetch rc={ sy-subrc } bytes={ xstrlen( lv_x ) } msg=[{ lv_msg }]| ).
  ENDMETHOD.
ENDCLASS.
