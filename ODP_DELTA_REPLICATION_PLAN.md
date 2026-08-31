# Plan — Implement ODP Delta Replication (red/green TDD)

Status: **SHIPPED + PATCHED.**
- **v2026.06.27** — ODP delta replication. Validated 26/26; first Codex review's
  11 findings fixed. PRs erpl-odp#1 (→ `93e9fd9`) + erpl#87 (→ master `a742ab8`).
- **v2026.06.28** — ODP delta hardening. A *second* Codex review of the shipped
  fix diff caught one new regression (CloseSession() not noexcept on the
  destructor path → `std::terminate` risk); fixed + released. PRs erpl-odp#2
  (→ `d934af7`) + erpl#88 (→ master `08db6f6`).
- Both releases live on get.erpl.io (DuckDB v1.4.5 + v1.5.4, all platforms).
- Customer email to Rakesh drafted + copied to clipboard (awaiting send).
Branch: `claude/odp-delta-replication` (odp submodule)
Date: 2026-06-27

## ⚠️ Major discovery (2026-06-27)

The feature was **already implemented** in a local, unpushed commit on the `odp`
submodule: **`a816ee3` "Add ODP delta cursor support"** (one commit ahead of the
pushed `origin/main`/gitlink `e35698b`). The debug binary was already built from
it, which is why `sap_odp_read_delta`, `sap_odp_close_delta_cursor`,
`sap_odp_get_last_modified`, and `sap_odp_get_subscriptions` all resolve in the
running binary even though the *checked-out* gitlink lacks them.

`a816ee3` contains (3400+ LOC): all four scanners/pragmas, the `odp_fetch`/
`OdpOpenRequest` delta plumbing (`I_EXTRACTION_MODE` F/D/R, stable
subscriber_process, `CloseSession`), a full ABAP test harness
(`ZERPL_DLT_T`/`ZERPLDLTV` + SEED/INS/UPD/DEL/MIX/TRUNC classes, `run_delta_tests.sh`),
and **16 SQL test files**.

My branch was re-baselined (ff-merge) onto `a816ee3`. Steps 2–4 (build a fixture,
write read_delta, RED→GREEN) are therefore **already done**. The remaining,
now-primary work is the tail of this plan:

- **Step 5 — validate** the existing implementation end-to-end (`run_delta_tests.sh`
  real-change suite + `make sql_tests_odp`).
- **Step 6 — Codex review** of the `a816ee3` diff.
- **Step 7 — reconcile docs + land** (push odp `main`, bump parent gitlink).

The original step text below is retained for reference / gap-checking against
what `a816ee3` actually implements.

## Motivation

A prospect (Rakesh Thatikonda, Hunter Douglas) is evaluating ERPL for **ABAP_CDS
ODP delta replication into BigQuery** and asked which package exposes:

- `sap_odp_read_delta`
- `sap_odp_get_last_modified`
- `sap_odp_get_subscriptions`
- `sap_odp_close_delta_cursor`

### Technical-check finding (the real problem)

`API_REFERENCE.md` documents `sap_odp_read_delta` and
`PRAGMA sap_odp_close_delta_cursor` in detail, **but neither is implemented**.
The shipped `erpl_odp` registers only: `sap_odp_show_contexts`, `sap_odp_show`,
`sap_odp_describe`, `sap_odp_preview`, `sap_odp_read_full`,
`sap_odp_show_subscriptions`, `sap_odp_show_cursors`, `PRAGMA sap_odp_drop`.

Root cause: `OdpOpenRequest::CreateOpenArguments()` (`odp/src/odp_fetch.cpp:87`)
**hardcodes** `I_EXTRACTION_MODE = FULL`, and there is **no confirm/close** step,
so a delta pointer could never advance. The DELTA infrastructure already exists
(`OdpReplicationType::DELTA`→`"D"`, `OdpOpenResult::Pointer()/HasDeltaExtension()`,
`sap_odp_show_cursors`, `sap_odp_show_subscriptions`) — it is simply not wired
into any read path.

The other two customer names do not exist and are out of scope as new functions:
`get_subscriptions` ≈ existing `sap_odp_show_subscriptions`; `get_last_modified`
has no equivalent (closest is `sap_odp_show_cursors`, which exposes pointer/TSN).
These will be addressed in the email reply, not in code.

## Decisions (confirmed with maintainer)

- **Scope:** implement the documented surface **plus RECOVER mode** —
  `sap_odp_read_delta(...)` with `replication_mode=>'DELTA'|'RECOVER'` (default
  `DELTA`) and `PRAGMA sap_odp_close_delta_cursor(...)`.
- **Fixture:** build a **dedicated delta-enabled Z CDS** via `uvx erpl-adt`
  (deterministic, controllable deltas) as the *primary* fixture. Mirror
  annotation/populator patterns from `../erpl-rev/abap/` (note: erpl-rev
  deliberately avoids ODP-RFC, so it has no ready-made extraction CDS — we author
  one).
- **Source-type coverage:** delta must work for *all* ODP contexts, not just
  `ABAP_CDS`. The extraction protocol (`RODPS_REPL_ODP_OPEN/FETCH/CLOSE`) is
  context-agnostic, so the same code path serves every source type — but each
  type differs in delta semantics and must be exercised. See
  "ODP source-type matrix" below.
- **Repo flow:** `odp/` is the private `DataZooDE/erpl-odp` submodule. C++ changes
  land as a PR on the submodule `main`; a parent `erpl` PR then bumps only the
  `odp` gitlink (per `project_bics_submodule_pr_flow`).

## ODP source-type matrix

`sap_odp_show_contexts()` lists the contexts present on a system. Delta behaviour
and delta-capability differ per source type; the read/confirm/close code is shared,
but each must be tested (where the source exists on a4h):

| Context (source type) | Example | Delta mechanism | Test intent |
|---|---|---|---|
| `ABAP_CDS` (CDS extraction view) | our `ZERPL_ODP_DELTA` | timestamp/`byElement` ODQ delta | **primary** controllable fixture; exact N/M assertions |
| `SAPI` (classic DataSource / extractor) | `2LIS_*`, `0FI_*`, generic | native extractor delta (after-image / ABR via ODQ) | second source type if a delta-enabled DataSource exists on a4h |
| `BW` (InfoProvider / ADSO / DSO) | `0D_*$F`, ADSO | request/TSN delta | covered if BW content present on a4h |
| `HANA` (HANA Calc/Analytic view) | — | typically **full-only** | negative path: `supports_delta=false` → clear error |
| any **full-only** source | e.g. a CDS without the delta annotation | none | requesting `DELTA` must raise a clear, actionable error, not silently full-load |

Discovery on a4h drives which of these are realistically testable; at minimum we
must cover (1) ABAP_CDS delta end-to-end, (2) at least one **non-CDS** delta source
if one exists, and (3) a **full-only** source negative test. What is not present on
a4h is documented as a coverage gap (no silent omission).

## Environment (verified 2026-06-27)

- `a4h` container up (ADT `:50000` open, RFC `:3300`/sysnr `00` open).
- Debug build present at `build/debug/duckdb`.
- Credentials (from memory): host `localhost`, sysnr `00`, client `001`,
  user `DEVELOPER`, password `ABAPtr2023#00`.
- `ERPL_SAP_*` env vars are **unset** in the shell — must be exported before
  running `make sql_tests_odp`.

## Steps

### Step 1 — Live RFC discovery (read-only, no code)

Confirm the exact confirm/close module names + parameters before coding, using
`erpl_rfc` introspection against a4h:

- `sap_rfc_search_function('RODPS_REPL_ODP*')`
- `sap_rfc_describe_function('RODPS_REPL_ODP_CLOSE')` (and cursor-close siblings)

Known: `sap_odp_drop` → `RODPS_REPL_ODP_RESET` (hard reset). Need the **graceful
confirm/close** counterpart that advances the delta pointer / closes a cursor
while leaving the subscription intact.

Also enumerate what delta sources actually exist on a4h, to fix the test matrix:

- `sap_odp_show_contexts()` — which contexts are installed.
- For each, `sap_odp_show('<CONTEXT>')` + `sap_odp_describe(...)` filtered on
  `supports_delta = true` — pick one non-CDS delta source and one full-only
  source for the matrix above.

#### Discovered facts (verified live 2026-06-27)

- **`RODPS_REPL_ODP_CLOSE`** is the graceful confirm/close. Imports:
  `I_POINTER` (BCD `DECIMAL(23,9)`, **required**) = the extraction pointer from
  OPEN; `I_SUBSCRIBER_FAILED` (CHAR1, optional) = mark run failed so the pointer
  is **not** advanced; `I_DONT_STOP_REALTIME` (optional). Returns `ET_RETURN`
  (BAPIRET). → Confirming a delta run advances the pointer; this is what
  `CloseSession()` and the close pragma call.
- **`RODPS_REPL_ODP_OPEN`** imports include `I_SUBSCRIBER_PROCESS` (**required**,
  stable delta key), `I_EXTRACTION_MODE` (`F`/`D`/`R`, optional → default full),
  `I_RECOVERY_POINTER` (`DECIMAL(23,9)`, optional, **required for RECOVER** =
  pointer of the interrupted run), and `I_EXPLICIT_CLOSE` (optional). For delta
  we set `I_EXPLICIT_CLOSE='X'` so the pointer advances **only** on our explicit
  `RODPS_REPL_ODP_CLOSE` (FULL keeps auto-close, behaviour unchanged).
- **`RODPS_REPL_CURSOR_GET_LIST`** filters by `I_CONTEXT`, `I_ODPNAME`,
  `I_SUBSCRIBER_PROCESS` and returns the cursor `pointer` (already surfaced by
  `sap_odp_show_cursors`). One shared helper `ResolveOpenCursorPointer(context,
  process, odpname)` serves both the close pragma and the RECOVER lookup.
- **Customer-name mapping (for the email, not implemented as new functions):**
  `RODPS_REPL_ODP_GET_LAST_MODIF` exists → maps to the customer's
  `sap_odp_get_last_modified` (closest ERPL surface today: `sap_odp_show_cursors`
  pointer/TSN); `RODPS_REPL_ODP_GET_SUBSCR` → maps to `sap_odp_get_subscriptions`
  (ERPL: `sap_odp_show_subscriptions`).

#### Design consequences

- Delta/RECOVER reads run **single-threaded** (`threads=1`) so package ordering
  and the single explicit confirm are deterministic (deltas are small; FULL keeps
  its parallel path).
- `CloseSession()` (confirm) is called once when the scan drains
  (`E_NO_MORE_DATA`, all packages finished). On query cancel/error before that,
  the cursor stays open → recoverable via `replication_mode=>'RECOVER'` or
  cleaned up via `PRAGMA sap_odp_close_delta_cursor`.

### Step 2 — Build delta fixture via `uvx erpl-adt` (package `$TMP`)

1. `ZERPL_ODP_DT` — transparent Z table: key field(s) + `CHANGED_AT` UTC timestamp
   (delta column).
2. `ZERPL_ODP_DELTA` — CDS view entity over the table with:
   - `@Analytics.dataExtraction.enabled: true`
   - `@Analytics.dataExtraction.delta.byElement.name: 'ChangedAt'`
   - `@Analytics.dataExtraction.delta.byElement.maxDelayInSeconds: ...`
   - plus the `@AbapCatalog`/`@AccessControl` annotations ABAP-Cloud strictness
     requires (missing → HTTP 400 on activate).
3. `ZCL_ERPL_ODP_POP` (`IF_OO_ADT_CLASSRUN`) — seed N rows; mutate/insert M rows
   bumping `CHANGED_AT`.
4. Verify `sap_odp_describe('ABAP_CDS','ZERPL_ODP_DELTA').supports_delta = true`
   and that it appears in `sap_odp_show('ABAP_CDS', search='ZERPL_ODP_DELTA*')`.
5. Wrap in `odp/test/harness/setup_delta_fixture.sh`.

#### Fixture facts (built & verified live 2026-06-27)

- **ODP name is `ZERPL_ODP_DELTA$E`** — extraction CDS views get a `$E` suffix in
  the `ABAP_CDS` context; the bare `ZERPL_ODP_DELTA` returns 0 rows / not found.
- **`utclong` breaks nametab generation** on this trial kernel → the change
  column uses data element **`timestampl`**.
- **`@Semantics.systemDateTime.lastChangedAt: true` on the change element is
  mandatory** — the `@Analytics.dataExtraction.delta.byElement` annotation alone
  left `supports_delta=false` / `delta_modes=[]`. With the semantics marker:
  `supports_delta=true`, `delta_modes=[C (after-image, delta-init), U]`.
- Baseline: `SEED` resets to 10 rows; `MUTATE` yields 5 after-images (2 U + 3 C).
- `SEPM_ISOI$P` is `supports_delta=false` → the **full-only negative-test** source.
- a4h has contexts: `ABAP_CDS`, `BW`, `HANA`, `SAPI`.

### Step 3 — RED

`odp/test/harness/run_delta_tdd.sh` orchestrates erpl-adt mutations between
SQLLogicTest assertions (`sap_odp_read_delta_init.test`,
`sap_odp_read_delta_incr.test`):

1. `sap_odp_read_delta('ABAP_CDS','ZERPL_ODP_DELTA','TESTPROC')` → DELTAINIT
   returns N (full current snapshot).
2. populator adds M rows → second call returns **exactly M** changed rows.
3. `PRAGMA sap_odp_close_delta_cursor('ABAP_CDS','TESTPROC','ZERPL_ODP_DELTA')`
   → cursor no longer in `sap_odp_show_cursors()`; subscription still present in
   `sap_odp_show_subscriptions()`.
4. Re-runnable: leading `sap_odp_drop(...)` (ignore-fail) for a clean slate.

Run → fails (functions don't exist) = **RED**.

### Step 4 — GREEN

- `OdpOpenRequest`: add `SetReplicationType(OdpReplicationType)`; use it in
  `CreateOpenArguments()` instead of hardcoded FULL.
- `OdpFetchSession`: carry `_replication_type` + `_subscriber_process`; in
  `OpenSession()`, for delta/recover use the stable-process ctor +
  `SetReplicationType`; full path unchanged.
- `OdpFetchSession::CloseSession()`: confirm/advance the pointer on successful
  end-of-stream (delta only). Wire into the no-more-data completion path.
- `scanner_odp_read_delta.{hpp,cpp}`: `sap_odp_read_delta(context, name,
  subscriber_process)` + named params `columns/filters/threads/secret/
  replication_mode` (default `DELTA`, accepts `RECOVER`). Mirrors
  `scanner_odp_read.cpp`.
- `pragma_odp_close_delta_cursor.{hpp,cpp}`: graceful cursor close (≠ RESET),
  mirrors `pragma_odp_drop.cpp`.
- Register all in `erpl_odp_extension.cpp`.
- Rebuild `GEN=ninja make debug`; iterate to **GREEN**.

### Step 5 — Thorough testing phase

Beyond the single happy-path lifecycle from Step 3, build out a full test suite.

**5a. Offline C++ unit tests** (no SAP — run via `erpl_odp_tests` binary):
- `OdpReplicationType` mapping `FULL/DELTA/RECOVER ↔ "F"/"D"/"R"` and
  `FromNamedParams` (incl. invalid/empty → UNDEFINED).
- Open-argument builder emits the correct `I_EXTRACTION_MODE` + **stable**
  `I_SUBSCRIBER_PROCESS` for delta/recover, random for full.
- `CloseSession` confirm-argument construction (pointer/package wiring).
- Bind-time validation: missing `subscriber_process`, bad `replication_mode`.

**5b. SQL integration matrix** (a4h; one `.test` file per concern):
- **Lifecycle (ABAP_CDS):** DELTAINIT=N → mutate(M) → delta=M → close → cursor
  gone, subscription intact. (primary, exact counts)
- **Source-type coverage:** repeat the delta read against the non-CDS delta
  source found in Step 1 (counts `>= 0`/monotonic where deltas aren't
  controllable); document any context absent on a4h as a coverage gap.
- **Full-only negative:** `sap_odp_read_delta` on a `supports_delta=false`
  source → clear, actionable error (no silent full-load).
- **RECOVER:** after a simulated interruption (open+fetch without close),
  `replication_mode=>'RECOVER'` re-streams the last unconfirmed packet; counts
  match the interrupted packet.
- **Subscriber keying:** two distinct `subscriber_process` values track
  independent pointers (process A's delta unaffected by process B's reads).
- **Idempotent / empty delta:** a delta read with no source changes → 0 rows,
  pointer unchanged; re-running is a no-op.
- **Change semantics:** deletes/updates surface via `ODQ_CHANGEMODE` (insert
  `C`/`I`, update, delete `D`); assert the change-mode column is populated.
- **Projection + filters:** `columns=>[...]` and `filters=>[...]` on delta reads
  behave as on full reads.
- **close vs drop:** `sap_odp_close_delta_cursor` leaves the subscription
  (visible in `sap_odp_show_subscriptions`); `sap_odp_drop` removes it and forces
  re-DELTAINIT on next read.
- **Re-runnability:** every `.test` self-cleans (leading `drop`, trailing
  `close`/`drop`) so the suite is order-independent and repeatable.

**5c. Regression:** the full `make sql_tests_odp` suite (incl. `read_full`,
lifecycle, cursors, subscriptions) stays green — confirm the FULL path is
behaviourally unchanged.

**5d. Harness:** `odp/test/harness/` scripts set `ERPL_SAP_*`, (re)build the
fixture, drive erpl-adt mutations between phases, and tear down. One entrypoint
`run_delta_tdd.sh` runs the whole matrix from clean.

### Step 6 — Codex code review

Run a structured review of the implemented diff with the **`codex-delegate`**
skill, scoped to: **code simplicity, robustness, bugs, and security**.

- Provide Codex the submodule diff (read_delta scanner, close pragma, fetch/open
  plumbing, `CloseSession`) + this plan as context.
- Focus prompts:
  - *Simplicity:* duplication vs. `scanner_odp_read.cpp`/`pragma_odp_drop.cpp`;
    is the FULL/DELTA/RECOVER plumbing minimal?
  - *Robustness:* pointer lifetime/threading (`OdpFetchSession` mutex), confirm
    on partial failure / query cancel, RECOVER correctness, connection reuse.
  - *Bugs:* off-by-one in pointer/package handling, leak of cursors/subscriptions
    on error paths, wrong extraction mode leaking into FULL.
  - *Security:* subscriber_process / SQL-arg injection into RFC params, secret
    handling parity with existing scanners, no credential logging in traces.
- Triage findings → fix the actionable ones → re-run Step 5 to green. Record the
  review summary + dispositions in the PR description.

### Step 7 — Finalize

- Reconcile `API_REFERENCE.md` with shipped signatures (add `replication_mode`/
  RECOVER details, source-type notes); `CHANGELOG.md` entry.
- Submodule `erpl-odp` PR + parent `erpl` gitlink-bump PR (with Codex review
  summary).
- Draft the customer email: delta is implemented/landing; note the **April-2026
  SAP ODP-RFC API policy** caveat (relevant to a BigQuery replication use case).

## Validation results (2026-06-27)

**Real-change suite (`run_delta_tests.sh`): 25/26 pass.** Insert, update, delete,
mixed-LUW, drain→0, RECOVER replay, bulk multi-package, filters-on-delta, and
projection all pass. One failure: `S7[threads=4]` under-counted (saw 5 of 5005
rows; `threads=1` and `threads=8` both passed) — a parallel-DELTA-fetch race
(see Codex High-1), surfaced intermittently.

Harness bug found & fixed on this branch: `setup.sh` omitted the `BULK` class
(`for op in SEED INS UPD DEL MIX TRUNC` → added `BULK`); before the fix the 3
bulk-dependent scenarios all failed for lack of `ZCL_ERPL_DLT_BULK`.

## Codex review (2026-06-27) — verdict: **ship-with-fixes**

Full output: `trace/codex_review_final.txt`.

- **High-1 — parallel DELTA package race** (`odp_fetch.cpp`/`scanner_odp_read_delta.cpp`):
  any worker hitting `E_NO_MORE_DATA` sets `_no_more_packages`, which can stop
  other workers prematurely; matches the `threads=4` under-count. Fix: serialize
  DELTA/RECOVER fetch (force `threads=1`) or centralize package allocation.
- **High-2 — unlocked active-column read** (`GetResultTypes(true)` reads
  `_active_names_*` without `thread_lock` while `ActivateColumns` mutates under
  lock). Fix: lock/snapshot.
- **Med** — RECOVER ignores that `columns`/`filters` may differ from the original
  open; `subscriber_process` only checked non-empty (no length/char limits);
  `get_subscriptions` uses `.ToString()` not `GetValue<string>()`; `LookupCursor`
  ignores subscriber name (could close wrong cursor); `CloseSession` swallows all
  errors (hides FULL leaks).
- **Low** — `OdpReplicationType::variant` uninitialized default ctor; `threads=0`
  accepted; `CreateInitialPackage` check-then-lock TOCTOU; read_delta duplicates
  FULL scanner plumbing.

## Open risk

Exact confirm / cursor-close RFC signatures unverified until Step 1. Design
absorbs this — RECOVER mode is the interrupted-run safety net, and the confirm
call is isolated in `CloseSession()`.

## Checkpoints (pause for review)

1. After Step 3 (RED in place) — before writing C++.
2. After Step 4 (GREEN) — before the full test matrix.
3. After Step 6 (testing + Codex review complete) — before opening PRs.
4. Before sending the customer email.
