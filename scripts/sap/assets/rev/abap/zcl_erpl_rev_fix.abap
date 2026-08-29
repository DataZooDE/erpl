CLASS zcl_erpl_rev_fix DEFINITION PUBLIC FINAL CREATE PUBLIC.
  PUBLIC SECTION.
    INTERFACES if_oo_adt_classrun.
  PRIVATE SECTION.
    DATA mo TYPE REF TO if_oo_adt_classrun_out.
    METHODS emit IMPORTING name TYPE string it TYPE ANY TABLE.
ENDCLASS.
CLASS zcl_erpl_rev_fix IMPLEMENTATION.
  METHOD emit.
    DATA(w) = cl_sxml_string_writer=>create( type = if_sxml=>co_xt_binary ).
    CALL TRANSFORMATION id SOURCE data = it RESULT XML w.
    DATA(x) = w->get_output( ).
    " hex of the full binary payload
    DATA lv_hex TYPE string.
    lv_hex = x.
    mo->write( |FIX { name } BIN_LEN={ xstrlen( x ) }| ).
    mo->write( |FIX { name } HEX={ lv_hex }| ).
    DATA(tw) = cl_sxml_string_writer=>create( type = if_sxml=>co_xt_xml10 ).
    CALL TRANSFORMATION id SOURCE data = it RESULT XML tw.
    mo->write( |FIX { name } TXT={ cl_abap_codepage=>convert_from( tw->get_output( ) ) }| ).
  ENDMETHOD.

  METHOD if_oo_adt_classrun~main.
    mo = out.
    " Case A: 1 col INT, 1 row
    TYPES: BEGIN OF ty_a, n TYPE i, END OF ty_a.
    DATA ta TYPE STANDARD TABLE OF ty_a WITH EMPTY KEY.
    ta = VALUE #( ( n = 7 ) ).
    emit( name = 'A_int1' it = ta ).
    " Case B: 1 col STRING, 2 rows
    TYPES: BEGIN OF ty_b, s TYPE string, END OF ty_b.
    DATA tb TYPE STANDARD TABLE OF ty_b WITH EMPTY KEY.
    tb = VALUE #( ( s = 'alpha' ) ( s = 'bravo' ) ).
    emit( name = 'B_str2' it = tb ).
    " Case C: int + string, 2 rows (the canonical case)
    TYPES: BEGIN OF ty_c, id TYPE i, name TYPE string, END OF ty_c.
    DATA tc TYPE STANDARD TABLE OF ty_c WITH EMPTY KEY.
    tc = VALUE #( ( id = 1 name = 'alpha' ) ( id = 2 name = 'bravo' ) ).
    emit( name = 'C_int_str2' it = tc ).
    " Case D: empty string + special chars + unicode
    DATA td TYPE STANDARD TABLE OF ty_b WITH EMPTY KEY.
    td = VALUE #( ( s = '' ) ( s = 'a<b&c>"x"' ) ( s = 'gr' && cl_abap_conv_in_ce=>uccp( '00FC' ) && 'n' ) ).
    emit( name = 'D_specials' it = td ).
    " Case E: decimal-ish via packed
    TYPES: BEGIN OF ty_e, p TYPE p LENGTH 8 DECIMALS 2, END OF ty_e.
    DATA te TYPE STANDARD TABLE OF ty_e WITH EMPTY KEY.
    te = VALUE #( ( p = '12.34' ) ( p = '-5.60' ) ).
    emit( name = 'E_packed2' it = te ).
  ENDMETHOD.
ENDCLASS.
