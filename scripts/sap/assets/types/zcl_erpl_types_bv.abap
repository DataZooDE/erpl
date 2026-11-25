CLASS zcl_erpl_types_bv DEFINITION
  PUBLIC FINAL CREATE PUBLIC.
  PUBLIC SECTION.
    INTERFACES if_oo_adt_classrun.
  PRIVATE SECTION.
    TYPES ty_exports TYPE STANDARD TABLE OF rsexp WITH DEFAULT KEY.
    TYPES ty_imports TYPE STANDARD TABLE OF rsimp WITH DEFAULT KEY.
    TYPES ty_tables  TYPE STANDARD TABLE OF rstbl WITH DEFAULT KEY.
    DATA mo_out TYPE REF TO if_oo_adt_classrun_out.
    METHODS say IMPORTING iv_text TYPE string.
    METHODS fill_rows.
    METHODS make_fm.
    METHODS utf8 IMPORTING iv_hex TYPE xstring RETURNING VALUE(rv) TYPE string.
ENDCLASS.

CLASS zcl_erpl_types_bv IMPLEMENTATION.


  METHOD say.
    mo_out->write( iv_text ).
  ENDMETHOD.

  METHOD utf8.
    rv = cl_abap_codepage=>convert_from( source = iv_hex codepage = `UTF-8` ).
  ENDMETHOD.

  METHOD fill_rows.
    DATA lt TYPE STANDARD TABLE OF zerpl_types_bv WITH DEFAULT KEY.
    DATA ls TYPE zerpl_types_bv.

    " MAX: every field at the largest value its declaration allows.
    CLEAR ls.
    ls-row_id = 'MAX'.
    ls-p1_0   = '9'.
    ls-p2_0   = '99'.
    ls-p2_1   = '9.9'.
    ls-p2_2   = '0.99'.
    ls-p3_1   = '99.9'.
    ls-p4_1   = '999.9'.
    ls-p4_2   = '99.99'.
    ls-p5_5   = '0.99999'.
    ls-p16_2  = '99999999999999.99'.
    ls-p30_14 = '9999999999999999.99999999999999'.
    ls-p31_14 = '99999999999999999.99999999999999'.
    ls-p31_0  = '9999999999999999999999999999999'.
    ls-waers  = 'EUR'.
    ls-c13_2  = '99999999999.99'.
    ls-c16_2  = '99999999999999.99'.
    ls-meins  = 'KG'.
    ls-q13_3  = '9999999999.999'.
    ls-q14_3  = '99999999999.999'.
    ls-n10    = '9999999999'.
    ls-ch10   = 'ABCDEFGHIJ'.
    ls-i1     = 255.
    ls-i2     = 32767.
    ls-i4     = 2147483647.
    ls-i8     = 9223372036854775807.
    ls-fl     = CONV f( '1.7976931348623157E+308' ).
    ls-df16   = CONV decfloat16( '9999999999999999' ).
    ls-df34   = CONV decfloat34( '9999999999999999999999999999999999' ).
    ls-dt     = '99991231'.
    ls-tm     = '235959'.
    ls-rw     = 'FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF'.
    ls-ac     = '999912'.
    ls-pr     = 99. " PREC holds a QUAN field's precision; HANA refuses 255 and 32767
    ls-lg     = 'E'.
    ls-ut     = CONV utclong( '9999-12-31T23:59:59.9999999' ).
    APPEND ls TO lt.

    " NEG: the most negative value of every signed field.
    CLEAR ls.
    ls-row_id = 'NEG'.
    ls-p1_0   = '-9'.
    ls-p2_0   = '-99'.
    ls-p2_1   = '-9.9'.
    ls-p2_2   = '-0.99'.
    ls-p3_1   = '-99.9'.
    ls-p4_1   = '-999.9'.
    ls-p4_2   = '-99.99'.
    ls-p5_5   = '-0.99999'.
    ls-p16_2  = '-99999999999999.99'.
    ls-p30_14 = '-9999999999999999.99999999999999'.
    ls-p31_14 = '-99999999999999999.99999999999999'.
    ls-p31_0  = '-9999999999999999999999999999999'.
    ls-waers  = 'EUR'.
    ls-c13_2  = '-99999999999.99'.
    ls-c16_2  = '-99999999999999.99'.
    ls-meins  = 'KG'.
    ls-q13_3  = '-9999999999.999'.
    ls-q14_3  = '-99999999999.999'.
    ls-i1     = 0.
    ls-i2     = -32768.
    ls-i4     = -2147483648.
    ls-i8     = -9223372036854775807 - 1.
    ls-fl     = CONV f( '-1.7976931348623157E+308' ).
    ls-df16   = CONV decfloat16( '-9999999999999999' ).
    ls-df34   = CONV decfloat34( '-9999999999999999999999999999999999' ).
    APPEND ls TO lt.

    " ZERO: every field initial (zero, blank, 00000000 date, initial UTCLONG).
    CLEAR ls.
    ls-row_id = 'ZERO'.
    APPEND ls TO lt.

    " SMALL: the smallest positive step of every field, leading zeros kept.
    CLEAR ls.
    ls-row_id = 'SMALL'.
    ls-p1_0   = '1'.
    ls-p2_0   = '1'.
    ls-p2_1   = '0.1'.
    ls-p2_2   = '0.01'.
    ls-p3_1   = '0.1'.
    ls-p4_1   = '0.1'.
    ls-p4_2   = '0.01'.
    ls-p5_5   = '0.00001'.
    ls-p16_2  = '0.01'.
    ls-p30_14 = '0.00000000000001'.
    ls-p31_14 = '0.00000000000001'.
    ls-p31_0  = '1'.
    ls-waers  = 'EUR'.
    ls-c13_2  = '0.01'.
    ls-c16_2  = '0.01'.
    ls-meins  = 'KG'.
    ls-q13_3  = '0.001'.
    ls-q14_3  = '0.001'.
    ls-n10    = '0000000001'.
    ls-ch10   = 'a'.
    ls-i1     = 1.
    ls-i2     = 1.
    ls-i4     = 1.
    ls-i8     = 1.
    ls-fl     = CONV f( '2.2250738585072014E-308' ).
    ls-df16   = CONV decfloat16( '1.234567890123456' ).
    ls-df34   = CONV decfloat34( '1.234567890123456789012345678901234' ).
    ls-dt     = '00010101'.
    ls-tm     = '000001'.
    ls-rw     = '00000000000000000000000000000001'.
    ls-ac     = '000101'.
    ls-pr     = 1.
    ls-lg     = 'D'.
    ls-ut     = CONV utclong( '0001-01-01T00:00:00.0000001' ).
    APPEND ls TO lt.

    " JPY: a currency without decimals (TCURX CURRDEC 0). SAP stores 12345 JPY
    " as 123.45 internally; the row pins whether a reader shifts it or not.
    CLEAR ls.
    ls-row_id = 'JPY'.
    ls-waers  = 'JPY'.
    ls-c13_2  = '123.45'.
    ls-c16_2  = '123.45'.
    APPEND ls TO lt.

    " UNI: full-width text. CHAR(10) is ten UTF-16 units, so five U+1F600 fill
    " it exactly (surrogate pairs); BMP10 is ten two-byte UTF-8 characters.
    CLEAR ls.
    ls-row_id = 'NONBMP'.
    ls-ch10   = utf8( CONV xstring( 'F09F9880F09F9880F09F9880F09F9880F09F9880' ) ).
    APPEND ls TO lt.
    CLEAR ls.
    ls-row_id = 'BMP10'.
    ls-ch10   = utf8( CONV xstring( 'C3A4C3B6C3BCC39FC384C396C39CC3A9C3A8C3AA' ) ).
    APPEND ls TO lt.

    DELETE FROM zerpl_types_bv.
    " Row by row, so a value the database cannot store names its row and the
    " column whose clearing makes the insert succeed.
    DATA lo_desc TYPE REF TO cl_abap_structdescr.
    DATA lv_ok   TYPE i.
    lo_desc ?= cl_abap_typedescr=>describe_by_name( 'ZERPL_TYPES_BV' ).
    LOOP AT lt INTO ls.
      TRY.
          INSERT zerpl_types_bv FROM @ls.
          lv_ok = lv_ok + 1.
        CATCH cx_sy_open_sql_db INTO DATA(lx).
          say( |{ ls-row_id }: insert failed: { lx->get_text( ) }| ).
          LOOP AT lo_desc->components INTO DATA(ls_comp).
            DATA(ls_try) = ls.
            ASSIGN COMPONENT ls_comp-name OF STRUCTURE ls_try TO FIELD-SYMBOL(<f>).
            CLEAR <f>.
            ls_try-row_id = 'PROBE'.
            TRY.
                INSERT zerpl_types_bv FROM @ls_try.
                say( |  { ls-row_id }: fits once { ls_comp-name } is cleared| ).
                DELETE FROM zerpl_types_bv WHERE row_id = 'PROBE'.
              CATCH cx_sy_open_sql_db.
            ENDTRY.
          ENDLOOP.
      ENDTRY.
    ENDLOOP.
    say( |ZERPL_TYPES_BV: { lv_ok } of { lines( lt ) } rows stored| ).
    COMMIT WORK.
  ENDMETHOD.

  METHOD make_fm.
    DATA lt_import TYPE ty_imports.
    DATA lt_export TYPE ty_exports.
    DATA lt_tables TYPE ty_tables.
    DATA lt_src    TYPE STANDARD TABLE OF string.
    DATA lt_new    TYPE STANDARD TABLE OF string.
    DATA lv_line   TYPE string.
    DATA lv_last   TYPE i.
    DATA lv_num    TYPE tfdir-include.
    DATA lv_incl   TYPE progname.
    DATA lv_name   TYPE rs38l_fnam VALUE 'Z_ERPL_TYPES_BV'.
    DATA lv_short  TYPE tftit-stext VALUE 'erpl boundary values (issue 255)'.

    APPEND VALUE #( parameter = 'ES_MAX' typ = 'ZERPL_TYPES_BV' ) TO lt_export.
    APPEND VALUE #( parameter = 'ET_ROWS' dbstruct = 'ZERPL_TYPES_BV' ) TO lt_tables.

    SELECT SINGLE include FROM tfdir INTO @lv_num WHERE funcname = @lv_name.
    IF sy-subrc <> 0.
      CALL FUNCTION 'RS_FUNCTIONMODULE_INSERT'
        EXPORTING
          funcname            = lv_name
          function_pool       = 'ZERPL_PYRFC'
          interface_global    = 'X'
          remote_call         = 'R'
          short_text          = lv_short
          suppress_corr_check = 'X'
        TABLES
          import_parameter    = lt_import
          export_parameter    = lt_export
          tables_parameter    = lt_tables
        EXCEPTIONS
          OTHERS              = 1.
      IF sy-subrc <> 0.
        say( |{ lv_name }: create failed { sy-msgid }{ sy-msgno }| ).
        RETURN.
      ENDIF.
      SELECT SINGLE include FROM tfdir INTO @lv_num WHERE funcname = @lv_name.
    ENDIF.
    IF lv_num IS INITIAL.
      say( |{ lv_name }: no include| ).
      RETURN.
    ENDIF.

    lv_incl = |LZERPL_PYRFCU{ lv_num }|.
    READ REPORT lv_incl INTO lt_src.
    LOOP AT lt_src INTO lv_line.
      IF condense( lv_line ) CP '*"*'.
        lv_last = sy-tabix.
      ENDIF.
    ENDLOOP.
    LOOP AT lt_src INTO lv_line.
      IF sy-tabix > lv_last.
        EXIT.
      ENDIF.
      APPEND lv_line TO lt_new.
    ENDLOOP.
    APPEND `  SELECT * FROM zerpl_types_bv INTO TABLE @et_rows ORDER BY row_id.` TO lt_new.
    APPEND `  SELECT SINGLE * FROM zerpl_types_bv INTO @es_max WHERE row_id = 'MAX'.` TO lt_new.
    APPEND `ENDFUNCTION.` TO lt_new.
    INSERT REPORT lv_incl FROM lt_new.
    say( |{ lv_name }: body -> { lv_incl }, subrc={ sy-subrc }| ).
  ENDMETHOD.

  METHOD if_oo_adt_classrun~main.
    " Boundary values for every DDIC type erpl maps (erpl issue #255): each packed
    " length at its largest value, negative and smallest values, initial values,
    " full-width text including non-BMP characters, integer extremes. Rows are
    " read through sap_read_table (table ZERPL_TYPES_BV) and through the invoke
    " path (Z_ERPL_TYPES_BV returns the same rows), so both paths see one truth.
    mo_out = out.
    fill_rows( ).
    make_fm( ).
  ENDMETHOD.

ENDCLASS.
