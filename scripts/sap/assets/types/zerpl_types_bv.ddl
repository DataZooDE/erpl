@EndUserText.label : 'erpl boundary values, one column per type'
@AbapCatalog.enhancement.category : #NOT_EXTENSIBLE
@AbapCatalog.tableCategory : #TRANSPARENT
@AbapCatalog.deliveryClass : #A
@AbapCatalog.dataMaintenance : #RESTRICTED
define table zerpl_types_bv {

  key client : abap.clnt not null;
  key row_id : abap.char(8) not null;
  p1_0       : abap.dec(1,0);
  p2_0       : abap.dec(2,0);
  p2_1       : abap.dec(2,1);
  p2_2       : abap.dec(2,2);
  p3_1       : abap.dec(3,1);
  p4_1       : abap.dec(4,1);
  p4_2       : abap.dec(4,2);
  p5_5       : abap.dec(5,5);
  p16_2      : abap.dec(16,2);
  p30_14     : abap.dec(30,14);
  p31_14     : abap.dec(31,14);
  p31_0      : abap.dec(31,0);
  waers      : abap.cuky;
  @Semantics.amount.currencyCode : 'zerpl_types_bv.waers'
  c13_2      : abap.curr(13,2);
  @Semantics.amount.currencyCode : 'zerpl_types_bv.waers'
  c16_2      : abap.curr(16,2);
  meins      : abap.unit(3);
  @Semantics.quantity.unitOfMeasure : 'zerpl_types_bv.meins'
  q13_3      : abap.quan(13,3);
  @Semantics.quantity.unitOfMeasure : 'zerpl_types_bv.meins'
  q14_3      : abap.quan(14,3);
  n10        : abap.numc(10);
  ch10       : abap.char(10);
  i1         : abap.int1;
  i2         : abap.int2;
  i4         : abap.int4;
  i8         : abap.int8;
  fl         : abap.fltp;
  df16       : abap.decfloat16;
  df34       : abap.decfloat34;
  dt         : abap.dats;
  tm         : abap.tims;
  rw         : abap.raw(16);
  ac         : abap.accp;
  pr         : abap.prec;
  lg         : abap.lang;
  ut         : abap.utclong;

}
