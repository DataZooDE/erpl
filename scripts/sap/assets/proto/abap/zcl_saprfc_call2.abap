" Calls STFC_CONNECTION twice through the same registered destination, so the
" capture shows what the gateway does between one call and the next: whether the
" second arrives on the connection the server already holds, or whether the
" server has to register again.
CLASS zcl_saprfc_call2 DEFINITION PUBLIC FINAL CREATE PUBLIC.
  PUBLIC SECTION.
    INTERFACES if_oo_adt_classrun.
ENDCLASS.
CLASS zcl_saprfc_call2 IMPLEMENTATION.
  METHOD if_oo_adt_classrun~main.
    DATA lv_echo TYPE c LENGTH 255.
    DATA lv_resp TYPE c LENGTH 255.
    " MESSAGE ... INTO needs a declared char-like field: an inline DATA() is
    " rejected in this position, and TYPE string is rejected too.
    DATA lv_msg  TYPE c LENGTH 255.
    DO 2 TIMES.
      DATA(lv_req) = |call { sy-index } from ABAP|.
      CLEAR: lv_echo, lv_resp.
      CALL FUNCTION 'STFC_CONNECTION' DESTINATION 'ZERPL_CAL2'
        EXPORTING  requtext = lv_req
        IMPORTING  echotext = lv_echo
                   resptext = lv_resp
        EXCEPTIONS system_failure        = 1 MESSAGE lv_msg
                   communication_failure = 2 MESSAGE lv_msg
                   OTHERS                = 3.
      out->write( |{ sy-index }: subrc={ sy-subrc } echo=[{ lv_echo }] resp=[{ lv_resp }] msg=[{ lv_msg }]| ).
    ENDDO.
  ENDMETHOD.
ENDCLASS.
