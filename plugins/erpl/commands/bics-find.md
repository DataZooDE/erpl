---
description: Find SAP BW queries or cubes by name or text and describe one — characteristics, key figures, variables
argument-hint: "<search text> [secret name]"
---

Find BW queries or cubes matching: $ARGUMENTS

Use the `erpl-bics-queries` skill.

**How to run SQL.** DuckDB CLI with `-unsigned`, script through stdin, `LOAD erpl;` first, never several
statements in one `duckdb -c` string, never print passwords. Name the secret if more than one `sap_rfc`
secret exists.

1. Search queries and cubes (wildcards `*` allowed; search both key and text):
   `SELECT technical_name, text, cube_name FROM sap_bics_show_queries(search => '*<text>*');` and
   `SELECT technical_name, text FROM sap_bics_show_cubes(search => '*<text>*');`.
   Show at most ~20 hits as a table; if there are more, ask for a narrower term.
2. When the user picks one (or there is a single hit), describe it:
   - `SELECT technical_name, text, characteristics, keyfigures FROM sap_bics_describe('<cube>', '<query>');`
     (for a cube: `sap_bics_describe('<cube>')`). Unnest `characteristics` and `keyfigures` into two short
     lists (technical name, text).
   - `SELECT name, text, mandatory, input_enabled, var_type, selection_type, reference_char FROM
     sap_bics_variables('<query>');` — highlight mandatory input-ready variables.
3. Finish with the exact `/erpl:bics-run` call that would open it, including placeholders for the mandatory
   variables.
