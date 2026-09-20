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

# The wsRFC client pair lives where erpl-proto's own live tests read it, which is
# gitignored -- a private key does not belong in the repository, and the committed
# wsrfc_client.der used to be a certificate whose key existed nowhere in either tree.
# Trusting a certificate nobody can present is pure attack surface, so it is generated
# here instead, as a pair, and regenerated only if it goes missing.
_WS_DIR="$(cd "$HERE/../.." && pwd)/proto/captures/wsrfc-self/clientcert"
_WS_CERT="$_WS_DIR/client_cert.der"
_WS_KEY="$_WS_DIR/client_key.pk8.der"

# Generates the wsRFC client pair if it is not already there. DER for both, because
# that is what rustls takes and what SUSR_CERT_ASSIGN wants; a PEM copy is kept only
# because sapgenpse will not read DER.
_ensure_ws_pair() {
    if [ -f "$_WS_CERT" ] && [ -f "$_WS_KEY" ]; then
        return 0
    fi
    mkdir -p "$_WS_DIR" || return 1
    openssl req -x509 -newkey rsa:2048 -nodes -days 3650 \
        -keyout "$_WS_DIR/client_key.pem" -out "$_WS_DIR/client.crt" \
        -subj "/C=DE/O=DataZoo/OU=erpl/CN=ERPL_WSRFC_CLIENT" >/dev/null 2>&1 || return 1
    openssl x509 -in "$_WS_DIR/client.crt" -outform DER -out "$_WS_CERT" || return 1
    openssl pkcs8 -topk8 -nocrypt -in "$_WS_DIR/client_key.pem" -outform DER \
        -out "$_WS_KEY" || return 1
    say "generated a wsRFC client pair in $_WS_DIR"
}

# Maps the certificate to the provisioning user. Trust alone is not enough: an ICM
# that trusts a certificate it cannot resolve to a user answers "Name or password is
# incorrect" after a successful TLS handshake, which reads like a credential problem
# and is not one.
_map_ws_cert() {
    local template="$HERE/assets/proto/abap/zcl_erpl_wsrfc_map.abap.in"
    [ -f "$template" ] || { warn "wsRFC cert mapping (template missing)"; return 1; }
    local generated; generated="$(mktemp -t zcl_erpl_wsrfc_map-XXXXXX.abap)"
    # 180 characters per chunk: the prefix brings the line to about 205, inside ABAP's
    # 255-character limit.
    local chunks; chunks="$(base64 -w0 < "$_WS_CERT" | fold -w 180 \
        | sed "s/^/    lv_b64 = lv_b64 \&\& '/; s/\$/'./")"
    awk -v repl="$chunks" '{ if ($0 == "@CERT_CHUNKS@") print repl; else print }' \
        "$template" > "$generated"
    cls ZCL_ERPL_WSRFC_MAP "$generated" "map the wsRFC client cert to a user" >/dev/null 2>&1
    rm -f "$generated"

    local out
    out=$(adt object run ZCL_ERPL_WSRFC_MAP 2>&1)
    case "$out" in
        *"mapped the certificate"*)
            ok "wsRFC cert mapped to $SAP_USER ($(printf '%s\n' "$out" \
                | grep -a 'USREXTID:' | head -1 | sed 's/.*USREXTID: //'))" ;;
        *)
            fail "wsRFC cert not mapped to $SAP_USER"
            printf '%s\n' "$out" | grep -aiE "subrc|USREXTID|error" | head -3 \
                | sed 's/^/       /'
            return 1 ;;
    esac
}

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
        say "would ensure SNC + wsRFC trust and the wsRFC user mapping (idempotent)"
    fi
    [ -f "$_WS_CERT" ] || say "would generate a wsRFC client pair in $_WS_DIR"
    return 0
fi

rc=0
_trust SAPSNCS.pse "$_CERTS/snc_client.crt" "SNC client cert" || rc=1

# sapgenpse wants PEM; the wsRFC certificate is kept as DER.
if _ensure_ws_pair; then
    _pem="$(mktemp -t wsclient-XXXXXX.crt)"
    if openssl x509 -inform DER -in "$_WS_CERT" -out "$_pem" 2>/dev/null; then
        _trust SAPSSLS.pse "$_pem" "wsRFC client cert" || rc=1
    else
        warn "wsRFC client cert (DER -> PEM conversion failed)"
        rc=1
    fi
    rm -f "$_pem"
else
    warn "wsRFC client pair (could not generate one in $_WS_DIR)"
    rc=1
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

# After the reload, not before: the mapping is pointless while the ICM still refuses
# the certificate, and doing it in this order means one failure line rather than two.
if [ -f "$_WS_CERT" ]; then
    _map_ws_cert || rc=1
fi

return $rc
