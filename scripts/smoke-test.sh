#!/usr/bin/env bash
# Post-build ERPL smoke test for Linux (glibc) and macOS.
# Downloads the official DuckDB CLI for the exact version the extension was built against,
# installs the extension, loads it, and verifies SAP functions are registered.
# No SAP connection required.
#
# Usage:
#   ./scripts/smoke-test.sh [/path/to/erpl.duckdb_extension]
#
# Environment variables (all optional):
#   DUCKDB_GIT_VERSION   e.g. "v1.5.1" — overrides submodule auto-detection
#   EXTENSION_PATH       path to erpl.duckdb_extension — overrides positional arg
#   DUCKDB_CLI_CACHE_DIR directory to cache downloaded CLI binaries (default: /tmp/duckdb-cli-cache)
#   OSX_BUILD_ARCH       set to "x86_64" on macOS to run the osx_amd64 extension slice via Rosetta
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
DUCKDB_VERSION_TAG="$DUCKDB_GIT_VERSION"  # e.g. "v1.5.1"

# ── 3. Detect platform ───────────────────────────────────────────────────────
OS="$(uname -s)"
ARCH="$(uname -m)"
case "$OS" in
    Linux)
        case "$ARCH" in
            x86_64)  CLI_SLUG="linux-amd64" ;;
            aarch64) CLI_SLUG="linux-aarch64" ;;
            *)       echo "ERROR: Unsupported Linux arch: $ARCH" >&2; exit 1 ;;
        esac ;;
    Darwin)
        CLI_SLUG="osx-universal" ;;
    *)
        echo "ERROR: Unsupported OS: $OS. Use scripts/smoke-test.ps1 for Windows." >&2
        exit 1 ;;
esac

# ── 4. Download and cache DuckDB CLI ─────────────────────────────────────────
CACHE_DIR="${DUCKDB_CLI_CACHE_DIR:-/tmp/duckdb-cli-cache}"
CLI_DIR="$CACHE_DIR/duckdb-${DUCKDB_VERSION_TAG}-${CLI_SLUG}"
DUCK="$CLI_DIR/duckdb"

if [[ ! -f "$DUCK" ]]; then
    mkdir -p "$CLI_DIR"
    URL="https://github.com/duckdb/duckdb/releases/download/${DUCKDB_VERSION_TAG}/duckdb_cli-${CLI_SLUG}.zip"
    echo "[smoke-test] Downloading DuckDB ${DUCKDB_VERSION_TAG} CLI for ${CLI_SLUG} ..."
    TMP_ZIP="$(mktemp /tmp/duckdb-cli-XXXXXX.zip)"
    if command -v curl &>/dev/null; then
        curl -fsSL --retry 3 -o "$TMP_ZIP" "$URL"
    else
        wget -q --tries=3 -O "$TMP_ZIP" "$URL"
    fi
    unzip -q "$TMP_ZIP" -d "$CLI_DIR"
    chmod +x "$DUCK"
    rm -f "$TMP_ZIP"
else
    echo "[smoke-test] Using cached CLI: $DUCK"
fi

# macOS: remove quarantine attribute so the downloaded CLI can run
if [[ "$OS" == "Darwin" ]]; then
    xattr -d com.apple.quarantine "$DUCK" 2>/dev/null || true
fi

# ── 5. Isolated HOME so extension installs go to a temp dir, not ~/.duckdb ──
SMOKE_HOME="$(mktemp -d /tmp/erpl-smoke-home-XXXXXX)"
trap 'rm -rf "$SMOKE_HOME"' EXIT INT TERM

# ── 6. Build CLI invocation (Rosetta for osx_amd64 on arm64 runner) ─────────
RUN_CLI=("$DUCK" -unsigned)
if [[ "$OS" == "Darwin" && "${OSX_BUILD_ARCH:-}" == "x86_64" ]]; then
    echo "[smoke-test] osx_amd64 extension — using Rosetta (arch -x86_64)"
    RUN_CLI=(arch -x86_64 "$DUCK" -unsigned)
fi

echo ""
echo "=== ERPL Smoke Test ==="
echo "Extension: $EXTENSION_PATH"
echo "DuckDB:    ${DUCKDB_VERSION_TAG} (${CLI_SLUG})"
echo ""

# ── 7. Run smoke test ────────────────────────────────────────────────────────
OUTPUT_FILE="$(mktemp /tmp/erpl-smoke-output-XXXXXX.txt)"
trap 'rm -rf "$SMOKE_HOME" "$OUTPUT_FILE"' EXIT INT TERM

HOME="$SMOKE_HOME" "${RUN_CLI[@]}" -c "
INSTALL '${EXTENSION_PATH}';
LOAD erpl;
SELECT extension_name, loaded
  FROM duckdb_extensions()
  WHERE extension_name LIKE 'erpl%'
  ORDER BY extension_name;
SELECT count(*) AS sap_function_count
  FROM duckdb_functions()
  WHERE function_name LIKE 'sap_%';
SELECT function_name
  FROM duckdb_functions()
  WHERE function_name IN ('sap_read_table', 'sap_rfc_invoke', 'sap_show_tables')
  ORDER BY function_name;
" 2>&1 | tee "$OUTPUT_FILE"
DUCK_EXIT="${PIPESTATUS[0]}"

if [[ $DUCK_EXIT -ne 0 ]]; then
    echo "ERROR: DuckDB exited with code $DUCK_EXIT" >&2
    exit "$DUCK_EXIT"
fi

# ── 8. Validate output ───────────────────────────────────────────────────────
if ! grep -q "erpl.*true" "$OUTPUT_FILE"; then
    echo "ERROR: erpl extension is not showing as loaded in duckdb_extensions()" >&2
    exit 1
fi
for FUNC in sap_read_table sap_rfc_invoke sap_show_tables; do
    if ! grep -q "$FUNC" "$OUTPUT_FILE"; then
        echo "ERROR: Expected function '$FUNC' not found in duckdb_functions()" >&2
        exit 1
    fi
done

echo ""
echo "=== Smoke test PASSED ==="
