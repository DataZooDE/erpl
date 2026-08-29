@AccessControl.authorizationCheck: #NOT_REQUIRED
@EndUserText.label: 'ERPL param test CDS over SFLIGHT'
define view entity ZERPL_CP_FLIGHTS
  with parameters
    p_carrid : abap.char(3)
  as select from sflight
{
  key carrid    as Carrid,
  key connid    as Connid,
  key fldate    as Fldate,
      seatsocc  as Seatsocc
}
where carrid = $parameters.p_carrid
