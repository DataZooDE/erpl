CLASS zcl_erpl_ape_probe DEFINITION PUBLIC FINAL CREATE PUBLIC.
  PUBLIC SECTION.
    INTERFACES if_oo_adt_classrun.
ENDCLASS.

CLASS zcl_erpl_ape_probe IMPLEMENTATION.
  METHOD if_oo_adt_classrun~main.
    DATA lv_uuid  TYPE char32.
    DATA lt_msg   TYPE dhape_t_graph_msg.
    DATA lt_in    TYPE dhape_t_graph_port.
    DATA lt_out   TYPE dhape_t_graph_port.
    DATA lv_graph TYPE string.
    DATA lv_st    TYPE c LENGTH 1.

    lv_graph = '{"Attributes":{"graphid":"erpl_ape_spike","protocol":"v7","graphkind":"user","multiplicity":"1"},"snapshotConfig":{"enabled":"false"},"Processes":{"ag'.
    lv_graph = lv_graph && 'ent":{"Component":"internal.agentOperator","Metadata":{"Config":{}}},"reader":{"Component":"com.sap.abap.reader","Metadata":{"Config":{"transferMode":'.
    lv_graph = lv_graph && '"Initial Load","objectName":{"remoteObjectReference":{"qualifiedName":"/CDS/TMP/ZERPL_APE_FLIGHT"},"schema":{"tableBasedRepresentation":{"attributes":'.
    lv_graph = lv_graph && '[{"name":"Carrid","datatype":"STRING","nativeDatatype":"CHAR"},{"name":"Connid","datatype":"STRING","nativeDatatype":"NUMC"},{"name":"Fldate","datatyp'.
    lv_graph = lv_graph && 'e":"DATE","nativeDatatype":"DATS"},{"name":"Price","datatype":"DECIMAL","nativeDatatype":"CURR","precision":"15","scale":"2"},{"name":"Currency","data'.
    lv_graph = lv_graph && 'type":"STRING","nativeDatatype":"CUKY"},{"name":"Planetype","datatype":"STRING","nativeDatatype":"CHAR"},{"name":"Seatsmax","datatype":"INTEGER","nati'.
    lv_graph = lv_graph && 'veDatatype":"INT4"},{"name":"Seatsocc","datatype":"INTEGER","nativeDatatype":"INT4"}]}}}},"AdditionalInPorts":null,"AdditionalOutPorts":[{"name":"outD'.
    lv_graph = lv_graph && 'ata","type":"table","vtype-ID":"$GRAPH.generated.reader_outData"}],"inports":null,"outports":null}}},"Connections":[],"Outports":{"0":{"Process":"read'.
    lv_graph = lv_graph && 'er","Port":"outData","Metadata":{"portNumber":0,"type":"table"}}},"vTypes":{}}'.

    CALL FUNCTION 'DHAPE_GRAPH_MANAGER'
      EXPORTING iv_mode = 'C' iv_appid = 'ERPL_APE_SPIKE' iv_graph = lv_graph
      IMPORTING ev_graph_uuid = lv_uuid et_msg = lt_msg.
    SELECT SINGLE status FROM dhape_graph INTO @lv_st WHERE uuid = @lv_uuid.
    out->write( |CREATE uuid={ lv_uuid } st={ lv_st } msgs={ lines( lt_msg ) }| ).
    LOOP AT lt_msg INTO DATA(ls1). out->write( |  C [{ ls1-type }] { ls1-text }| ). ENDLOOP.

    CALL FUNCTION 'DHAPE_GRAPH_MANAGER'
      EXPORTING iv_mode = 'R' iv_appid = 'ERPL_APE_SPIKE' iv_graph_uuid = lv_uuid
      IMPORTING et_msg = lt_msg.
    SELECT SINGLE status FROM dhape_graph INTO @lv_st WHERE uuid = @lv_uuid.
    out->write( |START st={ lv_st } msgs={ lines( lt_msg ) }| ).
    LOOP AT lt_msg INTO DATA(ls2). out->write( |  S [{ ls2-type }] { ls2-text }| ). ENDLOOP.

    DO 6 TIMES.
      CLEAR: lt_out, lt_msg.
      CALL FUNCTION 'DHAPE_GRAPH_ROUNDTRIP'
        EXPORTING iv_graph_uuid = lv_uuid it_port = lt_in it_msg = lt_msg
        IMPORTING et_port = lt_out et_msg = lt_msg.
      SELECT SINGLE status FROM dhape_graph INTO @lv_st WHERE uuid = @lv_uuid.
      out->write( |RT { sy-index }: st={ lv_st } ports={ lines( lt_out ) } msgs={ lines( lt_msg ) }| ).
      LOOP AT lt_out INTO DATA(ls_p).
        out->write( |  PORT n={ ls_p-port_number } dir={ ls_p-direction } pst={ ls_p-port_status } len={ strlen( ls_p-port_data ) }| ).
        out->write( |  DATA: { substring( val = ls_p-port_data off = 0 len = nmin( val1 = 900 val2 = strlen( ls_p-port_data ) ) ) }| ).
      ENDLOOP.
      LOOP AT lt_msg INTO DATA(ls3). out->write( |  M [{ ls3-type }] { ls3-text }| ). ENDLOOP.
    ENDDO.

    CALL FUNCTION 'DHAPE_GRAPH_MANAGER'
      EXPORTING iv_mode = 'S' iv_appid = 'ERPL_APE_SPIKE' iv_graph_uuid = lv_uuid
      IMPORTING et_msg = lt_msg.
    out->write( |STOP msgs={ lines( lt_msg ) }| ).
  ENDMETHOD.
ENDCLASS.