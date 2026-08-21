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

# ---------------------------------------------------------------------------------------
# Return codes.
#
# sapnwrfc.h's RFC_RC is an unnumbered enum, so every value is an ordinal you can only get
# right by counting declarations -- and five of the shim's were once wrong. Nothing about
# that is visible at a call site: the consumer branches on a code that means something
# else. erpl's RfcConnection::Close treats RFC_INVALID_HANDLE as benign, so receiving 8
# (RFC_TIMEOUT) instead of 13 turned every close of a stale handle into a thrown
# IOException. Worth a permanent gate.
PROTO_ABI="$REPO_DIR/proto/crates/erpl-proto-nwrfc/src/abi.rs"
SDK_HEADER="$REPO_DIR/nwrfcsdk/linux/include/sapnwrfc.h"

if [[ ! -f "$PROTO_ABI" || ! -f "$SDK_HEADER" ]]; then
	echo "NOTE: skipping the return-code check (needs both proto/ and the SDK headers)."
	exit 0
fi

python3 - "$SDK_HEADER" "$PROTO_ABI" <<'PYTHON'
import re, sys

header, abi = open(sys.argv[1]).read(), open(sys.argv[2]).read()

body = re.search(r'typedef enum _RFC_RC\s*\{(.*?)\}\s*RFC_RC;', header, re.S).group(1)
names = []
for line in body.splitlines():
    line = re.sub(r'///<.*|/\*.*?\*/', '', line).strip().rstrip(',').strip()
    if line and not line.startswith('/'):
        names.append(line)
sdk = {name: ordinal for ordinal, name in enumerate(names)}

shim = {m.group(1): int(m.group(2))
        for m in re.finditer(r'pub const (RFC_[A-Z0-9_]+):\s*RFC_RC\s*=\s*(\d+);', abi)}

mismatches = []
for name, value in sorted(shim.items(), key=lambda kv: kv[1]):
    expected = sdk.get(name)
    if expected is None:
        print(f"  ?  {name} = {value} is not an RFC_RC enumerator in the SDK header")
    elif expected != value:
        collision = next((k for k, v in sdk.items() if v == value), '?')
        mismatches.append(f"{name}: shim says {value} ({collision}), SDK says {expected}")

print(f"checked {len(shim)} RFC_RC constants against the SDK enum")
if mismatches:
    print("\nRETURN CODE MISMATCHES:")
    for m in mismatches:
        print(f"  {m}")
    print("\nA consumer branching on one of these takes one error for another.")
    sys.exit(1)
print("OK: every RFC_RC constant the shim defines matches the SDK enum.")
PYTHON
