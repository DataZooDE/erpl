# Changelog

All notable changes to ERPL are documented here. Versions follow `vYYYY.MM.DD`
(the date the binary set was cut). Same-day re-cuts append `.N`.

Each bullet is tagged with the affected sub-extension(s):

- **[rfc]** — `erpl_rfc`, the SAP RFC scan path
- **[bics]** — `erpl_bics`, BW BICS queries (lives in a git submodule)
- **[odp]** — `erpl_odp`, ODP data replication (lives in a git submodule)
- **[trampoline]** — `erpl`, the umbrella extension that bundles the SAP SDK
- **[all]** — cross-cutting work that touches every sub-extension

Binaries are self-distributed via [get.erpl.io](http://get.erpl.io) for the
matrix `{linux_amd64, linux_amd64_musl, osx_amd64, osx_arm64, windows_amd64} ×
{DuckDB v1.4.5, v1.5.5}`. Install with:

```sql
INSTALL erpl FROM 'http://get.erpl.io';
LOAD erpl;
```

---

## v2026.09.04 — scans that can be run twice, and a backend with no known gaps

`sap_read_table` gains row-range partitioning, and the three extensions gain one tuning
vocabulary. The reason this is one release rather than two is the second half of the list:
**four separate silent wrong-results defects**, all in released erpl, all returning data
rather than failing.

The sharpest is that a re-scanned `sap_read_table` returned nothing at all — a prepared
statement, a nested-loop join or an un-materialised CTE referenced twice would silently
answer zero rows on the second scan. If you use erpl from a prepared statement or in a
join, upgrade.

The `erpl-proto` backend also passes every suite with no recorded gaps for the first time.

### Added

- **[rfc]** **`sap_read_table` can now split a scan by rows.** Parallelism was per
  *column* — one concurrent `RFC_READ_TABLE` call per projected column — so a narrow
  extract got none at all, and raising `threads` did nothing. `partitions` hands each
  worker a row window read with `ROWSKIPS`/`ROWCOUNT`. Measured on a 164,664-row
  single-column extract: 2.23s unpartitioned, 0.83s at eight partitions (**2.7x**),
  with the knee visible at eight. Opt-in, because partitioned workers finish in
  whatever order they finish while an unpartitioned scan returns rows sorted.

- **[rfc]** `erpl_rfc_partitions` and `erpl_rfc_partition_window_rows`, plus a
  `partitions` named parameter.

- **[rfc]** **One tuning vocabulary across the extensions.** Paging and parallelism were
  spelled differently everywhere: RFC had `THREADS` plus `erpl_rfc_read_table_batch_budget`,
  ODP had `threads` and no settings at all, BICS had neither. `threads` /
  `erpl_rfc_max_threads` and `fetch_size` / `erpl_rfc_fetch_size` now mean the same thing
  wherever they appear, and `fetch_size` is settable per query rather than per session.
  No breaking renames — `erpl_rfc_read_table_batch_budget` still works and writes the same
  value.

- **[odp]** `erpl_odp_fetch_size` and `erpl_odp_max_threads`, ODP's first settings.
  `I_MAXPACKAGESIZE` was compiled in at 2 MiB and reachable from nowhere, so round-trips
  could not be traded against per-packet memory.

- **[bics]** `sap_bics_begin` honours its `rows`, `columns` and `filters` parameters.
  They were declared and never read — accepted with no error and no effect. Building a
  query this way also costs three fewer full session round-trips.

- **[rfc]** `sap_rfc_live_connections()`, `sap_rfc_connections_opened()` and
  `sap_rfc_connections_closed()` report how many SAP RFC connections erpl has opened,
  closed and still holds. Between queries the live count should be 0 — a non-zero value
  means SAP sessions are still reserved, which client-side timing cannot reveal.

### Fixed

- **[odp]** **`PRAGMA sap_odp_close_delta_cursor` never closed anything.** It matched the
  cursor on `(SUBSCRIBER_PROC, QUEUENAME)` and took the first row, but SAP retains every
  closed cursor as history, so that pair routinely has a dozen rows — the open one was
  found at position 7 behind six closed rows from previous days. The pragma reported
  `'CLOSED'` without ever calling `RODPS_REPL_ODP_CLOSE`, so delta cursors accumulated
  server-side while cleanup looked like it was working.

  Two more defects sat in the branch that made unreachable: every exception was reported
  as `'CLOSED'`, and the call's `ET_RETURN` was never read — `RODPS_REPL_ODP_CLOSE` returns
  `RFC_OK` even when it refuses, so a rejected close was indistinguishable from a
  successful one. **Behaviour change:** two new results, neither of which raises —
  `'REFUSED: <SAP message>'` when SAP rejects the close (typically
  `ILLEGAL_REQ_STATE_FOR_CONFIRM`, meaning the request was left mid-fetch and only
  `sap_odp_drop` will clear it) and `'STILL_OPEN'` when the cursor survives a call that
  reported no error.

- **[odp]** `sap_odp_read_full` crashed at `threads := 8` on a source with fewer packages
  than workers — a null local state, dereferenced unconditionally.

- **[bics]** The result-size refusal message stated what a query would really cost. BICS
  memory scales with result **rows**, not data cells, so the cell-derived figure it used
  to quote understated a 46,755-row result by 19x — and suggested raising a setting that
  would have let the query through and then run the process out of memory.

- **[rfc]** **`sap_rfc_invoke` failed on any module whose result parameters are all
  tables.** `RFC_READ_TABLE` is the canonical case — its results are `DATA`, `FIELDS`
  and `OPTIONS`, with no scalar export — and invoking it raised
  `Unimplemented type for cast (STRUCT(WA VARCHAR) -> STRUCT(WA VARCHAR)[])`.

  The result was treated as *pivoted* whenever every value happened to be list-shaped,
  so rows were unnested into columns still declared `LIST(STRUCT)`. Detection now also
  requires each element type to match its declared column type, which distinguishes
  genuinely pivoted (path-selected) data from a bare invoke. Selecting a table through
  `path :=` still pivots to the row's fields, as before.

- **[rfc]** `MAX_ROWS` combined with `partitions` hung. The unpartitioned path trims
  `ROWCOUNT` to land exactly on the limit, which a partitioned window cannot do because
  `ROWCOUNT` must stay batch-aligned; it over-fetches the final batch, and the clip then
  left the read spinning with rows it would never emit.

- **[rfc]** The runtime `RFC_READ_TABLE` fallback reassigned a `std::string` on the
  bind data from execute time. Column-parallel reads already made that reachable from
  two tasks at once; it is now serialised.

- **[rfc]** **`fetch_size` and `partitions` did nothing to the batch size of a
  single-column scan.** `MaxBatchSizeForColumnCount` returned the full 32768-row cap
  whenever a scan projected one column, discarding the budget that
  `ResolveEffectiveMaxBatchSize` had just divided across the partition workers — so every
  worker asked SAP for `ROWCOUNT=32768` however `erpl_rfc_fetch_size` and `partitions`
  were set. The exemption was deliberate, on the reasoning that a narrow scan is already
  under any sane budget; that reasoning ignored the division, and a narrow scan is
  precisely what partitioning exists for.

  Measured on a 300,000-row single-column read at `partitions = 8`: peak RSS **405 MB →
  307 MB (−24%)** with no loss of throughput (2.17s → 2.07s). The budget now binds for
  every column count; only a zero budget or a scan with no projected column bypasses it.

- **[rfc]** A partitioned scan never returned its allocator arenas to the OS. The serial
  path calls `malloc_trim(0)` when the scan finishes, but each partition worker retires on
  a different code path that did not, so every worker thread's glibc arena kept its
  high-water mark — which is what `getrusage` reports.

- **[rfc]** **A partitioned scan past the `ROWSKIPS` ceiling returned a truncated result
  instead of refusing.** The row-window scheduler signalled the ABAP `INT4` limit by
  returning "no more windows", which is the same answer it gives at the end of a table —
  and a worker retires on an empty chunk, which DuckDB reads as end-of-scan. A scan beyond
  2,147,483,647 rows therefore produced a silently short answer, while `API_REFERENCE`
  states that erpl refuses rather than wrapping. It now raises, naming the limit.

  The same guard was off by one and refused the **last legal window** (start
  2,147,450,880 at a 32768-row window). Both are covered by new boundary tests.

- **[rfc]** **The `RFC_READ_TABLE` fallback could retry without limit.** After the runtime
  fallback had switched functions, the selection call returns a cached success, and the
  retry branch neither counted the attempt nor slept — so a second `TABLE_WITHOUT_DATA`
  from the fallback function became a tight loop against the SAP system, once per
  partition worker. It now retries only when the selected function actually changed, and
  otherwise reports the original error.

- **[rfc]** **A re-scanned `sap_read_table` silently returned nothing.** The column state
  machines lived in *bind* data, which DuckDB reuses across executions of one bound plan,
  so the second scan resumed from an exhausted cursor:

  ```sql
  PREPARE q AS SELECT count(*) FROM sap_read_table('SFLIGHT');
  EXECUTE q;   -- 94
  EXECUTE q;   --  0   <- no error
  ```

  Any re-scanned plan hit it: a prepared statement, a nested-loop join, an
  un-materialised CTE referenced twice. Serial scans now build their machines in the
  *global* state, which DuckDB rebuilds per execution — the pattern the partitioned path
  already used, which is why `PARTITIONS` was never affected.

  The same defect was present in `sap_show_tables`, `sap_odp_show_subscriptions` and the
  internal table lister behind `ATTACH`; all three are fixed.

- **[rfc]** **The persistent-connection budget was spent permanently.**
  `erpl_rfc_max_persistent_connections` was a monotonic counter on bind data that never
  released a slot, so a second execution of a bound plan began with the budget already
  exhausted and every scan fell back to per-batch open/close. Slots are now leased:
  released when the connection is dropped, reset per execution.

- **[rfc]** **A long scan could follow a secret replaced underneath it.** The DuckDB
  secret was re-resolved on *every* connection open, so replacing it mid-query could send
  later windows of the same scan to a different SAP system, with no error. Credentials are
  now resolved once per execution.

- **[rfc]** **Setting a trace option while tracing was enabled deadlocked the process.**
  `erpl_trace_level`, `erpl_trace_output` and the other setters logged their own change
  while holding the tracer mutex, which the writer re-locks. The sequence documented for
  diagnosing SAP communication was itself the trigger; it presents as a hung query.

### Changed

- **[all]** **The `erpl-proto` backend is pinned to `v2026.9.2.1`, and its known-failure
  list is now empty.** The previous pin carried a table-delta padding gap
  (`sap_rfc_struct_layout`), and the releases that fixed it regressed BICS from 45/45 to
  23/45 on a nested-`TTYP` basXML defect. Both are now fixed upstream, so every suite
  passes on the proto backend with no recorded gaps for the first time: RFC 34/34,
  ODP 25/25, BICS 45/45.

### Build

- **[all]** The `erpl-proto` backend is pinned to the `v2026.8.29.1` release rather than
  an untagged commit.

---

## v2026.08.31 — every predicate applied, and most of them pushed to SAP

Two of the entries below are **silent wrong-results bugs in released erpl**, not
regressions: `sap_read_table` has been returning unfiltered rows for ranges,
conjunctions and wide `IN` lists. If you filter SAP tables from SQL, upgrade.

### Fixed

- **[rfc]** **`sap_read_table` returned unfiltered rows for any predicate it could not
  push to SAP.** `filter_pushdown = true` tells DuckDB the scan applies every filter it
  is handed, and DuckDB removes the filter from the plan accordingly — `EXPLAIN` shows no
  `FILTER` operator above `SAP_READ_TABLE`. erpl translated what `RFC_READ_TABLE` could
  express and silently ignored the rest, so ranges, conjunctions and wide `IN` lists were
  dropped: `WHERE SEATS_MAX > 350` on `/DMO/FLIGHT` returned all 40 rows instead of 14.
  Predicates that cannot go to the server are now evaluated by erpl instead, so the result
  is the same whether or not one reaches SAP.

- **[rfc]** Comparison literals were built without escaping, so a value containing an
  apostrophe closed the ABAP literal early and changed the predicate's meaning. Inequality
  was emitted as `!=`, which ABAP's dynamic `WHERE` does not accept.

- **[rfc]** The `OPTIONS` line splitter broke inside quoted literals whenever one contained
  a space, leaving an unbalanced apostrophe on both lines.

- **[rfc]** **A scan could stop early and truncate its result.** A table function signals
  end-of-scan by returning a chunk with no rows, and client-side filtering can legitimately
  reject every row of a batch — returning that empty chunk ended the scan and discarded
  everything still unread. On `DD03L` this returned 25,578 rows instead of 114,566. Only
  reachable on tables large enough to span several batches, which is why it survived a
  suite that tests against a 40-row table.

- **[rfc]** A `TIME` literal with a fractional second was pushed truncated. SAP `TIMS` is
  second-precision, so `t < TIME '09:05:03.500'` became `t < '090503'` and wrongly rejected
  rows stored as `09:05:03`. Such values are no longer pushed; erpl applies them exactly.

### Added

- **[rfc]** **Range predicates, `AND`/`OR` combinations and `IN` lists wider than five
  values are now pushed to SAP.** Previously only `=` and `IN` with at most five values
  reached the server, so a date-window predicate transferred the whole table and filtered
  it locally. `BETWEEN` in particular pushed nothing at all, because DuckDB expresses it as
  a conjunction. Literals are rendered as the DDIC expects them (`DATE` → `YYYYMMDD`,
  `TIME` → `HHMMSS`); types with no unambiguous ABAP spelling are left to erpl rather than
  guessed at. Predicates on the client field are never pushed — `RFC_READ_TABLE` rejects
  any clause naming it.

- **[rfc]** `erpl_rfc_pushdown_filters` (default `true`) to disable the translation. It
  changes only where a predicate is evaluated, never which rows come back, which makes it
  both an escape hatch for SAP releases that reject the generated syntax and a way to rule
  a filter out as the cause of a discrepancy.

---

## v2026.08.29 — BEx result sets that fit in memory

### Fixed

- **[bics]** **BEx query results were silently capped at a compiled-in 1,000,000 data
  cells** ([#120](https://github.com/DataZooDE/erpl/issues/120)). `BICS_PROV_GET_RESULT_SET`
  takes an `I_MAX_DATA_CELLS` budget and erpl passed a hard-coded million. Above it BW
  builds nothing and answers with empty axis tables and no message at all, which erpl
  reported as *"The BW server returned no result structure for this query"* — an error that
  named neither the cause nor a way out. The budget counts **data cells** (key-figure
  cells), not output columns, which is why a 63,000 × 31 query fit while a 2M-cell one did
  not.

- **[bics]** The member table was copied **once per result row**. `GenericTable::Rows()`
  returns its vector by value, and `RowReferenceIterator::NextPresentationValues` bound it
  to a reference on every row — O(rows × members) `Value` copies. Likely a large part of
  the long-standing "BICS burns CPU even on small result sets" behaviour.

- **[rfc]** A `StringUtil::Format` call had three placeholders and two arguments, so the
  throw itself threw and buried the real RFC error behind
  *"Expected 3 parameters, received 2"*.

### Added

- **[bics]** `erpl_bics_max_result_memory` — the result-set ceiling is now a **memory**
  budget, not a cell count. It accepts what DuckDB's own memory settings accept (`'8GB'`,
  `'80%'`), and when unset it follows DuckDB's `memory_limit`, which defaults to 80% of
  physical RAM. erpl converts it to the cell count BW needs.

  A query that does not fit now fails with something actionable:

  ```
  This BEx query is too large to read in one go: it would return 63000 rows x 31 columns
  = 1953000 data cells, which needs about 238.4 MB of memory. The limit is 4.0 MB. Either
  raise it with SET erpl_bics_max_result_memory = '287MiB' (make sure the machine actually
  has that much), or make the query smaller with sap_bics_filter().
  ```

  `erpl_bics_max_data_cells` remains as an explicit override for callers who want to pin
  exactly what BW receives.

- **[bics]** `erpl_bics_stream_result_tables` (default on) — an escape hatch that restores
  the previous materialising read path.

### Changed

- **[bics]** **The response is no longer materialised before the first row is produced.**
  The four tables that grow with the result — `E_T_DATA_CELLS`, `E_T_ROWS`, `E_T_MEMBER`,
  `E_T_MEMBER_PRESENTATION` — are read row by row off the SAP SDK handle, converting only
  the fields actually needed (the fetch loop reads three of a data cell's fourteen).
  `E_T_COLUMNS` stays materialised; it is bounded by the column count, not the result size.

  Measured on a 20,000,000-row BW fixture, same binary with the toggle flipped:

  | result rows | before | after | reduction |
  |---|---|---|---|
  | 106,729 | 2,386 MB | 1,327 MB | 44% |
  | 1,280,841 | 14,795 MB | 3,496 MB | **76%** |

  Row counts are identical either way, and the streamed path is marginally *faster*.

- **[rfc]** `RfcType::ConvertRfcTable` builds one STRUCT `LogicalType` per table instead of
  deriving and storing a fresh one per row — one copy of every field name and type, per
  row. Measured at 2,160 → 1,120 bytes per row on a 14-field BICS data cell, and 784 → 400
  on a 4-field axis element. This benefits every scan path, not just BICS.

- **[rfc]** `RfcResultSet` can now **defer** a TABLES parameter: its rows are not converted
  and the raw `RFC_TABLE_HANDLE` is exposed instead, so a caller can stream a very large
  table rather than box it.

### Notes

- There is no pagination to reach for in BICS: neither `BICS_PROV_GET_RESULT_SET` nor
  `BICS_PROV_GET_RESULTSET_DETAIL` accepts a row range, so BW builds the whole result set
  or none of it. Sizing the budget, or filtering with `sap_bics_filter()`, is the route.
- **[bics]** Adds `bics/test/fixtures`, an idempotent BW fixture (a Z aDSO created from the
  demo cube and loaded through `RSDSO_WRITE_API`) large enough to measure result-set
  behaviour on; the demo cube is left untouched.

---

## v2026.08.22 — A pure-Rust RFC backend, and the SSH tunnel moves out

### Added

- **[rfc]** **erpl can now run on either SAP's NetWeaver RFC SDK or
  [erpl-proto](https://github.com/DataZooDE/erpl-proto), a pure-Rust implementation of the
  classic RFC protocol — chosen at runtime.** `nwrfc` remains the default; nothing changes
  unless you ask for it:

  ```sql
  SET erpl_rfc_backend = 'proto';   -- or 'nwrfc' (default), or ERPL_RFC_BACKEND
  SELECT sap_rfc_backend();         -- which one is actually serving calls
  ```

  erpl no longer links `libsapnwrfc` at all. The 55 SDK entry points that `erpl_rfc`,
  `erpl_bics` and `erpl_odp` call between them are resolved through a dispatch table
  filled by `dlopen` + `dlsym` on first use. Both libraries export the same symbols, so
  only one could ever be linked — whichever the loader reached first would silently serve
  everything, which is why the indirection exists. The library is opened `RTLD_LOCAL` so
  the two can never interpose on each other.

  The backend freezes at the first SAP call: connections and function descriptors belong
  to one implementation, so a later `SET` is refused rather than allowed to hand a handle
  across. A missing proto library is a hard error, never a silent fallback to the SDK.

  Both backends pass the full suites against an ABAP Platform Trial — `rfc` 28/28,
  `bics` 43/43, `odp` 24/24, and 121 C++ test cases — including with the SDK removed from
  the library path entirely. `libsapucum` also left the link line; `strlenU16` was the
  only symbol erpl needed from it.

- **[rfc]** `sap_rfc_backend()` reports which implementation is serving RFC calls.

### Fixed

- **[rfc]** The `sap_rfc_invoke` example advertised in `duckdb_functions()` was wrong —
  `REQUTEXT='Hello'` is rejected by the binder, since the only named parameters that
  function takes are `path` and `secret`. RFC parameters travel as a STRUCT.

### Breaking

- **The bundled SSH tunnel has been removed.** It moved to the dedicated
  [erpl_tunnel](https://github.com/DataZooDE/erpl-tunnel) extension, which supports reverse
  tunnels and Tailscale/NetBird in addition to SSH:

  ```sql
  INSTALL erpl_tunnel FROM 'http://get.erpl.io';
  LOAD erpl_tunnel;
  ```

  `tunnel_create`, `tunnel_close`, `tunnel_close_all` and `tunnels()` remain registered as
  stubs that raise and point there, so an existing script says where the function went
  instead of failing with "does not exist". Loading `erpl_tunnel` replaces them.

  This also fixes a bug: while erpl bundled its own tunnel it **silently shadowed** the
  dedicated extension. `INSTALL erpl_tunnel; LOAD erpl_tunnel;` appeared to succeed and left
  you on the bundled implementation, so nobody with erpl installed could migrate.

  Two differences to be aware of: `tunnels()` gained `backend` and `direction` columns, so
  **column order changed** (queries naming columns are unaffected), and `tunnel_create` is a
  deprecated alias for `tunnel_import`. The `ssh_tunnel` secret type is unchanged.

  Requires erpl_tunnel with the replace-on-conflict registration fix; older builds abort
  the LOAD when erpl's stubs are present.

- The released `erpl` bundle is ~48 MB smaller, and `libssh2` is no longer a dependency.

## v2026.08.19 — Non-ASCII text, raw byte columns, and a ten-day release outage

**If you are on the 2026-08-08 build, upgrade.** Three separate defects could abort
a query outright, and a CI break meant none of the fixes reached `get.erpl.io` for
ten days.

### SAP scan path

- **[bics]** *Fix:* any BW object text containing a non-ASCII character could abort
  the query with `Invalid byte encountered in STRING -> BLOB conversion of string
  "..."` ([#107](https://github.com/DataZooDE/erpl/issues/107)). DuckDB's
  `Value::CreateValue` has an easy-to-miss overload split — `const char *` yields a
  VARCHAR while `std::string` yields a **BLOB** — so text was being routed through
  `Blob::ToBlob`, which rejects every byte above `0x7F`. Pure-ASCII input round-trips
  invisibly, which is why this survived both the test suite and the SAP demo content
  until the first accented character arrived. Fixed at 13 call sites;
  `sap_bics_describe_infoobject` hit it on every call.
- **[rfc]** *Fix:* reading a `UTCLONG` / `UTCL` / `UTCS` / `UTCM` column with
  `sap_read_table` **aborted the whole scan** with `Failed to cast value: invalid
  timestamp field format`. SAP's compact `YYYYMMDDHHMMSS,sssssss` is not an ISO
  timestamp and a blank cell is not castable at all, but the read-table path left
  both to an implicit cast instead of the parsing the direct RFC path already used.
  Reproduced on `ADRC`, a table present on every system.
- **[rfc]** *Fix:* an all-blank numeric cell aborted the whole scan — `Could not
  convert string "  " to DECIMAL(p,s)` for packed decimals, and the equivalent for
  `INT1` / `INT2` / `INT4` / `INT8` / `FLTP`. A blank cell is SAP's "no value" and
  now reads as `NULL`.

### Type system

- **[rfc]** *Fix:* `sap_read_table` returned `RAW` / `LRAW` / `RAWSTRING` / `RSTR`
  columns as their **hex spelling** instead of the bytes
  ([#109](https://github.com/DataZooDE/erpl/issues/109)). `RFC_READ_TABLE` cannot
  carry binary data, so it renders raw columns as hex text in the character `DATA`
  line, and that text was written straight into the BLOB. `octet_length()` was
  double the real size and the payload needed a manual `unhex()`. **Behaviour
  change:** drop any `unhex()` workaround. An empty raw column — which SAP renders
  as the field delimiter, previously stored as a one-byte `~` — is now `NULL`.
- **[rfc]** *Fix:* `DECFLOAT16` / `DECFLOAT34` columns were left as VARCHAR and
  implicitly cast per cell into their `DECIMAL` column; they are now parsed
  directly. The `DEC` / `CURR` / `QUAN` path applies the same `min(precision, 38)`
  cap as the column type it feeds.
- **[rfc]** `RfcType::ConvertCsvValue` now has an explicit branch for every DDIC type
  that maps to a non-VARCHAR column, so no read-table cell relies on an implicit
  per-cell cast. A test iterates the whole DDIC type map, so a mapping added without
  a matching branch fails the build — the absence of that check is what allowed the
  UTCLONG and blank-numeric aborts to sit unnoticed.

### Stability

- **[rfc]** *Fix:* the SAP NW RFC SDK faults inside its own static destructor at
  process exit, aborting an already-successful process with `pure virtual method
  called` on roughly 10% of runs ([#112](https://github.com/DataZooDE/erpl/issues/112)).
  A terminate handler now intercepts it and exits with the status the process had
  already chosen, so exit codes are trustworthy again. glibc Linux only. The SDK's
  own fault is unchanged — `RfcCleanup`, its documented teardown entry point, is
  declared in `sapnwrfc.h` but not exported by `libsapnwrfc.so`.

### Build & CI

- **[all]** *Fix:* **no binaries reached `get.erpl.io` between 2026-08-08 and
  2026-08-18** ([#115](https://github.com/DataZooDE/erpl/issues/115)). msys2 purged
  `msys2-runtime-3.5.4-2` from every mirror, which our pinned vcpkg commit fetches,
  so every Windows build failed deterministically — and the deploy jobs require the
  full matrix, so Linux and macOS stopped publishing too. Windows now uses a newer
  vcpkg pin (`vcpkg_commit_windows`); Linux and macOS stay on the proven pin, whose
  successor ships an openssl that will not build in `manylinux_2_28`.
  Reported and diagnosed by [@rafael-tesseralabs](https://github.com/rafael-tesseralabs).
- **[all]** Deploy jobs now run with `if: !cancelled()`, so one broken platform can
  no longer halt publishing for the others. The deploy matrix is per-architecture,
  so working platforms publish while a failed one stays visibly red.
- **[all]** *Fix:* `RUN_SQL_TESTS` never checked exit statuses — `make sql_tests_*`
  returned only the **last** file's result, so five failing ODP tests were invisible.
  Failures are now aggregated and reported.
- **[bics]/[odp]** SQL suites no longer pin absolute counts of SAP-shipped catalog
  content, which drift with system configuration rather than with our code. BICS is
  43/43 and ODP 24/24; both were failing before.

## v2026.07.30.1 — SNC logon and the full RfcOpenConnection parameter set

Connecting through **SNC (Kerberos, X.509, SAP Single Sign-On) is now possible**:
`SNC_MODE` could not be set on a `sap_rfc` secret, so the SAP RFC SDK never
activated SNC and a password was effectively mandatory.

- **[rfc]** The `sap_rfc` secret accepts the remaining documented
  `RfcOpenConnection` parameters ([#98](https://github.com/DataZooDE/erpl/issues/98)):
  `SNC_MODE`, `SNC_SSO`, `X509CERT`, `SAPROUTER`, `GWHOST`, `GWSERV`, `CODEPAGE`,
  `TRACE` and `DEST`, alongside the existing `SNC_QOP` / `SNC_MYNAME` /
  `SNC_PARTNERNAME` / `SNC_LIB`. A secret without `PASSWD` is valid — an
  SNC or SSO2 logon needs none. Names are passed to the SDK unchanged.
- **[rfc]** *Fix:* the `secret => '<name>'` argument selected a secret by **scope
  instead of by name**. It was passed to DuckDB's `LookupSecret()` as a path, so
  with more than one `sap_rfc` secret defined, ERPL could silently connect to a
  different system than the one named — and a misspelled name resolved to some
  other secret rather than erroring. Naming a secret now selects exactly that
  secret, and an unknown name is reported.
- **[rfc]** *Fix:* the password was **not redacted** in `duckdb_secrets()`. The
  redact list named `password` while the stored key is `passwd`, so the plaintext
  password was printed. `passwd`, `mysapsso2` and `x509cert` are now redacted.
- **[rfc]** The connection parameters are defined once in a single table instead
  of being repeated across four hand-maintained lists, which also removes a
  fixed-size `RFC_CONNECTION_PARAMETER params[15]` stack array that exactly
  matched the old parameter count. Telemetry's `auth_kind` recognises an
  SNC logon that sets only `SNC_MODE` (previously reported as `basic`).

Not verified end to end: the SNC handshake itself needs an SNC-enabled SAP
system, which the ABAP trial used for CI is not. What is verified live is that
`SNC_MODE '1'` makes the SDK initialize SNC and load the configured `SNC_LIB`.

---

## v2026.07.30 — BEx query variables

The headline is that **BEx queries with a variable prompt can finally be executed**.
Getting there uncovered a result-materialization bug that made several BEx queries
unreadable regardless of variables, so both are fixed here.

- **[bics]** Support **BEx query variable submission** ([#96](https://github.com/DataZooDE/erpl/issues/96)).
  `sap_bics_begin` gains `variables => LIST<STRUCT(NAME, SIGN, OP, LOW, HIGH)>`,
  `hierarchy_variables => …` and `variant => …`. Values are submitted with
  `BICS_PROV_OPEN` through `I_T_VIEW_VARIABLE_VALUES`, so a BEx query with a
  mandatory variable prompt can be initialized and executed. Single values,
  intervals, multiple values (repeat `NAME`) and hierarchy nodes are supported.
  The shape mirrors the existing ODP `filters` select-option convention.
- **[bics]** New `sap_bics_variables(info_provider [, query])` lists a query's
  variables with their `mandatory` / `input_enabled` flags, so callers can
  discover what has to be filled.
- **[bics]** A query whose mandatory variables are unfilled used to surface as
  a DuckDB internal error ("Table function must return at least one column").
  It now reports which variables are missing. A variable name the query does
  not expose as input-ready is rejected instead of being silently ignored by BW.
- **[bics]** Fix `sap_bics_result` for BEx queries whose row axis the state does not
  describe ([#99](https://github.com/DataZooDE/erpl/issues/99)). The result schema was
  sized from the persisted state's axis tables while the data was written using the
  result's own row-element count, so the schema collapsed to the column-axis leaves
  (all `DOUBLE`) and materialization failed with e.g. `Could not convert string
  'FC008' to DOUBLE`; data cells were targeted past the end of the chunk and silently
  dropped. The schema now follows the result set, which is the only source that always
  knows the axis width; a result with no rows at all falls back to the state, so a
  query's schema does not narrow just because it returned nothing. State-derived column
  names are unchanged whenever the state agrees on the count; otherwise names come from
  the result's own members, falling back to `ROW_<n>`.
- **[bics]** Name the row axis from the query's design-time metadata when the session
  state carries none, instead of falling back to `ROW_1`, `ROW_2`, …. The extra
  `BICS_PROV_GET_DESIGN_TIME_INFO` call is made only when the state cannot name the
  axis, and both sources are matched on the characteristic id rather than by position,
  so a column is never labelled with an unrelated characteristic.
- **[bics]** Name measure columns from the query's design-time metadata
  ([#101](https://github.com/DataZooDE/erpl/issues/101)). Measures came back as
  `dyn_kf_1`, `dyn_kf_2`, … for BEx queries whose session state carries no
  characteristic metadata — the same root cause as the row axis, one layer down.
  Naming order is technical name, then description, then the synthetic name; the
  middle step matters because these BEx structure members carry a description but an
  empty technical name. Descriptions may contain spaces, so such columns need quoting:
  `SELECT "Net Sales" FROM sap_bics_result('q1')`.
- **[odp]** `test_odp_com` hardcoded SAP credentials with a password that no longer
  matched the trial system, so every run performed a failed logon — enough repeats lock
  the SAP user globally and break every other suite. It now reads the standard
  `ERPL_SAP_*` variables and skips when they are unset.
- **[bics]** Revive the C++ tests that had gone dark: the test binaries link
  `dummy_static_extension_loader`, whose `LoadAllExtensions` is a no-op, so nothing
  registered `core_functions` and every test that evaluates a serialized `Value`
  (the captured-response fixtures, the state repository round trip) failed on a missing
  `list_value`. Test databases are now built through `MakeTestDatabase()`, which
  registers the statically linked extension directly. Seven test cases, including the
  `GetColumnNames` / `GetResultTypes` coverage over captured BW responses, run again.
- **[bics]** Fix session restore for query-based sessions: the InfoProvider and
  query names are now persisted with the state. `BICS_PROV_GET_INITIAL_STATE`
  does not carry them for queries, so any second statement against a session
  opened by query name previously failed with "Failed to open data provider …
  for info provider ''". This affected every chained `begin → rows → result`
  workflow on a BEx query, not just variable-driven ones.

## v2026.07.12 — Cross-product telemetry (schema 2)

- **[all]** Adopt the shared cross-product **posthog-telemetry schema-2**
  telemetry. The library is bumped to its analysis-first API (`e41682b`), which
  supersedes the prior shutdown-race pin (an ancestor commit) so the teardown
  SIGSEGV fix is retained while the new envelope + APIs are added. Every
  sub-extension now identifies the product at load
  (`SetProduct("erpl", …, "oss")`) and associates a pseudonymous `deployment`
  group; `extension_loaded` is unchanged. Opt-out is unchanged and still fully
  short-circuiting (`SET erpl_telemetry_enabled = false`, or
  `DATAZOO_DISABLE_TELEMETRY=1`).
- **[rfc]** Emit `feature_used` at the real entry points — `connection_opened`
  (`auth_kind` ∈ `basic|sso|snc`), `sap_rfc`/`bapi_call` (`duration_ms`, split by
  the `BAPI_` module-name prefix), and `rfc_table_read` (`duration_ms`). Caught
  failures on the connect/invoke/read paths emit an enumerated `$exception`
  (`error_class` mapped from `RFC_RC`, plus `feature`/`phase`) — never a SAP
  message, host, user, table, or SQL.
- **[odp]** Emit `odp_extract` `feature_used` with `mode` ∈ `full|delta` and
  `duration_ms` at the full/delta read binds.
- **[bics]** / **[tunnel]** Route all DuckDB function calls through
  `RecordFunctionCall(...)`, aggregated into one `function_executed` per function
  per session instead of a per-call event.
- **[all]** New header-only helper `rfc/src/include/erpl_telemetry.hpp` (bounded
  `feature`/`auth_kind`/`error_class`/`phase` enums + a success-only
  `ScopedFeature` timer), a `TELEMETRY.md` describing exactly what is collected,
  and a `[telemetry_verify]` regression test. Only enumerated/numeric property
  values ever leave the machine.

## v2026.07.02 — BICS transformation field mappings

- **[bics]** Fix: `sap_bics_meta_transform_fields` returned **0 rows on every
  system** (reported on BW/4HANA in #89). It queried `RSTRANFIELD` with column
  names that do not exist (`FIELDNAME`, `IOBJNM`, `ROLE`, `ROUTINE`, `RULE_*`);
  both the "enhanced" and "basic" reads threw and a catch-all swallowed the
  error into an empty result. The scanner now reads the real `RSTRANFIELD`
  schema (`TRANID, OBJVERS, RULEID, PARAMTYPE, PARAMNM, FIELDNM, FIELDTYPE,
  KEYFLAG`) and pairs source/target rows by `RULEID` via `PARAMTYPE`
  (`0` = source, `1` = target), preferring the active object version (`A`) over
  delivered (`D`). The swallow-to-empty path is replaced by a traced fallback so
  a genuinely missing table still degrades gracefully on non-BW systems. The
  `bw_transformation_mappings` view, which builds on this function, was broken
  the same way and is fixed by the same change.

## v2026.06.28 — ODP delta hardening

- **[odp]** Fix: make `OdpFetchSession::CloseSession()` exception-safe on the
  destructor path. It runs from `~OdpFetchSession` on the FULL read path; the
  v2026.06.27 trace-logging addition could throw (string allocation / trace
  macro) and escape the destructor as `std::terminate`. The trace call is now
  wrapped in its own guard with a final `catch(...)`. Found by a post-release
  Codex review of the shipped delta-replication diff.

## v2026.06.27 — SAP ODP delta replication

- **[odp]** ODP **delta replication**. New `sap_odp_read_delta(odp_context,
  odp_name, subscriber_process [, columns, filters, recover, threads, secret])`:
  the first call with a `subscriber_process` performs SAP's auto-DELTAINIT
  (full snapshot + registers a delta pointer); subsequent calls return only
  changes since the pointer. `recover=true` re-streams the last unconfirmed
  packet. Delta fetch is serialized (single-threaded) for correct multi-package
  ordering. Validated end-to-end against an ABAP_CDS byElement source on the
  trial system (insert/update/delete/mixed/bulk/recover/filters).
- **[odp]** New `PRAGMA sap_odp_close_delta_cursor(odp_context,
  subscriber_process, odp_name)` — graceful, idempotent cursor close that leaves
  the subscription resumable (counterpart to `sap_odp_drop`/`RODPS_REPL_ODP_RESET`).
- **[odp]** New `sap_odp_get_last_modified(odp_context, odp_name)` (cheap
  delta probe) and `sap_odp_get_subscriptions(odp_context, odp_name [, …])`
  (per-object subscription list).
- **[odp]** `sap_odp_drop` is now a `PragmaCall` with an explicit 4-argument
  signature (the previous zero-arg registration failed to bind the documented
  call).

## v2026.06.17 — DuckDB v1.4.5 (LTS) + v1.5.4 (latest)

Tracks DuckDB's latest patch releases on both supported lines. No SAP-facing behavior
changes — this is a build/toolchain refresh. Existing `DUCKDB_MINOR_VERSION` guards
(e.g. in `rfc/src/sap_storage.cpp`) already cover the 1.4 ↔ 1.5 storage API split, so no
extension source changes were required.

### Build & CI

- **[all]** The self-distributed binary matrix now targets **DuckDB v1.4.5** (LTS) and
  **v1.5.4** (latest), up from v1.4.4 / v1.5.3. Both legs build across
  `{linux_amd64, linux_amd64_musl, osx_amd64, osx_arm64, windows_amd64}`.
- **[all]** `extension-ci-tools` advanced to the rolling `v1.5-variegata` branch.
- **[all]** The bundled `duckdb` submodule used for local debug builds is pinned to v1.5.4.
- **[all]** Pinned the `linux_amd64_musl` build image to `alpine:3.21`. The rolling
  `alpine:3` tag had advanced to 3.22, whose repos dropped the `clang19` package, breaking
  the musl Docker image build for every leg.

---

## v2026.06.05 — BICS result: key figures no longer crash on DOUBLE → DATE

Fixes [#84](https://github.com/DataZooDE/erpl/issues/84): `sap_bics_result` aborted while
materializing some BW queries with `Invalid Input Error: Failed to cast value: Unimplemented type
for cast (DOUBLE -> DATE)`. The schema bound fine (`DESCRIBE` worked), but fetching values failed
because a numeric key-figure column had been declared `DATE`.

### SAP scan path

- **[bics]** Result column types are now derived from the **value domain the scanner actually
  writes**, not from the InfoObject's DDIC/ABAP type. The previous logic ran every column through
  the characteristic / key-figure metadata, so a key figure or characteristic whose ABAP type was
  `'D'` (date), `'T'` (time) or `'I'` (int) produced a `DATE`/`TIME`/`INTEGER` column — but the
  cells written into it are fixed by position: row-axis characteristics carry formatted member
  key/text **strings** (`VARCHAR`), the synthetic `HIER_LEVEL` columns are `INTEGER`, and
  column-axis (key-figure / data-cell) columns carry the `DOUBLE` `E_T_DATA_CELLS.VALUE`. The
  mismatch (e.g. a `DOUBLE` cell value into a `DATE` column) aborted materialization. It surfaced
  only now because the common CHAR/NUMC characteristics already map to `VARCHAR`; a key figure with
  a date/time/int DDIC type is the case that breaks.
- **[bics]** The same class of bug on the **row axis** (a date/time characteristic placed on rows
  would have failed identically) is fixed by the same change. The now-dead per-column typing
  machinery (`Characteristic::GetResultType()`, the `FindReturnType` chain, `bicstype2rfctype()`)
  was removed so result columns can no longer be typed from metadata again.

### Build & CI

- **[bics]** Added offline regression tests (`test/cpp/test_bics_result_types.cpp`) covering the
  column-axis key-figure and row-axis date-characteristic variants plus the CHAR/NUMC/key-figure
  baseline; verified the column-axis case fails on the pre-fix behaviour.

## v2026.06.04 — BICS: actionable open errors and cross-release result fetch

Two SAP BW (BICS) reliability fixes, both reported against live customer systems.

### Clearer errors when opening a BW data provider

Fixes [#80](https://github.com/DataZooDE/erpl/issues/80): `sap_bics_describe` / `sap_bics_begin`
occasionally failed with the opaque `Failed to open data provider on the BW server, giving up!`,
which gave no way to tell *why* the open was rejected (authorization, query not active in the
requested version, a transient server condition, …).

- **[bics]** BICS RFC modules report failures through `E_MAX_MESSAGE_TYPE` (the highest message
  severity) and an `E_T_MESSAGE` table (the individual `BICS_PROV_MESSAGE` rows). ERPL ignored both
  and only noticed the sentinel handle `0000`. `BICS_PROV_OPEN` and `BICS_CONS_CREATE_DATA_AREA`
  now surface the info provider, the query name, the message severity and the actual SAP message
  text in the thrown error, e.g. `… for info provider '0D_NW_C01' (query '0D_FC_NW_C01_Q0008'),
  giving up! (max message type 'E'): [E BRAIN 299] Query … is not available in version A`. A new
  `FormatBicsMessages()` helper renders the message table and is covered by offline unit tests.
  Diagnostics gathering is best-effort and never throws while building the exception.

### Result fetch works across BW releases

Fixes [#81](https://github.com/DataZooDE/erpl/issues/81): `sap_bics_result` failed on some BW
systems with `Failed to adapt value for invocation argument 'I_CONFIRM_AUTORETRY': Parameter
'I_CONFIRM_AUTORETRY' not found`. ERPL passed the optional import parameter `I_CONFIRM_AUTORETRY`
to `BICS_PROV_GET_RESULT_SET` unconditionally, but it only exists on newer BW releases — older
systems reject the call during argument adaptation.

- **[bics]** `FetchResultSet` now looks up the target function's actual parameter set and drops
  `I_CONFIRM_AUTORETRY` only when the release does not define it (the dropped argument is traced).
  Systems that do define it are unaffected.
- **[rfc]** New reusable, version-tolerant primitives backing the fix: `SelectSupportedNamedArgs()`
  removes version-specific optional fields from a named-argument struct — a field is dropped only
  when it is **both** absent from the target signature **and** in an explicit droppable allowlist,
  so a genuinely misspelled required parameter still raises a clear `Parameter not found` error
  rather than being silently swallowed. `RfcFunction::GetParameterNames()` introspects the target
  system's signature. Both are covered by offline unit tests (`[select_supported_args]`).

### Build & CI

- **[bics]** Relaxed three `GenericTable::Rows()` bindings in `bics_result.cpp` from `auto&` to
  `const auto&` (`Rows()` returns by value; binding the temporary to a non-const reference is
  rejected by newer compilers).

## v2026.06.01 — RFC connection teardown no longer crashes the host process

Fixes [#78](https://github.com/DataZooDE/erpl/issues/78): a long-running process (reported in a
container) crashed with `terminate called after throwing an instance of 'duckdb::IOException'` and
`Error during SAP RFC connection closing: RFC_INVALID_HANDLE`. This is a hard `std::terminate`
abort — not an out-of-memory kill — so it takes down the **entire host process**, not just the
query.

### SAP scan path

- **[rfc]** `RfcConnection`'s destructor no longer throws. Root cause: `RfcConnection::Close()`
  raised a `duckdb::IOException` whenever `RfcCloseConnection` returned anything other than
  `RFC_OK`, and the destructor called it with no guard. A C++ destructor is implicitly `noexcept`,
  so an escaping exception goes straight to `std::terminate`. On **long-running sessions** the
  cached connection handle can become invalid — the SAP gateway drops an idle/long-lived
  connection, or a failed first close left the handle non-NULL and the destructor closed it a
  second time — and the resulting `RFC_INVALID_HANDLE` aborted the process.
- **[rfc]** `Close()` now clears the handle immediately after `RfcCloseConnection` regardless of
  outcome (eliminating the double-close path) and treats `RFC_INVALID_HANDLE` as benign — the
  handle is already gone, so there is nothing left to close. Genuinely unexpected RFC return codes
  still raise for explicit callers.
- **[rfc]** The same hardening was applied to the `RfcInvocation` / `RfcFunction` destructors,
  which carried the identical latent `std::terminate` risk (`RfcDestroyFunction` failures are now
  traced via `ERPL_TRACE_ERROR` instead of thrown), and both are now correctly marked `noexcept`.
- Note: this stops the crash, but the underlying connection drop (most often an idle gateway
  timeout) still occurs — it is now handled gracefully instead of aborting. Keepalive/reconnect on
  cached connections is a possible follow-up.

### Build & CI

- **[rfc]** Added a regression test (`rfc/test/cpp/test_connection_close.cpp`) that reproduces the
  exact precondition against the live trial system — open a connection, invalidate its handle
  out-of-band, then assert close/teardown no longer throw.

## v2026.05.30 — RFC authorization reference: `sap_rfc_authorizations()`

Addresses [#71](https://github.com/DataZooDE/erpl/issues/71): there was no documentation of which
SAP RFC function modules ERPL invokes, which an admin needs to grant least-privilege `S_RFC`
authorizations to the ERPL service user.

### Discovery & metadata

- **[rfc]** New table function `sap_rfc_authorizations()` — a static, **connection-free** reference
  (no secret required, makes no RFC calls) listing which RFC function modules each ERPL function
  invokes, across `erpl_rfc`, `erpl_bics` and `erpl_odp`. Columns: `extension`, `duckdb_function`,
  `rfc_function_module`, `invocation` (`always` / `fallback` / `optional` / `metadata` /
  `user-specified`), and `purpose`. Use it to scope `S_RFC`, e.g.
  `SELECT DISTINCT rfc_function_module FROM sap_rfc_authorizations() WHERE rfc_function_module NOT LIKE '<%>'`.
  The mapping accounts for the runtime nuances — `sap_read_table`'s capability-dependent
  `RFC_READ_TABLE` fallback chain, the optional `RPY_FUNCTIONMODULE_READ`, shared
  `DDIF_FIELDINFO_GET`, and `sap_rfc_invoke`'s caller-supplied module. (DuckDB's function `comment`
  and `tags` fields can't be set for C++ functions in v1.5.3, so a queryable table is used — which
  also documents pragmas uniformly.)

## v2026.05.29 — BICS result: NULL date marshalling fix

Fixes [#72](https://github.com/DataZooDE/erpl/issues/72): `sap_bics_result` failed with
`Failed to adapt field 'HIERARCHY_DUEDATE' in structure 'I_TH_CHARACTERISTICS': Calling
GetValueInternal on a value that is NULL`. A recent SAP BW addition put a `HIERARCHY_DUEDATE`
(DATE) field into the characteristics structure that `sap_bics_result` round-trips back to SAP via
`BICS_PROV_SET_STATE`; it is NULL for non-hierarchy characteristics.

### SAP scan path

- **[rfc]** Harden the RFC argument marshaller (`RfcType::AdaptValue`): a NULL DuckDB value for a
  scalar field is now left at the SDK's initial value, instead of crashing. The `DATE`, `TIME`,
  `NUM`, `INT`/`INT1`/`INT2`/`INT8`, `BCD`/`DECF16`/`DECF34` and `FLOAT` branches previously called
  the value converter on the NULL without a guard (throwing `Calling GetValueInternal on a value
  that is NULL`), whereas `CHAR`/`STRING`/`XSTRING`/`BYTE`/`UTCLONG` already skipped NULLs — this
  closes that gap with a single guard. Because the fix is in the shared `erpl_rfc` marshalling
  layer, it also hardens **[bics]** `sap_bics_result` (the reported case) and any `sap_rfc_invoke`
  / **[odp]** call that passes a NULL date/time/number inside a structure or table parameter.
  Covered by an offline regression test (no live SAP needed) plus the full RFC and BICS SQL suites.

## v2026.05.28 — Wide-scan memory: stream RFC results, bound the SDK buffer

Continuation of [#63](https://github.com/DataZooDE/erpl/issues/63), addressing
[#69](https://github.com/DataZooDE/erpl/issues/69): a full `SELECT *` over a wide
table (BSEG, ACDOCA — hundreds of columns, ~75k+ rows) climbed to **>8 GB**, ran
slowly, and the memory was never released after the connection closed. This is
**not** a regression of the #63 `LIMIT`-path fix — that work (warm-up batching,
`ROWCOUNT` cap, persistent connections, LOAD copy-elision) stays intact. #69 was a
distinct, never-addressed layer. heaptrack on a 100k-row × 153-column scan
attributed the 8.2 GB peak to two separate causes, each fixed below.

### SAP scan path

- **[rfc]** The `sap_read_table` scan now **streams rows straight from the SAP SDK
  result-table handle into the output vector**, instead of first materializing each
  batch as a tree of `duckdb::Value` objects. That intermediate representation was
  responsible for ~95% of all heap allocations (285 M allocations on the test scan)
  — the real driver of the high CPU and of the heap fragmentation that kept RSS
  elevated after the query. The conversion reuses the exact same per-cell logic as
  before, so results are byte-for-byte identical; `sap_rfc_invoke` and other callers
  of the materializing path are unchanged. A `malloc_trim` at scan teardown returns
  freed arenas to the OS on glibc.

- **[rfc]** New `erpl_rfc_read_table_batch_budget` extension option (UINTEGER,
  default `1310720`) bounds the **SAP SDK's own result buffer**, which dominated the
  remaining peak (~6.7 GB): RFC_READ_TABLE materializes every row's fixed-width work
  area inside `libsapnwrfc`, and every projected column reads in parallel. The option
  is a *concurrent-row* budget — a cap on `columns × per-column batch size` — so it
  self-adapts to table width: wider tables automatically use smaller batches, while
  narrow tables keep the full batch for throughput. Lower it to cap memory harder (at
  the cost of more RFC round-trips); raise it for fewer round-trips; `0` disables the
  cap. The `ROWSKIPS % ROWCOUNT == 0` invariant from #63 is preserved (power-of-two
  cap). On the 100k × 153-col scan, peak RSS dropped **8.2 GB → 1.78 GB** at the
  default (≈0.9 GB on a 340-column BSEG; 0.55 GB at a 256k budget), with the memory
  ↔ round-trip tradeoff now under user control.

## v2026.05.27 — `SHOW TABLES` on attached catalogs

Fixes [#70](https://github.com/DataZooDE/erpl/issues/70): `SHOW TABLES FROM <attached
sap catalog>` returned no rows. A SAP system exposes tens of thousands of tables
(~55k on the ABAP trial), and each catalog entry needs a per-table dictionary
roundtrip to discover its schema, so the catalog cannot eagerly enumerate everything.

### SAP scan path

- **[rfc]** The `TABLES` ATTACH option now accepts glob patterns (`*`, `?`) in
  addition to exact names, e.g. `ATTACH '' AS sap (TYPE sap_rfc, TABLES '/DMO/*,Z*')`.
  Patterns are resolved once, at ATTACH time, against `DD02V` (the same dictionary
  view `sap_show_tables()` uses). `SHOW TABLES` and `information_schema.tables` then
  list the resolved, bounded set, and tables outside it are not exposed through the
  catalog. A safety cap (5000 tables) rejects unbounded patterns with an actionable
  hint. Without `TABLES`, tables remain resolvable on demand by name and `SHOW TABLES`
  stays empty by design — use `sap_show_tables()` to browse the full catalog.

## v2026.05.24 — Wide-table CPIC exhaustion fix

Targeted follow-up to [#67](https://github.com/DataZooDE/erpl/issues/67), reported
two days after v2026.05.22 against BSEG (~401 cols) and ACDOCA (~511 cols) on a
real S/4 system. `SELECT * FROM <attached> LIMIT 10` hung ~5 min then failed with
`Could not complete read task after 5 attempts`.

### SAP scan path

- **[rfc]** Cap the persistent-connection pool to 16 per scan (configurable
  via the new `erpl_rfc_max_persistent_connections` extension option, default
  16, set to 0 to disable persistent caching entirely). Wide-table scans
  previously opened one persistent RFC connection per column state machine
  and exhausted the SAP SDK's `MAX_CPIC_CONVERSATIONS=200` ceiling; the SDK
  then returned `RFC_COMMUNICATION_FAILURE "max no of 200 conversations
  exceeded"` and ERPL's retry loop (5×, exponential backoff) bailed out.
  State machines beyond the cap now fall back to per-batch open/close —
  slower than caching but never starves the CPIC budget. 16 leaves ~4×
  headroom over the ~3-4 effective intra-process concurrency the SAP gateway
  can sustain, while staying well clear of any plausible SAP-side limit
  (gateway `max_conn` ~500, DWP pool, HANA worker limits). Verified on a
  400-col × 100k-row synthetic wide table on the `a4h` trial: full
  `COPY (SELECT *) TO '/dev/null'` went from a 5-min hang to 111s clean.

### Errors and diagnostics

- **[rfc]** Cleaner error message when an `ATTACH ... TYPE sap_rfc` reference
  cannot be resolved — actionable hints for the four most common causes
  (no secret, bad secret, table missing from DDIC, server-side timeout) in
  place of the previous bare INTERNAL Error stack.

### Tooling

- **[rfc]** New reproduction harness under `tools/issue-67-repro/` (CDS DDL
  generator, populator ABAP class, end-to-end recipe via `erpl-adt`). 400
  columns × 23 type families × 100 000 rows on the `a4h` trial. Reusable for
  any future wide-table regression coverage.

---

## v2026.05.22 — DuckDB 1.5.3 support, wide-table memory fix, NULL date handling

First tagged release. Aggregates work landed on `master` over the past month,
including the DuckDB 1.5.3 build matrix bump and two reported correctness
fixes against the SAP RFC scan path.

### Scan path

- **[rfc]** **Memory regression on wide-table `LIMIT N` queries fixed (issue
  #63, PR #65).** `SELECT * FROM <attached>.<wide_table> LIMIT 10` against a
  350-column SAP table previously consumed > 16 GB of RAM and OOMed; it now
  lands in a few hundred MB. Three layers:
  - `RfcReadColumnStateMachine` now uses **divisibility-preserving warm-up
    batching**: each scan starts at `STANDARD_VECTOR_SIZE = 2048` rows and
    doubles only at moments when the running offset is divisible by the new
    size, preserving the ABAP server's `ROWSKIPS % ROWCOUNT == 0` invariant
    while keeping the first batch tiny so `LIMIT N` queries terminate cheaply.
  - **Persistent per-state-machine RFC connection + `RfcGetFunctionDesc`
    cache**, behind the new `erpl_rfc_persistent_connections` toggle
    (default `true`). Cuts the number of `RfcOpenConnection` round-trips per
    scan by ~80 %. Thread-affine so the SAP NW RFC SDK's "one connection
    per thread" rule stays satisfied when DuckDB's task scheduler routes a
    later batch to a different worker.
  - **LOAD-path data-copy elimination.** The hot loop that hands batches to
    DuckDB used to duplicate the full `LIST<VARCHAR>` children vector on
    every chunk delivery. Allocations in `LoadNextBatchToDuckDBColumn`
    dropped 95.6 % and temporary allocations 99 % (heaptrack).
- **[rfc]** **NULL DATS / TIMS values now round-trip as SQL NULL (issue #64,
  PR #66).** Empty / all-zero / all-spaces SAP date and time fields
  previously surfaced as `1970-01-01` or as wild values like
  `1439716-08-13`; they now produce proper NULL with type `DATE` / `TIME`.
- **[rfc]** **`MAX_ROWS = N` no longer rejects with `OPTION_NOT_VALID`** when
  N is between `STANDARD_VECTOR_SIZE` and `MAX_BATCH_SIZE` (Codex review
  follow-up to #65).
- **[rfc]** **SAP tables exposed as real `TableCatalogEntry` instead of
  view-wrapping** (PR #65). Unlocks projection pushdown into the underlying
  RFC scan when using ATTACH; earlier the view body hid the table function
  from the optimizer.

### Configuration

- **[rfc]** New option `erpl_rfc_persistent_connections` (default `true`) —
  toggle the RFC connection + function descriptor cache.

### Tests

- C++ unit suite: 297 assertions across 81 cases (full `erpl_rfc_tests` run).
- SQL suite against the ABAP Platform Trial: `sap_read_table.test` 55/55,
  `sap_rfc_attach.test` 37/37, `sap_rfc_attach_limit.test` 6/6 (new — covers
  the ATTACH + `LIMIT N` path end-to-end), `sap_rfc_invoke.test` 87/87.

### Known residuals

- Inside `libsapnwrfc.so`, `RfcTable::resetWithCapacity` still allocates the
  per-call SDK row buffer sized to `ROWCOUNT`. The warm-up keeps `ROWCOUNT`
  small for `LIMIT N`; for bulk scans this cost is unavoidable from the
  client side.
- `RfcResultSet` still materialises every cell as `duckdb::Value`. Writing
  directly into `FlatVector` would shave another ~30 % off the conversion
  path. Tracked as follow-up.

---

## v2026.05.20 — DuckDB v1.5.3 support

- **[all]** **DuckDB v1.5.3 added to the build matrix** (PR #61). Every
  subsequent tagged release ships binaries for both DuckDB v1.4.4 (LTS) and
  v1.5.3.
- `extension-ci-tools` submodule intentionally not bumped — same precedent
  as the v1.5.0 → v1.5.2 bump.

---

## v2026.05.17 — `sap_read_table` parallelism, `sap_rfc_invoke` hardening, and BICS AfO-style char properties

A big week — RFC robustness improvements landed alongside a meaty BICS
release pulled in via submodule bumps.

### RFC

- **[rfc]** **Five-round fuzz/verification pass against the ABAP Cloud
  Developer Trial** (PR #60). Twelve distinct bugs surfaced and fixed
  across the RFC scanner and `sap_rfc_invoke` marshalling, plus two
  previously-stubbed code paths implemented and one missing fallback added.
- **[rfc]** **`/SAPDS/RFC_READ_TABLE2` TBLOUT auto-discovery**: the scan
  now walks `/TBLOUT128 → /TBLOUT512 → /TBLOUT2048 → /TBLOUT8192 →
  /TBLOUT30000` on the same RFC invocation when the bind-time-chosen output
  table is empty for the projected row width.
- **[rfc]** **`sap_rfc_invoke` correctness**: empty function signatures,
  NULL, BLOB, path-to-scalar, positional arguments, and missing BYTE /
  UTCLONG I/O all handled correctly.
- **[rfc]** **`sap_rfc_describe_function` SDK-only fallback** when RPY
  trips an FL180 short-dump on older ABAP stacks.
- **[rfc]** 95 new test assertions across new and existing test files
  (57 fuzz + 14 + 7 + 17).

### BICS

- **[bics]** **`sap_bics_set_char_prop(state, char, prop, value)`** — new
  function for AfO-style characteristic properties: display KEY / TEXT /
  BOTH, sort ASC / DESC / NONE, totals SHOW / HIDE.
- **[bics]** **AfO-style hierarchy workflow** test coverage (query Q0010).
- **[bics]** **DisplayMode-aware result rendering** — TEXT for `0CALMONTH`,
  BOTH for `0D_NW_DIV` and similar pre-configured InfoObject properties
  are now honoured by the result parser.
- **[bics]** **Variable-less queries supported** — `E_VARIABLE_CONTAINER_HANDLE
  = '0000'` is now treated as legitimate rather than an error.
- **[bics]** **Multi-structure characteristic lookup** + **dynamic
  key-figure fallback** in result rendering.
- **[bics]** **`/E_T_COLUMNS` column-index map fixed** after
  `sap_bics_filter` injects structural header entries.
- **[bics]** **`bics-tui`** — Terminal UI example with Background Filter +
  Properties panel + SQL recorder, plus screenshots, capture script, and
  `__main__.py` entrypoint.
- **[bics]** New AfO workflow + property-regression test coverage.

### Docs

- `API_REFERENCE.md` updated with the new `sap_bics_set_char_prop` function,
  its prop/value matrix, and a sample end-to-end workflow combining filter
  + set_char_prop + result.

---

## v2026.05.14 — Type-system robustness, catalog-qualified function calls, ODP signature fix

### RFC

- **[rfc]** **VARCHAR fallback for unsupported SAP RFC types (issue #53,
  PR #59)**. Fields whose RFC type ERPL cannot natively map are surfaced as
  `VARCHAR` instead of failing the bind. Strict behaviour available via
  `SET erpl_rfc_strict_type_check = true`.
- **[rfc]** **`RFCTYPE_INT8` mapping fixed**: previously mapped to
  `LogicalType::INTEGER` (32-bit), now correctly `LogicalType::BIGINT`
  (64-bit) — matching `rfc2duck(RFC_INT8)` which already returned
  `Value::BIGINT()`.
- **[rfc]** 15 missing `ConvertRfcValue` cases filled in the schema switch.
- **[rfc]** **Catalog-qualified SAP function calls (issue #55, PR #58)**.
  After `ATTACH '' AS sap_s4 (TYPE sap_rfc, SECRET 'my_secret')` the seven
  SAP table functions become accessible as `sap_s4.sap_rfc_invoke(...)`,
  `sap_s4.sap_read_table(...)`, etc. The attachment secret is injected
  automatically. Multiple ATTACHes to different SAP systems coexist in one
  session, each routing through its own secret.

### BICS

- **[bics]** Submodule bumped — BICS metadata scanners now use real RFC
  table schemas; `RfcReadTableHelper` fixes from the rfc-side type work
  cascade through.

### ODP

- **[odp]** Consistent `SECRET` named parameter across `sap_odp_*`
  functions.
- **[odp]** Named parameters enumerated in function descriptions.
- **[odp]** `sap_odp_drop` documented with its real signature —
  `PRAGMA sap_odp_drop(odp_context, subscriber_name, subscriber_process,
  odp_name)`. The stale `PRAGMA odp_drop(subscription_id)` placeholder is
  removed from `API_REFERENCE.md`.

### Configuration

- **[rfc]** New option `erpl_rfc_strict_type_check` (default `false`).

---

## v2026.05.10 — Process stability fixes

- **[all]** **No more SIGSEGV when ERPL is loaded twice in the same
  process (issue #52, PR #56)**. Touches the trampoline plus every
  sub-extension. Three root causes addressed: trampoline self-pinning via
  `dlopen(RTLD_NODELETE)` so `dlclose` cannot drop the SAP SDK's
  `RTLD_GLOBAL` entries; ordering fixes around sub-extension teardown;
  protection against double initialisation of the telemetry singleton.
- **[all]** **`SapDefaultGenerator` no longer disabled after the first
  scan (issue #54, PR #57)**. Subsequent SAP table lookups in the same
  session keep working when other extensions (e.g. Ducklake) are also
  attached. Root cause was DuckDB's `CatalogSet::CreateDefaultEntries()`
  setting `created_all_entries = true` on the empty loop, after which our
  generator was never consulted again. Fix touches `sap_storage` in all
  sub-extensions that register a generator.

---

## v2026.05.02 — Build pipeline hardening, OpenSSL/Windows fixes

- **[rfc]** **`erpl_rfc` function metadata** (PR #51). `description`,
  `examples`, `categories`, and `parameter_names` registered on all
  `erpl_rfc` table functions so they appear in `duckdb_functions()` with
  rich documentation. (The same metadata for `erpl_bics`, `erpl_odp`, and
  `erpl_tunnel` landed earlier in v2026.04.09.)
- **[all]** **Post-build smoke tests across the full matrix**
  (`scripts/smoke-test.sh`, `smoke-test-musl.sh`, `smoke-test.ps1`).
  Download the matching DuckDB CLI, install the built artifact, and verify
  the SAP functions register correctly. Integrated into CI for all 5
  platforms × 3 DuckDB versions.
- **[all]** **OpenSSL `atexit` shutdown crash fixed** in *every*
  sub-extension. `openssl/ssl.h` registers an `atexit` cleanup hook on
  first use; combined with the SAP SDK's `RTLD_GLOBAL` symbol layout it
  triggered a SIGSEGV on extension unload. Suppression applied in
  `erpl_rfc`, `erpl_bics`, `erpl_odp`, `erpl_tunnel`, and the trampoline.
- **[all]** **Windows macros (`ERROR`, `WARN`, `INFO`, …) undef'd after
  the openssl include** in all sub-extensions — wingdi/windows.h macros
  were shadowing log-level identifiers.
- **[bics, odp]** **`parameter_types` removed from `FunctionDescription`**
  (root-cause fix) and then **restored** after the removal introduced a
  new v1.5.1 crash. Net result: both `erpl_bics` and `erpl_odp` keep the
  field, with the right type matrix.

---

## v2026.04.16 — DuckDB v1.5.2

- **[all]** **DuckDB v1.5.2 added** (PR #50). v1.4.4 LTS build unchanged.

---

## v2026.04.09 — `duckdb_functions()` metadata across all sub-extensions

- **[bics, odp, tunnel, trampoline]** **All 47 public table functions
  wrapped with `CreateTableFunctionInfo`** (PR #48). `description`,
  `examples`, `categories`, and `parameter_types` now populated in
  `duckdb_functions()`. `loader.SetDescription()` wired into each
  `LoadInternal()` so `duckdb_extensions()` output is complete too.
  (The corresponding `erpl_rfc` metadata work landed in v2026.05.02.)
- **[all]** Removes the deprecated `beads/bd` issue tracker, the
  pre-commit / post-merge hooks, and tracked `.beads/` files. `CLAUDE.md`
  updated to forbid AI attribution in commit messages.

---

## v2026.03.28 — DuckDB v1.5.1

- **[all]** **DuckDB v1.5.1 added** to the build matrix. v1.4.4 LTS and
  v1.5.0 builds unchanged at this point. (DuckDB v1.5.1 introduced a
  parameter-type-validation crash that was later worked around in
  v2026.05.02; v1.5.0 was dropped some weeks later when v1.5.2 landed.)

---

## v2026.03.11 — DuckDB v1.5.0 support

- **[all]** **DuckDB v1.5.0 added** (PR #47); v1.4.3 dropped; v1.4.4
  retained as LTS.
- **[all]** `extension-ci-tools` submodule moved to its v1.5.0 branch.
- **[rfc]** `rfc/src/sap_storage.cpp`: uses new
  `StorageExtension::Register()` static method — `config.storage_extensions[]`
  map was removed in v1.5.
- **[rfc, bics, odp]** Test CMakes link `dummy_static_extension_loader` —
  required by v1.5 because `LoadAllExtensions` moved out of
  `libduckdb_static`.

---

## v2026.02.11 — ATTACH syntax for SAP RFC, CDS view fix, BICS/ODP hardening

### RFC

- **[rfc]** **`ATTACH '' AS sap (TYPE sap_rfc, SECRET 'my_secret')` syntax
  (PR #45)**. Implements `StorageExtension` for the `sap_rfc` type so SAP
  systems can be ATTACHed as catalogs. A `DefaultGenerator` lazily creates
  views wrapping `sap_read_table()` for on-demand table resolution.
  - `SECRET` named parameter on `sap_read_table` for explicit secret
    selection.
  - Optional `TABLES` option (comma-separated) to whitelist accessible
    tables.
- **[rfc]** **CDS view scan no longer crashes (issue #42, PR #43)**.
  `sap_read_table` previously errored with `"Unsupported SAP table type
  name: NODE"` when reading CDS views like `/DMO/R_Booking_D`. New
  `RfcType::IsKnownDataType()` whitelist filters non-data DFIES entries
  (NODE, STRU, etc.) in `GetTableFieldMetas()` before they reach type
  mapping. `sap_describe_fields()` continues to surface NODE entries — it
  is metadata-only.

### BICS

- **[bics]** **Four unguarded `std::stoi` calls in metadata scanners
  hardened** — they no longer crash the process when SAP returns a
  non-numeric value where ERPL expected a digit string.
- **[bics]** 17 new test assertions across 6 test files cover the
  crash-prone metadata-scanner paths.

### ODP

- **[odp]** **Crash fixes and expanded test coverage** in the ODP scanner
  (submodule bump).

---

## v2026.01.28 — DuckDB v1.4.4 support (modern build matrix baseline)

- **[all]** **DuckDB v1.4.4 added** (PR #41). Marks the start of the
  current release/build matrix used by ERPL.
- **[all]** v1.4.2 and v1.4.3 support retained at this point (later
  removed in successive releases).

---

## Earlier history (pre-2026, not tagged)

Older work lives in the git log and is not retroactively tagged. Highlights:

- **2025-03**: maintenance update to DuckDB v0.10.3 build (#39). [all]
- **2024-06**: DuckDB v1.0.0 build (#34); OSX Apple Silicon support (#37).
  [all]
- **2024-04**: DuckDB v0.10.2 build (#26 – #29). [all]
- **2024-03**: DuckDB v0.10.1 build, Docker integration, ABAP trial
  update, initial DECIMAL parsing fix (#17, #19, #20, #23). [rfc]
- **2024-02**: project bootstrap — DuckDB v0.10.0 support (#10),
  BCD/decimal type conversion (#8, #14), first working
  `sap_odp_read_full` (#4). [rfc, odp]

For commit-level history, run `git log --oneline --no-merges` in the
repository.
