CLASS zcl_erpl_rev_pkg DEFINITION PUBLIC FINAL CREATE PUBLIC.
  PUBLIC SECTION.
    INTERFACES if_oo_adt_classrun.
  PRIVATE SECTION.
    METHODS mkpkg IMPORTING name TYPE devclass parent TYPE devclass txt TYPE string
                            req TYPE trkorr out TYPE REF TO if_oo_adt_classrun_out.
ENDCLASS.

CLASS zcl_erpl_rev_pkg IMPLEMENTATION.
  " Bootstrap the transportable package hierarchy for a transport delivery:
  "   ZERPL (root) -> ZERPL_CORE (production) + ZERPL_TEST (tests/demos/fixtures)
  " Software component HOME (transportable). Idempotent (reuse if present). Emits the
  " workbench request it recorded the packages on, so a packaging script can reuse it.
  METHOD mkpkg.
    DATA: ls TYPE scompkdtln, li TYPE REF TO if_package.
    ls-devclass = name. ls-ctext = txt. ls-as4user = sy-uname. ls-dlvunit = 'HOME'.
    IF parent IS NOT INITIAL. ls-parentcl = parent. ENDIF.
    TRY.
        cl_package_factory=>load_package( EXPORTING i_package_name = name
          IMPORTING e_package = li EXCEPTIONS object_not_existing = 1 OTHERS = 2 ).
        IF sy-subrc = 0. out->write( |{ name } exists| ). RETURN. ENDIF.
        cl_package_factory=>create_new_package(
          EXPORTING i_reuse_deleted_object = abap_true
          IMPORTING e_package = li CHANGING c_package_data = ls EXCEPTIONS OTHERS = 1 ).
        IF li IS NOT BOUND.
          out->write( |{ name } create FAIL { sy-msgid }{ sy-msgno } { sy-msgv1 }| ). RETURN.
        ENDIF.
        li->save( EXPORTING i_suppress_dialog = abap_true i_transport_request = req EXCEPTIONS OTHERS = 1 ).
        out->write( |{ name } created subrc={ sy-subrc } { sy-msgid }{ sy-msgno } { sy-msgv1 }| ).
      CATCH cx_root INTO DATA(lx). out->write( |{ name } EXC: { lx->get_text( ) }| ).
    ENDTRY.
  ENDMETHOD.

  METHOD if_oo_adt_classrun~main.
    " A workbench request WITH a task for the current user (TK248 without the task).
    DATA ls_hdr TYPE trwbo_request_header.
    CALL FUNCTION 'TR_INSERT_REQUEST_WITH_TASKS'
      EXPORTING iv_type = 'K' iv_text = 'erpl-rev: package bootstrap'
                it_users = VALUE scts_users( ( user = sy-uname type = 'S' ) )
      IMPORTING es_request_header = ls_hdr EXCEPTIONS OTHERS = 1.
    out->write( |REQ={ ls_hdr-trkorr }| ).
    DATA(lv_req) = ls_hdr-trkorr.
    mkpkg( name = 'ZERPL'      parent = ''      txt = 'erpl-rev: SAP -> DuckDB bridge'       req = lv_req out = out ).
    mkpkg( name = 'ZERPL_CORE' parent = 'ZERPL' txt = 'erpl-rev: production (transport set)' req = lv_req out = out ).
    mkpkg( name = 'ZERPL_TEST' parent = 'ZERPL' txt = 'erpl-rev: tests / demos / fixtures'   req = lv_req out = out ).
  ENDMETHOD.
ENDCLASS.
