@AbapCatalog.viewEnhancementCategory: [#NONE]
@AccessControl.authorizationCheck: #NOT_REQUIRED
@EndUserText.label: 'ERPL test CDS over SFLIGHT'
define view entity ZERPL_C_FLIGHTS
  as select from sflight
{
  key carrid    as Carrid,
  key connid    as Connid,
  key fldate    as Fldate,
      price     as Price,
      currency  as Currency,
      seatsmax  as Seatsmax,
      seatsocc  as Seatsocc
}
