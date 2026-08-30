CLASS zcl_saprfc_decf DEFINITION
  PUBLIC
  FINAL
  CREATE PUBLIC.

  PUBLIC SECTION.
    INTERFACES if_oo_adt_classrun.
ENDCLASS.

CLASS zcl_saprfc_decf IMPLEMENTATION.

  METHOD if_oo_adt_classrun~main.
    " RRT_MATH_LINREGPOINT takes a DECFLOAT16 and returns one, which makes it
    " the only function on this system that puts the type on the wire in both
    " directions.
    DATA lv_x TYPE decfloat16 VALUE '1.25'.
    DATA lv_y TYPE decfloat16.
    DATA lv_34 TYPE decfloat34 VALUE '-12345.6789'.

    " What ABAP holds internally, for comparison with what crosses the wire.
    FIELD-SYMBOLS <raw16> TYPE x.
    FIELD-SYMBOLS <raw34> TYPE x.
    ASSIGN lv_x TO <raw16> CASTING TYPE x.
    ASSIGN lv_34 TO <raw34> CASTING TYPE x.
    out->write( |internal decfloat16 1.25 = { <raw16> }| ).
    out->write( |internal decfloat34 -12345.6789 = { <raw34> }| ).

    CALL FUNCTION 'RRT_MATH_LINREGPOINT'
      DESTINATION 'ZERPL_DECF'
      EXPORTING
        i_point_val_x = lv_x
      IMPORTING
        e_point_val_y = lv_y
      EXCEPTIONS
        communication_failure = 1
        system_failure        = 2
        OTHERS                = 3.

    out->write( |rc = { sy-subrc }| ).
    out->write( |y = { lv_y }| ).
  ENDMETHOD.

ENDCLASS.
