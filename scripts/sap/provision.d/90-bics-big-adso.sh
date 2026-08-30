# ZERPLBIG: the large BW provider the BICS result-set work is measured on.
#
# BACKGROUNDED ON PURPOSE.  The container is disposable, so this reloads from scratch every
# time, and 20,000,000 rows take roughly two hours at ~2,500 rows/s.  Blocking the start
# script on that would make a fresh system unusable for the morning.  Everything before
# this step finishes in a few minutes; this fills in behind it.
#
# The loader appends from the current row count, so an interrupted load resumes rather than
# restarting -- which is what makes backgrounding safe.
#
#   ./scripts/sap/provision.sh --status     # progress
#
# Set FIXTURE_ROWS to load fewer (e.g. 200000 for a ~2 minute fixture that still yields
# ~255,000 BICS result rows).

_FIX="$HERE/../../bics/test/fixtures/setup_big_adso.sh"
_ROWS="${FIXTURE_ROWS:-20000000}"
_STATUS="$PROVISION_STATE_DIR/90-bics-big-adso.status"
_LOG="$PROVISION_STATE_DIR/90-bics-big-adso.log"

if [ ! -x "$_FIX" ]; then
    warn "no BICS fixture script at $_FIX (bics submodule not checked out?)"
    return 0
fi

if [ "$PROVISION_MODE" = check ]; then
    say "would load ZERPLBIG with $_ROWS rows in the background"
    return 0
fi

# Already loaded?  Re-running the whole load on a system that has it would cost hours for
# nothing, so ask the provider first.
_have=$(docker exec "$A4H_CONTAINER" /usr/sap/A4H/hdbclient/hdbsql -i 02 -d HDB \
          -u "${A4H_DB_USER:-SAPA4H}" -p "${A4H_DB_PASSWORD:-ABAPtr2023#00}" \
          "SELECT COUNT(*) FROM ${A4H_DB_USER:-SAPA4H}.\"/BIC/AZERPLBIG2\"" 2>/dev/null \
        | sed -n '2p' | tr -dc '0-9')
if [ -n "$_have" ] && [ "$_have" -ge "$_ROWS" ] 2>/dev/null; then
    ok "ZERPLBIG already holds $_have rows"
    echo "complete ($_have rows)" > "$_STATUS"
    return 0
fi

say "loading ZERPLBIG to $_ROWS rows in the background (hours; see --status)"
echo "running (started $(date -u +%H:%M:%SZ), target $_ROWS)" > "$_STATUS"
(
    if RESUME=1 "$_FIX" "$_ROWS" > "$_LOG" 2>&1; then
        echo "complete ($_ROWS rows)" > "$_STATUS"
    else
        echo "FAILED — see $_LOG" > "$_STATUS"
    fi
) </dev/null >/dev/null 2>&1 &
disown 2>/dev/null || true
ok "backgrounded (pid $!)"
return 0
