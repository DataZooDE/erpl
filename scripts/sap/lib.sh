# Shared helpers for a4h provisioning.  Sourced, not executed.
#
# Every repo that talks to the trial had grown its own copy of these -- erpl-proto's
# deploy-a4h-fixtures.sh, erpl-rev's deploy-abap.sh and erpl's own fixture scripts each
# reimplemented adt()/cls() and each carried its own "the trial wipes this, re-run me"
# header.  One copy, here.

: "${A4H_CONTAINER:=a4h}"
: "${SAP_HOST:=localhost}"
: "${SAP_PORT:=50000}"
: "${SAP_USER:=DEVELOPER}"
: "${SAP_CLIENT:=001}"
: "${SAP_PASSWORD:=ABAPtr2023#00}"   # the public trial image's standard credential

export SAP_PASSWORD                   # erpl-adt reads this name via --password-env

# Objects whose activation failed; retried by retry_failed() after the list is walked.
_DEFERRED=()

# Credentials are passed as explicit flags on purpose: erpl-adt reads --password-env /
# SAP_PASSWORD, never ERPL_ADT_*, and a cached .adt.creds is invalidated by an instance
# restart -- which BW activation requires, so the cache cannot be relied on here.
_ADT=(--host "$SAP_HOST" --port "$SAP_PORT" --user "$SAP_USER"
      --client "$SAP_CLIENT" --password-env SAP_PASSWORD)

adt() { uvx erpl-adt "${_ADT[@]}" "$@"; }

say()  { printf '  %s\n' "$*"; }
ok()   { printf '  OK   %s\n' "$*"; }
warn() { printf '  WARN %s\n' "$*"; }
fail() { printf '  FAIL %s\n' "$*"; }

# --- object deployment ------------------------------------------------------
# "Nothing to activate" means the source was already active and identical, which is
# success on a re-run -- treating it as failure made an idempotent script look broken
# every second time.
_write() {  # _write <TYPE> <NAME> <file> <desc>
    adt object create --type "$1" --name "$2" --package '$TMP' \
        --description "$4" >/dev/null 2>&1
    # Retried, because activation right after an instance restart fails transiently: the
    # ADT search that gates readiness needs far less of the system up than the ABAP
    # compiler does, so the first attempt can land too early.  Observed on a cold start --
    # all twelve classes warned, and every one activated on a later manual attempt.
    local attempt
    for attempt in 1 2 3; do
        if adt source write "$2" --file "$3" --activate 2>&1 \
             | grep -aiqE 'Activated|Nothing to activate'; then
            ok "$2"
            return 0
        fi
        sleep $(( attempt * 10 ))
    done
    # Record rather than give up: ABAP lists are not in dependency order, and an object
    # can fail purely because something it references is deployed later (observed with
    # ZCL_ERPL_REV_CDSTEST, which activated cleanly once the rest of the list was in).
    # retry_failed() below re-tries these once the list has been walked.
    _DEFERRED+=("$1|$2|$3|$4")
    warn "$2 (deferred — will retry after the rest of the list)"
    return 0
}

# Re-try everything _write deferred.  One extra pass resolves forward dependencies without
# anyone hand-maintaining a topological order; anything still failing after it is a real
# failure and is reported as such.
retry_failed() {
    [ ${#_DEFERRED[@]} -eq 0 ] && return 0
    local pending=("${_DEFERRED[@]}")
    _DEFERRED=()
    local rc=0 entry t n f d
    say "second pass over ${#pending[@]} deferred object(s)"
    for entry in "${pending[@]}"; do
        IFS='|' read -r t n f d <<<"$entry"
        if adt source write "$n" --file "$f" --activate 2>&1 \
             | grep -aiqE 'Activated|Nothing to activate'; then
            ok "$n (second pass)"
        else
            fail "$n (still failing after a second pass)"
            rc=1
        fi
    done
    _DEFERRED=()
    return $rc
}
cls()  { _write CLAS/OC "$1" "$2" "${3:-a4h fixture}"; }
intf() { _write INTF/OI "$1" "$2" "${3:-a4h fixture}"; }
prog() { _write PROG/P  "$1" "$2" "${3:-a4h fixture}"; }
tabl() { _write TABL/DT "$1" "$2" "${3:-a4h fixture}"; }

# --- readiness --------------------------------------------------------------
# `sapcontrol WaitforStarted` returning OK is NOT enough: it reports the instance
# started while the ABAP HTTP stack is still coming up, so an ADT call right after it
# fails with a connection error that looks like a broken system.
container_up() {
    docker ps --format '{{.Names}}' 2>/dev/null | grep -qx "$A4H_CONTAINER"
}

_process_list() {
    docker exec "$A4H_CONTAINER" bash -c \
        "su - a4hadm -c 'sapcontrol -nr 00 -function GetProcessList'" 2>/dev/null
}

# Default 40 minutes.  A *restart* is back in about a minute, but a COLD container is not:
# HANA recovers first and the ABAP processes do not even launch for ~14 minutes, after
# which disp+work sits at "YELLOW / Server in state STARTING" for several more.  A 15
# minute budget -- which is what this had, sized from restart experience -- gives up while
# the system is still starting normally.
wait_for_sap() {  # all four processes GREEN
    local deadline=$(( SECONDS + ${1:-2400} ))
    while (( SECONDS < deadline )); do
        if _process_list | grep -cE '\bGREEN\b' | grep -qx 4; then
            return 0
        fi
        sleep 15
    done
    # Say what it was doing when time ran out.  "instance did not come up" on its own sent
    # me looking for a broken system when it was simply still starting.
    echo "wait_for_sap: gave up after $(( ${1:-2400} / 60 )) minutes; last state was:" >&2
    _process_list | tail -5 >&2
    return 1
}

# The gateway (gwrd) has to be up before an RFC *server* can register against it, which is
# what erpl-proto's destinations do.  Right after a restart it is still "Scheduled", and a
# registration attempt then fails with a bare "Connection reset by peer" naming nothing.
wait_for_rfc() {
    local deadline=$(( SECONDS + ${1:-600} ))
    while (( SECONDS < deadline )); do
        if _process_list | grep -E '^gwrd' | grep -q GREEN; then
            return 0
        fi
        sleep 10
    done
    echo "wait_for_rfc: the gateway never reached GREEN" >&2
    return 1
}

wait_for_adt() {  # ABAP HTTP stack actually answering an authenticated request
    local deadline=$(( SECONDS + ${1:-1200} ))
    while (( SECONDS < deadline )); do
        if adt search CL_RSO_ADSO_API --type CLAS --max 1 >/dev/null 2>&1; then
            return 0
        fi
        sleep 10
    done
    return 1
}
