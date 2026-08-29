CLASS zcl_erpl_rev_query DEFINITION PUBLIC FINAL CREATE PUBLIC.
  PUBLIC SECTION.
    INTERFACES if_oo_adt_classrun.
ENDCLASS.

CLASS zcl_erpl_rev_query IMPLEMENTATION.
  METHOD if_oo_adt_classrun~main.
    DATA: lv_sql  TYPE string,
          lv_cols TYPE string,
          lv_rows TYPE string,
          lv_cnt  TYPE string,
          lv_err  TYPE string,
          lv_msg  TYPE c LENGTH 255.

    lv_sql = `SELECT payment_type, count(*) AS c, ` &&
             `CAST(sum(fare_amount) AS DECIMAL(10,2)) AS s ` &&
             `FROM read_parquet('/home/jr/Projects/tmp/research/` &&
             `2026-05-30-erpl-rev/data/taxi.parquet') ` &&
             `GROUP BY 1 ORDER BY 1`.

    CALL FUNCTION 'Z_DUCKDB_QUERY' DESTINATION 'ERPL_REV'
      EXPORTING iv_sql       = lv_sql
      IMPORTING ev_columns   = lv_cols
                ev_rows      = lv_rows
                ev_row_count = lv_cnt
                ev_error     = lv_err
      EXCEPTIONS system_failure = 1 MESSAGE lv_msg
                 communication_failure = 2 MESSAGE lv_msg
                 OTHERS = 3.

    out->write( |CALL subrc={ sy-subrc } msg={ lv_msg }| ).
    out->write( |ROW_COUNT={ lv_cnt }| ).
    out->write( |COLUMNS={ lv_cols }| ).
    out->write( |ROWS={ lv_rows }| ).
    out->write( |ERROR={ lv_err }| ).
  ENDMETHOD.
ENDCLASS.
