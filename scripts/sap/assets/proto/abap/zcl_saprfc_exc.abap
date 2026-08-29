CLASS zcl_saprfc_exc DEFINITION
  PUBLIC
  FINAL
  CREATE PUBLIC.

  PUBLIC SECTION.
    INTERFACES if_oo_adt_classrun.
ENDCLASS.

CLASS zcl_saprfc_exc IMPLEMENTATION.

  METHOD if_oo_adt_classrun~main.
    " RFC_READ_TABLE declares classic exceptions, so a server can raise one
    " and ABAP maps it to the sy-subrc the caller assigned. The MESSAGE
    " additions catch the other kind, which carries text instead of a name.
    DATA lv_msg TYPE c LENGTH 200.
    DATA lt_data TYPE TABLE OF tab512.

    CALL FUNCTION 'RFC_READ_TABLE'
      DESTINATION 'ZERPL_EXC'
      EXPORTING
        query_table = 'T000'
      TABLES
        data        = lt_data
      EXCEPTIONS
        table_not_available   = 1
        table_without_data    = 2
        option_not_valid      = 3
        field_not_valid       = 4
        not_authorized        = 5
        data_buffer_exceeded  = 6
        system_failure        = 8 MESSAGE lv_msg
        communication_failure = 9 MESSAGE lv_msg
        OTHERS                = 10.

    out->write( |rc = { sy-subrc }| ).
    out->write( |msg = { lv_msg }| ).
  ENDMETHOD.

ENDCLASS.
