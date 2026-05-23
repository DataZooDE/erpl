CLASS zcl_wide_bseg DEFINITION
  PUBLIC FINAL CREATE PUBLIC.
  PUBLIC SECTION.
    INTERFACES if_oo_adt_classrun.
ENDCLASS.

CLASS zcl_wide_bseg IMPLEMENTATION.
  METHOD if_oo_adt_classrun~main.
    " Truncate any prior fill — re-running this class always starts fresh.
    DELETE FROM zwide_bseg.

    CONSTANTS lc_rows             TYPE i VALUE 100000.
    CONSTANTS lc_batch_size       TYPE i VALUE 1000.
    " 200 BELNR x 500 BUZEI = 100_000 unique key combinations.  BUZEI
    " stays well below NUMC(3)'s 999 ceiling.
    CONSTANTS lc_buzei_per_belnr  TYPE i VALUE 500.

    DATA lt_buffer  TYPE TABLE OF zwide_bseg WITH EMPTY KEY.
    DATA ls_row     TYPE zwide_bseg.
    DATA lv_belnr_n TYPE i.
    DATA lv_buzei_n TYPE i.
    DATA lv_inserted TYPE i.

    DO lc_rows TIMES.
      lv_belnr_n = ( sy-index - 1 ) DIV lc_buzei_per_belnr + 1.
      lv_buzei_n = ( sy-index - 1 ) MOD lc_buzei_per_belnr + 1.

      CLEAR ls_row.

      " Keys
      ls_row-bukrs = '1000'.
      ls_row-belnr = |{ lv_belnr_n WIDTH = 10 ALIGN = RIGHT PAD = '0' }|.
      ls_row-gjahr = '2026'.
      ls_row-buzei = |{ lv_buzei_n WIDTH = 3 ALIGN = RIGHT PAD = '0' }|.

      " One representative payload field per type family so the RFC
      " response carries non-trivial bytes for every type.  The other
      " ~410 columns stay at their initial values (still serialised by
      " RFC_READ_TABLE).

      " Decimals at various precisions
      ls_row-wrbtr0000  = sy-index.
      ls_row-smabtr0000 = sy-index * '0.5'.
      ls_row-lgabtr0000 = sy-index * '1.0001'.
      ls_row-menge0000  = sy-index * '1.001'.

      " Floating point
      ls_row-ratio0000  = sy-index / '7'.

      " Character at various lengths
      ls_row-ckey0000   = |DOC{ sy-index }|.
      ls_row-ck200000   = |REF20-{ sy-index }|.
      ls_row-sgtxt0000  = |Synth row { sy-index } for issue 67 repro|.

      " Variable-length / structured strings
      ls_row-note0000   = |Free-form note for synthetic row { sy-index }; type-varied payload exercising the conversion path.|.
      ls_row-ssht0000   = |Short str { sy-index }|.

      " Binary
      ls_row-blob0000   = CONV xstring( |{ sy-index WIDTH = 32 ALIGN = RIGHT PAD = '0' }| ).

      " Date / time
      ls_row-augdt0000  = sy-datum.
      ls_row-uztim0000  = sy-uzeit.

      " NUMC at various lengths
      ls_row-monat0000  = '0001'.
      ls_row-n60000     = |{ sy-index WIDTH = 6 ALIGN = RIGHT PAD = '0' }|.
      ls_row-n100000    = |{ sy-index WIDTH = 10 ALIGN = RIGHT PAD = '0' }|.

      " Status flag / language / unit code
      ls_row-flag0000   = 'X'.
      ls_row-lang0000   = 'E'.
      ls_row-waers0000  = 'EUR'.

      " Integers
      ls_row-num40000   = sy-index.
      ls_row-num80000   = sy-index * 1000.
      ls_row-byte0000   = sy-index MOD 100.
      ls_row-short0000  = sy-index MOD 30000.

      APPEND ls_row TO lt_buffer.

      IF lines( lt_buffer ) >= lc_batch_size.
        INSERT zwide_bseg FROM TABLE @lt_buffer.
        lv_inserted = lv_inserted + lines( lt_buffer ).
        CLEAR lt_buffer.

        " Periodic progress report every 100_000 rows
        IF lv_inserted MOD 100000 = 0.
          out->write( |progress: { lv_inserted } rows inserted| ).
        ENDIF.
      ENDIF.
    ENDDO.

    IF lt_buffer IS NOT INITIAL.
      INSERT zwide_bseg FROM TABLE @lt_buffer.
      lv_inserted = lv_inserted + lines( lt_buffer ).
    ENDIF.

    SELECT COUNT(*) FROM zwide_bseg INTO @DATA(lv_count).
    out->write( |zwide_bseg populated: { lv_count } rows| ).
  ENDMETHOD.
ENDCLASS.
