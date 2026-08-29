CLASS zcl_saprfc_call DEFINITION
  PUBLIC
  FINAL
  CREATE PUBLIC.

  PUBLIC SECTION.
    INTERFACES if_oo_adt_classrun.
ENDCLASS.

CLASS zcl_saprfc_call IMPLEMENTATION.

  METHOD if_oo_adt_classrun~main.
    DATA lv_echo TYPE c LENGTH 255.
    DATA lv_resp TYPE c LENGTH 255.

    CALL FUNCTION 'STFC_CONNECTION'
      DESTINATION 'ZERPL_CALL'
      EXPORTING
        requtext = 'hello from ABAP'
      IMPORTING
        echotext = lv_echo
        resptext = lv_resp
      EXCEPTIONS
        communication_failure = 1
        system_failure        = 2
        OTHERS                = 3.

    out->write( |rc = { sy-subrc }| ).
    out->write( |echo = { lv_echo }| ).
    out->write( |resp = { lv_resp }| ).
  ENDMETHOD.

ENDCLASS.
