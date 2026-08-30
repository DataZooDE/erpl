CLASS zcl_erpl_rev_util DEFINITION PUBLIC FINAL CREATE PUBLIC.
  PUBLIC SECTION.
    CONSTANTS c_dest TYPE rfcdest VALUE 'ERPL_REV'.

    "! aRFC end-of-task callback for the ingest pipeline (must be public for
    "! `CALLING me->on_end ON END OF TASK`). Not for external use.
    METHODS on_end IMPORTING p_task TYPE clike.

    TYPES: BEGIN OF ty_query,
             columns   TYPE string,   " JSON array of {name,type}
             rows      TYPE string,    " JSON array of row objects
             row_count TYPE i,
             error     TYPE string,
           END OF ty_query.

    TYPES: BEGIN OF ty_field,
             name        TYPE string,
             text        TYPE string,   " DDIC field label (for value help)
             datatype    TYPE c LENGTH 10,
             length      TYPE i,
             decimals    TYPE i,
             is_key      TYPE abap_bool,
             duckdb_type TYPE string,
           END OF ty_field,
           tt_field TYPE STANDARD TABLE OF ty_field WITH EMPTY KEY.

    TYPES: BEGIN OF ty_tab,
             tabname TYPE tabname,
             text    TYPE string,       " DDIC short description
           END OF ty_tab,
           tt_tab TYPE STANDARD TABLE OF ty_tab WITH EMPTY KEY.

    TYPES: BEGIN OF ty_desc,
             fields     TYPE tt_field,
             keys       TYPE string,    " comma-separated key columns
             col_list   TYPE string,    " comma-separated selected column names (for SELECT)
             added_keys TYPE string,    " key columns auto-added by projection (not in iv_columns)
             ddl        TYPE string,    " CREATE TABLE IF NOT EXISTS ... (with PRIMARY KEY)
             ddl_nopk   TYPE string,    " same CREATE but WITHOUT the PRIMARY KEY (full-load heap)
             error      TYPE string,    " set if a requested column does not exist
           END OF ty_desc.

    TYPES: BEGIN OF ty_repl,
             rows_affected TYPE i,
             seconds       TYPE p LENGTH 8 DECIMALS 3,  " wall-clock of the data move
             error         TYPE string,
           END OF ty_repl.

    "! Run a DuckDB SQL script (';'-separated) and return the last result.
    CLASS-METHODS query
      IMPORTING iv_sql        TYPE string
      RETURNING VALUE(rs)     TYPE ty_query.

    "! Search DDIC tables/views by name pattern ('*'/'%' wildcards, blank = a
    "! small default sample) for the table value-help. Returns tabname + text.
    CLASS-METHODS search_tables
      IMPORTING iv_pattern    TYPE csequence DEFAULT ''
                iv_max        TYPE i DEFAULT 200
      RETURNING VALUE(rt)     TYPE tt_tab.

    "! List ALL columns of a table (name + label + DDIC/DuckDB types + key flag)
    "! for the column value-help. Convenience wrapper over describe_table.
    CLASS-METHODS list_columns
      IMPORTING iv_tab        TYPE csequence
      RETURNING VALUE(rt)     TYPE tt_field.

    "! True if the name is a CDS entity (DDLS in TADIR), not a DDIC table/view —
    "! lets the UI reveal CDS-only fields only when the source is actually a CDS.
    CLASS-METHODS is_cds
      IMPORTING iv_name   TYPE csequence
      RETURNING VALUE(rv) TYPE abap_bool.

    "! Format a field list as fixed-width "name  ddic  duckdb_type" lines for a
    "! report list. Pure (no RFC/DDIC) and safe for short names — field names are
    "! STRINGs, so this must NOT use length-bound substring access (name(24)
    "! dumps STRING_LENGTH_TOO_LARGE on a name shorter than 24).
    CLASS-METHODS format_fields
      IMPORTING it_fields     TYPE tt_field
      RETURNING VALUE(rt)     TYPE string_table.

    "! Describe a SAP table: fields + DuckDB types + key list + CREATE TABLE DDL.
    "! iv_columns (space/comma separated, blank = all) selects a subset of fields
    "! SLT-style ("field selection"). Key fields are ALWAYS kept (added_keys lists
    "! those auto-included). An unknown column name sets rs-error.
    CLASS-METHODS describe_table
      IMPORTING iv_tab        TYPE csequence
                iv_target     TYPE csequence
                iv_columns    TYPE string DEFAULT ''
      RETURNING VALUE(rs)     TYPE ty_desc.

    "! Replicate a SAP table into DuckDB (typed). The source is streamed
    "! PACKAGE-WISE (OPEN CURSOR / FETCH PACKAGE SIZE iv_batch) so memory stays
    "! bounded for very large tables (>100M rows). iv_columns selects a subset of
    "! columns (keys always kept); iv_where is an OpenSQL condition applied AT THE
    "! SAP SOURCE (SLT-style filter, blank = all rows). iv_truncate = full-load
    "! replace (create + empty the target up front) so a re-run is idempotent; the
    "! target is then empty so each package is a fast INSERT. iv_truncate=false
    "! keeps existing rows and uses iv_mode (UPSERT) — for a future delta model.
    "! Bad column/WHERE -> rs-error.
    CLASS-METHODS replicate
      IMPORTING iv_tab        TYPE csequence
                iv_target     TYPE csequence
                iv_init       TYPE string DEFAULT ''
                iv_mode       TYPE string DEFAULT 'UPSERT'
                iv_maxrows    TYPE i DEFAULT 0
                iv_batch      TYPE i DEFAULT 50000
                iv_columns    TYPE string DEFAULT ''
                iv_where      TYPE string DEFAULT ''
                iv_truncate   TYPE abap_bool DEFAULT abap_true
                iv_create     TYPE abap_bool DEFAULT abap_true
                iv_build_pk   TYPE abap_bool DEFAULT abap_true
                iv_total      TYPE i DEFAULT 0
                iv_params     TYPE string DEFAULT ''
                iv_key_cols   TYPE string DEFAULT ''
                ii_progress   TYPE REF TO zif_erpl_rev_progress OPTIONAL
                iv_record     TYPE abap_bool DEFAULT abap_true
      RETURNING VALUE(rs)     TYPE ty_repl.

    "! Partitioned PARALLEL full-load. The coordinator (re)creates the heap target,
    "! splits the source into iv_jobs DISJOINT ranges of iv_part_col (a numeric/NUMC
    "! key column — zero-padded values compare correctly), SUBMITs one background
    "! job per range (Z_ERPL_REV_REPL_WORKER, which replicates with iv_create=false
    "! + iv_build_pk=false), waits for all jobs, then builds the PRIMARY KEY once.
    "! The workers ingest concurrently on the server (per-connection model) for N x
    "! throughput on read/serialize-bound loads — provided the SAP system has enough
    "! free batch work processes. rs-rows_affected/seconds cover the whole run.
    CLASS-METHODS replicate_parallel
      IMPORTING iv_tab      TYPE csequence
                iv_target   TYPE csequence
                iv_part_col TYPE csequence
                iv_jobs     TYPE i DEFAULT 4
                iv_columns  TYPE string DEFAULT ''
                iv_where    TYPE string DEFAULT ''
                iv_init     TYPE string DEFAULT ''
                iv_batch    TYPE i DEFAULT 50000
                iv_params   TYPE string DEFAULT ''
                ii_progress TYPE REF TO zif_erpl_rev_progress OPTIONAL
                iv_record   TYPE abap_bool DEFAULT abap_true
      RETURNING VALUE(rs)   TYPE ty_repl.

    "! Record ONE replication-run statistics row into the DuckDB control table
    "! _erpl_rev_run_stats (created at server boot) — full or incremental. This is
    "! the dashboard feed (view erpl_rev_run_stats); see docs/stats.md. run_id + ts
    "! default in the server, so callers pass only the run summary they already hold.
    CLASS-METHODS record_run
      IMPORTING iv_target   TYPE csequence
                iv_source   TYPE csequence DEFAULT ''
                iv_run_type TYPE csequence            " FULL | DELTA
                iv_method   TYPE csequence            " FULL | WATERMARK | SNAPSHOT | CHANGEDOC | INSERT_ONLY
                iv_status   TYPE csequence            " SUCCESS | ERROR
                iv_ms       TYPE i DEFAULT 0
                iv_read     TYPE i DEFAULT 0
                iv_ins      TYPE i DEFAULT 0
                iv_upd      TYPE i DEFAULT 0
                iv_del      TYPE i DEFAULT 0
                iv_wm_from  TYPE csequence DEFAULT ''
                iv_wm_to    TYPE csequence DEFAULT ''
                iv_jobs     TYPE i DEFAULT 0
                iv_error    TYPE csequence DEFAULT ''.

    "! Build the dynamic FROM source token: the entity name, or — for a CDS view
    "! WITH PARAMETERS — `NAME( p1 = 'v', … )`. iv_params is the raw parameter-list
    "! text the caller supplies (e.g. `p_carrid = 'LH'`); the SPACED form is required
    "! (the kernel parses an unspaced `NAME(p=…)` as one over-long table name).
    CLASS-METHODS source_from
      IMPORTING iv_tab    TYPE csequence
                iv_params TYPE string DEFAULT ''
      RETURNING VALUE(rv) TYPE string.

    "! Auto-pick the best partition column for replicate_parallel from a field
    "! list (describe_table-fields): a numeric column whose zero-padded values
    "! range-partition correctly — NUMC / INT1/2/4/8 / DEC(.,0), never CLNT.
    "! Prefers KEY columns (highest cardinality lives in the key), then the widest
    "! (most digits → largest value space, e.g. a document number over a year).
    "! Returns '' when no numeric column exists (caller falls back to serial).
    CLASS-METHODS pick_partition_col
      IMPORTING it_fields     TYPE tt_field
      RETURNING VALUE(rv_col) TYPE string.

    "! Recommend a parallel job count for a row count. Below iv_min rows parallel
    "! isn't worth the job-dispatch overhead -> 1 (serial). Otherwise aim for
    "! ~iv_target rows per job, clamped to [2, iv_cap].
    CLASS-METHODS recommend_jobs
      IMPORTING iv_rows       TYPE i
                iv_cap        TYPE i DEFAULT 8
                iv_min        TYPE i DEFAULT 100000
                iv_target     TYPE i DEFAULT 250000
      RETURNING VALUE(rv)     TYPE i.

    "! Configured batch work-process count (profile rdisp/wp_no_btc) — the natural
    "! ceiling for iv_jobs (more jobs than batch WPs just queue + serialize).
    "! Best-effort kernel read; returns iv_default if unavailable.
    CLASS-METHODS max_batch_jobs
      IMPORTING iv_default TYPE i DEFAULT 8
      RETURNING VALUE(rv)  TYPE i.

    "! Publish a local DuckDB table (the holding table a replicate() just filled)
    "! to an external target, expressed as ONE DuckDB SQL statement — the engine
    "! does the federation. This is the "stage-then-publish" sink: replication keeps
    "! all its speed/typing on a local table, then this materializes the result
    "! anywhere DuckDB can write. iv_kind:
    "!   PARQUET - COPY (SELECT * FROM src) TO '<dest>' (FORMAT parquet[, PARTITION_BY..]).
    "!             dest = a file (single) or a directory (partitioned dataset).
    "!   TABLE   - a table in an ATTACHed catalog (ducklake / postgres / bigquery /
    "!             iceberg / another duckdb), dest = catalog.schema.table. FULL =
    "!             DROP+CREATE TABLE AS SELECT (overwrite); APPEND = INSERT INTO SELECT.
    "! Credentials/ATTACH for the catalog come from the server boot init (--init-sql/
    "! --init-file / ERPL_REV_DUCKDB_INIT), NOT from here. Returns rs-error on failure.
    CLASS-METHODS publish
      IMPORTING iv_source       TYPE csequence
                iv_kind         TYPE csequence
                iv_dest         TYPE csequence
                iv_partition_by TYPE string DEFAULT ''
                iv_mode         TYPE string DEFAULT 'FULL'   " FULL | APPEND
      RETURNING VALUE(rs)       TYPE ty_query.

    "! Replicate a NATIVE (ADBC) source — a HANA/BW object not addressable by Open
    "! SQL, e.g. a BW calculation view `"_SYS_BIC"."pkg/CV"` (input parameters via the
    "! HANA PLACEHOLDER syntax inside iv_from). Reads via cl_sql_statement native SQL,
    "! derives the target schema from the result-set metadata, then reuses the SAME
    "! BXML ingest as table/CDS replication. Full-load, keyless (no streaming key /
    "! parallel). This is the BW provider seam; Open SQL sources keep using replicate.
    "! NB: native SQL bypasses the ABAP client filter (sees all clients).
    CLASS-METHODS replicate_native
      IMPORTING iv_from     TYPE string
                iv_target   TYPE csequence
                iv_init     TYPE string DEFAULT ''
                iv_where    TYPE string DEFAULT ''
                iv_maxrows  TYPE i DEFAULT 0
                iv_batch    TYPE i DEFAULT 50000
                ii_progress TYPE REF TO zif_erpl_rev_progress OPTIONAL
      RETURNING VALUE(rs)   TYPE ty_repl.

    "! Build a dynamic internal table from a query result (columns+rows JSON),
    "! for display in a dynamic ALV grid. Returns a data ref to a standard table.
    CLASS-METHODS result_to_alv
      IMPORTING is_result     TYPE ty_query
      RETURNING VALUE(rr_tab) TYPE REF TO data.

    TYPES: BEGIN OF ty_stream,
             data      TYPE REF TO data,   " filled standard table (typed)
             columns   TYPE string,        " columns JSON (for ALV headers)
             row_count TYPE i,             " rows actually fetched into `data`
             truncated TYPE abap_bool,     " stopped at iv_maxrows before done
             error     TYPE string,
           END OF ty_stream.

    "! Stream a DuckDB SQL result via the OPEN/FETCH/CLOSE cursor FMs, decoding
    "! each binary-sXML page straight into a typed dynamic table. Fixed memory:
    "! one page in flight. iv_maxrows>0 caps the rows kept (display limit) and
    "! sets `truncated`; 0 = all rows.
    CLASS-METHODS query_stream
      IMPORTING iv_sql        TYPE string
                iv_maxrows    TYPE i DEFAULT 0
                iv_page       TYPE i DEFAULT 8192
      RETURNING VALUE(rs)     TYPE ty_stream.

  PRIVATE SECTION.
    "! Build an empty typed standard table from a columns JSON ([{name,type}]).
    CLASS-METHODS build_table
      IMPORTING iv_columns    TYPE string
      RETURNING VALUE(rr_tab) TYPE REF TO data.

    "! Send one ingest batch as binary sXML (IV_XDATA). first carries init+ddl.
    CLASS-METHODS ingest_bxml
      IMPORTING iv_target    TYPE csequence
                iv_keys      TYPE string
                iv_mode      TYPE string
                iv_init      TYPE string
                iv_ddl       TYPE string
                iv_xdata     TYPE xstring
      EXPORTING ev_affected  TYPE i
                ev_error     TYPE string.

    "! Split a space/comma separated column list into an uppercased, de-duplicated
    "! table of field names (blank input -> empty table = "all columns").
    CLASS-METHODS split_cols
      IMPORTING iv TYPE string
      RETURNING VALUE(rt) TYPE string_table.

    "! Format one key value as an OpenSQL literal (numeric unquoted; everything
    "! else quoted, single quotes escaped — DATS/TIMS/NUMC compare as char).
    CLASS-METHODS key_literal
      IMPORTING iv_datatype   TYPE csequence
                iv_value      TYPE string
      RETURNING VALUE(rv)     TYPE string.

    "! Lexicographic ">" keyset predicate so the next page resumes strictly after
    "! the last row's key tuple: (k1>v1) OR (k1=v1 AND k2>v2) OR …  Empty if no
    "! pagination keys. Used to page huge reads WITHOUT holding a DB cursor across
    "! the ingest RFC (a synchronous RFC commits and invalidates open cursors).
    CLASS-METHODS keyset_after
      IMPORTING it_fields     TYPE tt_field
                it_keys       TYPE string_table
                is_row        TYPE any
      RETURNING VALUE(rv)     TYPE string.

    " --- Async ingest pipeline (double-buffer) -------------------------------
    " Overlaps package N's ingest RFC with reading+serializing package N+1, via
    " asynchronous RFC (STARTING NEW TASK ... CALLING me->on_end). replicate uses
    " a transient instance to hold the in-flight state. Falls back to synchronous
    " ingest if a destination can't do aRFC. Out-of-order completion is safe:
    " packages are disjoint key ranges and the server serializes the actual write.
    DATA: mv_pending TYPE i,
          mv_rows    TYPE i,           " rows confirmed ingested by completed tasks
          mv_err     TYPE string,      " first error from any task
          mv_seq     TYPE i,
          mv_depth   TYPE i VALUE 2.   " max ingests in flight
    "! Dispatch one package for ingestion (async if possible, else synchronous),
    "! throttling to mv_depth in-flight tasks.
    METHODS pipe_send
      IMPORTING iv_target TYPE csequence
                iv_keys   TYPE string
                iv_mode   TYPE string
                iv_init   TYPE string
                iv_ddl    TYPE string
                iv_xdata  TYPE xstring.
    "! Wait for all in-flight ingests to complete.
    METHODS pipe_drain.
ENDCLASS.

CLASS zcl_erpl_rev_util IMPLEMENTATION.

  METHOD split_cols.
    DATA(lv) = to_upper( iv ).
    REPLACE ALL OCCURRENCES OF ',' IN lv WITH ` `.
    SPLIT lv AT ` ` INTO TABLE DATA(lt_raw).
    LOOP AT lt_raw INTO DATA(lv_c).
      DATA(lv_t) = condense( lv_c ).
      CHECK lv_t IS NOT INITIAL.
      READ TABLE rt TRANSPORTING NO FIELDS WITH KEY table_line = lv_t.
      IF sy-subrc <> 0. APPEND lv_t TO rt. ENDIF.
    ENDLOOP.
  ENDMETHOD.

  METHOD query.
    DATA: lv_cnt TYPE string,
          lv_msg TYPE c LENGTH 255.
    CALL FUNCTION 'Z_DUCKDB_QUERY' DESTINATION c_dest
      EXPORTING  iv_sql       = iv_sql
      IMPORTING  ev_columns   = rs-columns
                 ev_rows      = rs-rows
                 ev_row_count = lv_cnt
                 ev_error     = rs-error
      EXCEPTIONS system_failure = 1 MESSAGE lv_msg
                 communication_failure = 2 MESSAGE lv_msg
                 OTHERS = 3.
    IF sy-subrc <> 0.
      rs-error = |RFC subrc={ sy-subrc } { lv_msg }|.
    ENDIF.
    IF lv_cnt CO '0123456789'.
      rs-row_count = CONV i( lv_cnt ).
    ENDIF.
  ENDMETHOD.

  METHOD search_tables.
    " Normalise the pattern: '*' -> '%', blank -> match-all, ensure a trailing
    " wildcard so a prefix like "SFL" finds SFLIGHT.
    DATA(lv_pat) = to_upper( condense( CONV string( iv_pattern ) ) ).
    REPLACE ALL OCCURRENCES OF '*' IN lv_pat WITH '%'.
    IF lv_pat IS INITIAL.
      lv_pat = '%'.
    ELSEIF lv_pat NS '%'.
      lv_pat = |{ lv_pat }%|.
    ENDIF.

    " Real, query-able tables/views only (transparent / pool / cluster / view).
    SELECT a~tabname, t~ddtext
      FROM dd02l AS a
      LEFT OUTER JOIN dd02t AS t
        ON t~tabname = a~tabname AND t~ddlanguage = @sy-langu
      WHERE a~tabname LIKE @lv_pat
        AND a~tabclass IN ( 'TRANSP', 'POOL', 'CLUSTER', 'VIEW' )
        AND a~as4local = 'A'
      ORDER BY a~tabname
      INTO TABLE @DATA(lt_hits)
      UP TO @iv_max ROWS.

    LOOP AT lt_hits INTO DATA(ls_h).
      APPEND VALUE ty_tab( tabname = ls_h-tabname
                           text    = ls_h-ddtext ) TO rt.
    ENDLOOP.

    " Also offer CDS view entities (they live in DDLS, not DD02L). The DDL source
    " name in TADIR is the SQL-addressable entity name for view entities. Tagged so
    " the picker distinguishes them; they read through the same replication path.
    DATA(lv_left) = iv_max - lines( rt ).
    IF lv_left > 0.
      SELECT obj_name FROM tadir
        WHERE pgmid = 'R3TR' AND object = 'DDLS' AND obj_name LIKE @lv_pat
        ORDER BY obj_name
        INTO TABLE @DATA(lt_cds) UP TO @lv_left ROWS.
      LOOP AT lt_cds INTO DATA(ls_c).
        APPEND VALUE ty_tab( tabname = ls_c-obj_name text = '(CDS view)' ) TO rt.
      ENDLOOP.
    ENDIF.
  ENDMETHOD.

  METHOD list_columns.
    rt = describe_table( iv_tab = iv_tab iv_target = 'x' )-fields.
  ENDMETHOD.

  METHOD is_cds.
    DATA(lv_name) = to_upper( condense( CONV string( iv_name ) ) ).
    IF lv_name IS INITIAL. RETURN. ENDIF.
    SELECT SINGLE @abap_true FROM tadir INTO @rv
      WHERE pgmid = 'R3TR' AND object = 'DDLS' AND obj_name = @lv_name.
  ENDMETHOD.

  METHOD format_fields.
    DATA: lv_nm TYPE c LENGTH 24,
          lv_dt TYPE c LENGTH 8,
          lv_dk TYPE c LENGTH 40.
    LOOP AT it_fields INTO DATA(ls_f).
      lv_nm = ls_f-name.          " STRING -> c-field: truncates/pads, never dumps
      lv_dt = ls_f-datatype.
      lv_dk = ls_f-duckdb_type.
      APPEND |{ lv_nm } { lv_dt } { lv_dk }| TO rt.
    ENDLOOP.
  ENDMETHOD.

  METHOD describe_table.
    DATA lt_dfies TYPE STANDARD TABLE OF dfies.
    CALL FUNCTION 'DDIF_FIELDINFO_GET'
      EXPORTING  tabname   = CONV ddobjname( iv_tab )
                 langu     = sy-langu
      TABLES     dfies_tab = lt_dfies
      EXCEPTIONS not_found = 1 internal_error = 2 OTHERS = 3.
    IF sy-subrc <> 0.
      RETURN.
    ENDIF.

    " Field selection (SLT-style): blank = all columns. Requested names are
    " uppercased; every one must exist (else clean error, no later dump). Key
    " fields are ALWAYS kept even when not requested, so UPSERT/dedup stays
    " correct — added_keys lists the keys we pulled in beyond the selection.
    DATA(lt_want) = split_cols( iv_columns ).
    DATA lt_seen TYPE string_table.

    DATA lt_keys     TYPE string_table.
    DATA lt_cols     TYPE string_table.   " "NAME TYPE" pairs for the DDL
    DATA lt_addkeys  TYPE string_table.
    " DDIF_FIELDINFO_GET resolves CDS VIEW ENTITIES too (with correct datatypes +
    " keyflags), but prefixes a synthetic node pseudo-field (datatype 'NODE', name
    " '.NODE1'); skip those + any dot-prefixed structural entry so a CDS view maps
    " to exactly its scalar columns. (Plain tables have no such entries.)
    LOOP AT lt_dfies INTO DATA(ls_f)
        WHERE datatype IS NOT INITIAL AND datatype <> 'NODE' AND fieldname NP '.*'.
      DATA(lv_name) = to_upper( CONV string( ls_f-fieldname ) ).
      DATA(lv_selected) = xsdbool( lt_want IS INITIAL ).
      IF lt_want IS NOT INITIAL.
        READ TABLE lt_want TRANSPORTING NO FIELDS WITH KEY table_line = lv_name.
        IF sy-subrc = 0.
          lv_selected = abap_true.
          APPEND lv_name TO lt_seen.
        ELSEIF ls_f-keyflag = abap_true.
          " key not requested but kept anyway
          lv_selected = abap_true.
          APPEND lv_name TO lt_addkeys.
        ENDIF.
      ENDIF.
      CHECK lv_selected = abap_true.

      DATA(ls_field) = VALUE ty_field(
        name        = ls_f-fieldname
        text        = COND string( WHEN ls_f-scrtext_m IS NOT INITIAL THEN ls_f-scrtext_m
                                   WHEN ls_f-fieldtext IS NOT INITIAL THEN ls_f-fieldtext
                                   ELSE ls_f-reptext )
        datatype    = ls_f-datatype
        length      = CONV i( ls_f-leng )
        decimals    = CONV i( ls_f-decimals )
        is_key      = ls_f-keyflag
        duckdb_type = zcl_erpl_rev_typemap=>ddic_to_duckdb(
                        iv_datatype = ls_f-datatype
                        iv_length   = CONV i( ls_f-leng )
                        iv_decimals = CONV i( ls_f-decimals ) ) ).
      APPEND ls_field TO rs-fields.
      APPEND |{ ls_field-name } { ls_field-duckdb_type }| TO lt_cols.
      IF ls_field-is_key = abap_true.
        APPEND CONV string( ls_field-name ) TO lt_keys.
      ENDIF.
    ENDLOOP.

    " Reject any requested column that does not exist in the table.
    LOOP AT lt_want INTO DATA(lv_req).
      READ TABLE lt_seen TRANSPORTING NO FIELDS WITH KEY table_line = lv_req.
      IF sy-subrc <> 0.
        rs-error = |unknown column "{ lv_req }" in table { iv_tab }|.
        RETURN.
      ENDIF.
    ENDLOOP.

    rs-keys       = concat_lines_of( table = lt_keys sep = `,` ).
    rs-added_keys = concat_lines_of( table = lt_addkeys sep = `,` ).
    rs-col_list   = concat_lines_of( table = VALUE string_table(
                      FOR f IN rs-fields ( f-name ) ) sep = `, ` ).
    DATA(lv_collist) = concat_lines_of( table = lt_cols sep = `, ` ).
    DATA(lv_pk) = COND string( WHEN rs-keys IS NOT INITIAL
                               THEN |, PRIMARY KEY ({ rs-keys })| ELSE `` ).
    rs-ddl      = |CREATE TABLE IF NOT EXISTS { iv_target } ({ lv_collist }{ lv_pk });|.
    rs-ddl_nopk = |CREATE TABLE IF NOT EXISTS { iv_target } ({ lv_collist });|.
  ENDMETHOD.

  METHOD key_literal.
    CASE iv_datatype.
      WHEN 'INT1' OR 'INT2' OR 'INT4' OR 'INT8'
           OR 'DEC' OR 'CURR' OR 'QUAN' OR 'FLTP'.
        rv = condense( iv_value ).                         " numeric, unquoted
      WHEN OTHERS.                                         " char / NUMC / DATS / TIMS
        DATA(v) = iv_value.
        REPLACE ALL OCCURRENCES OF `'` IN v WITH `''`.     " escape single quotes
        rv = |'{ v }'|.
    ENDCASE.
  ENDMETHOD.

  METHOD keyset_after.
    " (k1 > v1) OR (k1 = v1 AND k2 > v2) OR (k1=v1 AND k2=v2 AND k3 > v3) ...
    DATA lt_terms TYPE string_table.
    DATA lv_i TYPE i.
    LOOP AT it_keys INTO DATA(lv_k).
      lv_i = sy-tabix.
      DATA lt_eq TYPE string_table.
      CLEAR lt_eq.
      DATA lv_j TYPE i.
      LOOP AT it_keys INTO DATA(lv_k2) TO lv_i.
        lv_j = sy-tabix.
        ASSIGN COMPONENT lv_k2 OF STRUCTURE is_row TO FIELD-SYMBOL(<v>).
        IF sy-subrc <> 0. CONTINUE. ENDIF.
        READ TABLE it_fields INTO DATA(ls_kf) WITH KEY name = lv_k2.
        DATA(lv_lit) = key_literal( iv_datatype = ls_kf-datatype
                                    iv_value    = CONV string( <v> ) ).
        DATA(lv_op) = COND string( WHEN lv_j < lv_i THEN `=` ELSE `>` ).
        APPEND |{ lv_k2 } { lv_op } { lv_lit }| TO lt_eq.
      ENDLOOP.
      APPEND |( { concat_lines_of( table = lt_eq sep = ` AND ` ) } )| TO lt_terms.
    ENDLOOP.
    rv = concat_lines_of( table = lt_terms sep = ` OR ` ).
  ENDMETHOD.

  METHOD on_end.
    DATA lv_aff TYPE string.
    DATA lv_err TYPE string.
    DATA lv_msg TYPE c LENGTH 255.
    RECEIVE RESULTS FROM FUNCTION 'Z_DUCKDB_INGEST'
      IMPORTING ev_rows_affected = lv_aff
                ev_error         = lv_err
      EXCEPTIONS system_failure = 1 MESSAGE lv_msg
                 communication_failure = 2 MESSAGE lv_msg
                 OTHERS = 3.
    IF sy-subrc <> 0.
      IF mv_err IS INITIAL. mv_err = |ingest RFC subrc={ sy-subrc } { lv_msg }|. ENDIF.
    ELSEIF lv_err IS NOT INITIAL.
      IF mv_err IS INITIAL. mv_err = lv_err. ENDIF.
    ELSE.
      mv_rows = mv_rows + CONV i( lv_aff ).
    ENDIF.
    mv_pending = mv_pending - 1.
  ENDMETHOD.

  METHOD pipe_send.
    " Throttle so at most mv_depth ingests are in flight (WAIT runs the aRFC
    " end-of-task callbacks, which decrement mv_pending).
    IF mv_pending >= mv_depth.
      WAIT UNTIL mv_pending < mv_depth UP TO 3600 SECONDS.
    ENDIF.
    mv_seq = mv_seq + 1.
    DATA(lv_task) = |EREV{ mv_seq }|.
    DATA lv_msg TYPE c LENGTH 255.
    CALL FUNCTION 'Z_DUCKDB_INGEST'
      STARTING NEW TASK lv_task
      DESTINATION c_dest
      CALLING me->on_end ON END OF TASK
      EXPORTING iv_target   = CONV string( iv_target )
                iv_mode     = iv_mode
                iv_keys     = iv_keys
                iv_init_sql = iv_init
                iv_ddl      = iv_ddl
                iv_xdata    = iv_xdata
      EXCEPTIONS communication_failure = 1 MESSAGE lv_msg
                 system_failure        = 2 MESSAGE lv_msg
                 resource_failure      = 3
                 OTHERS                = 4.
    IF sy-subrc = 0.
      mv_pending = mv_pending + 1.
    ELSE.
      " aRFC unavailable (no parallel resources / destination can't) -> run this
      " package synchronously so the load still completes (no overlap for it).
      DATA lv_aff TYPE i.
      DATA lv_serr TYPE string.
      ingest_bxml( EXPORTING iv_target = iv_target iv_keys = iv_keys iv_mode = iv_mode
                             iv_init = iv_init iv_ddl = iv_ddl iv_xdata = iv_xdata
                   IMPORTING ev_affected = lv_aff ev_error = lv_serr ).
      IF lv_serr IS NOT INITIAL.
        IF mv_err IS INITIAL. mv_err = lv_serr. ENDIF.
      ELSE.
        mv_rows = mv_rows + lv_aff.
      ENDIF.
    ENDIF.
  ENDMETHOD.

  METHOD pipe_drain.
    IF mv_pending > 0.
      WAIT UNTIL mv_pending <= 0 UP TO 3600 SECONDS.
    ENDIF.
  ENDMETHOD.

  METHOD record_run.
    " Compose one INSERT into the server-owned stats table (run_id + ts default in
    " the server). Controlled enums (run_type/method/status) are not escaped; the
    " free-text identifiers/error are single-quote-escaped.
    DATA(lv_tgt) = replace( val = CONV string( iv_target )  sub = `'` with = `''` occ = 0 ).
    DATA(lv_src) = replace( val = CONV string( iv_source )  sub = `'` with = `''` occ = 0 ).
    DATA(lv_wf)  = replace( val = CONV string( iv_wm_from ) sub = `'` with = `''` occ = 0 ).
    DATA(lv_wt)  = replace( val = CONV string( iv_wm_to )   sub = `'` with = `''` occ = 0 ).
    DATA(lv_err) = COND string( WHEN iv_error IS INITIAL THEN `NULL`
      ELSE |'{ replace( val = CONV string( iv_error ) sub = `'` with = `''` occ = 0 ) }'| ).
    query(
      |INSERT INTO _erpl_rev_run_stats | &&
      |(target,source,run_type,method,status,duration_ms,| &&
      |rows_read,rows_ins,rows_upd,rows_del,wm_from,wm_to,jobs,error_text) | &&
      |VALUES ('{ lv_tgt }','{ lv_src }','{ iv_run_type }','{ iv_method }','{ iv_status }',| &&
      |{ iv_ms },{ iv_read },{ iv_ins },{ iv_upd },{ iv_del },| &&
      |'{ lv_wf }','{ lv_wt }',{ iv_jobs },{ lv_err })| ).
  ENDMETHOD.

  METHOD replicate.
    DATA(ls_desc) = describe_table( iv_tab = iv_tab iv_target = iv_target
                                    iv_columns = iv_columns ).
    IF ls_desc-error IS NOT INITIAL.
      rs-error = ls_desc-error.
      RETURN.
    ENDIF.
    IF ls_desc-fields IS INITIAL.
      rs-error = |table { iv_tab } not found or has no fields|.
      RETURN.
    ENDIF.

    " Dynamic internal table holding ONLY the selected columns (keys always kept,
    " in DDIC order), so projection flows straight through to BXML and the DDL.
    DATA(lo_full) = CAST cl_abap_structdescr(
      cl_abap_typedescr=>describe_by_name( CONV string( iv_tab ) ) ).
    DATA(lt_allcomp) = lo_full->get_components( ).
    DATA lt_comp TYPE cl_abap_structdescr=>component_table.
    LOOP AT ls_desc-fields INTO DATA(ls_fld).
      READ TABLE lt_allcomp INTO DATA(ls_comp)
        WITH KEY name = to_upper( ls_fld-name ).
      IF sy-subrc = 0.
        APPEND ls_comp TO lt_comp.
      ENDIF.
    ENDLOOP.
    DATA(lo_struct) = cl_abap_structdescr=>create( lt_comp ).
    DATA(lo_tab) = cl_abap_tabledescr=>create( lo_struct ).

    " A fixed RAW is TRANSPORTED as an XSTRING, not as the source's x LENGTH n.
    "
    " asXML serialises a fixed X field with its trailing zero bytes REMOVED: a
    " RAW(16) holding DEADBEEF and twelve nulls comes out as four bytes, and an
    " all-zero RAW(16) comes out as an empty element. The same value in an
    " XSTRING is serialised whole, and assigning one to the other loses nothing.
    " (Both checked on the live system before being relied on.)
    "
    " The SELECT still reads into the SOURCE types, because
    " `INTO CORRESPONDING FIELDS` will not write a RAW column into an XSTRING --
    " it short-dumps. So the widening happens row by row after the read, and
    " only for tables that actually have a fixed RAW, so nothing else pays for it.
    DATA lt_tcomp TYPE cl_abap_structdescr=>component_table.
    DATA lv_raw TYPE abap_bool VALUE abap_false.
    LOOP AT lt_comp INTO DATA(ls_tc).
      IF ls_tc-type->type_kind = cl_abap_typedescr=>typekind_hex.
        ls_tc-type = cl_abap_elemdescr=>get_xstring( ).
        lv_raw = abap_true.
      ENDIF.
      APPEND ls_tc TO lt_tcomp.
    ENDLOOP.
    DATA(lo_tstruct) = COND #( WHEN lv_raw = abap_true
                               THEN cl_abap_structdescr=>create( lt_tcomp )
                               ELSE lo_struct ).
    DATA(lo_ttab) = cl_abap_tabledescr=>create( lo_tstruct ).

    " Full-load replace: (re)create the target up front as a HEAP — no PRIMARY KEY
    " — so a re-run is idempotent and, crucially, the packages INSERT without
    " paying per-package ART-index maintenance. The PRIMARY KEY is built ONCE at
    " the end (ALTER TABLE … ADD PRIMARY KEY) over the full, unique-on-key data.
    " DROP+CREATE (not CREATE IF NOT EXISTS + TRUNCATE) guarantees no leftover PK
    " from a prior run. Uses the existing multi-statement Z_DUCKDB_QUERY — no new FM.
    " iv_create=false: a coordinator already (re)created the heap target — used by
    " partitioned parallel replication, where N workers append disjoint key ranges
    " into one target and the coordinator builds the PK once after they all finish.
    IF iv_truncate = abap_true AND iv_create = abap_true.
      DATA(ls_q) = query( |DROP TABLE IF EXISTS { iv_target }; { ls_desc-ddl_nopk }| ).
      IF ls_q-error IS NOT INITIAL. rs-error = ls_q-error. RETURN. ENDIF.
    ENDIF.

    " A truncated target is empty, so INSERT is the fastest ingest (no conflict
    " resolution). Without truncate (future delta), honor iv_mode (UPSERT).
    DATA(lv_mode) = COND string( WHEN iv_truncate = abap_true THEN `INSERT` ELSE iv_mode ).

    " Dynamic projected column list: strict Open SQL wants ONE comma-separated row.
    DATA lt_select TYPE string_table.
    APPEND ls_desc-col_list TO lt_select.

    " Pagination keys = the table's key fields in order, EXCLUDING the client
    " (CLNT) field (auto-handled by Open SQL, constant within the client). With a
    " usable key we page by KEYSET — a synchronous ingest RFC commits and would
    " invalidate any held cursor, so each package is a fresh, resumable SELECT
    " `WHERE key > lastkey ORDER BY key`. Keyless tables fall back to a single
    " bounded read (no cursor held across the RFC either).
    " The dynamic FROM source: the entity name, or a CDS WITH PARAMETERS source
    " `NAME( p = 'v' )` when iv_params is given (works for tables/views/CDS alike).
    DATA(lv_from) = source_from( iv_tab = iv_tab iv_params = iv_params ).

    DATA lt_keyf  TYPE string_table.
    DATA lt_order TYPE string_table.
    DATA lv_order TYPE string.
    " iv_key_cols overrides the detected keys (comma list) — lets a keyless DB view
    " or CDS opt into keyset streaming + parallel by naming ordering column(s) that
    " uniquely + monotonically order the result. Else use the DDIC/CDS key fields
    " (auto-detected; CLNT excluded — constant within the client).
    IF iv_key_cols IS NOT INITIAL.
      LOOP AT split_cols( iv_key_cols ) INTO DATA(lv_kc).
        APPEND lv_kc TO lt_keyf.
        lv_order = COND #( WHEN lv_order IS INITIAL THEN |{ lv_kc } ASCENDING|
                           ELSE |{ lv_order }, { lv_kc } ASCENDING| ).
      ENDLOOP.
    ELSE.
      LOOP AT ls_desc-fields INTO DATA(ls_kf) WHERE is_key = abap_true AND datatype <> 'CLNT'.
        APPEND ls_kf-name TO lt_keyf.
        lv_order = COND #( WHEN lv_order IS INITIAL THEN |{ ls_kf-name } ASCENDING|
                           ELSE |{ lv_order }, { ls_kf-name } ASCENDING| ).
      ENDLOOP.
    ENDIF.
    " strict Open SQL ORDER BY is comma-separated -> ONE row (multi-row would
    " concatenate with blanks and the parser rejects it).
    IF lv_order IS NOT INITIAL. APPEND lv_order TO lt_order. ENDIF.

    DATA lr_pkg TYPE REF TO data.
    CREATE DATA lr_pkg TYPE HANDLE lo_tab.
    FIELD-SYMBOLS <pkg> TYPE STANDARD TABLE.

    " The transport image of a package, allocated only when a fixed RAW is present.
    DATA lr_tpk TYPE REF TO data.
    DATA lr_trow TYPE REF TO data.
    FIELD-SYMBOLS <tpk> TYPE STANDARD TABLE.
    FIELD-SYMBOLS <trow> TYPE any.
    ASSIGN lr_pkg->* TO <pkg>.

    DATA: lv_first  TYPE abap_bool VALUE abap_true,
          lv_total  TYPE i,
          lv_take   TYPE i,
          lv_aff    TYPE i,
          lv_err    TYPE string,
          lv_keyset TYPE string,       " "> lastkey" predicate, grows each page
          lv_off    TYPE i VALUE 0.    " keyless: slice offset into the full read
    DATA(lv_pkg) = COND i( WHEN iv_batch > 0 THEN iv_batch ELSE 50000 ).

    " Keyless tables can't keyset-page; read the whole (capped) set ONCE into
    " memory, then send it in slices (no cursor held across the ingest RFC). Keyed
    " tables stream with bounded memory via the keyset loop below.
    DATA lr_all TYPE REF TO data.
    FIELD-SYMBOLS <all> TYPE STANDARD TABLE.
    IF lt_keyf IS INITIAL.
      CREATE DATA lr_all TYPE HANDLE lo_tab.
      ASSIGN lr_all->* TO <all>.
      DATA lt_u TYPE string_table.
      IF iv_where IS NOT INITIAL. APPEND iv_where TO lt_u. ENDIF.
      TRY.
          IF iv_maxrows > 0.
            SELECT (lt_select) FROM (lv_from) WHERE (lt_u)
              INTO CORRESPONDING FIELDS OF TABLE @<all> UP TO @iv_maxrows ROWS.
          ELSE.
            SELECT (lt_select) FROM (lv_from) WHERE (lt_u)
              INTO CORRESPONDING FIELDS OF TABLE @<all>.
          ENDIF.
        CATCH cx_sy_dynamic_osql_syntax cx_sy_dynamic_osql_semantics
              cx_sy_open_sql_db cx_root INTO DATA(lx_all).
          rs-error = |SELECT failed: { lx_all->get_text( ) }|.
          RETURN.
      ENDTRY.
    ENDIF.

    " Async ingest pipeline: package 1 is sent synchronously (it creates the
    " table when iv_truncate=false), packages 2..N overlap with the next read.
    DATA(lo_pipe) = NEW zcl_erpl_rev_util( ).

    GET TIME STAMP FIELD DATA(lv_t0).      " wall-clock start of the data move
    TRY.
        DO.
          " Honor an optional total-row cap without overshooting the last package.
          lv_take = lv_pkg.
          IF iv_maxrows > 0.
            IF lv_total >= iv_maxrows. EXIT. ENDIF.
            IF iv_maxrows - lv_total < lv_take. lv_take = iv_maxrows - lv_total. ENDIF.
          ENDIF.

          CLEAR <pkg>.
          IF lt_keyf IS NOT INITIAL.
            " Effective WHERE = user filter AND keyset-resume predicate.
            DATA lt_w TYPE string_table.
            CLEAR lt_w.
            IF iv_where IS NOT INITIAL AND lv_keyset IS NOT INITIAL.
              APPEND |( { iv_where } ) AND ( { lv_keyset } )| TO lt_w.
            ELSEIF iv_where IS NOT INITIAL.
              APPEND iv_where TO lt_w.
            ELSEIF lv_keyset IS NOT INITIAL.
              APPEND lv_keyset TO lt_w.
            ENDIF.
            SELECT (lt_select) FROM (lv_from) WHERE (lt_w) ORDER BY (lt_order)
              INTO CORRESPONDING FIELDS OF TABLE @<pkg> UP TO @lv_take ROWS.
          ELSE.
            " keyless: next slice of the in-memory full read.
            FIELD-SYMBOLS <r> TYPE any.
            LOOP AT <all> ASSIGNING <r> FROM lv_off + 1 TO lv_off + lv_take.
              INSERT <r> INTO TABLE <pkg>.
            ENDLOOP.
            lv_off = lv_off + lines( <pkg> ).
          ENDIF.
          IF <pkg> IS INITIAL. EXIT. ENDIF.
          lv_total = lv_total + lines( <pkg> ).

          DATA(lo_w) = cl_sxml_string_writer=>create( type = if_sxml=>co_xt_binary ).
          IF lv_raw = abap_true.
            " Widen every fixed RAW to an XSTRING before serialising, so its
            " trailing zero bytes survive. See the note beside lt_tcomp.
            IF lr_tpk IS INITIAL.
              CREATE DATA lr_tpk TYPE HANDLE lo_ttab.
              ASSIGN lr_tpk->* TO <tpk>.
              CREATE DATA lr_trow TYPE HANDLE lo_tstruct.
              ASSIGN lr_trow->* TO <trow>.
            ENDIF.
            CLEAR <tpk>.
            FIELD-SYMBOLS <prow> TYPE any.
            LOOP AT <pkg> ASSIGNING <prow>.
              CLEAR <trow>.
              MOVE-CORRESPONDING <prow> TO <trow>.
              INSERT <trow> INTO TABLE <tpk>.
            ENDLOOP.
            CALL TRANSFORMATION id SOURCE data = <tpk> RESULT XML lo_w.
          ELSE.
            CALL TRANSFORMATION id SOURCE data = <pkg> RESULT XML lo_w.
          ENDIF.
          DATA(lv_xdata) = lo_w->get_output( ).
          IF lv_first = abap_true.
            " First package carries init (+ddl for delta only — full-load already
            " created the heap up front) and is sent synchronously so the target
            " exists before any overlapped (out-of-order) package lands.
            DATA(lv_first_ddl) = COND string(
              WHEN iv_truncate = abap_true THEN `` ELSE ls_desc-ddl ).
            ingest_bxml( EXPORTING iv_target = iv_target iv_keys = ls_desc-keys
                                   iv_mode = lv_mode iv_init = iv_init iv_ddl = lv_first_ddl
                                   iv_xdata = lv_xdata
                         IMPORTING ev_affected = lv_aff ev_error = lv_err ).
            IF lv_err IS NOT INITIAL. rs-error = lv_err. RETURN. ENDIF.
            rs-rows_affected = rs-rows_affected + lv_aff.
            lv_first = abap_false.
          ELSE.
            " Overlap this ingest with the next package's read+serialize.
            lo_pipe->pipe_send( iv_target = iv_target iv_keys = ls_desc-keys iv_mode = lv_mode
                                iv_init = `` iv_ddl = `` iv_xdata = lv_xdata ).
            IF lo_pipe->mv_err IS NOT INITIAL.   " an async task already failed
              lo_pipe->pipe_drain( ).
              rs-error = lo_pipe->mv_err. RETURN.
            ENDIF.
          ENDIF.

          " Live progress (status bar in dialog, job log in background) on rows
          " dispatched. Best effort — a display error must never abort the load.
          IF ii_progress IS BOUND.
            TRY.
                GET TIME STAMP FIELD DATA(lv_tn).
                DATA lv_el TYPE p LENGTH 8 DECIMALS 0.
                lv_el = cl_abap_tstmp=>subtract( tstmp1 = lv_tn tstmp2 = lv_t0 ).
                ii_progress->tick( iv_rows = lv_total iv_total = iv_total iv_seconds = lv_el ).
              CATCH cx_root ##NO_HANDLER.
            ENDTRY.
          ENDIF.

          " Done when the page was short. Keyed: advance keyset past the last key.
          " Keyless: stop once the whole in-memory set has been sent.
          IF lt_keyf IS INITIAL.
            IF lv_off >= lines( <all> ). EXIT. ENDIF.
          ELSE.
            IF lines( <pkg> ) < lv_take. EXIT. ENDIF.
            ASSIGN <pkg>[ lines( <pkg> ) ] TO FIELD-SYMBOL(<last>).
            lv_keyset = keyset_after( it_fields = ls_desc-fields
                                      it_keys   = lt_keyf
                                      is_row    = <last> ).
          ENDIF.
        ENDDO.
      CATCH cx_sy_dynamic_osql_syntax cx_sy_dynamic_osql_semantics
            cx_sy_open_sql_db cx_root INTO DATA(lx_sel).
        rs-error = |SELECT failed: { lx_sel->get_text( ) }|.
        lo_pipe->pipe_drain( ).
        RETURN.
    ENDTRY.

    " Wait for the overlapped ingests to finish and fold in their results.
    lo_pipe->pipe_drain( ).
    rs-rows_affected = rs-rows_affected + lo_pipe->mv_rows.
    IF rs-error IS INITIAL AND lo_pipe->mv_err IS NOT INITIAL.
      rs-error = lo_pipe->mv_err.
    ENDIF.

    " Full-load: build the PRIMARY KEY ONCE over the complete, unique-on-key data
    " (deferred from the per-package inserts — DuckDB builds the ART index far
    " faster in one pass than incrementally). Skip for keyless tables or on error.
    IF iv_truncate = abap_true AND iv_build_pk = abap_true
       AND ls_desc-keys IS NOT INITIAL AND rs-error IS INITIAL.
      DATA(ls_pk) = query( |ALTER TABLE { iv_target } ADD PRIMARY KEY ({ ls_desc-keys });| ).
      IF ls_pk-error IS NOT INITIAL. rs-error = |add primary key: { ls_pk-error }|. ENDIF.
    ENDIF.

    GET TIME STAMP FIELD DATA(lv_t1).
    rs-seconds = cl_abap_tstmp=>subtract( tstmp1 = lv_t1 tstmp2 = lv_t0 ).

    " Dashboard stats: one FULL run row (suppressed by iv_record=false for delta
    " sub-step reloads — watermark/changedoc/insert_only merges and the snapshot
    " staging reload — and for parallel workers; those are recorded by their owner).
    IF iv_record = abap_true.
      record_run( iv_target = iv_target iv_source = iv_tab
        iv_run_type = 'FULL' iv_method = 'FULL'
        iv_status = COND #( WHEN rs-error IS INITIAL THEN 'SUCCESS' ELSE 'ERROR' )
        iv_ms = CONV i( rs-seconds * 1000 )
        iv_read = rs-rows_affected iv_ins = rs-rows_affected
        iv_error = rs-error ).
    ENDIF.

    " Empty source with NO up-front truncate: still create the target (run DDL via
    " an empty ingest) so a 0-row delta load leaves an empty typed table.
    IF lv_first = abap_true AND iv_truncate = abap_false.
      ingest_bxml( EXPORTING iv_target = iv_target iv_keys = ls_desc-keys
                             iv_mode = lv_mode iv_init = iv_init iv_ddl = ls_desc-ddl
                             iv_xdata = VALUE xstring( )
                   IMPORTING ev_affected = lv_aff ev_error = lv_err ).
      IF lv_err IS NOT INITIAL. rs-error = lv_err. RETURN. ENDIF.
    ENDIF.
  ENDMETHOD.

  METHOD replicate_parallel.
    DATA(ld) = describe_table( iv_tab = iv_tab iv_target = iv_target iv_columns = iv_columns ).
    IF ld-error IS NOT INITIAL.  rs-error = ld-error. RETURN. ENDIF.
    IF ld-keys IS INITIAL.       rs-error = 'parallel load needs a keyed table'. RETURN. ENDIF.

    " partition column width (zero-padded NUMC/INT key)
    DATA(lv_pc) = to_upper( CONV string( iv_part_col ) ).
    DATA lv_len TYPE i.
    LOOP AT ld-fields INTO DATA(lf) WHERE name = lv_pc. lv_len = lf-length. ENDLOOP.
    IF lv_len = 0. rs-error = |partition column { lv_pc } not found|. RETURN. ENDIF.

    " coordinator: (re)create the heap target (no PRIMARY KEY)
    DATA(lc) = query( |DROP TABLE IF EXISTS { iv_target }; { ld-ddl_nopk }| ).
    IF lc-error IS NOT INITIAL. rs-error = lc-error. RETURN. ENDIF.

    " min/max of the partition column at the SAP source (honoring iv_where + any
    " CDS parameters via the same dynamic FROM token the workers use).
    DATA(lv_from) = source_from( iv_tab = iv_tab iv_params = iv_params ).
    DATA(lv_sel) = |MIN( { lv_pc } ), MAX( { lv_pc } )|.
    DATA lt_w TYPE string_table.
    IF iv_where IS NOT INITIAL. APPEND iv_where TO lt_w. ENDIF.
    DATA: lv_mins TYPE c LENGTH 40, lv_maxs TYPE c LENGTH 40.
    TRY.
        IF lt_w IS INITIAL.
          SELECT (lv_sel) FROM (lv_from) INTO (@lv_mins, @lv_maxs).
          ENDSELECT.
        ELSE.
          SELECT (lv_sel) FROM (lv_from) WHERE (lt_w) INTO (@lv_mins, @lv_maxs).
          ENDSELECT.
        ENDIF.
      CATCH cx_root INTO DATA(lx_mm). rs-error = |min/max failed: { lx_mm->get_text( ) }|. RETURN.
    ENDTRY.
    " the column must be DIGIT-VALUED (NUMC, or a CHAR/DATS key of digits like a
    " document number) so its values range-partition numerically. A non-numeric
    " column (e.g. an alphabetic CHAR key) errors cleanly instead of dumping on the
    " char->packed conversion below.
    DATA: lv_min TYPE p LENGTH 16, lv_max TYPE p LENGTH 16.
    TRY.
        lv_min = lv_mins. lv_max = lv_maxs.
      CATCH cx_sy_conversion_no_number.
        rs-error = |partition column { lv_pc } is not numeric-valued | &&
                   |(min=[{ condense( lv_mins ) }] max=[{ condense( lv_maxs ) }]); | &&
                   |choose a NUMC/integer or digit-valued key column|.
        RETURN.
    ENDTRY.

    DATA(lv_span) = lv_max - lv_min + 1.
    DATA lv_step TYPE p LENGTH 16.
    lv_step = lv_span / iv_jobs.
    IF lv_step < 1. lv_step = 1. ENDIF.

    GET TIME STAMP FIELD DATA(lv_t0).
    DATA(lv_pfx) = |ERPLR{ sy-uzeit }|.
    DATA lv_started TYPE i.
    DO iv_jobs TIMES.
      DATA(lv_i)  = sy-index.
      DATA lv_lo TYPE p LENGTH 16.
      DATA lv_hi TYPE p LENGTH 16.
      lv_lo = lv_min + ( lv_i - 1 ) * lv_step.
      lv_hi = COND #( WHEN lv_i < iv_jobs THEN lv_min + lv_i * lv_step - 1 ELSE lv_max ).
      IF lv_lo > lv_max. EXIT. ENDIF.   " more jobs than distinct keys
      DATA(lv_los) = condense( |{ lv_lo }| ).
      DATA(lv_his) = condense( |{ lv_hi }| ).
      DATA(lv_range) = |{ lv_pc } BETWEEN '{ lv_los WIDTH = lv_len ALIGN = RIGHT PAD = '0' }'| &&
                       | AND '{ lv_his WIDTH = lv_len ALIGN = RIGHT PAD = '0' }'|.
      DATA(lv_where) = COND string( WHEN iv_where IS NOT INITIAL
                                    THEN |( { iv_where } ) AND ( { lv_range } )| ELSE lv_range ).
      DATA lv_jn TYPE tbtcjob-jobname.
      DATA lv_jc TYPE tbtcjob-jobcount.
      lv_jn = |{ lv_pfx }_{ lv_i }|.
      CALL FUNCTION 'JOB_OPEN' EXPORTING jobname = lv_jn IMPORTING jobcount = lv_jc
        EXCEPTIONS OTHERS = 1.
      IF sy-subrc <> 0. rs-error = 'JOB_OPEN failed'. RETURN. ENDIF.
      SUBMIT z_erpl_rev_repl_worker
        WITH p_tab = iv_tab WITH p_tgt = iv_target WITH p_where = lv_where
        WITH p_cols = iv_columns WITH p_init = iv_init WITH p_batch = iv_batch
        WITH p_parm = iv_params
        VIA JOB lv_jn NUMBER lv_jc AND RETURN.
      CALL FUNCTION 'JOB_CLOSE' EXPORTING jobcount = lv_jc jobname = lv_jn strtimmed = abap_true
        EXCEPTIONS OTHERS = 1.
      lv_started = lv_started + 1.
    ENDDO.
    IF ii_progress IS BOUND.
      ii_progress->note( iv_text = |submitted { lv_started } background job(s) on { lv_pc }; waiting...|
                         iv_done = 0 iv_total = lv_started ).
    ENDIF.

    " wait for all workers (active = scheduled/released/ready/running). The dialog
    " BLOCKS here, so emit a status line each second (rows loaded climb live) —
    " otherwise the screen looks frozen and users re-trigger the load.
    DATA(lv_like) = |{ lv_pfx }_%|.
    DATA lv_active TYPE i.
    DO 3600 TIMES.
      SELECT COUNT(*) FROM tbtco INTO @lv_active
        WHERE jobname LIKE @lv_like AND status IN ( 'P', 'S', 'Y', 'R' ).
      IF lv_active = 0. EXIT. ENDIF.
      IF ii_progress IS BOUND.
        DATA(lv_done) = lv_started - lv_active.
        DATA(lqc) = query( |SELECT count(*) AS c FROM { iv_target }| ).
        FIND PCRE '"c":(\d+)' IN lqc-rows SUBMATCHES DATA(lv_loaded).
        ii_progress->note(
          iv_text  = |parallel load: { lv_done }/{ lv_started } jobs done, { lv_loaded } rows loaded|
          iv_done  = lv_done iv_total = lv_started ).
      ENDIF.
      WAIT UP TO 1 SECONDS.
    ENDDO.
    " If the loop fell through with workers still active, the wait timed out — report
    " it (with the prefix so the operator can find the jobs in SM37) rather than
    " silently building the PK over a partial load.
    IF lv_active > 0.
      rs-error = |timeout: { lv_active } of { lv_started } worker job(s) still running | &&
                 |after the wait window (jobs { lv_pfx }_*); check SM37|.
      RETURN.
    ENDIF.
    DATA lv_abend TYPE i.
    SELECT COUNT(*) FROM tbtco INTO @lv_abend
      WHERE jobname LIKE @lv_like AND status = 'A'.
    IF lv_abend > 0. rs-error = |{ lv_abend } of { lv_started } worker job(s) aborted|. ENDIF.

    " coordinator: build the PRIMARY KEY once over the merged data
    IF rs-error IS INITIAL.
      IF ii_progress IS BOUND.
        ii_progress->note( iv_text = |all { lv_started } jobs done; building primary key ({ ld-keys })...| ).
      ENDIF.
      DATA(lpk) = query( |ALTER TABLE { iv_target } ADD PRIMARY KEY ({ ld-keys });| ).
      IF lpk-error IS NOT INITIAL. rs-error = |add primary key: { lpk-error }|. ENDIF.
    ENDIF.

    GET TIME STAMP FIELD DATA(lv_t1).
    rs-seconds = cl_abap_tstmp=>subtract( tstmp1 = lv_t1 tstmp2 = lv_t0 ).
    DATA(qc) = query( |SELECT count(*) AS c FROM { iv_target }| ).
    FIND PCRE '"c":(\d+)' IN qc-rows SUBMATCHES DATA(lv_cs).
    IF sy-subrc = 0. rs-rows_affected = CONV i( lv_cs ). ENDIF.

    " Dashboard stats: one FULL run row for the whole parallel load (jobs = N). The
    " workers pass iv_record=false, so this is the single row for the run.
    IF iv_record = abap_true.
      record_run( iv_target = iv_target iv_source = iv_tab
        iv_run_type = 'FULL' iv_method = 'FULL'
        iv_status = COND #( WHEN rs-error IS INITIAL THEN 'SUCCESS' ELSE 'ERROR' )
        iv_ms = CONV i( rs-seconds * 1000 )
        iv_read = rs-rows_affected iv_ins = rs-rows_affected
        iv_jobs = iv_jobs iv_error = rs-error ).
    ENDIF.
  ENDMETHOD.

  METHOD pick_partition_col.
    " replicate_parallel range-partitions a column by its numeric MIN/MAX over
    " fixed-width zero-padded values, so the column must hold DIGIT-VALUED data:
    " NUMC / integer / DEC(.,0) always do, and a fixed-width CHAR/DATS/TIMS key
    " often does too (a document number like BELNR is CHAR(10) of digits — the
    " engine validates the content at run time and errors cleanly if it isn't).
    " We therefore pick the WIDEST KEY column (widest ~ highest cardinality for an
    " ID-like key: a document number beats a fiscal year), excluding CLNT (constant
    " across a load), long/binary/float types, and anything wider than 30. Manual
    " override is always available on the selection screen.
    DATA: lv_best_key  TYPE string, lv_best_klen TYPE i,
          lv_best_any  TYPE string, lv_best_alen TYPE i.
    LOOP AT it_fields INTO DATA(ls_f).
      DATA(lv_ok) = xsdbool(
        ls_f-datatype <> 'CLNT' AND ls_f-length > 0 AND ls_f-length <= 30 AND
        ( ls_f-datatype = 'NUMC' OR ls_f-datatype = 'CHAR' OR ls_f-datatype = 'DATS'
          OR ls_f-datatype = 'TIMS' OR ls_f-datatype = 'INT1' OR ls_f-datatype = 'INT2'
          OR ls_f-datatype = 'INT4' OR ls_f-datatype = 'INT8'
          OR ( ls_f-datatype = 'DEC' AND ls_f-decimals = 0 ) ) ).
      IF lv_ok = abap_false. CONTINUE. ENDIF.
      " widest wins; ties keep the LATER field (deeper key -> finer granularity).
      IF ls_f-is_key = abap_true.
        IF ls_f-length >= lv_best_klen. lv_best_klen = ls_f-length. lv_best_key = ls_f-name. ENDIF.
      ELSE.
        IF ls_f-length >= lv_best_alen. lv_best_alen = ls_f-length. lv_best_any = ls_f-name. ENDIF.
      ENDIF.
    ENDLOOP.
    rv_col = COND string( WHEN lv_best_key IS NOT INITIAL THEN lv_best_key ELSE lv_best_any ).
  ENDMETHOD.

  METHOD recommend_jobs.
    IF iv_rows < iv_min. rv = 1. RETURN. ENDIF.          " too small to parallelize
    " ceil(rows / target): use DIV (truncates) — ABAP '/' on integers ROUNDS, so
    " 1,249,999 / 250,000 would give 5, not the intended 4.
    rv = ( iv_rows + iv_target - 1 ) DIV iv_target.
    IF rv < 2.       rv = 2.      ENDIF.
    IF rv > iv_cap.  rv = iv_cap. ENDIF.
  ENDMETHOD.

  METHOD max_batch_jobs.
    rv = iv_default.
    TRY.
        DATA lv_val TYPE c LENGTH 32.
        CALL 'C_SAPGPARAM' ID 'NAME'  FIELD 'rdisp/wp_no_btc'
                           ID 'VALUE' FIELD lv_val.            "#EC CI_CCALL
        IF sy-subrc = 0 AND lv_val CO ' 0123456789'.
          DATA(lv_n) = CONV i( lv_val ).
          IF lv_n > 0. rv = lv_n. ENDIF.
        ENDIF.
      CATCH cx_root.
    ENDTRY.
  ENDMETHOD.

  METHOD publish.
    DATA lv_sql TYPE string.
    CASE to_upper( CONV string( iv_kind ) ).
      WHEN 'PARQUET'.
        " single file (default) or a Hive-partitioned dataset directory.
        DATA(lv_opt) = `FORMAT parquet`.
        IF iv_partition_by IS NOT INITIAL.
          lv_opt = |{ lv_opt }, PARTITION_BY ({ iv_partition_by }), OVERWRITE_OR_IGNORE 1|.
        ENDIF.
        lv_sql = |COPY (SELECT * FROM { iv_source }) TO '{ iv_dest }' ({ lv_opt });|.
      WHEN 'TABLE'.
        " a table in an ATTACHed catalog (ducklake/postgres/bigquery/iceberg/duckdb).
        IF to_upper( iv_mode ) = 'APPEND'.
          lv_sql = |INSERT INTO { iv_dest } SELECT * FROM { iv_source };|.
        ELSE.   " FULL: overwrite the destination table
          lv_sql = |DROP TABLE IF EXISTS { iv_dest }; | &&
                   |CREATE TABLE { iv_dest } AS SELECT * FROM { iv_source };|.
        ENDIF.
      WHEN OTHERS.
        rs-error = |unsupported target kind '{ iv_kind }' (expected PARQUET or TABLE)|.
        RETURN.
    ENDCASE.
    rs = query( lv_sql ).
  ENDMETHOD.

  METHOD source_from.
    rv = COND string( WHEN iv_params IS INITIAL THEN CONV string( iv_tab )
                      ELSE |{ iv_tab }( { iv_params } )| ).
  ENDMETHOD.

  METHOD replicate_native.
    DATA(lv_sql) = |SELECT * FROM { iv_from }|.
    IF iv_where IS NOT INITIAL. lv_sql = |{ lv_sql } WHERE { iv_where }|. ENDIF.
    GET TIME STAMP FIELD DATA(lv_t0).
    " A synchronous ingest RFC does an implicit COMMIT that invalidates an open DB
    " cursor (same gotcha as Open SQL). So we (1) read metadata + close, (2) create
    " the target, (3) read ALL rows into memory + close, then (4) ingest in slices —
    " never holding the ADBC cursor across an RFC. Bounded by result size (keyless).
    DATA lo_res TYPE REF TO cl_sql_result_set.
    TRY.
        lo_res = NEW cl_sql_statement( )->execute_query( lv_sql ).
      CATCH cx_root INTO DATA(lx_q).
        rs-error = |native query failed: { lx_q->get_text( ) }|. RETURN.
    ENDTRY.

    " Result-set metadata -> receive-itab components + DuckDB column types. ADBC
    " DATA_TYPE is an ABAP type kind ('C','N','P','I','D',...). Char/NUMC/DEC land as
    " text and the DuckDB VARCHAR/BLOB staging casts to the typed column.
    DATA(lt_md) = lo_res->get_metadata( ).
    lo_res->close( ).
    DATA lt_comp TYPE cl_abap_structdescr=>component_table.
    DATA lt_cols TYPE string_table.
    FIELD-SYMBOLS <md> TYPE any.
    LOOP AT lt_md ASSIGNING <md>.
      ASSIGN COMPONENT 'COLUMN_NAME' OF STRUCTURE <md> TO FIELD-SYMBOL(<nm>).
      ASSIGN COMPONENT 'DATA_TYPE'   OF STRUCTURE <md> TO FIELD-SYMBOL(<dt>).
      ASSIGN COMPONENT 'DECIMALS'    OF STRUCTURE <md> TO FIELD-SYMBOL(<dc>).
      DATA(lv_cn)   = to_upper( condense( CONV string( <nm> ) ) ).
      DATA(lv_kind) = to_upper( condense( CONV string( <dt> ) ) ).
      DATA(lv_dec)  = CONV i( <dc> ).
      DATA lo_el  TYPE REF TO cl_abap_elemdescr.
      DATA lv_duck TYPE string.
      CASE lv_kind.
        WHEN 'I' OR 'B' OR 'S'. lo_el = cl_abap_elemdescr=>get_i( ).      lv_duck = 'INTEGER'.
        WHEN 'F' OR 'E'.        lo_el = cl_abap_elemdescr=>get_f( ).      lv_duck = 'DOUBLE'.
        WHEN 'D'.               lo_el = cl_abap_elemdescr=>get_d( ).      lv_duck = 'DATE'.
        WHEN 'T'.               lo_el = cl_abap_elemdescr=>get_t( ).      lv_duck = 'TIME'.
        WHEN 'X' OR 'Y'.        lo_el = cl_abap_elemdescr=>get_xstring( ). lv_duck = 'BLOB'.
        WHEN 'P'.               lo_el = cl_abap_elemdescr=>get_p( p_length = 16 p_decimals = lv_dec ).
                                lv_duck = |DECIMAL(38,{ lv_dec })|.
        WHEN OTHERS.            lo_el = cl_abap_elemdescr=>get_string( ). lv_duck = 'VARCHAR'.   " C/N/8/string
      ENDCASE.
      APPEND VALUE #( name = lv_cn type = lo_el ) TO lt_comp.
      APPEND |{ lv_cn } { lv_duck }| TO lt_cols.
    ENDLOOP.
    IF lt_comp IS INITIAL. rs-error = 'native source has no columns'. RETURN. ENDIF.

    " (re)create the heap target (no PK: native/BW is full-load, keyless).
    DATA(lc) = query( |DROP TABLE IF EXISTS { iv_target }; | &&
                      |CREATE TABLE IF NOT EXISTS { iv_target } ({ concat_lines_of( table = lt_cols sep = `, ` ) });| ).
    IF lc-error IS NOT INITIAL. rs-error = lc-error. RETURN. ENDIF.

    DATA(lo_tab) = cl_abap_tabledescr=>create( cl_abap_structdescr=>create( lt_comp ) ).
    DATA: lr_all TYPE REF TO data, lr_pkg TYPE REF TO data.
    CREATE DATA lr_all TYPE HANDLE lo_tab.
    CREATE DATA lr_pkg TYPE HANDLE lo_tab.
    FIELD-SYMBOLS: <all> TYPE STANDARD TABLE, <pkg> TYPE STANDARD TABLE.
    ASSIGN lr_all->* TO <all>.
    ASSIGN lr_pkg->* TO <pkg>.

    " (3) read ALL rows into memory on a FRESH cursor (no RFC in this loop).
    TRY.
        DATA(lo_r2) = NEW cl_sql_statement( )->execute_query( lv_sql ).
        lo_r2->set_param_table( lr_pkg ).
        DO.
          DATA(lv_n) = lo_r2->next_package( ).
          IF lv_n = 0. EXIT. ENDIF.
          APPEND LINES OF <pkg> TO <all>.
          CLEAR <pkg>.
          IF iv_maxrows > 0 AND lines( <all> ) >= iv_maxrows. EXIT. ENDIF.
        ENDDO.
        lo_r2->close( ).
      CATCH cx_root INTO DATA(lx).
        rs-error = |native fetch failed: { lx->get_text( ) }|. RETURN.
    ENDTRY.

    " (4) ingest in slices (each ingest RFC commits, but no cursor is open now).
    DATA(lv_pkg) = COND i( WHEN iv_batch > 0 THEN iv_batch ELSE 50000 ).
    DATA lr_sl TYPE REF TO data.
    CREATE DATA lr_sl TYPE HANDLE lo_tab.
    FIELD-SYMBOLS <sl> TYPE STANDARD TABLE.
    ASSIGN lr_sl->* TO <sl>.
    DATA: lv_off TYPE i VALUE 0, lv_first TYPE abap_bool VALUE abap_true,
          lv_aff TYPE i, lv_err TYPE string.
    DO.
      IF lv_off >= lines( <all> ). EXIT. ENDIF.
      CLEAR <sl>.
      APPEND LINES OF <all> FROM lv_off + 1 TO lv_off + lv_pkg TO <sl>.
      lv_off = lv_off + lv_pkg.
      DATA(lo_w) = cl_sxml_string_writer=>create( type = if_sxml=>co_xt_binary ).
      CALL TRANSFORMATION id SOURCE data = <sl> RESULT XML lo_w.
      ingest_bxml( EXPORTING iv_target = iv_target iv_keys = `` iv_mode = `INSERT`
                             iv_init = COND #( WHEN lv_first = abap_true THEN iv_init ELSE `` )
                             iv_ddl = `` iv_xdata = lo_w->get_output( )
                   IMPORTING ev_affected = lv_aff ev_error = lv_err ).
      lv_first = abap_false.
      IF lv_err IS NOT INITIAL. rs-error = lv_err. RETURN. ENDIF.
      rs-rows_affected = rs-rows_affected + lv_aff.
      IF ii_progress IS BOUND.
        GET TIME STAMP FIELD DATA(lv_tn).
        ii_progress->tick( iv_rows = rs-rows_affected iv_total = lines( <all> )
                           iv_seconds = cl_abap_tstmp=>subtract( tstmp1 = lv_tn tstmp2 = lv_t0 ) ).
      ENDIF.
    ENDDO.

    GET TIME STAMP FIELD DATA(lv_t1).
    rs-seconds = cl_abap_tstmp=>subtract( tstmp1 = lv_t1 tstmp2 = lv_t0 ).
  ENDMETHOD.

  METHOD ingest_bxml.
    DATA lv_aff TYPE string.
    DATA lv_msg TYPE c LENGTH 255.
    CALL FUNCTION 'Z_DUCKDB_INGEST' DESTINATION c_dest
      EXPORTING  iv_target   = CONV string( iv_target )
                 iv_mode     = iv_mode
                 iv_keys     = iv_keys
                 iv_init_sql = iv_init
                 iv_ddl      = iv_ddl
                 iv_xdata    = iv_xdata
      IMPORTING  ev_rows_affected = lv_aff
                 ev_error         = ev_error
      EXCEPTIONS system_failure = 1 MESSAGE lv_msg
                 communication_failure = 2 MESSAGE lv_msg
                 OTHERS = 3.
    IF sy-subrc <> 0.
      ev_error = |RFC subrc={ sy-subrc } { lv_msg }|.
    ELSE.
      ev_affected = CONV i( lv_aff ).
    ENDIF.
  ENDMETHOD.

  METHOD build_table.
    " Parse the columns JSON ([{ "name":.., "type":.. }]) into a component table,
    " mapping each DuckDB type back to a DDIC type via the typemap, and return an
    " empty typed standard table.
    TYPES: BEGIN OF ty_col, name TYPE string, type TYPE string, END OF ty_col.
    DATA lt_cols TYPE STANDARD TABLE OF ty_col WITH EMPTY KEY.
    /ui2/cl_json=>deserialize( EXPORTING json = iv_columns
                               CHANGING  data = lt_cols ).

    DATA lt_comp TYPE cl_abap_structdescr=>component_table.
    LOOP AT lt_cols INTO DATA(ls_col).
      DATA(ls_d) = zcl_erpl_rev_typemap=>duckdb_to_ddic( ls_col-type ).
      DATA lo_elem TYPE REF TO cl_abap_elemdescr.
      CASE ls_d-datatype.
        WHEN 'INT4'. lo_elem = cl_abap_elemdescr=>get_i( ).
        WHEN 'INT8'. lo_elem = cl_abap_elemdescr=>get_int8( ).
        WHEN 'DATS'. lo_elem = cl_abap_elemdescr=>get_d( ).
        WHEN 'TIMS'. lo_elem = cl_abap_elemdescr=>get_t( ).
        WHEN 'DEC'.  lo_elem = cl_abap_elemdescr=>get_p(
                       p_length = COND i( WHEN ls_d-length > 0 THEN ( ls_d-length + 2 ) / 2 ELSE 8 )
                       p_decimals = ls_d-decimals ).
        WHEN 'FLTP'. lo_elem = cl_abap_elemdescr=>get_f( ).
        WHEN OTHERS.
          lo_elem = cl_abap_elemdescr=>get_string( ).
      ENDCASE.
      APPEND VALUE #( name = ls_col-name type = lo_elem ) TO lt_comp.
    ENDLOOP.

    IF lt_comp IS INITIAL.
      RETURN.
    ENDIF.

    DATA(lo_struct) = cl_abap_structdescr=>get( lt_comp ).
    DATA(lo_tab) = cl_abap_tabledescr=>create( lo_struct ).
    CREATE DATA rr_tab TYPE HANDLE lo_tab.
  ENDMETHOD.

  METHOD result_to_alv.
    rr_tab = build_table( is_result-columns ).
    IF rr_tab IS NOT BOUND.
      RETURN.
    ENDIF.
    FIELD-SYMBOLS <lt> TYPE STANDARD TABLE.
    ASSIGN rr_tab->* TO <lt>.
    " Fill it from the rows JSON.
    /ui2/cl_json=>deserialize( EXPORTING json = is_result-rows
                               CHANGING  data = <lt> ).
  ENDMETHOD.

  METHOD query_stream.
    DATA: lv_handle TYPE string, lv_cols TYPE string,
          lv_err TYPE string, lv_msg TYPE c LENGTH 255.

    CALL FUNCTION 'Z_DUCKDB_OPEN' DESTINATION c_dest
      EXPORTING iv_sql = iv_sql
      IMPORTING ev_handle = lv_handle ev_columns = lv_cols ev_error = lv_err
      EXCEPTIONS system_failure = 1 MESSAGE lv_msg
                 communication_failure = 2 MESSAGE lv_msg OTHERS = 3.
    IF sy-subrc <> 0.
      rs-error = |OPEN RFC subrc={ sy-subrc } { lv_msg }|. RETURN.
    ELSEIF lv_err IS NOT INITIAL.
      rs-error = lv_err. RETURN.
    ENDIF.
    rs-columns = lv_cols.

    " Typed target table built once from the column metadata; pages append here.
    rs-data = build_table( lv_cols ).
    FIELD-SYMBOLS <all> TYPE STANDARD TABLE.
    IF rs-data IS BOUND.
      ASSIGN rs-data->* TO <all>.
    ENDIF.

    DATA: lv_done TYPE string, lv_fetched TYPE string, lv_xdata TYPE xstring,
          lv_capped TYPE abap_bool.
    DO.
      CLEAR: lv_xdata, lv_done, lv_fetched, lv_err.
      CALL FUNCTION 'Z_DUCKDB_FETCH' DESTINATION c_dest
        EXPORTING iv_handle = lv_handle iv_page_rows = CONV string( iv_page )
        IMPORTING ev_xdata = lv_xdata ev_fetched = lv_fetched
                  ev_done = lv_done ev_error = lv_err
        EXCEPTIONS system_failure = 1 MESSAGE lv_msg
                   communication_failure = 2 MESSAGE lv_msg OTHERS = 3.
      IF sy-subrc <> 0.
        rs-error = |FETCH RFC subrc={ sy-subrc } { lv_msg }|. EXIT.
      ELSEIF lv_err IS NOT INITIAL.
        rs-error = lv_err. EXIT.
      ENDIF.

      IF lv_xdata IS NOT INITIAL AND <all> IS ASSIGNED.
        " The slim decode: standard binary-sXML reader + identity transformation
        " straight into a fresh typed page table, then append.
        DATA lr_page TYPE REF TO data.
        lr_page = build_table( lv_cols ).
        FIELD-SYMBOLS <page> TYPE STANDARD TABLE.
        ASSIGN lr_page->* TO <page>.
        DATA(lo_rd) = cl_sxml_string_reader=>create( input = lv_xdata ).
        CALL TRANSFORMATION id SOURCE XML lo_rd RESULT data = <page>.
        INSERT LINES OF <page> INTO TABLE <all>.
        rs-row_count = lines( <all> ).
      ENDIF.

      " Stop early once we have enough for the display cap (server keeps the rest
      " until CLOSE — we just stop pulling). truncated unless the stream ended.
      IF iv_maxrows > 0 AND rs-row_count >= iv_maxrows.
        IF lv_done <> 'X'. lv_capped = abap_true. ENDIF.
        EXIT.
      ENDIF.
      IF lv_done = 'X'. EXIT. ENDIF.
    ENDDO.
    rs-truncated = lv_capped.

    " Always CLOSE (frees the server-side cursor + its connection).
    CALL FUNCTION 'Z_DUCKDB_CLOSE' DESTINATION c_dest
      EXPORTING iv_handle = lv_handle IMPORTING ev_error = lv_err
      EXCEPTIONS OTHERS = 0.
  ENDMETHOD.

ENDCLASS.
