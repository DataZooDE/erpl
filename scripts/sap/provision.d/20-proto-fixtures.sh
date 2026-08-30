# erpl-proto's live-server fixtures: the ABAP caller classes and the SM59 destinations.
#
# The ABAP lives here (assets/proto/abap) so there is one copy rather than one per repo.
#
# The DESTINATIONS do not move.  They are created by
# `cargo run -p erpl-proto --example make_test_destination`, which needs erpl-proto's Rust
# crate and toolchain -- there is no erpl-side equivalent, and reimplementing SM59
# registration here would be a rewrite of erpl-proto's approach rather than a move.  So
# this step deploys the ABAP from erpl and delegates the destinations to an erpl-proto
# checkout when one is reachable.
#
# When it is not reachable this WARNS rather than skipping silently.  Silent skipping is
# precisely the failure mode that hid the missing certificate trust: erpl-proto's tests
# already skip when their fixtures are absent, so a second silent skip here would compound
# an invisible failure rather than surface it.

_AB="$HERE/assets/proto/abap"

if [ "$PROVISION_MODE" = check ]; then
    say "would deploy 12 ABAP caller classes"
    if [ -d "${ERPL_PROTO_DIR:-$HERE/../../../erpl-proto}" ]; then
        say "would delegate SM59 destinations to erpl-proto"
    else
        warn "no erpl-proto checkout — SM59 destinations would NOT be created"
    fi
    return 0
fi

rc=0
for f in "$_AB"/zcl_saprfc_*.abap "$_AB"/zcl_erpl_abi_*.abap; do
    [ -e "$f" ] || continue
    n="$(basename "$f" .abap)"
    cls "$(printf '%s' "$n" | tr '[:lower:]' '[:upper:]')" "$f" \
        "erpl-proto live server test" || rc=1
done

retry_failed || rc=1

# --- destinations, delegated ------------------------------------------------
# ERPL_A4H_DELEGATED is the recursion guard, and BOTH sides must spell it the same way.
# They did not on the first attempt -- this side set ERPL_SKIP_ABAP while erpl-proto
# checked ERPL_A4H_DELEGATED -- so neither guard fired and the two scripts called each
# other until the run was killed.
if [ -n "${ERPL_A4H_DELEGATED:-}" ]; then
    say "destinations: handled by the caller (erpl-proto invoked us)"
    return $rc
fi

_PROTO="${ERPL_PROTO_DIR:-$HERE/../../../erpl-proto}"
if [ -f "$_PROTO/scripts/deploy-a4h-fixtures.sh" ]; then
    say "delegating SM59 destinations to $_PROTO"
    if ( cd "$_PROTO" && SAP_PASSWD="$SAP_PASSWORD" ERPL_A4H_DELEGATED=1 \
            ./scripts/deploy-a4h-fixtures.sh 2>&1 | sed 's/^/    /' ); then
        ok "erpl-proto destinations"
    else
        warn "erpl-proto destinations (check the output above)"
        rc=1
    fi
else
    warn "no erpl-proto checkout at $_PROTO"
    warn "  SM59 destinations were NOT created — erpl-proto's live tests will SKIP,"
    warn "  which reads as green.  Set ERPL_PROTO_DIR to a checkout, or run its"
    warn "  scripts/deploy-a4h-fixtures.sh by hand."
fi

return $rc
