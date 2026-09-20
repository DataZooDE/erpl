* Make the modules erpl needs callable over wsRFC.
*
* WHY THIS IS NEEDED AT ALL.  UCON applies to wsRFC and not to classic RFC.  On a
* system where UCON was never set up there is no allow-list to consult, and the
* kernel answers every application module with "UCON RFC Rejected" while letting a
* small basis set (RFC_PING, RFC_SYSTEM_INFO, RFC_GET_FUNCTION_INTERFACE) through.
* That is the system's policy, not a transport limit -- on an S/4HANA Cloud tenant the
* communication arrangement is what grants the same access.
*
* WHY IT IS THREE STEPS.  cl_ucon_api_factory=>register_rfm_list resolves the default
* communication assembly through cl_ucon_setup=>get_default_object_names, which raises
* CX_UCON_NOT_ACTIVE until UCON's default objects exist.  So the assembly has to be
* generated before anything can be added to it.
*
* And registering is not enough on its own: it writes *customizing*, while the RFC
* runtime reads the RT tables (UCONRFCCART, UCONRFCSRVFMRT, UCONRFCSTATERT).  Measured:
* after register_rfm_list alone, UCONRFCSTATEHEAD held 22,925 rows and UCONRFCSTATERT
* held none -- and every call was still answered "UCON RFC Rejected", because the
* runtime had nothing to permit it with.  cl_uconrfc_runtime pushes customizing into
* those tables; setup itself calls the same class for the objects it generates.
*
* setup_dark is the non-interactive form of what UCONCOCKPIT does on first use.  Current
* client only and no change documents, so it needs no transport.  There is a supported
* cl_ucon_setup=>revert( ) if it has to be undone.
*
* IV_NAME_SPACE IS THE LOCAL-SAVE SWITCH, not decoration.  create_default_com_assemblies
* branches on it:
*
*     IF iv_name_space = c_gen_prefix.        "'/1BCMIDRF/'
*       li_ca->save( dev_class = '$TMP' ).    "local
*     ELSE.
*       li_ca->save( ).                       "asks for a transport
*
* and the transport branch tries to raise the transport-request dynpro, which in a
* headless class run fails as "Sending of dynpro SAPLSTRD 0353 not possible: No window
* system type specified".  IV_LOCAL does not cover it.  So the namespace has to be
* c_gen_prefix, which also means the generated objects are named /1BCMIDRF/DEFAULT_CA
* and friends -- the same names UCONCOCKPIT generates.
CLASS zcl_erpl_ucon_release DEFINITION
  PUBLIC FINAL CREATE PUBLIC.
  PUBLIC SECTION.
    INTERFACES if_oo_adt_classrun.
  PRIVATE SECTION.
    " One assembly per application id, named /1BCMIDRF/APP_<id> by the API itself.
    CONSTANTS c_app_id TYPE ucon_app_id VALUE 'ERPL'.
ENDCLASS.

CLASS zcl_erpl_ucon_release IMPLEMENTATION.
  METHOD if_oo_adt_classrun~main.
    DATA lt_rfm TYPE ucon_rfcfunc_list_ttyp.

    " The ABAP Pipeline Engine and its metadata browser: what erpl_ape drives.
    APPEND 'DHAPE_GRAPH_MANAGER'           TO lt_rfm.
    APPEND 'DHAPE_GRAPH_ROUNDTRIP'         TO lt_rfm.
    APPEND 'DHAPE_GRAPH_VERSION'           TO lt_rfm.
    APPEND 'DHAMB_SERVICE_SYSTEM'          TO lt_rfm.
    APPEND 'DHAMB_SERVICE_HEALTH'          TO lt_rfm.
    APPEND 'DHAMB_SERVICE_DSET_BROWSE'     TO lt_rfm.
    APPEND 'DHAMB_SERVICE_DSET_DEFINITION' TO lt_rfm.
    APPEND 'DHAMB_SERVICE_DSET_PREVIEW'    TO lt_rfm.
    APPEND 'DHAMB_SERVICE_DSET_LINEAGE'    TO lt_rfm.
    " erpl_rfc's own paths, so a wsRFC connection is usable for more than ape and the
    " test oracle that compares the two transports can run.
    APPEND 'RFC_READ_TABLE'                TO lt_rfm.
    APPEND 'DDIF_FIELDINFO_GET'            TO lt_rfm.

    " ---- 1. UCON's default objects, if they are not there yet ----
    DATA lv_default_ca TYPE uconcaid.
    TRY.
        cl_ucon_setup=>get_default_object_names(
          EXPORTING client_independent_only = abap_true
          IMPORTING default_ca_name         = lv_default_ca ).
        out->write( |default communication assembly: { lv_default_ca }| ).
      CATCH cx_ucon_not_active.
        out->write( |UCON has no default objects yet; generating them| ).
        TRY.
            cl_ucon_setup=>setup_dark(
              iv_local                = abap_true
              iv_name_space           = cl_ucon_setup=>c_gen_prefix
              iv_transport_rfc_states = abap_false
              iv_current_client_only  = abap_true
              iv_avoid_cd             = abap_true ).
            COMMIT WORK AND WAIT.
            out->write( |setup_dark completed| ).
          CATCH cx_root INTO DATA(lx_setup).
            out->write( |setup_dark failed: { lx_setup->get_text( ) }| ).
            RETURN.
        ENDTRY.
    ENDTRY.

    DATA lt_range TYPE ucon_funcname_range.
    LOOP AT lt_rfm INTO DATA(lv_fm).
      APPEND VALUE #( sign = 'I' option = 'EQ' low = lv_fm ) TO lt_range.
    ENDLOOP.

    " ---- 2. Register the modules, unless that was already done ----
    " Checked rather than caught: register_rfm_list raises the same
    " CX_UCON_API_HELPER for "already exists" as for a real failure, and a re-run
    " reporting a failure it does not have is worse than no report.
    DATA(lv_ca_id) = |/1BCMIDRF/APP_{ c_app_id }|.
    IF cl_ucon_api_factory=>exists_communication_assembly(
         ca_id = CONV uconservid( lv_ca_id ) ) = abap_true.
      out->write( |{ lv_ca_id } already exists; nothing to register| ).
    ELSE.
      TRY.
          cl_ucon_api_factory=>register_rfm_list(
            iv_application_id = c_app_id
            iv_rfm_list       = lt_rfm ).
          COMMIT WORK AND WAIT.
          out->write( |registered { lines( lt_rfm ) } module(s) in { lv_ca_id }| ).
        CATCH cx_root INTO DATA(lx_reg).
          out->write( |register_rfm_list failed: { lx_reg->get_text( ) }| ).
          RETURN.
      ENDTRY.
    ENDIF.

    " ---- 3. Push it into the runtime tables the kernel reads ----
    " Unconditional, not only after a fresh registration: this is exactly the state a
    " system ends up in when the registration succeeded and the runtime push did not,
    " and re-running has to repair that rather than report "already done".
    TRY.
        cl_uconrfc_runtime=>set_ucon_ca_by_rfm_list_4_rt(
          it_funcname         = lt_range
          ip_service_assembly = CONV uconservid( lv_ca_id )
          ip_action           = 'I' ).
        cl_uconrfc_runtime=>set_ucon_rfc_state_by_rfm_list(
          it_funcname         = lt_range
          ip_service_assembly = CONV uconservid( lv_ca_id )
          ip_action           = 'I' ).
        cl_uconrfc_runtime=>sync_db_buffer_all( ).
        COMMIT WORK AND WAIT.
      CATCH cx_root INTO DATA(lx_rt).
        out->write( |runtime push failed: { lx_rt->get_text( ) }| ).
        RETURN.
    ENDTRY.

    " Report the runtime rows rather than claiming success: an empty UCONRFCSTATERT is
    " precisely the silent failure this step exists to fix.
    SELECT COUNT(*) FROM uconrfccart   INTO @DATA(lv_cart).
    SELECT COUNT(*) FROM uconrfcstatert INTO @DATA(lv_statert).
    SELECT COUNT(*) FROM uconrfcsrvfmrt INTO @DATA(lv_srvfmrt).
    out->write( |runtime: CART={ lv_cart } STATERT={ lv_statert } SRVFMRT={ lv_srvfmrt }| ).
    out->write( |is_ws_rfc_active = { cl_ucon_setup=>is_ws_rfc_active( ) }| ).
    out->write( |ucon release done| ).
  ENDMETHOD.
ENDCLASS.
