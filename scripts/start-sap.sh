#!/bin/bash
# Start the A4H trial container, wait until it is genuinely serving, and provision it.
#
#   ./scripts/start-sap.sh                  # start, wait, provision, stay in foreground
#   ./scripts/start-sap.sh --detach         # same but return once provisioned
#   ./scripts/start-sap.sh --no-provision   # bare container, nothing applied
#
# The container is disposable (--rm, no data volume), so EVERYTHING the tests need is
# destroyed with it and has to be reapplied on every start -- the ABAP repository, the BW
# Modeling service activation, the certificate trust and the BW fixtures.  That is what
# scripts/sap/provision.sh does, and why this script runs it by default: a fresh container
# with nothing applied is not a usable system, and forgetting the second command is the
# failure mode this replaces.
#
# The slow part (the 20M-row BICS fixture) is backgrounded by the provisioner, so this
# returns in a few minutes; `scripts/sap/provision.sh --status` reports the rest.
set -uo pipefail

APAB_PLATFORM_IMAGE="sapse/abap-cloud-developer-trial:2023"
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd )"
PROFILE_FILE="$SCRIPT_DIR/A4H_D00_vhcala4hci.profile"
LICENSE_FILE="$SCRIPT_DIR/A4H_Multiple.txt"

PROVISION=1
DETACH=0
while [ $# -gt 0 ]; do
    case "$1" in
        --no-provision) PROVISION=0 ;;
        --detach|-d)    DETACH=1 ;;
        --license)      LICENSE_FILE="${2:?--license needs a path}"; shift ;;
        -h|--help)      sed -n '2,18p' "$0"; exit 0 ;;
        *)              LICENSE_FILE="$1" ;;   # backwards compatible: bare licence path
    esac
    shift
done

provision_when_ready() {
    # shellcheck source=sap/lib.sh
    . "$SCRIPT_DIR/sap/lib.sh"
    echo "==> waiting for the instance"
    wait_for_sap 900 || { echo "instance did not come up" >&2; return 1; }
    # sapcontrol reporting GREEN is NOT enough: it says started while the ABAP HTTP stack
    # is still coming up, so an ADT call right after it fails in a way that looks like a
    # broken system rather than an early one.
    echo "==> waiting for ADT to answer"
    wait_for_adt 900 || { echo "ADT never answered" >&2; return 1; }
    echo "==> provisioning"
    "$SCRIPT_DIR/sap/provision.sh"
}

docker_run() {  # docker_run <-d|-i>
    docker run \
        --stop-timeout 3600 "$1" --rm \
        --name a4h -h vhcala4hci \
        --mac-address="02:42:ac:11:00:02" \
        -p 3200:3200 -p 3300:3300 -p 8443:8443 \
        -p 30213:30213 -p 50000:50000 -p 50001:50001 \
        --sysctl kernel.shmmax=42949672960 \
        --sysctl kernel.shmmni=32768 \
        --sysctl kernel.shmall=10485760 \
        --sysctl kernel.msgmni=1024 \
        --sysctl kernel.sem="1250 256000 100 8192" \
        --ulimit nofile=1048576:1048576 \
        -v "$PROFILE_FILE":/usr/sap/A4H/SYS/profile/A4H_D00_vhcala4hci \
        -v "$LICENSE_FILE":/opt/sap/ASABAP_license \
        $APAB_PLATFORM_IMAGE -agree-to-sap-license -skip-limits-check
}

if [ "$DETACH" = 1 ]; then
    docker_run -d || exit $?
    [ "$PROVISION" = 1 ] && provision_when_ready
    exit $?
fi

# Foreground: Ctrl-C still stops SAP, as it always did.  Provisioning runs in a background
# waiter so it can gate on readiness without detaching the container.
[ "$PROVISION" = 1 ] && ( provision_when_ready ) &

docker_run -i
