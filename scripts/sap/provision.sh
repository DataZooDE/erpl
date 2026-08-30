#!/usr/bin/env bash
# Provision the a4h trial: run provision.d/* in order.
#
# The trial container is disposable (`docker run --rm`, no data volume), so everything
# below is destroyed with it and has to be reapplied on every fresh container.  Each step
# is idempotent, so re-running against a live system is cheap and safe.
#
#   ./scripts/sap/provision.sh                 # everything, in order
#   ./scripts/sap/provision.sh --check         # report state, change nothing
#   ./scripts/sap/provision.sh --only 30       # one step (prefix match)
#   ./scripts/sap/provision.sh --status        # progress of backgrounded steps
#
# ORDERING IS THE POINT OF THIS FILE.
#
#   10 activates the BW Modeling services, which requires a full instance restart.
#   A restart re-materialises every PSE from the database, silently discarding anything
#   sapgenpse wrote into a PSE *file* -- destroying the SNC and wsRFC certificate trust
#   that erpl-proto's live tests depend on.  Those tests *skip* when trust is missing
#   rather than failing, so the loss is invisible: a suite that has lost fourteen tests
#   still reads green.
#
#   Therefore 30 (certificate trust) MUST run after 10, and is re-run whenever 10
#   actually restarted.  See DataZooDE/erpl-proto#30.
set -uo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=lib.sh
. "$HERE/lib.sh"

MODE=run
ONLY=
STATE_DIR="${TMPDIR:-/tmp}/erpl-a4h-provision"
mkdir -p "$STATE_DIR"

while [ $# -gt 0 ]; do
    case "$1" in
        --check)  MODE=check ;;
        --status) MODE=status ;;
        --only)   ONLY="${2:?--only needs a step prefix}"; shift ;;
        -h|--help) sed -n '2,20p' "$0"; exit 0 ;;
        *) echo "unknown argument: $1" >&2; exit 2 ;;
    esac
    shift
done

if [ "$MODE" = status ]; then
    shopt -s nullglob
    found=0
    for f in "$STATE_DIR"/*.status; do
        found=1
        printf '%-28s %s\n' "$(basename "$f" .status)" "$(cat "$f")"
    done
    [ "$found" = 1 ] || echo "no backgrounded steps have run"
    exit 0
fi

if ! container_up; then
    echo "container '$A4H_CONTAINER' is not running — start it with scripts/start-sap.sh" >&2
    exit 1
fi

# RESTARTED is the channel between steps: 10 sets it to 1 when it restarted the instance,
# and 30 uses it to know its work was wiped and must be redone.
export RESTARTED=0
export PROVISION_STATE_DIR="$STATE_DIR"
export PROVISION_MODE="$MODE"

# _PROVISION_RC, not rc: the steps are SOURCED, so they share this namespace.  A plain
# `rc` was clobbered by the next step initialising its own -- step 25 failed, step 30 set
# rc=0, and the run exited 0 reporting success.  A provisioner that returns green while a
# step failed is the exact failure mode this whole file exists to prevent.
_PROVISION_RC=0
_PROVISION_FAILED=
shopt -s nullglob
for step in "$HERE"/provision.d/*.sh; do
    name="$(basename "$step")"
    [ -n "$ONLY" ] && case "$name" in "$ONLY"*) ;; *) continue ;; esac
    echo "== $name"
    # shellcheck disable=SC1090
    if ! . "$step"; then
        fail "$name"
        _PROVISION_RC=1
        _PROVISION_FAILED="$_PROVISION_FAILED $name"
    fi
done
rc="$_PROVISION_RC"

if [ "$rc" = 0 ]; then
    echo "== provisioning complete =="
    echo "   backgrounded steps, if any: ./scripts/sap/provision.sh --status"
else
    echo "== provisioning finished WITH FAILURES:$_PROVISION_FAILED ==" >&2
fi
exit $rc
