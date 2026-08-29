CLASS zcl_erpl_rev_typemap DEFINITION PUBLIC FINAL CREATE PUBLIC.
  PUBLIC SECTION.
    "! Result of mapping a DuckDB type back to an ABAP DDIC type.
    TYPES: BEGIN OF ty_ddic,
             datatype TYPE c LENGTH 10,
             length   TYPE i,
             decimals TYPE i,
           END OF ty_ddic.

    "! SAP DDIC data type -> DuckDB SQL type string.
    "! Mirrors erpl's rfctype2logicaltype (rfc/src/sap_type_conversion.cpp).
    "! @parameter iv_datatype | DD03L/DFIES DATATYPE, e.g. CHAR, DEC, DATS, INT4
    CLASS-METHODS ddic_to_duckdb
      IMPORTING iv_datatype     TYPE csequence
                iv_length       TYPE i DEFAULT 0
                iv_decimals     TYPE i DEFAULT 0
      RETURNING VALUE(rv_type)  TYPE string.

    "! DuckDB SQL type string -> SAP DDIC data type (best-effort inverse).
    CLASS-METHODS duckdb_to_ddic
      IMPORTING iv_type        TYPE csequence
      RETURNING VALUE(rs_ddic) TYPE ty_ddic.
ENDCLASS.

CLASS zcl_erpl_rev_typemap IMPLEMENTATION.

  METHOD ddic_to_duckdb.
    DATA(lv_dt) = to_upper( iv_datatype ).
    CASE lv_dt.
      WHEN 'INT1' OR 'INT2' OR 'INT4'.
        rv_type = 'INTEGER'.
      WHEN 'INT8'.
        rv_type = 'BIGINT'.
      WHEN 'DEC' OR 'CURR' OR 'QUAN'.
        DATA(lv_p) = COND i( WHEN iv_length > 0 THEN iv_length ELSE 38 ).
        rv_type = |DECIMAL({ lv_p },{ iv_decimals })|.
      WHEN 'FLTP' OR 'DF16_DEC' OR 'DF34_DEC' OR 'D16R' OR 'D34R'
           OR 'DF16_RAW' OR 'DF34_RAW'.
        rv_type = 'DOUBLE'.
      WHEN 'DATS'.
        rv_type = 'DATE'.
      WHEN 'TIMS'.
        rv_type = 'TIME'.
      WHEN 'RAW' OR 'LRAW' OR 'RSTR'.
        rv_type = 'BLOB'.
      WHEN OTHERS.
        " CHAR, CLNT, LANG, CUKY, UNIT, NUMC, STRG, SSTR, ... -> text
        rv_type = 'VARCHAR'.
    ENDCASE.
  ENDMETHOD.

  METHOD duckdb_to_ddic.
    DATA(lv_t) = to_upper( iv_type ).
    " Strip a parameter list, e.g. DECIMAL(10,2) -> base DECIMAL + p/s.
    DATA lv_base TYPE string.
    DATA lv_args TYPE string.
    SPLIT lv_t AT '(' INTO lv_base lv_args.
    lv_base = condense( lv_base ).

    CASE lv_base.
      WHEN 'TINYINT'.   rs_ddic = VALUE #( datatype = 'INT1' length = 3 ).
      WHEN 'SMALLINT'.  rs_ddic = VALUE #( datatype = 'INT2' length = 5 ).
      WHEN 'INTEGER'.   rs_ddic = VALUE #( datatype = 'INT4' length = 10 ).
      WHEN 'BIGINT' OR 'HUGEINT'.
                        rs_ddic = VALUE #( datatype = 'INT8' length = 19 ).
      WHEN 'DOUBLE' OR 'FLOAT' OR 'REAL'.
                        rs_ddic = VALUE #( datatype = 'FLTP' length = 16 decimals = 16 ).
      WHEN 'DECIMAL' OR 'NUMERIC'.
        DATA: lv_p TYPE string, lv_s TYPE string.
        REPLACE ALL OCCURRENCES OF ')' IN lv_args WITH ''.
        SPLIT lv_args AT ',' INTO lv_p lv_s.
        rs_ddic = VALUE #( datatype = 'DEC'
                           length   = COND i( WHEN lv_p IS NOT INITIAL THEN CONV i( lv_p ) ELSE 38 )
                           decimals = COND i( WHEN lv_s IS NOT INITIAL THEN CONV i( lv_s ) ELSE 0 ) ).
      WHEN 'DATE'.      rs_ddic = VALUE #( datatype = 'DATS' length = 8 ).
      WHEN 'TIME'.      rs_ddic = VALUE #( datatype = 'TIMS' length = 6 ).
      WHEN 'BLOB' OR 'BYTEA'.
                        rs_ddic = VALUE #( datatype = 'RAW' length = 256 ).
      WHEN OTHERS.
        " VARCHAR, TEXT, BOOLEAN, TIMESTAMP, ... -> character
        rs_ddic = VALUE #( datatype = 'CHAR' length = 255 ).
    ENDCASE.
  ENDMETHOD.

ENDCLASS.
