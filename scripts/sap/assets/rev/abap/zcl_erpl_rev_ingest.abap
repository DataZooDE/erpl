CLASS zcl_erpl_rev_ingest DEFINITION PUBLIC FINAL CREATE PUBLIC.
  PUBLIC SECTION.
    INTERFACES if_oo_adt_classrun.
  PRIVATE SECTION.
    DATA out TYPE REF TO if_oo_adt_classrun_out.
    METHODS jesc IMPORTING v TYPE string RETURNING VALUE(r) TYPE string.
    METHODS query IMPORTING sql TYPE string.
    METHODS ingest IMPORTING data TYPE string mode TYPE string.
ENDCLASS.

CLASS zcl_erpl_rev_ingest IMPLEMENTATION.

  METHOD jesc.
    r = v.
    REPLACE ALL OCCURRENCES OF `\` IN r WITH `\\`.
    REPLACE ALL OCCURRENCES OF `"` IN r WITH `\"`.
  ENDMETHOD.

  METHOD query.
    DATA: lv_rows TYPE string, lv_cnt TYPE string, lv_err TYPE string, lv_msg TYPE c LENGTH 255.
    CALL FUNCTION 'Z_DUCKDB_QUERY' DESTINATION 'ERPL_REV'
      EXPORTING iv_sql = sql
      IMPORTING ev_rows = lv_rows ev_row_count = lv_cnt ev_error = lv_err
      EXCEPTIONS system_failure = 1 MESSAGE lv_msg
                 communication_failure = 2 MESSAGE lv_msg OTHERS = 3.
    out->write( |Q subrc={ sy-subrc } cnt={ lv_cnt } rows={ lv_rows } err={ lv_err }{ lv_msg }| ).
  ENDMETHOD.

  METHOD ingest.
    DATA: lv_aff TYPE string, lv_err TYPE string, lv_msg TYPE c LENGTH 255.
    CALL FUNCTION 'Z_DUCKDB_INGEST' DESTINATION 'ERPL_REV'
      EXPORTING iv_target = 'zt000' iv_mode = mode iv_keys = 'mandt'
                iv_parquet_out = '/tmp/erpl_rev_sap_export.parquet'
                iv_data = data
      IMPORTING ev_rows_affected = lv_aff ev_error = lv_err
      EXCEPTIONS system_failure = 1 MESSAGE lv_msg
                 communication_failure = 2 MESSAGE lv_msg OTHERS = 3.
    out->write( |I subrc={ sy-subrc } affected={ lv_aff } err={ lv_err }{ lv_msg }| ).
  ENDMETHOD.

  METHOD if_oo_adt_classrun~main.
    me->out = out.

    " 1) Create the target table in the server's DuckDB.
    query( `DROP TABLE IF EXISTS zt000` ).
    query( `CREATE TABLE zt000(mandt VARCHAR PRIMARY KEY, mtext VARCHAR)` ).

    " 2) Real SAP data: a few client rows from T000 -> JSON array.
    SELECT mandt, mtext FROM t000 ORDER BY mandt INTO TABLE @DATA(lt) UP TO 5 ROWS.
    DATA lv_json TYPE string.
    lv_json = `[`.
    LOOP AT lt INTO DATA(ls).
      IF sy-tabix > 1. lv_json = lv_json && `,`. ENDIF.
      lv_json = lv_json &&
        `{"mandt":"` && jesc( CONV string( ls-mandt ) ) && `","mtext":"` &&
        jesc( CONV string( ls-mtext ) ) && `"}`.
    ENDLOOP.
    lv_json = lv_json && `]`.
    out->write( |SAP rows: { lines( lt ) }| ).

    " 3) Ingest (UPSERT) SAP rows -> DuckDB -> parquet.
    ingest( data = lv_json mode = 'UPSERT' ).

    " 4) Verify parquet round-trip and count.
    query( `SELECT count(*) AS c FROM read_parquet('/tmp/erpl_rev_sap_export.parquet')` ).

    " 5) Demonstrate UPSERT: update client 000's text, re-export, re-read.
    ingest( data = `[{"mandt":"000","mtext":"ERPL-REV-UPSERT"}]` mode = 'UPSERT' ).
    query( `SELECT mtext FROM read_parquet('/tmp/erpl_rev_sap_export.parquet') WHERE mandt='000'` ).
  ENDMETHOD.

ENDCLASS.
