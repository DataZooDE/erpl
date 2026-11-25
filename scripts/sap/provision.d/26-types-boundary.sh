# Boundary values for every DDIC type erpl maps (issue #255 and its audit).
#
# ZERPL_TYPES_BV is a transparent table with one column per type -- packed fields at
# every length/decimals combination up to DEC(31,14), CURR/QUAN, INT1..INT8, FLTP,
# DECFLOAT16/34, DATS, TIMS, RAW, NUMC, CHAR, ACCP, PREC, LANG, UTCLONG -- and rows at
# the largest, most negative, initial and smallest values, a currency without decimals
# and full-width non-BMP text. ZCL_ERPL_TYPES_BV fills it and creates Z_ERPL_TYPES_BV
# (in function group ZERPL_PYRFC), which returns the same rows, so the invoke path and
# the bindings see the values sap_read_table sees.
#
# Read by rfc/test/sql/sap_read_table_packed_even_length.test, and by erpl-proto's
# oracle/nwrfc_types_bv.cpp as a differential between SAP's library and the shim.
#
# Depends on ZERPL_PYRFC existing (erpl-proto's pyrfc fixtures); without it the table
# and rows are still created and only the function module is skipped.

_AB="$HERE/assets/types"

if [ "$PROVISION_MODE" = check ]; then
    say "would deploy ZERPL_TYPES_BV + ZCL_ERPL_TYPES_BV and run it (rows, Z_ERPL_TYPES_BV)"
    return 0
fi

rc=0
tabl ZERPL_TYPES_BV "$_AB/zerpl_types_bv.ddl" "erpl boundary values (issue 255)" || rc=1
cls ZCL_ERPL_TYPES_BV "$_AB/zcl_erpl_types_bv.abap" "fill ZERPL_TYPES_BV, create Z_ERPL_TYPES_BV" || rc=1
retry_failed || rc=1
if out="$(adt object run ZCL_ERPL_TYPES_BV 2>&1)"; then
    printf '%s\n' "$out" | sed 's/^/  /'
    printf '%s\n' "$out" | grep -q '7 of 7 rows stored' && ok "ZERPL_TYPES_BV rows" || { fail "ZERPL_TYPES_BV rows"; rc=1; }
else
    fail "ZCL_ERPL_TYPES_BV run"; rc=1
fi
return $rc
