#!/usr/bin/env bash
#
# Runs a suite's SQLLogicTest files against one RFC backend.
#
# erpl can be served by either SAP's NetWeaver RFC SDK or the pure-Rust erpl-proto
# implementation, and both are expected to pass the same suites. The proto backend is
# still closing gaps, so each suite may carry a list of tests known to fail there; a
# listed test that fails is reported as a known gap rather than a failure, and a listed
# test that *passes* fails the run, so the list cannot quietly rot into a blanket excuse.
#
# Usage: run_sql_tests.sh <suite-dir> [options]
#   --backend nwrfc|proto     which implementation to test (default: nwrfc)
#   --known-failures <file>   expected-failure list, one test filename per line
#   --test-file <name>        run a single file from the suite's test/sql/
#
# The caller supplies SAP credentials and LD_LIBRARY_PATH through the environment; see
# the Makefile.

set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="$(dirname "$SCRIPT_DIR")"
UNITTEST="$REPO_DIR/build/debug/test/unittest"

suite_dir=""
backend="nwrfc"
known_failures_file=""
test_file=""

while [[ $# -gt 0 ]]; do
	case "$1" in
		--backend)        backend="$2"; shift 2 ;;
		--known-failures) known_failures_file="$2"; shift 2 ;;
		--test-file)      test_file="$2"; shift 2 ;;
		*)                suite_dir="$1"; shift ;;
	esac
done

if [[ -z "$suite_dir" ]]; then
	echo "usage: run_sql_tests.sh <suite-dir> [--backend nwrfc|proto] [--known-failures <file>] [--test-file <name>]" >&2
	exit 2
fi

cd "$REPO_DIR/$suite_dir" || exit 1

if [[ ! -x "$UNITTEST" ]]; then
	echo "ERROR: $UNITTEST not found. Run 'GEN=ninja make debug_tests' first." >&2
	exit 1
fi

# The proto backend is selected through the extension's own settings, which read these.
# Deliberately no fallback: if the library is missing the extension raises, rather than
# quietly serving the run from the SDK and reporting a green proto leg that never ran.
if [[ "$backend" == "proto" ]]; then
	export ERPL_RFC_BACKEND=proto
	proto_lib="${ERPL_RFC_BACKEND_PATH:-$REPO_DIR/build/debug/liberpl_proto_nwrfc.so}"
	if [[ ! -f "$proto_lib" ]]; then
		echo "ERROR: proto backend requested but $proto_lib does not exist." >&2
		echo "       The erpl-proto submodule must be present and the build must have staged it." >&2
		exit 1
	fi
	export ERPL_RFC_BACKEND_PATH="$proto_lib"
else
	export ERPL_RFC_BACKEND=nwrfc
fi

declare -A known_failure=()
if [[ -n "$known_failures_file" && -f "$known_failures_file" ]]; then
	while IFS= read -r line; do
		line="${line%%#*}"
		line="$(echo -n "$line" | tr -d '[:space:]')"
		[[ -n "$line" ]] && known_failure["$line"]=1
	done < "$known_failures_file"
fi

passed=0 failed=0 sdk_aborts=0 known_gaps=0 unexpected_passes=0
failed_names=() unexpected_names=()

run_one() {
	local path="$1" name log rc
	name="$(basename "$path")"
	log="$(mktemp)"
	echo "Running test: $path"
	"$UNITTEST" --test-dir . "$path" > "$log" 2>&1
	rc=$?

	# The SAP SDK aborts inside its own static destructor at process exit ("pure virtual
	# method called", erpl#112) on roughly 10% of runs, after every assertion has already
	# passed. Third-party teardown, not a test result. Matched on both the pass marker and
	# the SDK signature so any other abort -- including one that loses assertions -- still
	# fails. The proto backend has no SDK static destructor, so there this tolerance does
	# not apply and an abort is a real failure.
	if [[ $rc -ne 0 && "$backend" == "nwrfc" ]] \
		&& grep -q "All tests passed" "$log" && grep -q "pure virtual method called" "$log"; then
		echo "  WARN: assertions passed, SAP SDK aborted at exit (erpl#112)"
		sdk_aborts=$((sdk_aborts + 1))
		rc=0
	fi

	if [[ -n "${known_failure[$name]:-}" ]]; then
		if [[ $rc -eq 0 ]]; then
			echo "  UNEXPECTED PASS: $name is listed as a known $backend failure but passed."
			echo "                   Remove it from $known_failures_file."
			unexpected_passes=$((unexpected_passes + 1))
			unexpected_names+=("$name")
		else
			echo "  known $backend gap: $name"
			known_gaps=$((known_gaps + 1))
		fi
		rm -f "$log"
		return
	fi

	if [[ $rc -ne 0 ]]; then
		echo "  FAIL: $path"
		cat "$log"
		failed=$((failed + 1))
		failed_names+=("$name")
	else
		passed=$((passed + 1))
	fi
	rm -f "$log"
}

if [[ -n "$test_file" ]]; then
	run_one "test/sql/$test_file"
else
	for f in test/sql/*.test; do
		run_one "$f"
	done
fi

echo
echo "=== $suite_dir on the $backend backend ==="
echo "    passed:            $passed"
echo "    failed:            $failed"
echo "    known gaps:        $known_gaps"
echo "    unexpected passes: $unexpected_passes"
[[ $sdk_aborts -gt 0 ]] && echo "    SDK exit aborts:   $sdk_aborts (erpl#112, assertions had passed)"

if [[ $failed -gt 0 ]]; then
	echo "FAILED: ${failed_names[*]}"
	exit 1
fi
if [[ $unexpected_passes -gt 0 && "${ALLOW_UNEXPECTED_PASS:-0}" != "1" ]]; then
	echo "FAILED: these are listed as known $backend failures but passed: ${unexpected_names[*]}"
	echo "        Remove them from $known_failures_file, or re-run with ALLOW_UNEXPECTED_PASS=1."
	exit 1
fi
echo "All SQL tests passed"
