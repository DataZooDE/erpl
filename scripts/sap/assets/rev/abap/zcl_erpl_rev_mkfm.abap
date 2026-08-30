CLASS zcl_erpl_rev_mkfm DEFINITION PUBLIC FINAL CREATE PUBLIC.
  PUBLIC SECTION.
    INTERFACES if_oo_adt_classrun.
  PRIVATE SECTION.
    " Create one RFC-enabled FM in function group ZERPL_REV with the given
    " IMPORTING / EXPORTING parameter names (all TYPE STRING, pass by value).
    METHODS make
      IMPORTING out  TYPE REF TO if_oo_adt_classrun_out
                name TYPE rs38l_fnam
                imps TYPE string_table
                exps TYPE string_table
                txt  TYPE string.
ENDCLASS.

CLASS zcl_erpl_rev_mkfm IMPLEMENTATION.

  METHOD make.
    " Types must match RS_FUNCTIONMODULE_INSERT's signature exactly, otherwise
    " CALL FUNCTION raises CX_SY_DYN_CALL_ILLEGAL_TYPE:
    "   FUNCNAME LIKE RS38L-NAME, FUNCTION_POOL LIKE RS38L-AREA,
    "   SHORT_TEXT LIKE TFTIT-STEXT.
    DATA: lt_imp TYPE TABLE OF rsimp,
          lt_exp TYPE TABLE OF rsexp,
          ls_imp TYPE rsimp,
          ls_exp TYPE rsexp,
          lv_name TYPE rs38l-name,
          lv_area TYPE rs38l-area,
          lv_text TYPE tftit-stext.

    " RFC modules require pass-BY-VALUE parameters. In RSIMP/RSEXP, REFERENCE='X'
    " means pass by reference (rejected for RFC: msg FL/381); leave it blank for
    " by value.
    " Each entry is NAME or NAME:TYPE (default STRING) — e.g. EV_XDATA:XSTRING.
    LOOP AT imps INTO DATA(lv_i).
      CLEAR ls_imp.
      SPLIT lv_i AT ':' INTO DATA(lv_pn) DATA(lv_pt).
      ls_imp-parameter = lv_pn.
      ls_imp-typ       = COND #( WHEN lv_pt IS INITIAL THEN 'STRING' ELSE lv_pt ).
      ls_imp-reference = ' '.        " by value (required for RFC)
      ls_imp-optional  = 'X'.
      APPEND ls_imp TO lt_imp.
    ENDLOOP.
    LOOP AT exps INTO DATA(lv_e).
      CLEAR ls_exp.
      SPLIT lv_e AT ':' INTO DATA(lv_en) DATA(lv_et).
      ls_exp-parameter = lv_en.
      ls_exp-typ       = COND #( WHEN lv_et IS INITIAL THEN 'STRING' ELSE lv_et ).
      ls_exp-reference = ' '.
      APPEND ls_exp TO lt_exp.
    ENDLOOP.

    lv_name = name.
    lv_area = 'ZERPL_REV'.
    lv_text = txt.
    CALL FUNCTION 'RS_FUNCTIONMODULE_INSERT'
      EXPORTING
        funcname                = lv_name
        function_pool           = lv_area
        interface_global        = 'X'
        remote_call             = 'R'
        short_text              = lv_text
      TABLES
        import_parameter        = lt_imp
        export_parameter        = lt_exp
      EXCEPTIONS
        double_task             = 1
        error_message           = 2
        function_already_exists = 3
        invalid_function_pool   = 4
        invalid_name            = 5
        too_many_functions      = 6
        no_modify_permission    = 7
        no_show_permission      = 8
        enqueue_system_failure  = 9
        canceled_in_corr        = 10
        OTHERS                  = 11.
    DATA lv_m TYPE string.
    MESSAGE ID sy-msgid TYPE 'I' NUMBER sy-msgno
      WITH sy-msgv1 sy-msgv2 sy-msgv3 sy-msgv4 INTO lv_m.
    out->write( |{ name } insert subrc={ sy-subrc } [{ sy-msgid }/{ sy-msgno }] { lv_m }| ).

    SELECT SINGLE funcname, fmode FROM tfdir
      WHERE funcname = @lv_name INTO @DATA(ls_dir).
    out->write( |{ name } tfdir subrc={ sy-subrc } fmode={ ls_dir-fmode }| ).
  ENDMETHOD.

  METHOD if_oo_adt_classrun~main.
    " Wrap everything so any short dump surfaces as a readable line instead of
    " an opaque HTTP 500 from the classrun runner.
    TRY.
        make( out  = out
              name = 'Z_DUCKDB_QUERY'
              imps = VALUE #( ( `IV_SQL` ) )
              exps = VALUE #( ( `EV_COLUMNS` ) ( `EV_ROWS` ) ( `EV_ROW_COUNT` ) ( `EV_ERROR` ) )
              txt  = 'erpl-rev: run DuckDB query' ).

        make( out  = out
              name = 'Z_DUCKDB_INGEST'
              imps = VALUE #( ( `IV_TARGET` ) ( `IV_MODE` ) ( `IV_KEYS` )
                              ( `IV_PARQUET_OUT` ) ( `IV_INIT_SQL` ) ( `IV_DDL` )
                              ( `IV_DATA` ) ( `IV_XDATA:XSTRING` ) ( `IV_OP_COL` ) )
              exps = VALUE #( ( `EV_ROWS_AFFECTED` ) ( `EV_ERROR` ) )
              txt  = 'erpl-rev: ingest rows into DuckDB' ).

        " Delta snapshot diff/merge (physical-delete reconciliation).
        make( out  = out
              name = 'Z_DUCKDB_SNAPSHOT_MERGE'
              imps = VALUE #( ( `IV_TARGET` ) ( `IV_STAGING` ) ( `IV_KEYS` ) )
              exps = VALUE #( ( `EV_INS` ) ( `EV_UPD` ) ( `EV_DEL` ) ( `EV_ERROR` ) )
              txt  = 'erpl-rev: snapshot diff merge' ).

        " Trigger-CDC FMs (opt-in physical-delete tier). CDC_PLAN returns the whole
        " platform plan as one JSON string (EV_PLAN); CDC_APPLY applies one staged
        " log batch and returns counts + the prune bound.
        make( out  = out
              name = 'Z_DUCKDB_CDC_PLAN'
              imps = VALUE #( ( `IV_TARGET` ) ( `IV_SOURCE` ) ( `IV_KEYS` )
                              ( `IV_MODE` ) ( `IV_PLATFORM` ) ( `IV_ACTION` ) )
              exps = VALUE #( ( `EV_PLAN` ) ( `EV_ERROR` ) )
              txt  = 'erpl-rev: CDC plan (DDL/read/prune)' ).

        make( out  = out
              name = 'Z_DUCKDB_CDC_APPLY'
              imps = VALUE #( ( `IV_TARGET` ) ( `IV_STAGING` ) ( `IV_KEYS` ) )
              exps = VALUE #( ( `EV_INS` ) ( `EV_UPD` ) ( `EV_DEL` )
                              ( `EV_PRUNE` ) ( `EV_APPLIED` ) ( `EV_ERROR` ) )
              txt  = 'erpl-rev: CDC apply staged log batch' ).

        " Streaming cursor FMs. EV_XDATA carries the binary sXML page (XSTRING).
        make( out  = out
              name = 'Z_DUCKDB_OPEN'
              imps = VALUE #( ( `IV_SQL` ) )
              exps = VALUE #( ( `EV_HANDLE` ) ( `EV_COLUMNS` ) ( `EV_ERROR` ) )
              txt  = 'erpl-rev: open streaming cursor' ).

        make( out  = out
              name = 'Z_DUCKDB_FETCH'
              imps = VALUE #( ( `IV_HANDLE` ) ( `IV_PAGE_ROWS` ) )
              exps = VALUE #( ( `EV_XDATA:XSTRING` ) ( `EV_FETCHED` )
                              ( `EV_DONE` ) ( `EV_ERROR` ) )
              txt  = 'erpl-rev: fetch one BXML page' ).

        make( out  = out
              name = 'Z_DUCKDB_CLOSE'
              imps = VALUE #( ( `IV_HANDLE` ) )
              exps = VALUE #( ( `EV_ERROR` ) )
              txt  = 'erpl-rev: close streaming cursor' ).
      CATCH cx_root INTO DATA(lx).
        out->write( |EXCEPTION: { lx->get_text( ) }| ).
    ENDTRY.
  ENDMETHOD.

ENDCLASS.
