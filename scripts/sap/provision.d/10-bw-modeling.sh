# Activate the BW Modeling ADT services.
#
# They ship INACTIVE on sapse/abap-cloud-developer-trial:2023, so every `erpl-adt bw ...`
# call returns HTTP 403 until they are on.  Activating them needs a full instance restart
# to flush the ICF cache -- and that restart destroys certificate trust, which is why
# 30-cert-trust.sh runs after this one and keys off RESTARTED.

_bw_active() {
    docker exec "$A4H_CONTAINER" /usr/sap/A4H/hdbclient/hdbsql -i 02 -d HDB \
        -u "${A4H_DB_USER:-SAPA4H}" -p "${A4H_DB_PASSWORD:-ABAPtr2023#00}" \
        "SELECT ICF_NAME, ICFACTIVE FROM ${A4H_DB_USER:-SAPA4H}.ICFSERVLOC
         WHERE ICF_NAME IN ('BW','MODELING')" 2>/dev/null \
      | grep -c '"X"'
}

active="$(_bw_active)"
if [ "${active:-0}" = 2 ]; then
    ok "BW Modeling services already active"
    return 0
fi

if [ "$PROVISION_MODE" = check ]; then
    warn "BW Modeling services NOT active ($active/2) — would activate and restart"
    return 0
fi

say "activating (this restarts the instance)"
if "$HERE/../activate_bw_modeling.sh" >/dev/null 2>&1; then
    # Tell the rest of the run that every PSE was just re-materialised from the database.
    RESTARTED=1
    export RESTARTED
    if wait_for_adt 600; then
        ok "BW Modeling services activated; instance back up"
    else
        fail "instance did not come back within 10 minutes"
        return 1
    fi
else
    fail "activation failed"
    return 1
fi
