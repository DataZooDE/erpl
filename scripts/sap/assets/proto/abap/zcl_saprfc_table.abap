CLASS zcl_saprfc_table DEFINITION
  PUBLIC
  FINAL
  CREATE PUBLIC.

  PUBLIC SECTION.
    INTERFACES if_oo_adt_classrun.
ENDCLASS.

CLASS zcl_saprfc_table IMPLEMENTATION.

  METHOD if_oo_adt_classrun~main.
    " STFC_STRUCTURE carries a structure in both directions and a table
    " parameter, which is what makes it the test for server-side tables.
    DATA ls_in   TYPE rfctest.
    DATA ls_echo TYPE rfctest.
    DATA lv_resp TYPE c LENGTH 255.
    DATA lt_tab  TYPE TABLE OF rfctest.
    DATA ls_row  TYPE rfctest.

    ls_in-rfcfloat = '1.25'.
    ls_in-rfcchar1 = 'A'.
    ls_in-rfcint2  = 4242.
    ls_in-rfcint1  = 7.
    ls_in-rfcchar4 = 'ABCD'.
    ls_in-rfcint4  = 123456.
    ls_in-rfchex3  = 'ABCDEF'.
    ls_in-rfcchar2 = 'XY'.
    ls_in-rfctime  = '123456'.
    ls_in-rfcdate  = '20260811'.
    ls_in-rfcdata1 = 'straße 東京 payload'.
    ls_in-rfcdata2 = 'second payload'.

    ls_row-rfcchar4 = 'ROW1'.
    ls_row-rfcint4  = 11.
    APPEND ls_row TO lt_tab.
    ls_row-rfcchar4 = 'ROW2'.
    ls_row-rfcint4  = 22.
    APPEND ls_row TO lt_tab.

    CALL FUNCTION 'STFC_STRUCTURE'
      DESTINATION 'ZERPL_TBL'
      EXPORTING
        importstruct = ls_in
      IMPORTING
        echostruct   = ls_echo
        resptext     = lv_resp
      TABLES
        rfctable     = lt_tab
      EXCEPTIONS
        communication_failure = 1
        system_failure        = 2
        OTHERS                = 3.

    out->write( |rc = { sy-subrc }| ).
    out->write( |resp = { lv_resp }| ).
    out->write( |echo char4 = { ls_echo-rfcchar4 } int4 = { ls_echo-rfcint4 }| ).
    out->write( |echo date = { ls_echo-rfcdate } time = { ls_echo-rfctime }| ).
    out->write( |echo hex3 = { ls_echo-rfchex3 } float = { ls_echo-rfcfloat }| ).
    out->write( |rows = { lines( lt_tab ) }| ).
    LOOP AT lt_tab INTO ls_row.
      out->write( |row { sy-tabix } char4 = { ls_row-rfcchar4 } int4 = { ls_row-rfcint4 }| ).
    ENDLOOP.
  ENDMETHOD.

ENDCLASS.
