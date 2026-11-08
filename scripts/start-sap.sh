#!/bin/bash
# Start the A4H trial container, wait until it is genuinely serving, and provision it.
#
#   ./scripts/start-sap.sh                  # start, wait, provision, stay in foreground
#   ./scripts/start-sap.sh --detach         # same but return once provisioned
#   ./scripts/start-sap.sh --no-provision   # bare container, nothing applied
#   ./scripts/start-sap.sh --license FILE   # also install this AS ABAP licence (minisap, system A4H)
#   ./scripts/start-sap.sh --no-lcp         # boot unpatched (SAP date window enforced)
#
# The image ships a three-month AS ABAP licence and a valid HANA licence, so no licence file
# is needed.  A licence file is bound to the hardware key of one image: the image's update
# script deletes the installed licence before it installs the file, so a file for another
# image (A4H_Multiple.txt was issued for :2023) leaves the system with no valid licence.
# That is why a licence is only mounted when one is passed explicitly.
#
# The trial keys expire 2027-02-06 (ABAP) and 2028-02-11 (HANA) and cannot be re-issued, so by
# default the container is started through the lcp submodule's entrypoint wrapper, which removes
# the two date comparisons in the license engine before SAP starts.  lcp lives in the private
# erpl-license-patch repo, checked out as ./lcp (git submodule update --init lcp, ERP_LCP_DIR
# overrides).  The ready-made plan is pinned to the 2025 image, which is why that image is used.
# ERP_LCP=off does the same via the environment.
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

APAB_PLATFORM_IMAGE="${A4H_IMAGE:-sapse/abap-cloud-developer-trial:2025}"
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd )"
PROFILE_FILE="$SCRIPT_DIR/A4H_D00_vhcala4hci.profile"
LICENSE_FILE=""
# Private submodule (like bics/odp); ERP_LCP_DIR points elsewhere if needed.
LCP_DIR="${ERP_LCP_DIR:-$SCRIPT_DIR/../lcp}"
LCP=1
[ "${ERP_LCP:-on}" = "off" ] && LCP=0

PROVISION=1
DETACH=0
while [ $# -gt 0 ]; do
    case "$1" in
        --no-provision) PROVISION=0 ;;
        --detach|-d)    DETACH=1 ;;
        --license)      LICENSE_FILE="${2:?--license needs a path}"; shift ;;
        --no-lcp)       LCP=0 ;;
        -h|--help)      sed -n '2,30p' "$0"; exit 0 ;;
        *)              LICENSE_FILE="$1" ;;   # backwards compatible: bare licence path
    esac
    shift
done

provision_when_ready() {
    # shellcheck source=sap/lib.sh
    . "$SCRIPT_DIR/sap/lib.sh"
    echo "==> waiting for the instance"
    wait_for_sap 2400 || { echo "instance did not come up (see the state above)" >&2; return 1; }
    # sapcontrol reporting GREEN is NOT enough: it says started while the ABAP HTTP stack
    # is still coming up, so an ADT call right after it fails in a way that looks like a
    # broken system rather than an early one.
    echo "==> waiting for ADT to answer"
    wait_for_adt 1200 || { echo "ADT never answered" >&2; return 1; }
    echo "==> provisioning"
    "$SCRIPT_DIR/sap/provision.sh"
}

ensure_lcp() {
    [ "$LCP" = 1 ] || return 0
    if [ ! -f "$LCP_DIR/CMakeLists.txt" ]; then
        echo "note: lcp submodule not checked out at $LCP_DIR (git submodule update --init lcp); booting unpatched" >&2
        echo "note: the SAP keys expire 2027-02-06, so this container will stop working then" >&2
        LCP=0
        return 0
    fi
    if [ ! -x "$LCP_DIR/build/lcp" ]; then
        echo "==> building lcp (first run)"
        cmake -S "$LCP_DIR" -B "$LCP_DIR/build" -DCMAKE_BUILD_TYPE=Release >/dev/null &&
            cmake --build "$LCP_DIR/build" -j >/dev/null || {
            echo "lcp build failed; booting unpatched" >&2
            LCP=0
            return 0
        }
    fi
    if [ ! -x "$LCP_DIR/build/lcp" ] || [ ! -f "$LCP_DIR/scripts/lcp-init.sh" ]; then
        echo "lcp build is incomplete in $LCP_DIR; booting unpatched" >&2
        LCP=0
    fi
}

docker_run() {  # docker_run <-d|-i>
    local lcp_mount=() lcp_entrypoint=()
    if [ "$LCP" = 1 ]; then
        lcp_mount=(
            -v "$LCP_DIR/build/lcp":/opt/erpl/lcp:ro
            -v "$LCP_DIR/scripts/lcp-init.sh":/opt/erpl/lcp-init:ro
        )
        lcp_entrypoint=(--entrypoint /opt/erpl/lcp-init)
    else
        echo "note: lcp disabled; the SAP keys expire 2027-02-06" >&2
    fi
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
        ${LICENSE_FILE:+-v "$LICENSE_FILE":/opt/sap/ASABAP_license} \
        "${lcp_mount[@]}" \
        "${lcp_entrypoint[@]}" \
        $APAB_PLATFORM_IMAGE -agree-to-sap-license -skip-limits-check
}

ensure_lcp

if [ "$DETACH" = 1 ]; then
    docker_run -d || exit $?
    [ "$PROVISION" = 1 ] && provision_when_ready
    exit $?
fi

# Foreground: Ctrl-C still stops SAP, as it always did.  Provisioning runs in a background
# waiter so it can gate on readiness without detaching the container.
[ "$PROVISION" = 1 ] && ( provision_when_ready ) &

docker_run -i
