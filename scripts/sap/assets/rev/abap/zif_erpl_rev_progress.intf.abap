INTERFACE zif_erpl_rev_progress PUBLIC.
  "! Progress callback for zcl_erpl_rev_util=>replicate: invoked once per ingested
  "! package so callers can report live progress (status bar / background job log)
  "! without coupling the util to any UI. iv_total = 0 when the total is unknown.
  METHODS tick
    IMPORTING iv_rows    TYPE i   " rows ingested so far
              iv_total   TYPE i   " expected total (0 = unknown)
              iv_seconds TYPE p.  " elapsed whole seconds so far

  "! Free-text status line (status bar / job log). Used by replicate_parallel,
  "! whose coordinator BLOCKS the dialog while polling background jobs — without
  "! this the screen looks frozen and users re-trigger the load. iv_done/iv_total
  "! drive the progress-bar fraction (0/0 = indeterminate, e.g. "building PK").
  METHODS note
    IMPORTING iv_text  TYPE string
              iv_done  TYPE i DEFAULT 0
              iv_total TYPE i DEFAULT 0.
ENDINTERFACE.
