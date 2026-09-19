@AbapCatalog.sqlViewName: 'ZERPLAPEFLGT'
@AbapCatalog.compiler.compareFilter: true
@AbapCatalog.preserveKey: true
@AccessControl.authorizationCheck: #NOT_REQUIRED
@EndUserText.label: 'ERPL APE test view over SFLIGHT'
@Analytics.dataExtraction.enabled: true
define view ZERPL_APE_FLIGHT as select from sflight {
  key carrid    as Carrid,
  key connid    as Connid,
  key fldate    as Fldate,
      price     as Price,
      currency  as Currency,
      planetype as Planetype,
      seatsmax  as Seatsmax,
      seatsocc  as Seatsocc
}
