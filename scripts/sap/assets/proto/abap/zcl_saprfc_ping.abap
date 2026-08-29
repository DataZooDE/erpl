" One RFC_PING through the test destination, and nothing else.
"
" The obvious way to make the gateway dispatch a ping is
" CNV_MBT_RFC_CONNECTION_CHECK, and it was used here first. It is the wrong
" tool: after the ping it calls STFC_CONNECTION again (with 'probe', then
" 'threaded call'), and a test that answers only the ping leaves those queued at
" the gateway. They are then delivered to the next server that arms under this
" program ID (spec SRV-9) -- so the *next* test receives a call it never made
" and fails naming a function it never mentions.
"
" One call in, one call out, nothing left behind.
CLASS zcl_saprfc_ping DEFINITION PUBLIC FINAL CREATE PUBLIC.
  PUBLIC SECTION.
    INTERFACES if_oo_adt_classrun.
ENDCLASS.
CLASS zcl_saprfc_ping IMPLEMENTATION.
  METHOD if_oo_adt_classrun~main.
    DATA lv_msg TYPE c LENGTH 255.
    CALL FUNCTION 'RFC_PING' DESTINATION 'ZERPL_PING'
      EXCEPTIONS system_failure        = 1 MESSAGE lv_msg
                 communication_failure = 2 MESSAGE lv_msg
                 OTHERS                = 3.
    out->write( |ping rc = { sy-subrc } msg = { lv_msg }| ).
  ENDMETHOD.
ENDCLASS.
