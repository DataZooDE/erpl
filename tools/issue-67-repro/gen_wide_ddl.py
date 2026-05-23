#!/usr/bin/env python3
# /// script
# requires-python = ">=3.10"
# ///
"""Generate a wide CDS DDL similar in shape to SAP BSEG, with deliberate
type variety so the RFC scan path is exercised across every type family.

The trial's ABAP-Cloud CDS parser refused several types when combined at
~400 columns (`int1`, `int2`, `numc(6)`, `numc(10)`, `lang`, `fltp`,
`string`, `sstring`, `raw`, `dec(31,9)`). Activation returned HTTP 400
with `ExceptionResourceAlreadyExists` despite `source check` reporting no
syntax errors. We stick to the type families known to work at scale on
the trial: `char(n)`, `numc(3..4)`, `dec(13,2)`/`(13,3)`, `dats`, `tims`,
`int4`, `int8`. Variety comes from multiple lengths/precisions within
those families and is still well beyond what DD03L's 31 columns offer.
"""

from __future__ import annotations
import sys


FAMILIES: list[tuple[str, int, str]] = [
    # Decimals at multiple precisions
    ("wrbtr",   50, "abap.dec(13,2)"),
    ("menge",   20, "abap.dec(13,3)"),
    ("smabtr",  15, "abap.dec(7,2)"),
    ("lgabtr",   5, "abap.dec(23,4)"),
    # Floating point (distinct storage class from DEC)
    ("ratio",   10, "abap.fltp"),
    # Characters at varied lengths
    ("flag",    25, "abap.char(1)"),
    ("waers",   25, "abap.char(3)"),
    ("ckey",    40, "abap.char(10)"),
    ("ck20",    10, "abap.char(20)"),
    ("sgtxt",   20, "abap.char(40)"),
    # Variable-length / structured strings
    ("note",     5, "abap.string"),
    ("ssht",     5, "abap.sstring(40)"),
    # Binary
    ("blob",     5, "abap.raw(16)"),
    # Numeric character (NUMC) at multiple lengths
    ("monat",   20, "abap.numc(4)"),
    ("n6",      10, "abap.numc(6)"),
    ("n10",     10, "abap.numc(10)"),
    # Language code (1-char with code-page semantics)
    ("lang",     5, "abap.lang"),
    # Dates / times
    ("augdt",   25, "abap.dats"),
    ("uztim",   10, "abap.tims"),
    # Integers at every supported size
    ("byte",    10, "abap.int1"),
    ("short",   10, "abap.int2"),
    ("num4",    40, "abap.int4"),
    ("num8",    20, "abap.int8"),
]


def field(prefix: str, i: int, abap_type: str) -> str:
    name = f"{prefix}{i:04d}".lower()
    return f"  {name:<24} : {abap_type};"


def main() -> int:
    lines: list[str] = []
    lines.append("@EndUserText.label : 'Issue 67 wide-table reproduction (BSEG-shaped)'")
    lines.append("@AbapCatalog.enhancement.category : #NOT_EXTENSIBLE")
    lines.append("@AbapCatalog.tableCategory : #TRANSPARENT")
    lines.append("@AbapCatalog.deliveryClass : #A")
    lines.append("@AbapCatalog.dataMaintenance : #RESTRICTED")
    lines.append("define table zwide_bseg {")
    lines.append("")
    lines.append("  key client     : abap.clnt not null;")
    lines.append("  key bukrs      : abap.char(4) not null;")
    lines.append("  key belnr      : abap.char(10) not null;")
    lines.append("  key gjahr      : abap.numc(4) not null;")
    lines.append("  key buzei      : abap.numc(3) not null;")
    lines.append("")

    # Note: do NOT emit "-- Nx abap.type" header comments between
    # families.  ABAP-Cloud CDS DDL rejects sources with multiple
    # `--` comment lines inside the `define table { … }` body
    # (HTTP 400 ExceptionResourceAlreadyExists, despite source check
    # reporting "no syntax errors").  Blank-line separators are fine.
    total_payload = 0
    for prefix, count, abap_type in FAMILIES:
        for i in range(count):
            lines.append(field(prefix, i, abap_type))
        lines.append("")
        total_payload += count

    lines.append("}")

    total = total_payload + 5
    print(f"-- Generated wide-table DDL: {total} columns total "
          f"({len(FAMILIES)} type families)", file=sys.stderr)
    print("\n".join(lines))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
