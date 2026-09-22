# Make every remote-enabled function module callable over wsRFC (UCON).
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
#   2. Adds a single wildcard entry to the *default* communication assembly.  The RFC
#      runtime forces a '*' entry to the active phase for external scope, so this makes
#      wsRFC behave like classic RFC on a development system.  A deliberately broad
#      grant: right for a disposable trial, wrong for anything anyone relies on.
#   3. Pushes it into the runtime tables the kernel reads.  Step 2 writes customizing
#      only, and the two are measurably different: registering eleven modules left
#      UCONRFCSTATEHEAD with 22,925 rows while every call was still rejected.  Done on
#      every run, because "in customizing but not in the runtime" is exactly the state a
#      half-finished run leaves behind.
#
# NOT SUFFICIENT ON ITS OWN, AS OF 2026-09-22.  Everything UCON documents is configured
# and permissive after this step -- a '*' entry at scope E and phase A on the default
# assembly, a virtual host matching any inbound address (IPADDR/PORT/SNC all '*'), and
# CA -> vhost -> config wired in UCONCAVHOSTRT -- and an application module over wsRFC is
# still answered "UCON RFC Rejected".  Two runtime tables stay empty, UCONRFCCFGRT and
# UCONRFCSRVRT, and set_ucon_config_name_4_rt / set_ucon_service_name_4_rt do not fill
# them when called directly either.  They carry the configuration's identity and
# timestamp, so the likeliest remaining explanation is the kernel's own cache, whose
# lever is an instance restart -- which re-materialises every PSE from the database and
# so destroys the certificate trust step 30 just installed.  Parked there deliberately:
# on an S/4HANA Cloud tenant the communication arrangement grants this access, so the
# whole problem is a trial workaround rather than a product gap.
#
# WHY THE WILDCARD GOES ON THE DEFAULT ASSEMBLY and not a private per-application one:
# register_rfm_list builds its own (/1BCMIDRF/APP_<id>), but the runtime setter only
# emits rows for an assembly that has a UCONRFCSERVCUST entry with a config and a vhost,
# and only the default assembly has one -- so a private assembly pushes nothing, without
# raising.  That cost an afternoon; the class header records it.
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
        # Print the wildcard's own runtime row -- the one that decides a call -- and the
        # config runtime counts. An earlier version grepped for words the class no longer
        # prints and for the first line matching "runtime:", which is the config line, so
        # it showed an empty verdict and hid the row that mattered.
        ok "UCON: $(printf '%s\n' "$_ucon_out" | grep -aE "added '|is already in" | head -1)"
        printf '%s\n' "$_ucon_out" | grep -aE '^runtime: |^config runtime: ' \
            | sed 's/^/       /'
        case "$_ucon_out" in
            *"CFGRT=0"*|*"SRVRT=0"*)
                warn "UCONRFCCFGRT/UCONRFCSRVRT are empty; calls are still refused" ;;
        esac ;;
    *)  fail "UCON: erpl's modules are not callable over wsRFC"; _ucon_rc=1 ;;
esac
# Print the diagnosis on failure rather than only the verdict: setup_dark and
# register_rfm_list both report through this output and both can fail for reasons worth
# reading (no transport, insufficient authorisation, UCON not switched on).
if [ "$_ucon_rc" != 0 ]; then
    printf '%s\n' "$_ucon_out" | grep -aiE "failed|CX_|not active" | head -4 | sed 's/^/       /'
fi

return $_ucon_rc
