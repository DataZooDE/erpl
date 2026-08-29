" Calls STFC_CONNECTION through ZERPL_ABI_TEST, a destination reserved for the
" nwrfc C-ABI test.
"
" It exists so that suite cannot contend with the ones on ZSAPRFC_TEST. Two runs
" sharing a program ID steal each other's calls: the gateway dispatches to
" whichever server is armed, and has no way to know which run asked. That shows
" up as a test receiving a function it never mentions.
CLASS zcl_erpl_abi_call DEFINITION PUBLIC FINAL CREATE PUBLIC.
  PUBLIC SECTION.
    INTERFACES if_oo_adt_classrun.
ENDCLASS.
CLASS zcl_erpl_abi_call IMPLEMENTATION.
  METHOD if_oo_adt_classrun~main.
    DATA lv_echo TYPE c LENGTH 255.
    DATA lv_resp TYPE c LENGTH 255.
    DATA lv_msg  TYPE c LENGTH 255.
    CALL FUNCTION 'STFC_CONNECTION' DESTINATION 'ZERPL_ABI_TEST'
      EXPORTING  requtext = 'hello from the ABI test'
      IMPORTING  echotext = lv_echo
                 resptext = lv_resp
      EXCEPTIONS system_failure        = 1 MESSAGE lv_msg
                 communication_failure = 2 MESSAGE lv_msg
                 OTHERS                = 3.
    out->write( |rc = { sy-subrc }| ).
    out->write( |echo = { lv_echo }| ).
    out->write( |resp = { lv_resp }| ).
    out->write( |msg = { lv_msg }| ).
  ENDMETHOD.
ENDCLASS.
