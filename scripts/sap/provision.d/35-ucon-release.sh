# Make the modules erpl needs callable over wsRFC (UCON).
#
# THIS STEP IS ABOUT wsRFC ONLY.  UCON applies to WebSocket RFC and not to classic RFC,
# so nothing here changes how the existing suites reach the system.  What it changes is
# that an *external* wsRFC caller may invoke the DHAPE_* / DHAMB_* modules at all: on a
# system where UCON was never set up there is no allow-list to consult, and the kernel
# answers every application module with "UCON RFC Rejected" while letting a small basis
# set (RFC_PING, RFC_SYSTEM_INFO, RFC_GET_FUNCTION_INTERFACE) through.  That is the
# system's policy rather than a transport limit -- on an S/4HANA Cloud tenant the
# communication arrangement grants the same access, which is why this is a trial concern.
#
# IT RUNS AFTER 30 because wsRFC sign-on has to work before a rejection is the thing in
# the way: without the client certificate mapped, the call fails at logon instead and
# this step's effect is invisible.
#
# WHAT IT ACTUALLY DOES is three things, because they are ordered:
#
#   1. Generates UCON's default objects if they are missing (cl_ucon_setup=>setup_dark,
#      the non-interactive form of what UCONCOCKPIT does on first use).  Current client
#      only, no change documents, and the generated-objects namespace so the save is
#      local -- which is the parameter that keeps it out of a transport dialog.
#      register_rfm_list cannot work before this: it resolves the default communication
#      assembly through get_default_object_names, which raises CX_UCON_NOT_ACTIVE until
#      the defaults exist.
#   2. Registers the module list under application id ERPL, which the API places in an
#      assembly of its own (/1BCMIDRF/APP_ERPL) included in the default one.
#   3. Pushes that into the runtime tables the kernel reads.  Step 2 writes customizing
#      only: measured, after registering alone UCONRFCSTATEHEAD held 22,925 rows,
#      UCONRFCSTATERT held none, and every call was still rejected.  Done on every run,
#      not only after a fresh registration, because "registered but not in the runtime"
#      is exactly the state this repairs.
#
# THIS WRITES THE SYSTEM'S SECURITY CONFIGURATION on infrastructure shared by three
# repositories.  It is reversible -- cl_ucon_setup=>revert( ) is supported, and the
# container is disposable in any case -- but it is not a fixture, so it is worth knowing
# what it touches before running it.  Verify afterwards that the classic path still
# answers (`make sql_tests_rfc` is the cheap check) as well as the wsRFC one.

_UCON_ABAP="$HERE/assets/proto/abap/zcl_erpl_ucon_release.abap"

if [ "$PROVISION_MODE" = check ]; then
    # Read-only: report what UCON state the system is in without touching it.
    _probe="$(mktemp -t zcl_erpl_ucon_probe-XXXXXX.abap)"
    cat > "$_probe" <<'ABAP'
CLASS zcl_erpl_ucon_probe DEFINITION PUBLIC FINAL CREATE PUBLIC.
  PUBLIC SECTION.
    INTERFACES if_oo_adt_classrun.
ENDCLASS.
CLASS zcl_erpl_ucon_probe IMPLEMENTATION.
  METHOD if_oo_adt_classrun~main.
    out->write( |ws_rfc_active={ cl_ucon_setup=>is_ws_rfc_active( ) }| ).
    TRY.
        DATA lv_ca TYPE uconcaid.
        cl_ucon_setup=>get_default_object_names(
          EXPORTING client_independent_only = abap_true
          IMPORTING default_ca_name         = lv_ca ).
        out->write( |default_ca={ lv_ca }| ).
      CATCH cx_ucon_not_active.
        out->write( |default_ca=NONE (UCON not set up)| ).
    ENDTRY.
    IF cl_ucon_api_factory=>exists_communication_assembly(
         ca_id = '/1BCMIDRF/APP_ERPL' ) = abap_true.
      out->write( |erpl_assembly=present| ).
    ELSE.
      out->write( |erpl_assembly=absent| ).
    ENDIF.
  ENDMETHOD.
ENDCLASS.
ABAP
    cls ZCL_ERPL_UCON_PROBE "$_probe" "report UCON state" >/dev/null 2>&1
    rm -f "$_probe"
    _state="$(adt object run ZCL_ERPL_UCON_PROBE 2>&1 | grep -aE '=' | tr '\n' ' ')"
    if [ -n "$_state" ]; then
        say "UCON: $_state"
    else
        warn "could not read UCON state"
    fi
    case "$_state" in
        *erpl_assembly=present*) say "would leave UCON alone (already registered)" ;;
        *) say "would generate UCON defaults if absent and register erpl's modules" ;;
    esac
    return 0
fi

[ -f "$_UCON_ABAP" ] || { fail "UCON release (class source missing)"; return 1; }

cls ZCL_ERPL_UCON_RELEASE "$_UCON_ABAP" "release erpl's modules for wsRFC" >/dev/null 2>&1

_ucon_out="$(adt object run ZCL_ERPL_UCON_RELEASE 2>&1)"
_ucon_rc=0
# Matched on the final line, not on "registered": registering writes customizing and
# the kernel reads the runtime tables, so a run that registered and failed to push the
# runtime changes nothing observable and must not read as success.
case "$_ucon_out" in
    *"ucon release done"*)
        ok "UCON: $(printf '%s\n' "$_ucon_out" | grep -aE 'registered|already exists' | head -1)"
        say "$(printf '%s\n' "$_ucon_out" | grep -a 'runtime:' | head -1)" ;;
    *)  fail "UCON: erpl's modules are not callable over wsRFC"; _ucon_rc=1 ;;
esac
# Print the diagnosis on failure rather than only the verdict: setup_dark and
# register_rfm_list both report through this output and both can fail for reasons worth
# reading (no transport, insufficient authorisation, UCON not switched on).
if [ "$_ucon_rc" != 0 ]; then
    printf '%s\n' "$_ucon_out" | grep -aiE "failed|CX_|not active" | head -4 | sed 's/^/       /'
fi

return $_ucon_rc
