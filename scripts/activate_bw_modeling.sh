#!/usr/bin/env bash
# Activates the BW Modeling ADT services on the local a4h trial container.
#
# They ship INACTIVE on sapse/abap-cloud-developer-trial:2023, so every `erpl-adt bw ...`
# command returns HTTP 403 until this has run.  Activation survives a container restart
# but not an image recreation, so this needs re-running after a rebuild.
#
# Procedure and the GUID constants come from erpl-adt's own documentation:
#   https://github.com/DataZooDE/erpl-adt/blob/main/docs/activate-bw-modeling-on-a4h.md
#
# WARNING -- this restarts the ABAP instance, and that has a side effect beyond this repo.
# Restarting the *instance* (not just the container) re-materialises every PSE from the
# database, silently discarding anything `sapgenpse` wrote into a PSE *file*.  That
# destroys the SNC and wsRFC client-certificate trust erpl-proto's live tests rely on --
# and those tests *skip* rather than fail when trust is missing, so a suite that has lost
# fourteen tests still reads green.
#
# After running this, re-run erpl-proto's scripts/deploy-a4h-fixtures.sh to restore the
# trust, and check the skip count rather than the colour.  See DataZooDE/erpl-proto#30.
#
# This script exists so the operation can be granted as ONE reviewable, narrowly scoped
# permission.  Allow-listing the raw `docker exec ... hdbsql "UPDATE ..."` it performs
# would grant the ability to write any row of any table in the SAP database; allow-listing
# this script grants exactly "turn the BW modeling services on, on the local throwaway
# trial", which is auditable in review and cannot be repurposed.
#
#   ./scripts/activate_bw_modeling.sh          # activate (idempotent) and verify
#   ./scripts/activate_bw_modeling.sh --check  # report status only, change nothing
set -euo pipefail

CONTAINER=${A4H_CONTAINER:-a4h}
HDBSQL=/usr/sap/A4H/hdbclient/hdbsql
DBUSER=${A4H_DB_USER:-SAPA4H}
DBPASS=${A4H_DB_PASSWORD:-'ABAPtr2023#00'}   # standard credential of the public trial image

# Delivered content, stable across restarts on the same image.
BW_GUID=DFFAEATGKMFLCDXQ04F0J7FXK
MODELING_GUID=3FWVDBADCM6B4KLQKF4R70SS5

sql() { docker exec "$CONTAINER" "$HDBSQL" -i 02 -d HDB -u "$DBUSER" -p "$DBPASS" "$1"; }

status() {
    sql "SELECT ICF_NAME, ICFACTIVE FROM ${DBUSER}.ICFSERVLOC
         WHERE (ICF_NAME = 'BW' AND ICFPARGUID = '${BW_GUID}')
            OR (ICF_NAME = 'MODELING' AND ICFPARGUID = '${MODELING_GUID}')"
}

if [[ "${1:-}" == "--check" ]]; then
    status
    exit 0
fi

echo "==> current state"
status

echo "==> activating /sap/bw/ and /sap/bw/modeling/"
sql "UPDATE ${DBUSER}.ICFSERVLOC SET ICFACTIVE = 'X'
     WHERE ICF_NAME = 'BW' AND ICFPARGUID = '${BW_GUID}'"
sql "UPDATE ${DBUSER}.ICFSERVLOC SET ICFACTIVE = 'X'
     WHERE ICF_NAME = 'MODELING' AND ICFPARGUID = '${MODELING_GUID}'"

echo "==> activating BW search (RSOSSEARCH)"
sql "UPDATE ${DBUSER}.RSOSSEARCH SET ACTIVEFL = 'X' WHERE TLOGO = 'BIMO'"

# A full instance restart is required: SIGHUP to icman and sapcontrol ICMRestart do NOT
# flush the ICF service cache.
echo "==> restarting the instance (required to flush the ICF cache)"
docker exec "$CONTAINER" bash -c "su - a4hadm -c 'sapcontrol -nr 00 -function RestartInstance'"
docker exec "$CONTAINER" bash -c "su - a4hadm -c 'sapcontrol -nr 00 -function WaitforStarted 300 15'"
docker exec "$CONTAINER" bash -c "su - a4hadm -c 'sapcontrol -nr 00 -function GetProcessList'" || true

echo "==> new state"
status

cat <<'DONE'

==> done.  Verify with:

    uvx erpl-adt --host localhost --port 50000 --user DEVELOPER \
        --password 'ABAPtr2023#00' --client 001 bw discover
DONE
