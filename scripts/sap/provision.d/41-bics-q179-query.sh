# ZERPL_Q179_ATTR: 0D_NW_PROD on rows with its display attribute 0D_NW_PRDGP active in the
# query definition, which sap_bics_display_attributes.test reads (issue #179).  One save.

_FIX="$HERE/../../bics/test/fixtures/setup_q179_query.sh"

if [ ! -x "$_FIX" ]; then
    warn "no query fixture script at $_FIX (bics submodule not checked out?)"
    return 0
fi

if [ "$PROVISION_MODE" = check ]; then
    say "would save ZERPL_Q179_ATTR"
    return 0
fi

if "$_FIX" > "$PROVISION_STATE_DIR/41-bics-q179-query.log" 2>&1; then
    ok "ZERPL_Q179_ATTR saved"
else
    fail "ZERPL_Q179_ATTR — see $PROVISION_STATE_DIR/41-bics-q179-query.log"
fi
return 0
