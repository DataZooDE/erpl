@EndUserText.label : 'erpl-rev delta watermark test table'
@AbapCatalog.enhancement.category : #NOT_EXTENSIBLE
@AbapCatalog.tableCategory : #TRANSPARENT
@AbapCatalog.deliveryClass : #A
@AbapCatalog.dataMaintenance : #RESTRICTED
define table zdelta_wm {

  key client     : abap.clnt not null;
  key id         : abap.char(10) not null;
  name           : abap.char(40);
  val            : abap.int4;
  changed_at     : abap.dec(21,7);

}
