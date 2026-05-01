#!/usr/bin/env bash
# Post-build ERPL smoke test for the linux_amd64_musl target.
# There is no official musl DuckDB CLI release zip, so this script runs the
# test inside an alpine:3 Docker container using the pip duckdb musllinux wheel.
#
# Usage:
#   ./scripts/smoke-test-musl.sh [/path/to/erpl.duckdb_extension]
#
# Environment variables (all optional):
#   DUCKDB_GIT_VERSION   e.g. "v1.5.1" — overrides submodule auto-detection
#   EXTENSION_PATH       path to erpl.duckdb_extension — overrides positional arg
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJ_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

# ── 1. Resolve extension path ────────────────────────────────────────────────
EXTENSION_PATH="${EXTENSION_PATH:-${1:-}}"
if [[ -z "$EXTENSION_PATH" ]]; then
    EXTENSION_PATH="$PROJ_DIR/build/release/extension/erpl/erpl.duckdb_extension"
fi
EXTENSION_PATH="$(realpath "$EXTENSION_PATH")"

if [[ ! -f "$EXTENSION_PATH" ]]; then
    echo "ERROR: Extension not found: $EXTENSION_PATH" >&2
    exit 1
fi

# ── 2. Detect DuckDB version ─────────────────────────────────────────────────
if [[ -z "${DUCKDB_GIT_VERSION:-}" ]]; then
    DUCKDB_GIT_VERSION="$(git -C "$PROJ_DIR/duckdb" describe --tags --exact-match 2>/dev/null || true)"
fi
if [[ -z "${DUCKDB_GIT_VERSION:-}" ]]; then
    echo "ERROR: Cannot determine DuckDB version." >&2
    echo "       Set DUCKDB_GIT_VERSION (e.g. v1.5.1) or ensure duckdb/ submodule is on a tagged commit." >&2
    exit 1
fi
DUCKDB_VERSION="${DUCKDB_GIT_VERSION#v}"  # strip leading 'v' for pip install

EXT_DIR="$(dirname "$EXTENSION_PATH")"
EXT_FILE="$(basename "$EXTENSION_PATH")"

echo ""
echo "=== ERPL Smoke Test (musl/alpine) ==="
echo "Extension: $EXTENSION_PATH"
echo "DuckDB:    ${DUCKDB_GIT_VERSION} (pip duckdb==${DUCKDB_VERSION} inside alpine:3)"
echo ""

# Run the test inside alpine to get a musl-compatible Python duckdb wheel.
# The extension directory is mounted read-only at /ext.
docker run --rm \
    -v "${EXT_DIR}:/ext:ro" \
    -e DUCKDB_VERSION="$DUCKDB_VERSION" \
    -e EXT_FILE="$EXT_FILE" \
    alpine:3 /bin/sh -c '
set -e
apk add -q python3 py3-pip 2>/dev/null
pip3 install --quiet --break-system-packages "duckdb==${DUCKDB_VERSION}"
python3 << PYEOF
import duckdb, sys, os

ext_path = "/ext/" + os.environ["EXT_FILE"]
print("[smoke-test] Extension:", ext_path)
print("[smoke-test] duckdb version:", duckdb.__version__)

con = duckdb.connect(config={"allow_unsigned_extensions": True})
con.execute("INSTALL " + repr(ext_path))
con.execute("LOAD erpl")

loaded = [r[0] for r in con.execute(
    "SELECT extension_name FROM duckdb_extensions() "
    "WHERE extension_name LIKE '\''erpl%'\'' AND loaded"
).fetchall()]
assert "erpl" in loaded, f"erpl not in loaded extensions: {loaded}"

count = con.execute(
    "SELECT count(*) FROM duckdb_functions() WHERE function_name LIKE '\''sap_%'\''"
).fetchone()[0]
assert count > 0, f"No SAP functions registered (count={count})"

for fn in ["sap_read_table", "sap_rfc_invoke", "sap_show_tables"]:
    rows = con.execute(
        f"SELECT 1 FROM duckdb_functions() WHERE function_name = '\''{fn}'\''"
    ).fetchall()
    assert rows, f"{fn} not found in duckdb_functions()"

print(f"[smoke-test] PASSED — {count} sap_* functions registered")
PYEOF
'

echo ""
echo "=== Smoke test PASSED (musl) ==="
