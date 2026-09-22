* Make every remote-enabled function module callable over wsRFC.
*
* WHY THIS IS NEEDED AT ALL.  UCON applies to wsRFC and not to classic RFC.  On a
* system where UCON was never set up there is no allow-list to consult, and the kernel
* answers every application module with "UCON RFC Rejected" while letting a small basis
* set (RFC_PING, RFC_SYSTEM_INFO, RFC_GET_FUNCTION_INTERFACE) through.  That is the
* system's policy, not a transport limit -- on an S/4HANA Cloud tenant the communication
* arrangement grants the same access.
*
* WHAT THIS DOES is make wsRFC behave like classic RFC on a development system: one
* wildcard entry, which the runtime forces to the active phase for external scope.  A
* deliberately broad grant, appropriate for a disposable trial and not for a system
* anyone relies on.
*
* WHY A WILDCARD ON THE *DEFAULT* ASSEMBLY rather than a private one per application.
* Two measured reasons, both of which cost an afternoon:
*
*   1. cl_ucon_api_factory=>register_rfm_list creates its own assembly
*      (/1BCMIDRF/APP_<id>) and includes it in the default one.  That writes
*      *customizing*; the RFC runtime reads the RT tables.  After registering eleven
*      modules, UCONRFCSTATEHEAD held 22,925 rows and every call was still rejected.
*   2. Pushing the runtime for that private assembly inserts nothing, silently.
*      set_ucon_rfc_state_by_rfm_list, given an ip_service_assembly, only emits rows for
*      an assembly that has a UCONRFCSERVCUST entry carrying a config and a vhost -- and
*      only the default assembly has one.  Every module hit its CONTINUE.
*
* So the grant goes on the assembly the runtime can actually resolve.
*
* IV_NAME_SPACE IS THE LOCAL-SAVE SWITCH for setup, not decoration.
* create_default_com_assemblies branches on it:
*
*     IF iv_name_space = c_gen_prefix.        "'/1BCMIDRF/'
*       li_ca->save( dev_class = '$TMP' ).    "local
*     ELSE.
*       li_ca->save( ).                       "asks for a transport
*
* and the transport branch tries to raise the transport-request dynpro, which in a
* headless class run fails as "Sending of dynpro SAPLSTRD 0353 not possible: No window
* system type specified".  IV_LOCAL does not cover it.
CLASS zcl_erpl_ucon_release DEFINITION
  PUBLIC FINAL CREATE PUBLIC.
  PUBLIC SECTION.
    INTERFACES if_oo_adt_classrun.
  PRIVATE SECTION.
    " The runtime forces this entry to phase 'A' whatever the per-module phase says:
    "   "set phase for function module '*' to A in all cases"  -- CL_UCONRFC_RUNTIME
    CONSTANTS c_all_modules TYPE rs38l_fnam VALUE '*'.
ENDCLASS.

CLASS zcl_erpl_ucon_release IMPLEMENTATION.
  METHOD if_oo_adt_classrun~main.

    " ---- 1. UCON's default objects, if they are not there yet ----
    DATA lv_default_ca TYPE uconcaid.
    TRY.
        cl_ucon_setup=>get_default_object_names(
          EXPORTING client_independent_only = abap_true
          IMPORTING default_ca_name         = lv_default_ca ).
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
            cl_ucon_setup=>get_default_object_names(
              EXPORTING client_independent_only = abap_true
              IMPORTING default_ca_name         = lv_default_ca ).
          CATCH cx_root INTO DATA(lx_setup).
            out->write( |setup_dark failed: { lx_setup->get_text( ) }| ).
            RETURN.
        ENDTRY.
    ENDTRY.
    out->write( |default communication assembly: { lv_default_ca }| ).

    " ---- 2. The wildcard entry on that assembly ----
    TRY.
        DATA(lo_ca) = cl_ucon_api_factory=>get_communication_assembly(
                        com_assembly_name = CONV uconservid( lv_default_ca )
                        modifiable        = abap_true ).

        " Checked rather than caught: add_sub_rfcfunc raises for a duplicate, and a
        " re-run reporting a failure it does not have is worse than no report.
        DATA(lt_existing) = lo_ca->get_sub_rfcfunc( ).
        READ TABLE lt_existing TRANSPORTING NO FIELDS
             WITH KEY table_line = c_all_modules.
        IF sy-subrc = 0.
          out->write( |'{ c_all_modules }' is already in { lv_default_ca }| ).
        ELSE.
          lo_ca->add_sub_rfcfunc( rfcfunc_module = c_all_modules ).
          lo_ca->activate( ).
          " $TMP, because the default assembly lives in the generated namespace and a
          " plain save( ) would ask for a transport, which a class run cannot answer.
          lo_ca->save( dev_class = '$TMP' ).
          COMMIT WORK AND WAIT.
          out->write( |added '{ c_all_modules }' to { lv_default_ca }| ).
        ENDIF.
        lo_ca->free( ).
      CATCH cx_root INTO DATA(lx_ca).
        out->write( |assembly update failed: { lx_ca->get_text( ) }| ).
        RETURN.
    ENDTRY.

    " ---- 3. Push it into the runtime tables the kernel reads ----
    " Unconditional: "in customizing but not in the runtime" is exactly the state a
    " half-finished run leaves behind, and a re-run has to repair that rather than
    " report there is nothing to do.
    DATA lt_range TYPE ucon_funcname_range.
    APPEND VALUE #( sign = 'I' option = 'EQ' low = c_all_modules ) TO lt_range.
    TRY.
        cl_uconrfc_runtime=>set_ucon_ca_by_rfm_list_4_rt(
          it_funcname         = lt_range
          ip_service_assembly = CONV uconservid( lv_default_ca )
          ip_action           = 'I' ).
        cl_uconrfc_runtime=>set_ucon_rfc_state_by_rfm_list(
          it_funcname         = lt_range
          ip_service_assembly = CONV uconservid( lv_default_ca )
          ip_action           = 'I' ).
        cl_uconrfc_runtime=>sync_db_buffer_all( ).
        COMMIT WORK AND WAIT.
      CATCH cx_root INTO DATA(lx_rt).
        out->write( |runtime push failed: { lx_rt->get_text( ) }| ).
        RETURN.
    ENDTRY.

    " ---- 4. Report the row that actually decides a call ----
    " Not a count: the question is whether a '*' row exists for external scope in the
    " active phase, and an earlier version of this class reported success off tables the
    " API does not even write.
    SELECT funcname, scope, actual_phase, ca_name, virt_hostname
      FROM uconrfcsrvmainrt
      WHERE funcname = @c_all_modules
      INTO TABLE @DATA(lt_rt).
    IF lt_rt IS INITIAL.
      out->write( |no runtime row for '{ c_all_modules }' -- calls will still be refused| ).
      RETURN.
    ENDIF.
    LOOP AT lt_rt INTO DATA(ls_rt).
      out->write( |runtime: { ls_rt-funcname } scope={ ls_rt-scope } | &&
                  |phase={ ls_rt-actual_phase } ca={ ls_rt-ca_name } | &&
                  |vhost={ ls_rt-virt_hostname }| ).
    ENDLOOP.
    out->write( |ucon release done| ).
  ENDMETHOD.
ENDCLASS.
