*&---------------------------------------------------------------------*
*& Report  Z_ERPL_REV_REPLICATE
*&---------------------------------------------------------------------*
*& erpl-rev: replicate an arbitrary SAP table into DuckDB via the
*& ERPL_REV RFC server, SLT-style. Three per-table controls mirror
*& SLT (transaction LTRS): table selection (p_tab), field selection /
*& target minimization (p_cols), and a filter condition applied at the
*& SAP source (p_where) so unmatched rows are never transferred. Keys
*& are always kept so the typed UPSERT dedups.
*&
*& Ergonomics: press F4 on "Source SAP table" to search the DDIC by
*& name pattern, and F4 on "Columns" to multi-pick the columns of that
*& table. All heavy lifting lives in zcl_erpl_rev_util / the C++ server;
*& this report is a thin shell. The F4 popups (CL_*/DDIC dialogs) only
*& run in a real SAP GUI — their backing data comes from tested util
*& methods (search_tables / list_columns).
*&---------------------------------------------------------------------*
REPORT z_erpl_rev_replicate.

TYPE-POOLS vrm.   " VRM_SET_VALUES (dropdown listbox values for the Delta tab)
TYPES ty_meth TYPE c LENGTH 12.   " a LISTBOX + USER-COMMAND param can't carry an inline length

" Labels live in comment variables (filled at INITIALIZATION) so the screen
" reads in plain language without a maintained text pool. p_cols/p_where/
" p_target/p_init are LOWER CASE so values keep their case (a filter literal
" like 'LH', a lower-case DuckDB table name).
" Tabstrip: Source / Target / Parallel / External as tabbed subscreens, so the
" screen reads as four short panes instead of one long scroll.
SELECTION-SCREEN BEGIN OF TABBED BLOCK tabb FOR 14 LINES.
  SELECTION-SCREEN TAB (22) ts_src USER-COMMAND ts1 DEFAULT SCREEN 0100.
  SELECTION-SCREEN TAB (22) ts_tgt USER-COMMAND ts2 DEFAULT SCREEN 0200.
  SELECTION-SCREEN TAB (22) ts_par USER-COMMAND ts3 DEFAULT SCREEN 0300.
  SELECTION-SCREEN TAB (22) ts_ext USER-COMMAND ts4 DEFAULT SCREEN 0400.
  SELECTION-SCREEN TAB (22) ts_dlt USER-COMMAND ts5 DEFAULT SCREEN 0500.
SELECTION-SCREEN END OF BLOCK tabb.

" MODIF ID groups drive show/hide in AT SELECTION-SCREEN OUTPUT:
"   tab/sql/mod/par = Open-SQL source path (hidden when BW is ticked)
"   bwn             = BW/native path     (shown only when BW is ticked)
SELECTION-SCREEN BEGIN OF SCREEN 0100 AS SUBSCREEN.
SELECTION-SCREEN BEGIN OF BLOCK src WITH FRAME TITLE t_src.
  SELECTION-SCREEN BEGIN OF LINE.
    SELECTION-SCREEN COMMENT 1(24) c_tab FOR FIELD p_tab MODIF ID tab.
    PARAMETERS p_tab TYPE tabname MODIF ID tab.
  SELECTION-SCREEN END OF LINE.
  SELECTION-SCREEN BEGIN OF LINE.
    SELECTION-SCREEN COMMENT 1(24) c_cols FOR FIELD p_cols MODIF ID sql.
    PARAMETERS p_cols(255) TYPE c LOWER CASE VISIBLE LENGTH 50 MODIF ID sql.
  SELECTION-SCREEN END OF LINE.
  SELECTION-SCREEN BEGIN OF LINE.
    SELECTION-SCREEN COMMENT 1(24) c_where FOR FIELD p_where MODIF ID sql.
    PARAMETERS p_where(255) TYPE c LOWER CASE VISIBLE LENGTH 50 MODIF ID sql.
  SELECTION-SCREEN END OF LINE.
  " CDS-only (optional): input parameters for a WITH PARAMETERS view, and an
  " order/key-column override to enable streaming/parallel on a keyless source.
  " MODIF ID 'cds' -> shown only when the entered source is actually a CDS view.
  SELECTION-SCREEN BEGIN OF LINE.
    SELECTION-SCREEN COMMENT 1(24) c_cparm FOR FIELD p_cparm MODIF ID cds.
    PARAMETERS p_cparm(255) TYPE c LOWER CASE VISIBLE LENGTH 50 MODIF ID cds.
  SELECTION-SCREEN END OF LINE.
  SELECTION-SCREEN BEGIN OF LINE.
    SELECTION-SCREEN COMMENT 1(24) c_skey FOR FIELD p_skey MODIF ID cds.
    PARAMETERS p_skey(120) TYPE c LOWER CASE VISIBLE LENGTH 50 MODIF ID cds.
  SELECTION-SCREEN END OF LINE.
  " BW / native (ADBC): read a HANA object not addressable by Open SQL (e.g. a calc
  " view "_SYS_BIC"."pkg/CV"). When ticked, p_nfrom is the native FROM source and the
  " Open-SQL-only fields above (columns/filter/CDS params/parallel) don't apply.
  SELECTION-SCREEN BEGIN OF LINE.
    SELECTION-SCREEN COMMENT 1(24) c_bw FOR FIELD p_bw.
    PARAMETERS p_bw AS CHECKBOX USER-COMMAND bw.
  SELECTION-SCREEN END OF LINE.
  SELECTION-SCREEN BEGIN OF LINE.
    SELECTION-SCREEN COMMENT 1(24) c_nfrom FOR FIELD p_nfrom MODIF ID bwn.
    PARAMETERS p_nfrom(255) TYPE c LOWER CASE VISIBLE LENGTH 60 MODIF ID bwn.
  SELECTION-SCREEN END OF LINE.
SELECTION-SCREEN END OF BLOCK src.
SELECTION-SCREEN END OF SCREEN 0100.

SELECTION-SCREEN BEGIN OF SCREEN 0200 AS SUBSCREEN.
SELECTION-SCREEN BEGIN OF BLOCK tgt WITH FRAME TITLE t_tgt.
  SELECTION-SCREEN BEGIN OF LINE.
    SELECTION-SCREEN COMMENT 1(24) c_target FOR FIELD p_target.
    PARAMETERS p_target(120) TYPE c LOWER CASE VISIBLE LENGTH 40.
  SELECTION-SCREEN END OF LINE.
  SELECTION-SCREEN BEGIN OF LINE.
    SELECTION-SCREEN COMMENT 1(24) c_init FOR FIELD p_init.
    PARAMETERS p_init(255) TYPE c LOWER CASE VISIBLE LENGTH 50.
  SELECTION-SCREEN END OF LINE.
  SELECTION-SCREEN BEGIN OF LINE.
    SELECTION-SCREEN COMMENT 1(24) c_mode MODIF ID mod.
    PARAMETERS p_up RADIOBUTTON GROUP mod DEFAULT 'X' MODIF ID mod.
    SELECTION-SCREEN COMMENT 28(22) c_up FOR FIELD p_up MODIF ID mod.
  SELECTION-SCREEN END OF LINE.
  SELECTION-SCREEN BEGIN OF LINE.
    SELECTION-SCREEN COMMENT 1(24) c_void MODIF ID mod.
    PARAMETERS p_ins RADIOBUTTON GROUP mod MODIF ID mod.
    SELECTION-SCREEN COMMENT 28(22) c_ins FOR FIELD p_ins MODIF ID mod.
  SELECTION-SCREEN END OF LINE.
  SELECTION-SCREEN BEGIN OF LINE.
    SELECTION-SCREEN COMMENT 1(24) c_max FOR FIELD p_maxrow.
    PARAMETERS p_maxrow TYPE i DEFAULT 0.
  SELECTION-SCREEN END OF LINE.
  SELECTION-SCREEN BEGIN OF LINE.
    SELECTION-SCREEN COMMENT 1(24) c_batch FOR FIELD p_batch.
    PARAMETERS p_batch TYPE i DEFAULT 50000.
  SELECTION-SCREEN END OF LINE.
  SELECTION-SCREEN BEGIN OF LINE.
    SELECTION-SCREEN COMMENT 1(24) c_trunc FOR FIELD p_trunc MODIF ID mod.
    PARAMETERS p_trunc AS CHECKBOX DEFAULT 'X' MODIF ID mod.
  SELECTION-SCREEN END OF LINE.
  SELECTION-SCREEN BEGIN OF LINE.
    SELECTION-SCREEN COMMENT 1(24) c_ver FOR FIELD p_verify.
    PARAMETERS p_verify AS CHECKBOX DEFAULT 'X'.
  SELECTION-SCREEN END OF LINE.
  SELECTION-SCREEN BEGIN OF LINE.
    SELECTION-SCREEN COMMENT 1(24) c_full FOR FIELD p_full.
    PARAMETERS p_full AS CHECKBOX.
  SELECTION-SCREEN END OF LINE.
SELECTION-SCREEN END OF BLOCK tgt.
SELECTION-SCREEN END OF SCREEN 0200.

SELECTION-SCREEN BEGIN OF SCREEN 0300 AS SUBSCREEN.
" Parallel full-load: split the source by a numeric key column into N disjoint
" ranges and replicate them with N background jobs into one target, building the
" PRIMARY KEY once at the end (zcl_erpl_rev_util=>replicate_parallel). The
" partition column is auto-prefilled (AT SELECTION-SCREEN OUTPUT) and the job
" count is auto-determined from the row count + batch-WP ceiling — both overridable.
SELECTION-SCREEN BEGIN OF BLOCK par WITH FRAME TITLE t_par.
  SELECTION-SCREEN BEGIN OF LINE.
    SELECTION-SCREEN COMMENT 1(24) c_par FOR FIELD p_par MODIF ID par.
    " USER-COMMAND forces a screen round-trip on toggle so AT SELECTION-SCREEN
    " OUTPUT re-runs and enables + prefills the partition column / jobs fields
    " (a plain checkbox doesn't trigger PAI/PBO, so they'd stay greyed + empty).
    PARAMETERS p_par AS CHECKBOX USER-COMMAND par MODIF ID par.
  SELECTION-SCREEN END OF LINE.
  SELECTION-SCREEN BEGIN OF LINE.
    SELECTION-SCREEN COMMENT 1(24) c_pcol FOR FIELD p_pcol MODIF ID par.
    PARAMETERS p_pcol TYPE fieldname MODIF ID par.
  SELECTION-SCREEN END OF LINE.
  SELECTION-SCREEN BEGIN OF LINE.
    SELECTION-SCREEN COMMENT 1(24) c_jobs FOR FIELD p_jobs MODIF ID par.
    PARAMETERS p_jobs TYPE i DEFAULT 0 MODIF ID par.
  SELECTION-SCREEN END OF LINE.
SELECTION-SCREEN END OF BLOCK par.
SELECTION-SCREEN END OF SCREEN 0300.

SELECTION-SCREEN BEGIN OF SCREEN 0400 AS SUBSCREEN.
" External target (stage-then-publish): load into a local DuckDB holding table as
" usual, then materialize it elsewhere with ONE DuckDB statement. PARQUET = COPY to
" a file or partitioned dataset; ATTACHED TABLE = CREATE TABLE AS into a catalog
" attached at server boot (ducklake / postgres / bigquery / iceberg / duckdb). The
" catalog ATTACH + credentials live in the server's --init-sql/--init-file.
SELECTION-SCREEN BEGIN OF BLOCK ext WITH FRAME TITLE t_ext.
  SELECTION-SCREEN BEGIN OF LINE.
    SELECTION-SCREEN COMMENT 1(24) c_tkind.
    PARAMETERS r_kd RADIOBUTTON GROUP tk DEFAULT 'X' USER-COMMAND tk.
    SELECTION-SCREEN COMMENT 28(34) c_kd FOR FIELD r_kd.
  SELECTION-SCREEN END OF LINE.
  SELECTION-SCREEN BEGIN OF LINE.
    SELECTION-SCREEN COMMENT 1(24) c_v2.
    PARAMETERS r_kp RADIOBUTTON GROUP tk.
    SELECTION-SCREEN COMMENT 28(34) c_kp FOR FIELD r_kp.
  SELECTION-SCREEN END OF LINE.
  SELECTION-SCREEN BEGIN OF LINE.
    SELECTION-SCREEN COMMENT 1(24) c_v3.
    PARAMETERS r_kt RADIOBUTTON GROUP tk.
    SELECTION-SCREEN COMMENT 28(34) c_kt FOR FIELD r_kt.
  SELECTION-SCREEN END OF LINE.
  SELECTION-SCREEN BEGIN OF LINE.
    SELECTION-SCREEN COMMENT 1(24) c_dest FOR FIELD p_dest.
    PARAMETERS p_dest(255) TYPE c LOWER CASE VISIBLE LENGTH 60.
  SELECTION-SCREEN END OF LINE.
  SELECTION-SCREEN BEGIN OF LINE.
    SELECTION-SCREEN COMMENT 1(24) c_part FOR FIELD p_part.
    PARAMETERS p_part(120) TYPE c LOWER CASE VISIBLE LENGTH 40.
  SELECTION-SCREEN END OF LINE.
SELECTION-SCREEN END OF BLOCK ext.
SELECTION-SCREEN END OF SCREEN 0400.

SELECTION-SCREEN BEGIN OF SCREEN 0500 AS SUBSCREEN.
" Delta (incremental): after this (full) load — the SEED — register the DuckDB target
" in _erpl_rev_delta_state so future runs load only what changed, and optionally
" install the heartbeat background job that drives it. Applies to a local DuckDB
" target (External target = DuckDB table). Press F1 on any field for an explanation.
SELECTION-SCREEN BEGIN OF BLOCK dlt WITH FRAME TITLE t_dlt.
  SELECTION-SCREEN BEGIN OF LINE.
    SELECTION-SCREEN COMMENT 1(28) c_dlt FOR FIELD p_dlt.
    PARAMETERS p_dlt AS CHECKBOX.
  SELECTION-SCREEN END OF LINE.
  SELECTION-SCREEN BEGIN OF LINE.
    SELECTION-SCREEN COMMENT 1(28) c_dmeth FOR FIELD p_dmeth.
    PARAMETERS p_dmeth TYPE ty_meth AS LISTBOX VISIBLE LENGTH 48 USER-COMMAND dm DEFAULT 'SNAPSHOT'.
  SELECTION-SCREEN END OF LINE.
  SELECTION-SCREEN BEGIN OF LINE.
    SELECTION-SCREEN COMMENT 1(28) c_dwm FOR FIELD p_dwm MODIF ID dwm.
    PARAMETERS p_dwm(30) TYPE c MODIF ID dwm.
  SELECTION-SCREEN END OF LINE.
  SELECTION-SCREEN BEGIN OF LINE.
    SELECTION-SCREEN COMMENT 1(28) c_dwmk FOR FIELD p_dwmk MODIF ID dwm.
    PARAMETERS p_dwmk(10) TYPE c AS LISTBOX VISIBLE LENGTH 48 DEFAULT 'NUMTS' MODIF ID dwm.
  SELECTION-SCREEN END OF LINE.
  SELECTION-SCREEN BEGIN OF LINE.
    SELECTION-SCREEN COMMENT 1(28) c_dxtra FOR FIELD p_dxtra MODIF ID dxt.
    PARAMETERS p_dxtra(60) TYPE c LOWER CASE MODIF ID dxt.
  SELECTION-SCREEN END OF LINE.
  SELECTION-SCREEN BEGIN OF LINE.
    SELECTION-SCREEN COMMENT 1(28) c_djob FOR FIELD p_djobs MODIF ID dsn.
    PARAMETERS p_djobs TYPE i DEFAULT 1 MODIF ID dsn.    " parallel workers for the SNAPSHOT reload
  SELECTION-SCREEN END OF LINE.
  SELECTION-SCREEN BEGIN OF LINE.
    SELECTION-SCREEN COMMENT 1(28) c_dcad FOR FIELD p_dcad.
    PARAMETERS p_dcad(20) TYPE c AS LISTBOX VISIBLE LENGTH 48 DEFAULT 'nightly'.
  SELECTION-SCREEN END OF LINE.
  SELECTION-SCREEN BEGIN OF LINE.
    SELECTION-SCREEN COMMENT 1(28) c_dsch FOR FIELD p_dsch.
    PARAMETERS p_dsch AS CHECKBOX.
  SELECTION-SCREEN END OF LINE.
  SELECTION-SCREEN BEGIN OF LINE.
    SELECTION-SCREEN COMMENT 1(28) c_cdc FOR FIELD p_cdc.
    PARAMETERS p_cdc AS CHECKBOX.
  SELECTION-SCREEN END OF LINE.
SELECTION-SCREEN END OF BLOCK dlt.
SELECTION-SCREEN END OF SCREEN 0500.

*----------------------------------------------------------------------*
INITIALIZATION.
  ts_src = 'Source'.
  ts_tgt = 'Target & options'.
  ts_par = 'Parallel load'.
  ts_ext = 'External target'.
  ts_dlt = 'Delta & schedule'.
  t_src    = 'Source (what to replicate)'.
  t_tgt    = 'Target & options'.
  c_tab    = 'Source Table / CDS View'.
  c_cols   = 'Columns (F4 = pick)'.
  c_where  = 'Filter — SQL WHERE'.
  c_cparm  = 'CDS parameters'.
  c_skey   = 'Order/key cols (CDS)'.
  c_bw     = 'BW/native source (ADBC)'.
  c_nfrom  = 'Native FROM (calc view)'.
  c_target = 'DuckDB target table'.
  c_init   = 'Init SQL (e.g. LOAD)'.
  c_mode   = 'Write mode'.
  c_up     = 'UPSERT (dedupe on key)'.
  c_ins    = 'INSERT (append)'.
  c_max    = 'Max rows (0 = all)'.
  c_batch  = 'Package size (rows)'.
  c_trunc  = 'Full-load replace'.
  c_ver    = 'Verify row counts'.
  c_full   = 'Show full column mapping'.
  t_par    = 'Parallel load (large tables)'.
  c_par    = 'Parallel background jobs'.
  c_pcol   = 'Partition column (auto)'.
  c_jobs   = 'Jobs (0 = auto)'.
  t_ext    = 'External target (write elsewhere)'.
  c_tkind  = 'Write target to'.
  c_kd     = 'DuckDB table (in-process)'.
  c_kp     = 'Parquet file / dataset'.
  c_kt     = 'Table in attached catalog'.
  c_dest   = 'Destination (path / ref)'.
  c_part   = 'Parquet partition by'.
  t_dlt    = 'Delta (incremental) + periodic schedule'.
  c_dlt    = 'Register as delta target'.
  c_dmeth  = 'Method (F1 = explain)'.
  c_dwm    = 'Watermark column (F4)'.
  c_dwmk   = 'Watermark kind'.
  c_dxtra  = 'Change-doc object (JSON)'.
  c_djob   = 'Parallel jobs (reload)'.
  c_dcad   = 'Refresh interval'.
  c_dsch   = 'Run it automatically (job)'.
  c_cdc    = 'Also capture physical deletes (trigger CDC)'.

*----------------------------------------------------------------------*
* F4: search DDIC tables by the pattern currently typed into P_TAB.
AT SELECTION-SCREEN ON VALUE-REQUEST FOR p_tab.
  PERFORM f4_table.

* F4: multi-pick the columns of the table currently in P_TAB (into P_COLS / P_SKEY).
AT SELECTION-SCREEN ON VALUE-REQUEST FOR p_cols.
  PERFORM f4_columns USING 'P_COLS' abap_true.
AT SELECTION-SCREEN ON VALUE-REQUEST FOR p_skey.
  PERFORM f4_columns USING 'P_SKEY' abap_true.
* F4: single-pick the watermark column from the source table's columns (Delta tab).
AT SELECTION-SCREEN ON VALUE-REQUEST FOR p_dwm.
  PERFORM f4_columns USING 'P_DWM' abap_false.

* F4: browse HANA catalog objects (views / functions, incl. _SYS_BIC calc views).
AT SELECTION-SCREEN ON VALUE-REQUEST FOR p_nfrom.
  PERFORM f4_nfrom.

* F1: plain-language help for the Delta tab fields (press F1 on the field). The text
* is passed in pieces (one short source line each) and joined in FORM help.
AT SELECTION-SCREEN ON HELP-REQUEST FOR p_dlt.
  PERFORM help USING 'Register as delta target'
    'After this full load (the SEED), register the DuckDB target so future runs load only'
    ' what CHANGED instead of reloading everything. Leave off for a plain one-off load.'
    ''.
AT SELECTION-SCREEN ON HELP-REQUEST FOR p_dmeth.
  PERFORM help USING 'Delta method'
    'How changes are detected. SNAPSHOT: reload + compare - the only method that catches physical DELETES (good default for small/medium tables).'
    ' WATERMARK: read rows where a change column exceeds the last high-water (needs a timestamp/sequence column).'
    ' CHANGEDOC / INSERT_ONLY: driven by SAP change documents (CDHDR/CDPOS).'.
AT SELECTION-SCREEN ON HELP-REQUEST FOR p_dwm.
  PERFORM help USING 'Watermark column'
    'For WATERMARK: the source column whose values only ever grow (a UTC timestamp or a sequence).'
    ' Each cycle reads the rows where this column is greater than the last value seen.'
    ''.
AT SELECTION-SCREEN ON HELP-REQUEST FOR p_dwmk.
  PERFORM help USING 'Watermark kind'
    'The type of the watermark column, so the high-water compares correctly.'
    ' NUMTS = numeric timestamp, DATETIME = date+time pair, CHANGENR = change number,'
    ' INT = integer, DATE = date only (coarse - nightly only).'.
AT SELECTION-SCREEN ON HELP-REQUEST FOR p_dxtra.
  PERFORM help USING 'Change-document object'
    'For CHANGEDOC / INSERT_ONLY: which change-document object class to read, as JSON,'
    ' e.g. {"objectclas":"MATERIAL"}.'
    ''.
AT SELECTION-SCREEN ON HELP-REQUEST FOR p_djobs.
  PERFORM help USING 'Parallel jobs (SNAPSHOT reload)'
    'SNAPSHOT reloads the whole table each cycle. With > 1, that reload runs across this many'
    ' background workers (same engine as the parallel full load), partitioned on a numeric key'
    ' column - useful for large tables. 1 = serial. Needs free batch work processes.'.
AT SELECTION-SCREEN ON HELP-REQUEST FOR p_dcad.
  PERFORM help USING 'Refresh interval'
    'How fresh this target is kept - a delta cycle for it runs about this often.'
    ' It is ALSO the period for the background job below when you tick "Run it automatically".'
    ' Pick "manual" to never auto-run it.'.
AT SELECTION-SCREEN ON HELP-REQUEST FOR p_dsch.
  PERFORM help USING 'Run it automatically (background job)'
    'Install (or re-time) ONE SAP background job that wakes every "Refresh interval" and runs all DUE delta targets.'
    ' It is the supported way to run delta on a schedule - monitor/stop it in SM37 (job ERPL_REV_DELTA).'
    ' Off = run delta by hand (report Z_ERPL_REV_DELTA).'.
AT SELECTION-SCREEN ON HELP-REQUEST FOR p_cdc.
  PERFORM help USING 'Capture physical deletes (trigger CDC)'
    'Opt-in extra tier: also create DB triggers (delete-only) on the source so PHYSICAL deletes are captured.'
    ' The watermark/change-doc methods cannot see a row that was hard-deleted; this closes that gap without a full snapshot.'
    ' Customer-owned, in-namespace (ZCDC_*) triggers; transparent tables only. See docs/cdc.md.'.

*----------------------------------------------------------------------*
* Validate on Execute (sy-ucomm='ONLI') only — not on the USER-COMMAND toggles
* (bw/tk/par). Replaces p_tab OBLIGATORY so BW mode needn't carry a dummy table.
AT SELECTION-SCREEN.
  IF sy-ucomm = 'ONLI'.
    IF p_bw = abap_true AND p_nfrom IS INITIAL.
      MESSAGE 'Enter the Native FROM source for a BW/native load' TYPE 'E'.
    ELSEIF p_bw = abap_false AND p_tab IS INITIAL.
      MESSAGE 'Enter the source table or CDS view' TYPE 'E'.
    ENDIF.
  ENDIF.

*----------------------------------------------------------------------*
* Show only the fields that apply to the chosen source (Open-SQL table/CDS vs
* BW/native) and target kind. Also auto-prefill the partition column once a
* table is known and parallel is chosen (DDIC-only describe, no RFC; only when
* still blank, so a manual override sticks).
AT SELECTION-SCREEN OUTPUT.
  IF p_bw = abap_false AND p_par = abap_true AND p_tab IS NOT INITIAL AND p_pcol IS INITIAL.
    DATA(ls_pd) = zcl_erpl_rev_util=>describe_table(
                    iv_tab = p_tab iv_target = 'x'
                    iv_columns = condense( CONV string( p_cols ) ) ).
    IF ls_pd-error IS INITIAL.
      p_pcol = zcl_erpl_rev_util=>pick_partition_col( ls_pd-fields ).
    ENDIF.
  ENDIF.
  " CDS-only fields appear only when the entered source is actually a CDS view.
  DATA(lv_src_cds) = COND abap_bool(
    WHEN p_bw = abap_false THEN zcl_erpl_rev_util=>is_cds( p_tab ) ELSE abap_false ).

  " Delta tab dropdowns (method / watermark-kind / refresh interval). The option
  " texts are the explanation; the stored key is the value used at registration.
  DATA lt_vrm TYPE vrm_values.
  lt_vrm = VALUE #(
    ( key = 'SNAPSHOT'    text = 'SNAPSHOT - reload + compare; catches deletes' )
    ( key = 'WATERMARK'   text = 'WATERMARK - rows where a change column > last seen' )
    ( key = 'CHANGEDOC'   text = 'CHANGEDOC - via CDHDR change docs, re-read by key' )
    ( key = 'INSERT_ONLY' text = 'INSERT_ONLY - append-only via CDHDR change numbers' ) ).
  CALL FUNCTION 'VRM_SET_VALUES' EXPORTING id = 'P_DMETH' values = lt_vrm.
  lt_vrm = VALUE #(
    ( key = 'NUMTS'    text = 'NUMTS - numeric UTC timestamp' )
    ( key = 'DATETIME' text = 'DATETIME - date + time pair' )
    ( key = 'CHANGENR' text = 'CHANGENR - change-document number' )
    ( key = 'INT'      text = 'INT - monotonic integer sequence' )
    ( key = 'DATE'     text = 'DATE - date only (nightly only)' ) ).
  CALL FUNCTION 'VRM_SET_VALUES' EXPORTING id = 'P_DWMK' values = lt_vrm.
  lt_vrm = VALUE #(
    ( key = 'micro:60'   text = 'every minute' )
    ( key = 'micro:300'  text = 'every 5 minutes' )
    ( key = 'micro:1800' text = 'every 30 minutes' )
    ( key = 'hourly'     text = 'hourly' )
    ( key = 'nightly'    text = 'nightly (once a day)' )
    ( key = 'manual'     text = 'manual (only when you run it)' ) ).
  CALL FUNCTION 'VRM_SET_VALUES' EXPORTING id = 'P_DCAD' values = lt_vrm.

  LOOP AT SCREEN.
    " BW/native vs Open-SQL source: show one path, hide the other.
    IF screen-group1 = 'BWN'.
      screen-active = COND i( WHEN p_bw = abap_true THEN 1 ELSE 0 ).
    ELSEIF screen-group1 = 'CDS'.
      screen-active = COND i( WHEN lv_src_cds = abap_true THEN 1 ELSE 0 ).
    ELSEIF screen-group1 = 'TAB' OR screen-group1 = 'SQL'
        OR screen-group1 = 'MOD' OR screen-group1 = 'PAR'.
      screen-active = COND i( WHEN p_bw = abap_true THEN 0 ELSE 1 ).
    ENDIF.
    " Within the parallel block, detail fields enable only when parallel is on.
    IF screen-name = 'P_PCOL' OR screen-name = 'P_JOBS'.
      screen-input = COND i( WHEN p_par = abap_true THEN 1 ELSE 0 ).
    ENDIF.
    " External target: destination unless DuckDB; partition-by only for parquet.
    IF screen-name = 'P_DEST'. screen-input = COND i( WHEN r_kd = abap_true THEN 0 ELSE 1 ). ENDIF.
    IF screen-name = 'P_PART'. screen-input = COND i( WHEN r_kp = abap_true THEN 1 ELSE 0 ). ENDIF.
    " Delta tab: show only the fields the chosen method needs (watermark column/kind
    " for WATERMARK; change-document object for CHANGEDOC / INSERT_ONLY).
    IF screen-group1 = 'DWM'.
      screen-active = COND i( WHEN p_dmeth = 'WATERMARK' THEN 1 ELSE 0 ).
    ELSEIF screen-group1 = 'DXT'.
      screen-active = COND i( WHEN p_dmeth = 'CHANGEDOC' OR p_dmeth = 'INSERT_ONLY' THEN 1 ELSE 0 ).
    ELSEIF screen-group1 = 'DSN'.
      screen-active = COND i( WHEN p_dmeth = 'SNAPSHOT' THEN 1 ELSE 0 ).
    ENDIF.
    MODIFY SCREEN.
  ENDLOOP.

*----------------------------------------------------------------------*
* Progress sink: each package tick goes to the status bar (dialog) and the
* background job log via cl_progress_indicator — so progress is visible both
* interactively and when the report runs as a background job.
CLASS lcl_prog DEFINITION.
  PUBLIC SECTION.
    INTERFACES zif_erpl_rev_progress.
    DATA mv_label TYPE string.
ENDCLASS.
CLASS lcl_prog IMPLEMENTATION.
  METHOD zif_erpl_rev_progress~tick.
    DATA lv_rps TYPE i.
    IF iv_seconds > 0. lv_rps = iv_rows / iv_seconds. ENDIF.
    DATA lv_txt TYPE string.
    IF iv_total > 0.
      " compute in int8 — iv_rows*100 overflows a 4-byte int past ~21.4M rows.
      DATA(lv_pct) = CONV i( CONV int8( iv_rows ) * 100 / iv_total ).
      lv_txt = |{ mv_label }: { iv_rows } / { iv_total } rows ({ lv_pct }%) | &&
               |@ { lv_rps } rows/s, { iv_seconds }s|.
      cl_progress_indicator=>progress_indicate(
        i_text = lv_txt i_processed = iv_rows i_total = iv_total
        i_output_immediately = abap_true ).
    ELSE.
      lv_txt = |{ mv_label }: { iv_rows } rows @ { lv_rps } rows/s, { iv_seconds }s|.
      cl_progress_indicator=>progress_indicate(
        i_text = lv_txt i_output_immediately = abap_true ).
    ENDIF.
  ENDMETHOD.

  METHOD zif_erpl_rev_progress~note.
    " free-text status (parallel coordinator). With a known job total, drive the
    " progress-bar fraction; otherwise just show the text (e.g. "building PK").
    IF iv_total > 0.
      cl_progress_indicator=>progress_indicate(
        i_text = |{ mv_label }: { iv_text }|
        i_processed = iv_done i_total = iv_total i_output_immediately = abap_true ).
    ELSE.
      cl_progress_indicator=>progress_indicate(
        i_text = |{ mv_label }: { iv_text }| i_output_immediately = abap_true ).
    ENDIF.
  ENDMETHOD.
ENDCLASS.

*----------------------------------------------------------------------*
START-OF-SELECTION.
  DATA(lv_target) = COND string( WHEN p_target IS NOT INITIAL THEN p_target
                                 ELSE to_lower( CONV string( p_tab ) ) ).
  DATA(lv_mode)   = COND string( WHEN p_ins = abap_true THEN 'INSERT' ELSE 'UPSERT' ).
  DATA(lv_cols)   = condense( CONV string( p_cols ) ).
  DATA(lv_where)  = condense( CONV string( p_where ) ).
  DATA(lv_cparm)  = condense( CONV string( p_cparm ) ).   " CDS WITH PARAMETERS
  DATA(lv_skey)   = condense( CONV string( p_skey ) ).    " CDS order/key override

  " ---- external-target resolution (stage-then-publish) ----
  " kind D = native DuckDB table (load straight into lv_target, as before).
  " kind P/T = load into a LOCAL holding table, then publish to lv_dest.
  DATA(lv_kind) = COND string( WHEN r_kp = abap_true THEN 'P'
                               WHEN r_kt = abap_true THEN 'T' ELSE 'D' ).
  DATA(lv_dest) = condense( CONV string( p_dest ) ).
  DATA(lv_part) = condense( CONV string( p_part ) ).
  IF lv_kind <> 'D' AND lv_dest IS INITIAL.
    WRITE: / '  ERROR: choose a Destination (path or catalog.schema.table) for the'.
    WRITE: / '         external target, or pick "DuckDB table (in-process)".'.
    ULINE. RETURN.
  ENDIF.
  " the DuckDB table the load actually fills: the target itself for D, else a
  " transient holding table that publish reads from and we drop afterwards.
  DATA(lv_local) = COND string( WHEN lv_kind = 'D' THEN lv_target
                                ELSE |{ lv_target }_stg| ).

  " ===== BW / native (ADBC) source branch (Open-SQL describe/parallel N/A) =====
  IF p_bw = abap_true.
    DATA(lv_native) = condense( CONV string( p_nfrom ) ).
    IF lv_native IS INITIAL.
      WRITE: / '  ERROR: enter the Native FROM source (e.g. "_SYS_BIC"."pkg/CV")'.
      WRITE: / '         when "BW/native source (ADBC)" is ticked.'. ULINE. RETURN.
    ENDIF.
    ULINE.
    WRITE: / '  erpl-rev  |  BW/native (ADBC) -> DuckDB'.
    ULINE.
    WRITE: / |  { 'Native source' WIDTH = 14 } : { lv_native }|.
    IF lv_kind = 'D'.
      WRITE: / |  { 'DuckDB target' WIDTH = 14 } : { lv_target }|.
    ELSE.
      WRITE: / |  { 'Target' WIDTH = 14 } : { COND string( WHEN lv_kind = 'P' THEN 'parquet' ELSE 'attached table' ) } -> { lv_dest }|.
    ENDIF.
    ULINE.
    DATA(lo_progb) = NEW lcl_prog( ).
    lo_progb->mv_label = |{ lv_native } -> { lv_local }|.
    DATA(ls_bw) = zcl_erpl_rev_util=>replicate_native(
                    iv_from = lv_native iv_target = lv_local
                    iv_init = CONV string( p_init ) iv_maxrows = p_maxrow
                    iv_batch = p_batch ii_progress = lo_progb ).
    IF ls_bw-error IS NOT INITIAL.
      WRITE: / |  NATIVE ERROR: { ls_bw-error }|. ULINE. RETURN.
    ENDIF.
    IF lv_kind <> 'D'.
      DATA(lv_bkind) = COND string( WHEN lv_kind = 'P' THEN 'PARQUET' ELSE 'TABLE' ).
      DATA(ls_bpub) = zcl_erpl_rev_util=>publish(
        iv_source = lv_local iv_kind = lv_bkind iv_dest = lv_dest
        iv_partition_by = COND string( WHEN lv_kind = 'P' THEN lv_part ELSE `` ) iv_mode = 'FULL' ).
      zcl_erpl_rev_util=>query( |DROP TABLE IF EXISTS { lv_local }| ).
      IF ls_bpub-error IS NOT INITIAL.
        WRITE: / |  PUBLISH ERROR ({ lv_bkind } -> { lv_dest }): { ls_bpub-error }|. ULINE. RETURN.
      ENDIF.
    ENDIF.
    WRITE: / '  RESULT'.
    WRITE: / |  { 'Rows replicated' WIDTH = 16 } : { ls_bw-rows_affected }|.
    WRITE: / |  { 'Duration' WIDTH = 16 } : { ls_bw-seconds }s|.
    IF p_verify = abap_true AND lv_kind <> 'P'.
      DATA(lv_brel) = COND string( WHEN lv_kind = 'D' THEN lv_target ELSE lv_dest ).
      DATA(ls_bc) = zcl_erpl_rev_util=>query( |SELECT count(*) AS c FROM { lv_brel }| ).
      WRITE: / |  { 'Verify' WIDTH = 16 } : target has { ls_bc-rows } (rep { ls_bw-rows_affected })|.
    ENDIF.
    ULINE. RETURN.
  ENDIF.

  " Resolve + validate the DDIC -> DuckDB mapping.
  DATA(ls_desc) = zcl_erpl_rev_util=>describe_table(
                    iv_tab = p_tab iv_target = lv_target iv_columns = lv_cols ).
  IF ls_desc-error IS NOT INITIAL.
    WRITE: / |ERROR: { ls_desc-error }|. RETURN.
  ENDIF.
  IF ls_desc-fields IS INITIAL.
    WRITE: / |ERROR: table { p_tab } not found / no fields|. RETURN.
  ENDIF.

  " Compact column summary: a DuckDB-type histogram instead of one line per
  " column (a 400-column table would otherwise flood the list).
  TYPES: BEGIN OF ty_h, type TYPE string, n TYPE i, END OF ty_h.
  DATA lt_h TYPE STANDARD TABLE OF ty_h WITH EMPTY KEY.
  LOOP AT ls_desc-fields INTO DATA(ls_f).
    READ TABLE lt_h ASSIGNING FIELD-SYMBOL(<h>) WITH KEY type = ls_f-duckdb_type.
    IF sy-subrc = 0. <h>-n = <h>-n + 1.
    ELSE. APPEND VALUE #( type = ls_f-duckdb_type n = 1 ) TO lt_h. ENDIF.
  ENDLOOP.
  SORT lt_h BY n DESCENDING.
  DATA lv_mix TYPE string.
  LOOP AT lt_h INTO DATA(ls_hh).
    lv_mix = COND #( WHEN lv_mix IS INITIAL THEN |{ ls_hh-type }:{ ls_hh-n }|
                     ELSE |{ lv_mix }, { ls_hh-type }:{ ls_hh-n }| ).
  ENDLOOP.

  " Up-front source count (same filter + CDS params) so progress + verify have a total.
  DATA lv_total TYPE i.
  DATA(lv_srcfrom) = zcl_erpl_rev_util=>source_from( iv_tab = p_tab iv_params = lv_cparm ).
  SPLIT lv_where AT |{ cl_abap_char_utilities=>newline }| INTO TABLE DATA(lt_w).
  DELETE lt_w WHERE table_line IS INITIAL.
  TRY.
      SELECT COUNT(*) FROM (lv_srcfrom) WHERE (lt_w) INTO @lv_total.
    CATCH cx_root.
      lv_total = 0.                       " a bad WHERE is reported by replicate()
  ENDTRY.
  IF p_maxrow > 0 AND ( lv_total = 0 OR p_maxrow < lv_total ). lv_total = p_maxrow. ENDIF.

  " ---- parallel resolution (auto partition column + auto job count) ----
  " replicate_parallel is always a full-load (heap + one PK build); it ignores
  " UPSERT/INSERT mode and the max-rows cap, so resolve here and note the overrides.
  DATA: lv_par  TYPE abap_bool,
        lv_pcol TYPE string,
        lv_jobs TYPE i.
  lv_par = p_par.
  IF lv_par = abap_true.
    lv_pcol = condense( to_upper( CONV string( p_pcol ) ) ).
    IF lv_pcol IS INITIAL.
      lv_pcol = to_upper( zcl_erpl_rev_util=>pick_partition_col( ls_desc-fields ) ).
    ENDIF.
    IF lv_pcol IS INITIAL.
      WRITE: / '  ERROR: parallel load needs a numeric partition column, but the'.
      WRITE: / '         table has none. Enter one in "Partition column", or'.
      WRITE: / '         uncheck "Parallel background jobs" for a serial load.'.
      ULINE. RETURN.
    ENDIF.
    lv_jobs = p_jobs.
    IF lv_jobs <= 0.
      lv_jobs = zcl_erpl_rev_util=>recommend_jobs(
                  iv_rows = lv_total
                  iv_cap  = zcl_erpl_rev_util=>max_batch_jobs( ) ).
    ENDIF.
    " 1 job (tiny table or capped) isn't worth the dispatch overhead -> serial.
    IF lv_jobs <= 1. lv_par = abap_false. ENDIF.
  ENDIF.

  DATA(lv_modetxt) = COND string(
    WHEN lv_par = abap_true THEN |parallel full-load, { lv_jobs } jobs on { lv_pcol }|
    WHEN p_trunc = abap_true THEN `full-load replace (INSERT)`
    ELSE |append ({ lv_mode })| ).

  " ---- header ----
  ULINE.
  WRITE: / '  erpl-rev  |  SAP -> DuckDB replication'.
  ULINE.
  WRITE: / |  { 'Source table' WIDTH = 14 } : { p_tab }|.
  IF lv_kind = 'D'.
    WRITE: / |  { 'DuckDB target' WIDTH = 14 } : { lv_target }|.
  ELSE.
    WRITE: / |  { 'Target' WIDTH = 14 } : { COND string( WHEN lv_kind = 'P' THEN 'parquet' ELSE 'attached table' ) } -> { lv_dest }|.
    WRITE: / |  { '' WIDTH = 14 }   (staged via local DuckDB table { lv_local }){ COND string( WHEN lv_kind = 'P' AND lv_part IS NOT INITIAL THEN |, partition by { lv_part }| ELSE `` ) }|.
  ENDIF.
  WRITE: / |  { 'Columns' WIDTH = 14 } : { lines( ls_desc-fields ) }  (key: { ls_desc-keys })|.
  IF ls_desc-added_keys IS NOT INITIAL.
    WRITE: / |  { '' WIDTH = 14 }   key cols auto-kept: { ls_desc-added_keys }|.
  ENDIF.
  WRITE: / |  { 'Column types' WIDTH = 14 } : { lv_mix }|.
  IF lv_cols IS NOT INITIAL.
    WRITE: / |  { 'Selected' WIDTH = 14 } : { lv_cols }|.
  ENDIF.
  WRITE: / |  { 'Filter (WHERE)' WIDTH = 14 } : { COND string( WHEN lv_where IS NOT INITIAL THEN lv_where ELSE `(all rows)` ) }|.
  IF p_init IS NOT INITIAL.
    WRITE: / |  { 'Init SQL' WIDTH = 14 } : { p_init }|.
  ENDIF.
  WRITE: / |  { 'Mode' WIDTH = 14 } : { lv_modetxt }, package { p_batch } rows|.
  WRITE: / |  { 'Source rows' WIDTH = 14 } : { lv_total }|.
  IF lv_par = abap_true AND p_maxrow > 0.
    WRITE: / |  { '' WIDTH = 14 }   (note: parallel load ignores the max-rows cap)|.
  ENDIF.
  ULINE.

  " Optional full column mapping (off by default).
  IF p_full = abap_true.
    WRITE: / '  Column                   DDIC     DuckDB type'.
    LOOP AT zcl_erpl_rev_util=>format_fields( ls_desc-fields ) INTO DATA(lv_line).
      WRITE: / '   ', lv_line.
    ENDLOOP.
    ULINE.
  ENDIF.

  " ---- replicate (progress streamed to status bar / job log) ----
  DATA(lo_prog) = NEW lcl_prog( ).
  lo_prog->mv_label = |{ p_tab } -> { lv_local }|.

  DATA ls_rep TYPE zcl_erpl_rev_util=>ty_repl.
  IF lv_par = abap_true.
    " coordinator submits N background workers, polls them, builds the PK once.
    ls_rep = zcl_erpl_rev_util=>replicate_parallel(
               iv_tab      = p_tab
               iv_target   = lv_local
               iv_part_col = lv_pcol
               iv_jobs     = lv_jobs
               iv_columns  = lv_cols
               iv_where    = lv_where
               iv_init     = CONV string( p_init )
               iv_batch    = p_batch
               iv_params   = lv_cparm
               ii_progress = lo_prog ).
  ELSE.
    ls_rep = zcl_erpl_rev_util=>replicate(
               iv_tab      = p_tab
               iv_target   = lv_local
               iv_init     = CONV string( p_init )
               iv_mode     = lv_mode
               iv_maxrows  = p_maxrow
               iv_columns  = lv_cols
               iv_where    = lv_where
               iv_batch    = p_batch
               iv_truncate = p_trunc
               iv_total    = lv_total
               iv_params   = lv_cparm
               iv_key_cols = lv_skey
               ii_progress = lo_prog ).
  ENDIF.
  IF ls_rep-error IS NOT INITIAL.
    WRITE: / |  REPLICATE ERROR: { ls_rep-error }|.
    ULINE. RETURN.
  ENDIF.

  " ---- publish to the external target, then drop the local holding table ----
  IF lv_kind <> 'D'.
    DATA(lv_kindname) = COND string( WHEN lv_kind = 'P' THEN 'PARQUET' ELSE 'TABLE' ).
    lo_prog->zif_erpl_rev_progress~note( iv_text = |publishing { ls_rep-rows_affected } rows to { lv_dest }...| ).
    DATA(ls_pub) = zcl_erpl_rev_util=>publish(
      iv_source       = lv_local
      iv_kind         = lv_kindname
      iv_dest         = lv_dest
      iv_partition_by = COND string( WHEN lv_kind = 'P' THEN lv_part ELSE `` )
      iv_mode         = 'FULL' ).
    zcl_erpl_rev_util=>query( |DROP TABLE IF EXISTS { lv_local }| ).   " cleanup staging
    IF ls_pub-error IS NOT INITIAL.
      WRITE: / |  PUBLISH ERROR ({ lv_kindname } -> { lv_dest }): { ls_pub-error }|.
      ULINE. RETURN.
    ENDIF.
  ENDIF.

  " ---- result ----
  DATA lv_rate TYPE i.
  IF ls_rep-seconds > 0. lv_rate = ls_rep-rows_affected / ls_rep-seconds. ENDIF.
  WRITE: / '  RESULT'.
  WRITE: / |  { 'Rows replicated' WIDTH = 16 } : { ls_rep-rows_affected }|.
  WRITE: / |  { 'Duration' WIDTH = 16 } : { ls_rep-seconds }s|.
  WRITE: / |  { 'Throughput' WIDTH = 16 } : { COND string(
              WHEN ls_rep-seconds > 0 THEN |{ lv_rate } rows/s| ELSE `(sub-second)` ) }|.

  " Verify: read the target back and compare its row count to the source. The
  " read-back relation depends on the target kind (native table / parquet file or
  " dataset glob / attached-catalog table).
  IF p_verify = abap_true.
    DATA(lv_relation) = SWITCH string( lv_kind
      WHEN 'D' THEN lv_target
      WHEN 'T' THEN lv_dest
      WHEN 'P' THEN COND string(
        WHEN lv_part IS NOT INITIAL THEN |read_parquet('{ lv_dest }/**/*.parquet')|
        ELSE |read_parquet('{ lv_dest }')| ) ).
    DATA(ls_cnt) = zcl_erpl_rev_util=>query( |SELECT count(*) AS c FROM { lv_relation }| ).
    IF ls_cnt-rows CS |"c":{ lv_total }|.
      WRITE: / |  { 'Verify' WIDTH = 16 } : OK (SAP { lv_total } = target { lv_total })|.
    ELSE.
      WRITE: / |  { 'Verify' WIDTH = 16 } : MISMATCH (SAP { lv_total }, target { ls_cnt-rows })|.
    ENDIF.
  ENDIF.

  " ---- delta: register the just-loaded target as incremental + optional cron ----
  " The full load above is the SEED; from here future runs load only what changed.
  IF p_dlt = abap_true.
    ULINE.
    IF lv_kind <> 'D'.
      WRITE: / '  DELTA: skipped — delta tracks a local DuckDB target' &&
               ' (set External target = DuckDB table).'.
    ELSE.
      DATA(lv_meth) = to_upper( condense( CONV string( p_dmeth ) ) ).
      DATA(ls_dd)   = zcl_erpl_rev_util=>describe_table( iv_tab = p_tab iv_target = lv_local ).
      " WATERMARK/INSERT_ONLY: seed the high-water from the current source max so the
      " first delta cycle only picks up changes made AFTER registration.
      DATA lv_wmv TYPE string.
      IF ( lv_meth = 'WATERMARK' OR lv_meth = 'INSERT_ONLY' ) AND p_dwm IS NOT INITIAL.
        DATA lv_mx TYPE p LENGTH 11 DECIMALS 7.
        DATA lt_mx TYPE string_table.
        APPEND |max( { p_dwm } )| TO lt_mx.
        TRY.
            SELECT SINGLE (lt_mx) FROM (p_tab) INTO @lv_mx.
            lv_wmv = condense( |{ lv_mx }| ).
          CATCH cx_root ##NO_HANDLER.
        ENDTRY.
      ENDIF.
      " extra JSON is method-specific: change-doc object for CHANGEDOC/INSERT_ONLY;
      " a parallel-reload hint {"jobs":N} for a SNAPSHOT with > 1 worker.
      DATA(lv_extra) = condense( CONV string( p_dxtra ) ).
      IF lv_meth = 'SNAPSHOT' AND p_djobs > 1.
        lv_extra = |\{ "jobs": { p_djobs } \}|.
      ENDIF.
      DATA(lv_rerr) = zcl_erpl_rev_delta=>register( VALUE #(
        target      = lv_local
        method      = lv_meth
        source_from = condense( CONV string( p_tab ) )
        keys        = ls_dd-keys
        chg_col     = condense( CONV string( p_dwm ) )
        wm_kind     = condense( CONV string( p_dwmk ) )
        wm_value    = lv_wmv
        cadence     = condense( CONV string( p_dcad ) )
        extra       = lv_extra ) ).
      IF lv_rerr IS INITIAL.
        WRITE: / |  DELTA: registered '{ lv_local }' as { lv_meth }| &&
                 | (keys { ls_dd-keys }, cadence { p_dcad }).|.
      ELSE.
        WRITE: / |  DELTA: register error: { lv_rerr }|.
      ENDIF.
      " Optional trigger-CDC tier: provision real DB triggers (delete-only) so the
      " PHYSICAL deletes the watermark/change-doc methods can't see are captured too.
      IF p_cdc = abap_true.
        DATA(lv_cerr) = zcl_erpl_rev_cdc=>provision(
          iv_target = lv_local
          iv_source = condense( CONV string( p_tab ) )
          iv_keys   = ls_dd-keys
          iv_mode   = 'DELETE_ONLY' ).
        IF lv_cerr IS INITIAL.
          WRITE: / |  CDC: provisioned trigger-CDC (delete-only) on { p_tab }.|.
        ELSE.
          WRITE: / |  CDC: provision error: { lv_cerr }|.
        ENDIF.
      ENDIF.
    ENDIF.
  ENDIF.
  IF p_dsch = abap_true.
    " One setting drives both: the background-job period = the chosen refresh interval.
    DATA(lv_jobmin) = zcl_erpl_rev_delta=>cadence_minutes( CONV string( p_dcad ) ).
    IF lv_jobmin > 0.
      WRITE: / |  DELTA: { zcl_erpl_rev_delta=>schedule( iv_minutes = lv_jobmin ) }|.
    ELSE.
      WRITE: / |  DELTA: refresh interval is 'manual' — no background job scheduled.|.
    ENDIF.
  ENDIF.
  ULINE.

*&---------------------------------------------------------------------*
*&      Form  help — F1 explanation popup for a Delta-tab field
*&---------------------------------------------------------------------*
FORM help USING iv_title TYPE clike iv_t1 TYPE clike iv_t2 TYPE clike iv_t3 TYPE clike.
  DATA lv_answer TYPE c LENGTH 1.
  CALL FUNCTION 'POPUP_TO_CONFIRM'
    EXPORTING titlebar              = CONV string( iv_title )
              text_question         = |{ iv_t1 }{ iv_t2 }{ iv_t3 }|
              display_cancel_button = abap_false
    IMPORTING answer                = lv_answer
    EXCEPTIONS OTHERS               = 0.
ENDFORM.

*&---------------------------------------------------------------------*
*&      Form  f4_table — value help for the source table
*&---------------------------------------------------------------------*
FORM f4_table.
  DATA lv_pat TYPE string.
  PERFORM read_screen_field USING 'P_TAB' CHANGING lv_pat.
  DATA(lt_hits) = zcl_erpl_rev_util=>search_tables( iv_pattern = lv_pat ).
  IF lt_hits IS INITIAL.
    MESSAGE |No tables match "{ lv_pat }"| TYPE 'S'.
    RETURN.
  ENDIF.

  " value_org='C': one column of table names; F4's own filter row searches it,
  " and dynprofield writes the pick straight back into P_TAB.
  DATA lt_vals TYPE STANDARD TABLE OF ddshretval.
  DATA lt_names TYPE STANDARD TABLE OF tabname.
  LOOP AT lt_hits INTO DATA(ls_h).
    APPEND ls_h-tabname TO lt_names.
  ENDLOOP.

  CALL FUNCTION 'F4IF_INT_TABLE_VALUE_REQUEST'
    EXPORTING  retfield        = 'TABNAME'
               dynpprog        = sy-cprog
               dynpnr          = sy-dynnr
               dynprofield     = 'P_TAB'
               value_org       = 'C'
    TABLES     value_tab       = lt_names
               return_tab      = lt_vals
    EXCEPTIONS parameter_error = 1
               no_values_found = 2
               OTHERS          = 3.
ENDFORM.

*&---------------------------------------------------------------------*
*&      Form  f4_columns — multi-pick the columns of the chosen table
*&---------------------------------------------------------------------*
FORM f4_columns USING iv_target TYPE csequence
                      iv_multi  TYPE abap_bool ##CALLED.
  DATA lv_tab TYPE string.
  PERFORM read_screen_field USING 'P_TAB' CHANGING lv_tab.
  " When invoked from another subscreen (e.g. the Delta tab's watermark column),
  " P_TAB isn't on the current dynpro, so DYNP_VALUES_READ returns nothing — fall
  " back to the global parameter, already transported when the tab was switched.
  IF lv_tab IS INITIAL.
    lv_tab = condense( CONV string( p_tab ) ).
  ENDIF.
  IF lv_tab IS INITIAL.
    MESSAGE 'Enter (or F4-pick) the source table first' TYPE 'S'.
    RETURN.
  ENDIF.
  DATA(lt_cols) = zcl_erpl_rev_util=>list_columns( lv_tab ).
  IF lt_cols IS INITIAL.
    MESSAGE |Table "{ lv_tab }" not found / has no fields| TYPE 'S'.
    RETURN.
  ENDIF.

  DATA lt_names TYPE STANDARD TABLE OF fieldname.
  LOOP AT lt_cols INTO DATA(ls_c).
    APPEND ls_c-name TO lt_names.
  ENDLOOP.

  " multiple_choice='X': the user ticks several rows; we join the picks into a
  " comma list and push it back. A single-pick caller (e.g. the watermark column)
  " passes iv_multi=space and gets exactly one value written.
  DATA lt_ret TYPE STANDARD TABLE OF ddshretval.
  CALL FUNCTION 'F4IF_INT_TABLE_VALUE_REQUEST'
    EXPORTING  retfield        = 'FIELDNAME'
               dynpprog        = sy-cprog
               dynpnr          = sy-dynnr
               dynprofield     = iv_target
               value_org       = 'C'
               multiple_choice = iv_multi
    TABLES     value_tab       = lt_names
               return_tab      = lt_ret
    EXCEPTIONS parameter_error = 1
               no_values_found = 2
               OTHERS          = 3.
  IF sy-subrc <> 0 OR lt_ret IS INITIAL.
    RETURN.
  ENDIF.

  DATA lt_pick TYPE string_table.
  LOOP AT lt_ret INTO DATA(ls_r).
    APPEND condense( CONV string( ls_r-fieldval ) ) TO lt_pick.
  ENDLOOP.
  DATA(lv_joined) = concat_lines_of( table = lt_pick sep = `, ` ).

  " write the joined list back into the requested screen field.
  DATA lt_upd TYPE STANDARD TABLE OF dynpread.
  APPEND VALUE dynpread( fieldname = iv_target fieldvalue = lv_joined ) TO lt_upd.
  CALL FUNCTION 'DYNP_VALUES_UPDATE'
    EXPORTING  dyname     = sy-cprog
               dynumb     = sy-dynnr
    TABLES     dynpfields = lt_upd
    EXCEPTIONS OTHERS     = 1.
ENDFORM.

*&---------------------------------------------------------------------*
*&      Form  read_screen_field — current on-screen value of a field
*&---------------------------------------------------------------------*
FORM read_screen_field USING iv_field TYPE csequence
                       CHANGING rv_value TYPE string ##CALLED.
  DATA lt TYPE STANDARD TABLE OF dynpread.
  APPEND VALUE dynpread( fieldname = iv_field ) TO lt.
  CALL FUNCTION 'DYNP_VALUES_READ'
    EXPORTING  dyname               = sy-cprog
               dynumb               = sy-dynnr
    TABLES     dynpfields           = lt
    EXCEPTIONS OTHERS               = 1.
  IF sy-subrc = 0.
    READ TABLE lt INTO DATA(ls) INDEX 1.
    IF sy-subrc = 0.
      rv_value = condense( CONV string( ls-fieldvalue ) ).
    ENDIF.
  ENDIF.
ENDFORM.

*&---------------------------------------------------------------------*
*&      Form  f4_nfrom — browse HANA catalog (views + functions) via ADBC
*&---------------------------------------------------------------------*
FORM f4_nfrom ##CALLED.
  DATA lv_pat TYPE string.
  PERFORM read_screen_field USING 'P_NFROM' CHANGING lv_pat.
  REPLACE ALL OCCURRENCES OF '"' IN lv_pat WITH ''.
  DATA(lv_like) = |%{ to_upper( condense( lv_pat ) ) }%|.
  TYPES: BEGIN OF ty_nf, nfrom TYPE c LENGTH 130, END OF ty_nf.
  DATA lt_v TYPE STANDARD TABLE OF ty_nf.
  TRY.
      DATA(lv_q) =
        |SELECT SCHEMA_NAME, VIEW_NAME AS OBJ_NAME FROM SYS.VIEWS WHERE VIEW_NAME LIKE '{ lv_like }'|
        && | UNION ALL SELECT SCHEMA_NAME, FUNCTION_NAME AS OBJ_NAME FROM SYS.FUNCTIONS WHERE FUNCTION_NAME LIKE '{ lv_like }'|
        && | ORDER BY 1, 2 LIMIT 200|.
      DATA(lo_r) = NEW cl_sql_statement( )->execute_query( lv_q ).
      DATA: BEGIN OF ls, schema_name TYPE c LENGTH 100, obj_name TYPE c LENGTH 130, END OF ls.
      GET REFERENCE OF ls INTO DATA(lr).
      lo_r->set_param_struct( lr ).
      WHILE lo_r->next( ) > 0.
        APPEND VALUE ty_nf(
          nfrom = |"{ condense( ls-schema_name ) }"."{ condense( ls-obj_name ) }"| ) TO lt_v.
      ENDWHILE.
      lo_r->close( ).
    CATCH cx_root INTO DATA(lx).
      MESSAGE |HANA catalog query failed: { lx->get_text( ) }| TYPE 'S'. RETURN.
  ENDTRY.
  IF lt_v IS INITIAL. MESSAGE 'No HANA views/functions match' TYPE 'S'. RETURN. ENDIF.
  DATA lt_ret TYPE STANDARD TABLE OF ddshretval.
  CALL FUNCTION 'F4IF_INT_TABLE_VALUE_REQUEST'
    EXPORTING  retfield    = 'NFROM'
               dynpprog    = sy-cprog
               dynpnr      = sy-dynnr
               dynprofield = 'P_NFROM'
               value_org   = 'C'
    TABLES     value_tab   = lt_v
               return_tab  = lt_ret
    EXCEPTIONS OTHERS      = 3.
ENDFORM.
