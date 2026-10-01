# ZERPL_Q154_KF: the BEx query with hidden structure members on both axes that
# sap_bics_query_structures.test reads (issue #154).  Seconds, not hours: one save.

_FIX="$HERE/../../bics/test/fixtures/setup_q154_query.sh"

if [ ! -x "$_FIX" ]; then
    warn "no query fixture script at $_FIX (bics submodule not checked out?)"
    return 0
fi

if [ "$PROVISION_MODE" = check ]; then
    say "would save ZERPL_Q154_KF"
    return 0
fi

if "$_FIX" > "$PROVISION_STATE_DIR/40-bics-q154-query.log" 2>&1; then
    ok "ZERPL_Q154_KF saved"
else
    fail "ZERPL_Q154_KF — see $PROVISION_STATE_DIR/40-bics-q154-query.log"
fi
return 0
