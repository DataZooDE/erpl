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
    if adt source write "$2" --file "$3" --activate 2>&1 \
         | grep -aiqE 'Activated|Nothing to activate'; then
        ok "$2"
    else
        warn "$2 (activation — check)"
        return 1
    fi
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

wait_for_sap() {  # all four processes GREEN
    local deadline=$(( SECONDS + ${1:-600} ))
    while (( SECONDS < deadline )); do
        if docker exec "$A4H_CONTAINER" bash -c \
             "su - a4hadm -c 'sapcontrol -nr 00 -function GetProcessList'" 2>/dev/null \
             | grep -cE '\bGREEN\b' | grep -qx 4; then
            return 0
        fi
        sleep 10
    done
    return 1
}

wait_for_adt() {  # ABAP HTTP stack actually answering an authenticated request
    local deadline=$(( SECONDS + ${1:-600} ))
    while (( SECONDS < deadline )); do
        if adt search CL_RSO_ADSO_API --type CLAS --max 1 >/dev/null 2>&1; then
            return 0
        fi
        sleep 10
    done
    return 1
}
