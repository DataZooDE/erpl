CLASS zcl_erpl_rev_diag DEFINITION PUBLIC FINAL CREATE PUBLIC.
  PUBLIC SECTION.
    INTERFACES if_oo_adt_classrun.
ENDCLASS.
CLASS zcl_erpl_rev_diag IMPLEMENTATION.
  METHOD if_oo_adt_classrun~main.
    DATA: lv_req TYPE c LENGTH 255 VALUE 'hi',
          lv_echo TYPE c LENGTH 255, lv_resp TYPE c LENGTH 255,
          lv_msg TYPE c LENGTH 255.
    CALL FUNCTION 'STFC_CONNECTION' DESTINATION 'ERPL_REV'
      EXPORTING requtext = lv_req
      IMPORTING echotext = lv_echo resptext = lv_resp
      EXCEPTIONS system_failure = 1 MESSAGE lv_msg
                 communication_failure = 2 MESSAGE lv_msg
                 OTHERS = 3.
    out->write( |subrc={ sy-subrc }| ).
    out->write( |msg=[{ lv_msg }]| ).
    out->write( |msgid={ sy-msgid } msgno={ sy-msgno }| ).
    out->write( |v1=[{ sy-msgv1 }] v2=[{ sy-msgv2 }]| ).
    out->write( |echo=[{ lv_echo }] resp=[{ lv_resp }]| ).
  ENDMETHOD.
ENDCLASS.
