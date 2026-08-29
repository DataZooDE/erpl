# Restore the SNC and wsRFC client-certificate trust, then make the ICM re-read its PSE.
#
# THIS STEP EXISTS BECAUSE OF STEP 10.  Restarting the ABAP *instance* -- which activating
# the BW Modeling services requires -- re-materialises every PSE from the database and
# silently discards anything sapgenpse wrote into a PSE *file*.  Observed: ten PSEs
# rewritten within the same second, both client certificates gone, on a container that had
# been up four days.
#
# The failure is quiet, which is the dangerous half: erpl-proto's SNC and wsRFC tests
# *skip* when trust is missing rather than failing, because they cannot tell an
# unconfigured machine from a broken one.  A suite that has lost fourteen tests still reads
# green.  Diagnosed in DataZooDE/erpl-proto#30.
#
# Making it durable would mean storing the PSEs back into the database, as STRUST does when
# it saves (SSFPSE_STORE takes a file).  Deliberately not automated: that writes the
# system's security configuration, and a wrong PSE identity there breaks SSL rather than a
# test.

_CERTS="$HERE/assets/proto/certs"
_SEC=/usr/sap/A4H/D00/sec
_RUN=/usr/sap/A4H/SYS/exe/run

_trust() {  # _trust <pse> <local pem> <label>
    local base; base="$(basename "$2")"
    # World-readable on purpose: mktemp makes 0600, docker cp preserves the mode, and the
    # owner maps to a different uid inside the container -- so sapgenpse, running as
    # a4hadm, reported the file as "not existing".  These are public certificates.
    chmod 644 "$2" 2>/dev/null
    docker cp "$2" "$A4H_CONTAINER:/tmp/$base" >/dev/null 2>&1 || {
        warn "$3 (could not copy $2 into the container)"; return 1; }
    local out
    out=$(docker exec -u a4hadm "$A4H_CONTAINER" bash -lc \
          "export SECUDIR=$_SEC LD_LIBRARY_PATH=$_RUN; cd $_RUN; \
           ./sapgenpse maintain_pk -p $1 -x '' -a /tmp/$base" 2>&1)
    # Three distinct outcomes.  Lumping them together hid the one that matters:
    # "Duplicate" is success on a re-run, anything else is a real failure that must not
    # read as reassurance.
    case "$out" in
        *"PKList updated"*) ok   "$3 -> $1 (added)" ;;
        *Duplicate*)        ok   "$3 -> $1 (already trusted)" ;;
        *) fail "$3 -> $1"; printf '%s\n' "$out" | tail -3 | sed 's/^/       /'; return 1 ;;
    esac
}

if [ "$PROVISION_MODE" = check ]; then
    if [ "${RESTARTED:-0}" = 1 ]; then
        warn "instance was restarted — certificate trust would be restored"
    else
        say "would ensure SNC + wsRFC trust (idempotent)"
    fi
    return 0
fi

rc=0
_trust SAPSNCS.pse "$_CERTS/snc_client.crt" "SNC client cert" || rc=1

# sapgenpse wants PEM; the wsRFC certificate is kept as DER.
if [ -f "$_CERTS/wsrfc_client.der" ]; then
    _pem="$(mktemp -t wsclient-XXXXXX.crt)"
    if openssl x509 -inform DER -in "$_CERTS/wsrfc_client.der" -out "$_pem" 2>/dev/null; then
        _trust SAPSSLS.pse "$_pem" "wsRFC client cert" || rc=1
    else
        warn "wsRFC client cert (DER -> PEM conversion failed)"
        rc=1
    fi
    rm -f "$_pem"
fi

# The ICM holds the server PSE open, so new trust stays invisible until the next restart --
# which is exactly what we are recovering from.  grep, not tail: the last line of that
# output is blank, so tailing it printed nothing and looked like a silent failure.
cls ZCL_ERPL_ICM_RELOAD "$HERE/assets/proto/abap/zcl_erpl_icm_reload.abap" \
    "reload the ICM's PSE" >/dev/null 2>&1
if _icm=$(adt object run ZCL_ERPL_ICM_RELOAD 2>&1 | grep -i "ICM_SSL_PSE_CHANGED"); then
    ok "ICM reloaded its PSE ($_icm)"
else
    fail "ICM did not reload its PSE; new trust is not live yet"
    rc=1
fi

return $rc
