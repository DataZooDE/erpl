CLASS zcl_erpl_icm_reload DEFINITION
  PUBLIC FINAL CREATE PUBLIC.
  PUBLIC SECTION.
    INTERFACES if_oo_adt_classrun.
ENDCLASS.

CLASS zcl_erpl_icm_reload IMPLEMENTATION.
  METHOD if_oo_adt_classrun~main.
    " The ICM caches the server PSE; adding a trusted client certificate to
    " SAPSSLS.pse has no effect until it is told to re-read it. Not
    " remote-enabled, hence a classrun rather than an RFC call.
    CALL FUNCTION 'ICM_SSL_PSE_CHANGED'
      EXCEPTIONS
        OTHERS = 1.
    out->write( |ICM_SSL_PSE_CHANGED subrc={ sy-subrc }| ).
  ENDMETHOD.
ENDCLASS.
