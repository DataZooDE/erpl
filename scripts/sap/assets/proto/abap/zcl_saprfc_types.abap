CLASS zcl_saprfc_types DEFINITION
  PUBLIC
  FINAL
  CREATE PUBLIC.

  PUBLIC SECTION.
    INTERFACES if_oo_adt_classrun.
ENDCLASS.

CLASS zcl_saprfc_types IMPLEMENTATION.

  METHOD if_oo_adt_classrun~main.
    " RFC_METADATA_TEST exists to exercise the type system: variable-length
    " STRING and XSTRING, DATE, TIME, FLOAT and INT4 in, and a mix out.
    DATA lv_string   TYPE string.
    DATA lv_xstring  TYPE xstring.
    DATA lv_date     TYPE d.
    DATA lv_time     TYPE t.
    DATA lv_float    TYPE f.
    DATA lv_int      TYPE i.
    DATA lv_int_out  TYPE i.
    DATA lv_exitcode TYPE i.

    lv_string  = 'straße 東京 🦀 string in'.
    lv_xstring = 'DEADBEEF0011FF'.
    lv_date    = '20260811'.
    lv_time    = '235959'.
    lv_float   = '2.5'.
    lv_int     = 424242.

    CALL FUNCTION 'RFC_METADATA_TEST'
      DESTINATION 'ZERPL_TYPE'
      EXPORTING
        abap_string  = lv_string
        abap_xstring = lv_xstring
        abap_date    = lv_date
        abap_time    = lv_time
        abap_float   = lv_float
        abap_int     = lv_int
      IMPORTING
        abap_int_out = lv_int_out
        exitcode     = lv_exitcode
      EXCEPTIONS
        communication_failure = 1
        system_failure        = 2
        OTHERS                = 3.

    out->write( |rc = { sy-subrc }| ).
    out->write( |int_out = { lv_int_out }| ).
    out->write( |exitcode = { lv_exitcode }| ).
  ENDMETHOD.

ENDCLASS.
