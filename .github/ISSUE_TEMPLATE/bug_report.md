---
name: Bug report
about: Something does not work as documented
title: ''
labels: ''
assignees: ''

---

**What happens**
A clear description of the problem, and the exact error text if there is one.

**To reproduce**
The SQL that shows it, with names you can share:

```sql
CREATE SECRET ...;
SELECT ...;
```

**Expected behavior**
What you expected instead, and why (e.g. the same query in RSRT shows values).

**Diagnostics**
See [TROUBLESHOOTING.md](../blob/master/TROUBLESHOOTING.md). For a BICS query, paste the
output of `sap_bics_result_stats('<state_id>')` here; it holds counts only, no data:

```
```

Any `[WARN]` lines from `SET erpl_trace_enabled = true; SET erpl_trace_level = 'WARN';`:

```
```

**Environment**
- ERPL: output of `SELECT extension_name, extension_version FROM duckdb_extensions() WHERE extension_name LIKE 'erpl%'`
- DuckDB version and client (CLI, Python, ...), OS and architecture
- SAP release (`bw_release` from the stats row, or NetWeaver / S/4 / BW/4 version)
- RFC backend, if you changed it (`erpl_rfc_backend`)

**Additional context**
Anything else that may matter. Raw `erpl_bics_trace` files contain your data; offer them
rather than attaching them.
