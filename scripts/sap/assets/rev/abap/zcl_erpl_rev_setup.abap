CLASS zcl_erpl_rev_setup DEFINITION PUBLIC FINAL CREATE PUBLIC.
  PUBLIC SECTION.
    INTERFACES if_oo_adt_classrun.
ENDCLASS.
CLASS zcl_erpl_rev_setup IMPLEMENTATION.
  METHOD if_oo_adt_classrun~main.
    CALL FUNCTION 'RFC_MODIFY_TCPIP_DESTINATION'
      EXPORTING destination = 'ERPL_REV' action = 'D' EXCEPTIONS OTHERS = 0.
    CALL FUNCTION 'RFC_MODIFY_TCPIP_DESTINATION'
      EXPORTING destination = 'ERPL_REV' action = 'I'
                program = 'ERPL_REV' gwservice = 'sapgw00' method = 'R'
      EXCEPTIONS OTHERS = 9.
    " RFC_MODIFY_TCPIP_DESTINATION updates RFCDES via an update task — without an
    " explicit COMMIT WORK the classrun's implicit rollback discards it and the
    " destination never persists ("RFC destination ERPL_REV does not exist").
    COMMIT WORK AND WAIT.
    SELECT SINGLE rfcoptions FROM rfcdes WHERE rfcdest = 'ERPL_REV' INTO @DATA(o).
    out->write( |setup subrc={ sy-subrc } opts=[{ o }]| ).
  ENDMETHOD.
ENDCLASS.
