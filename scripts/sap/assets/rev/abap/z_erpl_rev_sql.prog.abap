*&---------------------------------------------------------------------*
*& Report  Z_ERPL_REV_SQL
*&---------------------------------------------------------------------*
*& erpl-rev: DuckDB SQL console. Type one or more ';'-separated
*& statements (e.g. INSTALL httpfs; LOAD httpfs; SELECT ...) into the
*& TextEdit pane, press the Execute button, and the LAST result set is
*& shown in the ALV pane below. All work is delegated to
*& zcl_erpl_rev_util / the C++ RFC server; this report is the GUI shell.
*&
*& NO Screen Painter / dynpro needed: the UI is a CL_GUI_DOCKING_CONTAINER
*& + CL_GUI_SPLITTER_CONTAINER hosted on the standard selection screen and
*& built in AT SELECTION-SCREEN OUTPUT — so it is created/activated entirely
*& via erpl-adt. (Controls only RENDER in the SAP GUI; run via SA38 -> F8.)
*&---------------------------------------------------------------------*
REPORT z_erpl_rev_sql.

TYPE-POOLS vrm.   " VRM_SET_VALUES listbox types (vrm_values) for the example dropdown

TYPES: gtt_txt    TYPE STANDARD TABLE OF char255 WITH EMPTY KEY,
       ty_demokey TYPE c LENGTH 2.   " example-dropdown key ('00'..'06')

DATA: go_dock   TYPE REF TO cl_gui_docking_container,
      go_split  TYPE REF TO cl_gui_splitter_container,
      go_top    TYPE REF TO cl_gui_container,
      go_bottom TYPE REF TO cl_gui_container,
      go_editor TYPE REF TO cl_gui_textedit,
      go_salv   TYPE REF TO cl_salv_table,
      go_msg    TYPE REF TO cl_gui_textedit,
      gr_result TYPE REF TO data,        " dynamic result table (kept alive)
      gt_sql    TYPE gtt_txt,            " current editor content
      gt_gen    TYPE gtt_txt,            " generated ABAP snippet (shown instead of the grid)
      gv_err    TYPE string,
      gv_total  TYPE i,                  " rows fetched into the grid
      gv_truncated TYPE abap_bool,       " capped before the result was exhausted
      gv_info   TYPE string.

" One toolbar line: [Execute] [Generate]  Examples: <picker>.
" The example picker doubles as the field that keeps this screen alive: a
" selection screen with no input field is auto-skipped (AT SELECTION-SCREEN
" OUTPUT never fires, nothing renders); p_demo is a PARAMETERS field, so no
" separate dummy parameter is needed.
SELECTION-SCREEN BEGIN OF LINE.
SELECTION-SCREEN PUSHBUTTON  1(30) b_exec USER-COMMAND exec.
SELECTION-SCREEN PUSHBUTTON 33(30) b_gen  USER-COMMAND gen.
SELECTION-SCREEN COMMENT    65(9) c_demo FOR FIELD p_demo.
PARAMETERS p_demo TYPE ty_demokey AS LISTBOX VISIBLE LENGTH 45
           USER-COMMAND demo DEFAULT '00'.
SELECTION-SCREEN END OF LINE.

INITIALIZATION.
  b_exec = 'Execute DuckDB SQL'.
  b_gen  = 'Generate ABAP snippet'.
  c_demo = 'Examples'.
  gt_sql = VALUE #( ( |SELECT 42 AS answer, 'hello duckdb' AS msg| ) ).

  " Populate the example dropdown. Picking an entry (USER-COMMAND DEMO) loads the
  " matching query into the editor; the user then presses Execute.
  DATA(lt_demo) = VALUE vrm_values(
    ( key = '00' text = 'Load an example query...' )
    ( key = '01' text = '1. DuckDB friendly SQL (synthetic, no network)' )
    ( key = '02' text = '2. NYC taxi - 12 monthly Parquet files, 40M+ rows (HTTPS)' )
    ( key = '03' text = '3. MotherDuck - SUMMARIZE sample taxi' )
    ( key = '04' text = '4. MotherDuck - top Hacker News stories' )
    ( key = '05' text = '5. MotherDuck - list sample_data tables' )
    ( key = '06' text = '6. MotherDuck - push a table to your instance' ) ).
  CALL FUNCTION 'VRM_SET_VALUES'
    EXPORTING id = 'P_DEMO' values = lt_demo.

*&---------------------------------------------------------------------*
*&  PAI: on Execute, read the editor, run the script, stash the result,
*&  then drop the container so PBO rebuilds the panes with fresh data
*&  (rebuilding side-steps changing the ALV's column structure in place).
*&---------------------------------------------------------------------*
AT SELECTION-SCREEN.
  " Example dropdown: load the chosen query into the editor and rebuild the panes.
  IF sy-ucomm = 'DEMO'.
    IF p_demo = '00'. RETURN. ENDIF.   " the prompt row is a no-op
    PERFORM load_demo USING p_demo.
    CLEAR: gr_result, gv_err, gv_info, gv_total, gt_gen, gv_truncated.
    gv_info = |Example loaded. Edit if you like, then press "Execute DuckDB SQL".|.
    PERFORM reset_ui.
    RETURN.
  ENDIF.
  IF sy-ucomm = 'EXEC' OR sy-ucomm = 'GEN'.
    " Read the current editor content (both actions work on it).
    IF go_editor IS BOUND.
      " get_text_as_r3table declares CLASSIC exceptions; if none are mapped, a
      " raised one (notably POTENTIAL_DATA_LOSS) becomes an uncaught short dump.
      " POTENTIAL_DATA_LOSS still returns the text (it only warns that very long
      " lines could be truncated into the char255 table), so we ignore it and
      " only treat real transport errors as failures.
      go_editor->get_text_as_r3table(
        IMPORTING table               = gt_sql
        EXCEPTIONS potential_data_loss = 0
                   error_dp            = 1
                   error_dp_create     = 2
                   OTHERS              = 3 ).
      IF sy-subrc <> 0.
        MESSAGE |Could not read SQL editor (rc={ sy-subrc })| TYPE 'I'.
        RETURN.
      ENDIF.
    ENDIF.
    CLEAR: gr_result, gv_err, gv_info, gv_total, gt_gen.

    IF sy-ucomm = 'GEN'.
      " Generate a ready-to-paste ABAP snippet that runs THIS query. To make the
      " loop concrete (a named field-symbol per column), introspect the result
      " columns — but ONLY for a read-only statement, so Generate never executes DML.
      " Join with a newline (NOT a space): a space would fold the editor's lines
      " into one, turning every '--' line comment into a comment that swallows the
      " rest of the query. DuckDB parses the multi-line text directly.
      DATA(lv_gsql) = concat_lines_of( table = gt_sql sep = cl_abap_char_utilities=>newline ).
      DATA(lv_head) = to_upper( lv_gsql ).
      SHIFT lv_head LEFT DELETING LEADING ` `.
      DATA lv_cols TYPE string.
      IF lv_head CP 'SELECT*' OR lv_head CP 'WITH*'   OR lv_head CP 'FROM*'
        OR lv_head CP 'DESCRIBE*' OR lv_head CP 'SHOW*' OR lv_head CP 'PRAGMA*'
        OR lv_head CP 'VALUES*'   OR lv_head CP 'TABLE*'.
        DATA(ls_probe) = zcl_erpl_rev_util=>query_stream( iv_sql = lv_gsql iv_maxrows = 1 ).
        IF ls_probe-error IS INITIAL. lv_cols = ls_probe-columns. ENDIF.
      ENDIF.
      PERFORM gen_abap USING lv_cols.
    ELSE.
      " Newline join (see GEN above): preserves '--' line comments and multi-line
      " statements instead of folding everything onto one comment-killed line.
      DATA(lv_sql) = concat_lines_of( table = gt_sql sep = cl_abap_char_utilities=>newline ).
      " Stream the result via the cursor FMs (fixed memory, binary sXML pages).
      " Cap what the grid holds so a multi-million-row SELECT can't blow up the
      " front end; the server stops pulling once the cap is reached.
      CONSTANTS c_display_cap TYPE i VALUE 100000.
      DATA(ls_res) = zcl_erpl_rev_util=>query_stream(
                       iv_sql = lv_sql iv_maxrows = c_display_cap ).
      IF ls_res-error IS NOT INITIAL.
        gv_err = ls_res-error.
      ELSE.
        gr_result   = ls_res-data.
        gv_total    = ls_res-row_count.
        gv_truncated = ls_res-truncated.
        gv_info     = |{ ls_res-row_count } row(s)|.
        IF gr_result IS NOT BOUND.
          gv_info = |statement ok, no result set ({ gv_info })|.
        ENDIF.
      ENDIF.
    ENDIF.

    PERFORM reset_ui.
  ENDIF.

*&---------------------------------------------------------------------*
*&  PBO: build the docking container with a TextEdit (top) and either the
*&  ALV result or an error/info TextEdit (bottom).
*&---------------------------------------------------------------------*
AT SELECTION-SCREEN OUTPUT.
  IF go_dock IS INITIAL.
    go_dock  = NEW #( repid   = sy-repid dynnr = sy-dynnr
                      side    = cl_gui_docking_container=>dock_at_bottom
                      caption = 'DuckDB SQL console'
                      ratio   = 88 ).
    go_split = NEW #( parent = go_dock rows = 2 columns = 1 ).
    go_split->set_row_height( id = 1 height = 35 ).
    go_top    = go_split->get_container( row = 1 column = 1 ).
    go_bottom = go_split->get_container( row = 2 column = 1 ).

    " Top pane: editable SQL editor, seeded with the current script.
    go_editor = NEW #( parent = go_top
                       wordwrap_mode = cl_gui_textedit=>wordwrap_off ).
    go_editor->set_text_as_r3table( EXPORTING table = gt_sql
                                    EXCEPTIONS OTHERS = 0 ).

    " Bottom pane: the generated ABAP snippet (after Generate), else the result
    " grid, else the error / info message — all read-only.
    IF gt_gen IS NOT INITIAL.
      " Read-only, no word-wrap: select all (Ctrl+A) + copy (Ctrl+C) the snippet.
      go_msg = NEW #( parent = go_bottom wordwrap_mode = cl_gui_textedit=>wordwrap_off ).
      go_msg->set_readonly_mode( 1 ).
      go_msg->set_text_as_r3table( EXPORTING table = gt_gen EXCEPTIONS OTHERS = 0 ).
    ELSEIF gr_result IS BOUND.
      FIELD-SYMBOLS <t> TYPE STANDARD TABLE.
      ASSIGN gr_result->* TO <t>.
      TRY.
          cl_salv_table=>factory( EXPORTING r_container  = go_bottom
                                  IMPORTING r_salv_table = go_salv
                                  CHANGING  t_table      = <t> ).
          " Dynamic (RTTS) columns have no DDIC text, so SALV shows blank
          " headers. Set each column's heading from its own name (= the DuckDB
          " column name). scrtext_s/m/l cap at 10/20/40 chars; set all three so
          " SALV always has a label to show whatever the column width allows.
          DATA(lo_cols) = go_salv->get_columns( ).
          lo_cols->set_optimize( ).
          LOOP AT lo_cols->get( ) INTO DATA(ls_colref).
            DATA(lv_h) = CONV string( ls_colref-columnname ).
            ls_colref-r_column->set_short_text(  CONV scrtext_s( lv_h ) ).
            ls_colref-r_column->set_medium_text( CONV scrtext_m( lv_h ) ).
            ls_colref-r_column->set_long_text(   CONV scrtext_l( lv_h ) ).
          ENDLOOP.
          " Title flags a capped (streamed-and-truncated) result.
          FIELD-SYMBOLS <rt> TYPE STANDARD TABLE.
          ASSIGN gr_result->* TO <rt>.
          DATA(lv_shown) = lines( <rt> ).
          DATA(lv_title) = COND lvc_title(
            WHEN gv_truncated = abap_true
            THEN |DuckDB: first { lv_shown } rows (more available)|
            ELSE |DuckDB: { lv_shown } row(s)| ).
          go_salv->get_display_settings( )->set_list_header( lv_title ).
          go_salv->get_functions( )->set_all( ).
          go_salv->display( ).
        CATCH cx_salv_msg INTO DATA(lx).
          gv_err = lx->get_text( ).
      ENDTRY.
    ENDIF.

    IF gt_gen IS INITIAL AND gr_result IS NOT BOUND.
      DATA(lv_msg) = COND string( WHEN gv_err IS NOT INITIAL
                                  THEN |ERROR: { gv_err }|
                                  ELSE COND string( WHEN gv_info IS NOT INITIAL
                                                    THEN gv_info
                                                    ELSE |Enter SQL above; "Execute" runs it, "Generate ABAP snippet" emits paste-ready code.| ) ).
      go_msg = NEW #( parent = go_bottom ).
      go_msg->set_readonly_mode( 1 ).
      go_msg->set_text_as_r3table( EXPORTING table = VALUE gtt_txt( ( CONV char255( lv_msg ) ) )
                                   EXCEPTIONS OTHERS = 0 ).
    ENDIF.
  ENDIF.

*&---------------------------------------------------------------------*
*&  Build a ready-to-paste ABAP snippet that runs the CURRENT SQL via
*&  erpl-rev and reads the typed result table. Lines go into gt_gen.
*&---------------------------------------------------------------------*
FORM gen_abap USING iv_cols TYPE string.
  " Keep only the non-empty SQL lines so the literal isn't padded with blanks.
  DATA lt_lines TYPE gtt_txt.
  LOOP AT gt_sql INTO DATA(lv_ln).
    IF lv_ln IS NOT INITIAL. APPEND lv_ln TO lt_lines. ENDIF.
  ENDLOOP.
  IF lt_lines IS INITIAL. APPEND |SELECT 1 AS x| TO lt_lines. ENDIF.

  CLEAR gt_gen.
  APPEND |* erpl-rev: run a DuckDB query and read its typed result.| TO gt_gen.
  APPEND |* Generated by Z_ERPL_REV_SQL. Needs the erpl-rev RFC server + destination ERPL_REV.| TO gt_gen.
  APPEND || TO gt_gen.

  " The SQL as a multi-line string template:  |line1 | && |line2 | && ... |lineN|.
  " Each line is escaped for |...| ( \ | { } -> \\ \| \{ \} ) and gets a trailing
  " space so tokens don't merge across the concatenation.
  DATA(lv_n) = lines( lt_lines ).
  APPEND |  DATA(lv_sql) =| TO gt_gen.
  LOOP AT lt_lines INTO lv_ln.
    DATA(lv_e) = lv_ln.
    REPLACE ALL OCCURRENCES OF `\` IN lv_e WITH `\\`.
    REPLACE ALL OCCURRENCES OF `|` IN lv_e WITH `\|`.
    REPLACE ALL OCCURRENCES OF `{` IN lv_e WITH `\{`.
    REPLACE ALL OCCURRENCES OF `}` IN lv_e WITH `\}`.
    IF sy-tabix < lv_n.
      APPEND |    \|{ lv_e } \| &&| TO gt_gen.
    ELSE.
      APPEND |    \|{ lv_e }\|.| TO gt_gen.
    ENDIF.
  ENDLOOP.
  APPEND || TO gt_gen.

  APPEND |  DATA(ls_res) = zcl_erpl_rev_util=>query_stream( iv_sql = lv_sql ).| TO gt_gen.
  APPEND |  IF ls_res-error IS NOT INITIAL.| TO gt_gen.
  APPEND |    MESSAGE ls_res-error TYPE 'I'.   " the query failed| TO gt_gen.
  APPEND |  ELSEIF ls_res-data IS BOUND.| TO gt_gen.
  APPEND |    FIELD-SYMBOLS <tab> TYPE STANDARD TABLE.| TO gt_gen.
  APPEND |    ASSIGN ls_res-data->* TO <tab>.   " a STANDARD TABLE typed from the result columns| TO gt_gen.
  APPEND |    LOOP AT <tab> ASSIGNING FIELD-SYMBOL(<row>).| TO gt_gen.

  " Concrete loop body: one named field-symbol per result column (introspected via a
  " 1-row probe). Falls back to a generic comment when the columns aren't known
  " (e.g. a non-SELECT statement, where we deliberately did NOT run the SQL).
  TYPES: BEGIN OF ty_col, name TYPE string, type TYPE string, END OF ty_col.
  DATA lt_cols TYPE STANDARD TABLE OF ty_col WITH EMPTY KEY.
  IF iv_cols IS NOT INITIAL.
    TRY.
        /ui2/cl_json=>deserialize( EXPORTING json = iv_cols CHANGING data = lt_cols ).
      CATCH cx_root ##NO_HANDLER.
    ENDTRY.
  ENDIF.

  IF lt_cols IS NOT INITIAL.
    " widest column name, so the ').' and the type comment line up.
    DATA lv_w TYPE i.
    LOOP AT lt_cols INTO DATA(ls_c).
      IF strlen( ls_c-name ) > lv_w. lv_w = strlen( ls_c-name ). ENDIF.
    ENDLOOP.
    DATA lt_fs TYPE string_table.
    LOOP AT lt_cols INTO ls_c.
      DATA(lv_fs)  = to_lower( ls_c-name ).
      " padding OUTSIDE the quotes/FS so the component name itself stays exact.
      DATA(lv_pad) = repeat( val = ` ` occ = lv_w - strlen( ls_c-name ) ).
      APPEND |      ASSIGN COMPONENT '{ ls_c-name }'{ lv_pad } OF STRUCTURE <row> | &&
             |TO FIELD-SYMBOL(<{ lv_fs }>){ lv_pad }.   " { ls_c-type }| TO gt_gen.
      APPEND |<{ lv_fs }>| TO lt_fs.
    ENDLOOP.
    " a ready example using the first few field-symbols.
    DATA lv_eg TYPE string.
    DATA lv_i  TYPE i.
    LOOP AT lt_fs INTO DATA(lv_one).
      lv_i = lv_i + 1.
      IF lv_i > 4. EXIT. ENDIF.
      lv_eg = COND #( WHEN lv_eg IS INITIAL THEN lv_one ELSE |{ lv_eg }, { lv_one }| ).
    ENDLOOP.
    APPEND |      " e.g.  WRITE: / { lv_eg }.| TO gt_gen.
  ELSE.
    APPEND |      " <row> has one component per SELECT column (= the DuckDB column names);| TO gt_gen.
    APPEND |      " read them with  ASSIGN COMPONENT 'COLNAME' OF STRUCTURE <row> TO FIELD-SYMBOL(<c>).| TO gt_gen.
  ENDIF.

  APPEND |    ENDLOOP.| TO gt_gen.
  APPEND |  ENDIF.| TO gt_gen.
ENDFORM.

*&---------------------------------------------------------------------*
*&  Drop the container so PBO rebuilds the panes with fresh state.
*&---------------------------------------------------------------------*
FORM reset_ui.
  IF go_dock IS BOUND.
    go_dock->free( ).
    CLEAR: go_dock, go_split, go_top, go_bottom, go_editor, go_salv, go_msg.
  ENDIF.
ENDFORM.

*&---------------------------------------------------------------------*
*&  Load one of the bundled example queries into the editor buffer.
*&  A small gallery of "impressive" DuckDB queries: a multi-file NYC-taxi
*&  aggregate over a year of public Parquet (12 files, 40M+ rows),
*&  MotherDuck's shared sample_data sets, and a push-a-table-to-MotherDuck
*&  round-trip. The MotherDuck ones need the server booted with
*&  `... ATTACH 'md:'` (token via motherduck_token).
*&---------------------------------------------------------------------*
FORM load_demo USING iv_key TYPE c.
  CASE iv_key.
    WHEN '01'.   " DuckDB 'friendly SQL' — pure, no network
      gt_sql = VALUE #(
        ( |-- DuckDB 'friendly SQL': generate a year of data, GROUP BY ALL, no boilerplate.| )
        ( |SELECT| )
        ( |    monthname(d)                                      AS month,| )
        ( |    count(*)                                          AS days,| )
        ( |    round(avg(15 + 12 * sin(dayofyear(d) / 58.0)), 1) AS avg_temp_c,| )
        ( |    round(min(15 + 12 * sin(dayofyear(d) / 58.0)), 1) AS coldest,| )
        ( |    round(max(15 + 12 * sin(dayofyear(d) / 58.0)), 1) AS hottest| )
        ( |FROM range(TIMESTAMP '2024-01-01', TIMESTAMP '2025-01-01', INTERVAL 1 DAY) t(d)| )
        ( |GROUP BY ALL| )
        ( |ORDER BY min(d);| ) ).
    WHEN '02'.   " NYC taxi over MANY public Parquet files (httpfs, needs internet egress)
      gt_sql = VALUE #(
        ( |-- 40M+ NYC taxi trips across ALL 12 monthly Parquet files of 2024.| )
        ( |-- read_parquet takes the whole LIST of remote files; union_by_name aligns| )
        ( |-- the per-file schemas. count(*) is answered from Parquet METADATA - no row| )
        ( |-- scan, no download - so it returns in seconds (a full aggregate over 40M| )
        ( |-- remote rows would block the GUI for minutes; keep heavy scans out of the| )
        ( |-- synchronous console). DuckDB auto-loads httpfs; files are the NYC TLC dataset.| )
        ( |SELECT count(*) AS trips_2024| )
        ( |FROM read_parquet(| )
        ( |    list_transform(range(1, 13),| )
        ( |        lambda m: printf('https://d37ci6vzurychx.cloudfront.net/trip-data/yellow_tripdata_2024-%02d.parquet', m)),| )
        ( |    union_by_name = true);| ) ).
    WHEN '03'.   " MotherDuck shared sample_data — SUMMARIZE (schema-agnostic)
      gt_sql = VALUE #(
        ( |-- MotherDuck: every account has a shared read-only 'sample_data' database.| )
        ( |-- Needs the server booted with: INSTALL motherduck; LOAD motherduck; ATTACH 'md:';| )
        ( |-- SUMMARIZE profiles every column (stats, nulls, cardinality) - any schema.| )
        ( |SUMMARIZE sample_data.nyc.taxi;| ) ).
    WHEN '04'.   " MotherDuck Hacker News
      gt_sql = VALUE #(
        ( |-- Top Hacker News stories of all time (MotherDuck shared sample_data).| )
        ( |-- Needs the server attached to MotherDuck (ATTACH 'md:').| )
        ( |SELECT title, score, "by" AS author, to_timestamp(time) AS posted| )
        ( |FROM sample_data.hn.hacker_news| )
        ( |WHERE type = 'story' AND title IS NOT NULL| )
        ( |ORDER BY score DESC| )
        ( |LIMIT 50;| ) ).
    WHEN '05'.   " Discover MotherDuck's shared sample tables
      gt_sql = VALUE #(
        ( |-- Discover what is inside MotherDuck's shared sample database.| )
        ( |-- Needs the server attached to MotherDuck (ATTACH 'md:').| )
        ( |SELECT database, schema, name| )
        ( |FROM (SHOW ALL TABLES)| )
        ( |WHERE database = 'sample_data'| )
        ( |ORDER BY schema, name;| ) ).
    WHEN '06'.   " Push a table into YOUR MotherDuck instance
      gt_sql = VALUE #(
        ( |-- Push a table to YOUR MotherDuck instance (needs ATTACH 'md:').| )
        ( |-- Replace my_db with one of your MotherDuck databases.| )
        ( |CREATE OR REPLACE TABLE _erpl_demo AS| )
        ( |    SELECT i AS id, concat('item_', i) AS name, (i * 7) % 1000 AS qty| )
        ( |    FROM range(1, 1001) t(i);| )
        ( |CREATE OR REPLACE TABLE my_db.main.erpl_demo AS SELECT * FROM _erpl_demo;| )
        ( |SELECT count(*) AS pushed, min(id) AS lo, max(id) AS hi FROM my_db.main.erpl_demo;| ) ).
  ENDCASE.
ENDFORM.
