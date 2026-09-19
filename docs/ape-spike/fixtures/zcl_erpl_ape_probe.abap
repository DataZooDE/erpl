CLASS zcl_erpl_ape_probe DEFINITION PUBLIC FINAL CREATE PUBLIC.
  PUBLIC SECTION.
    INTERFACES if_oo_adt_classrun.
ENDCLASS.

CLASS zcl_erpl_ape_probe IMPLEMENTATION.
  METHOD if_oo_adt_classrun~main.
    DATA lv_uuid TYPE char32.
    DATA lt_msg  TYPE dhape_t_graph_msg.
    DATA lt_in   TYPE dhape_t_graph_port.
    DATA lt_out  TYPE dhape_t_graph_port.
    DATA lv_graph TYPE string.
    DATA lv_rj   TYPE dhape_reconnect_json.
    DATA lv_rmd  TYPE string.
    DATA lv_sub  TYPE string.

    lv_graph = '{"Attributes":{"graphid":"erpl_ape_v6","protocol":"v6","graphkind":"user","multiplicity":"1"},"Processes":{"reader":{"Component":"com.sap.abap.cds.rea'.
    lv_graph = lv_graph && 'der.v2","Metadata":{"Config":{"subscriptionType":"New","subscriptionName":"@@SUB@@","cdsname":"ZERPL_APE_FLIGHT","action":"Initial Load","chunkSize":1'.
    lv_graph = lv_graph && '00,"wireformat":"Required Conversions Plus Time Format and Currency"}}}},"Connections":[],"Outports":{"0":{"Process":"reader","Port":"outMessageData",'.
    lv_graph = lv_graph && '"Metadata":{"portNumber":0,"type":"message"}}},"vTypes":{}}'.

    lv_sub = |ERPL_S1_{ sy-datum }{ sy-uzeit }|.
    REPLACE ALL OCCURRENCES OF '@@SUB@@' IN lv_graph WITH lv_sub.

    CALL FUNCTION 'DHAPE_GRAPH_MANAGER'
      EXPORTING iv_mode = 'C' iv_appid = 'ERPL_APE_V6' iv_graph = lv_graph
      IMPORTING ev_graph_uuid = lv_uuid et_msg = lt_msg.
    out->write( |CREATE uuid={ lv_uuid } msgs={ lines( lt_msg ) }| ).
    LOOP AT lt_msg INTO DATA(ls1). out->write( |  C [{ ls1-type }] { ls1-text }| ). ENDLOOP.
    IF lv_uuid IS INITIAL. RETURN. ENDIF.

    DO 8 TIMES.
      CLEAR: lt_out, lt_msg, lv_rj, lv_rmd.
      lt_in = VALUE #( ( graph_uuid = lv_uuid port_number = 0 direction = 'O' port_status = 'R' ) ).
      CALL FUNCTION 'DHAPE_GRAPH_ROUNDTRIP'
        EXPORTING iv_graph_uuid = lv_uuid it_port = lt_in it_msg = lt_msg
        IMPORTING et_port = lt_out et_msg = lt_msg
                  ev_reconnect_json = lv_rj ev_rucksack_md = lv_rmd.
      out->write( |RT { sy-index }: ports={ lines( lt_out ) } msgs={ lines( lt_msg ) } rucksack_len={ strlen( lv_rmd ) } reconn_len={ strlen( lv_rj ) }| ).
      IF strlen( lv_rmd ) > 0.
        out->write( |  RUCKSACK: { substring( val = lv_rmd off = 0 len = nmin( val1 = 1200 val2 = strlen( lv_rmd ) ) ) }| ).
      ENDIF.
      LOOP AT lt_out INTO DATA(ls_p).
        out->write( |  PORT n={ ls_p-port_number } pst={ ls_p-port_status } len={ strlen( ls_p-port_data ) }| ).
      ENDLOOP.
      LOOP AT lt_msg INTO DATA(ls3). out->write( |  M [{ ls3-type }] { ls3-text }| ). ENDLOOP.
    ENDDO.

    CALL FUNCTION 'DHAPE_GRAPH_MANAGER'
      EXPORTING iv_mode = 'S' iv_appid = 'ERPL_APE_V6' iv_graph_uuid = lv_uuid
      IMPORTING et_msg = lt_msg.
    out->write( 'STOP' ).
  ENDMETHOD.
ENDCLASS.