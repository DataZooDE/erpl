#!/usr/bin/env bash
#
# Checks that the erpl-proto backend exports every RFC entry point erpl dispatches
# through. This is the contract between the two repositories, and it is worth a gate of
# its own: a missing entry point is otherwise found at runtime, on whichever SAP call
# happens to need it first, on whichever test happens to run first.
#
# The expected list is read from rfc/src/include/sap_rfc_api.hpp rather than kept here,
# so the two cannot drift.
#
# Usage: check_proto_backend_symbols.sh [path-to-liberpl_proto_nwrfc.so]

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="$(dirname "$SCRIPT_DIR")"
HEADER="$REPO_DIR/rfc/src/include/sap_rfc_api.hpp"
LIBRARY="${1:-$REPO_DIR/build/debug/liberpl_proto_nwrfc.so}"

[[ -f "$HEADER" ]]  || { echo "ERROR: $HEADER not found" >&2; exit 1; }
[[ -f "$LIBRARY" ]] || { echo "ERROR: $LIBRARY not found. Build it first (GEN=ninja make debug_tests)." >&2; exit 1; }

# The X(...) entries of ERPL_RFC_API_ENTRY_POINTS, and nothing else in the file.
expected="$(sed -n '/#define ERPL_RFC_API_ENTRY_POINTS/,/^$/p' "$HEADER" \
	| grep -oE '^\s*X\([A-Za-z0-9_]+\)' | grep -oE '\(.*\)' | tr -d '()' | sort -u)"

expected_count="$(wc -l <<< "$expected")"
if [[ "$expected_count" -lt 2 ]]; then
	echo "ERROR: could not parse the entry-point list out of $HEADER (got $expected_count)." >&2
	exit 1
fi

# SAP's own library versions its symbols (RfcOpenConnection@@libsapnwrfc.so), so the
# version suffix is stripped -- dlsym resolves the default version under the bare name,
# which is how erpl reaches them. The proto shim carries no version suffix.
exported="$(nm -D --defined-only "$LIBRARY" | awk '$2 == "T" { sub(/@.*/, "", $3); print $3 }' | sort -u)"

missing="$(comm -23 <(echo "$expected") <(echo "$exported"))"

echo "erpl dispatches $expected_count RFC entry points"
echo "$(basename "$LIBRARY") exports $(wc -l <<< "$exported")"

if [[ -n "$missing" ]]; then
	echo
	echo "MISSING from the proto backend:"
	sed 's/^/  /' <<< "$missing"
	echo
	echo "erpl would fail at runtime on the first call to any of these. Either implement"
	echo "them in erpl-proto, or stop calling them from erpl."
	exit 1
fi

echo "OK: the proto backend provides every entry point erpl needs."
