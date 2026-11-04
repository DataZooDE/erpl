#!/usr/bin/env python3
"""Opt-in live regression check for DataZooDE/erpl#179 (see companion README)."""

import argparse
import json
from pathlib import Path
import sys


def literal(value):
    return "'" + value.replace("'", "''") + "'"


def rows(connection, sql):
    result = connection.execute(sql)
    names = [column[0] for column in result.description]
    return [dict(zip(names, row)) for row in result.fetchall()]


def check_result(schema, records, member_column, member, attribute_column, expected):
    """Check the bound schema and a known member, not merely a nonempty result."""
    names = [column['column_name'] for column in schema]
    for name in (member_column, attribute_column):
        if name not in names:
            raise ValueError(f"Missing result column {name!r}; got {names!r}")
    matching = [row for row in records if row[member_column] == member]
    if not matching:
        raise ValueError(f"Expected member {member!r} is absent; check fixture and display mode")
    if any(row[attribute_column] != expected for row in matching):
        raise ValueError(f"Attribute {attribute_column!r} does not equal {expected!r} for {member!r}")


def run(connection, args):
    report = {}
    # Capture each stage before checking the result so a missing column does not
    # prevent collecting the statistics needed to locate the loss in BICS.
    report['duckdb_version'] = rows(connection, 'SELECT version() AS version')
    report['extensions'] = rows(connection, "SELECT extension_name, extension_version FROM duckdb_extensions() WHERE loaded")
    report['design_metadata'] = rows(connection, f'SELECT * FROM sap_bics_describe({literal(args.cube)}, {literal(args.query)})')
    state_rows = rows(connection, f'SELECT * FROM sap_bics_begin({literal(args.query)})')
    if not state_rows or not state_rows[0].get('state_id'):
        raise ValueError('sap_bics_begin returned no state_id')
    state = literal(state_rows[0]['state_id'])
    report['session_metadata'] = state_rows
    report['result_stats'] = rows(connection, f'SELECT * FROM sap_bics_result_stats({state})')
    report['schema'] = rows(connection, f'DESCRIBE SELECT * FROM sap_bics_result({state})')
    records = rows(connection, f'SELECT * FROM sap_bics_result({state})')
    report['row_count'] = len(records)
    # Limit persisted data to the fixture member. Use only a small test query.
    report['fixture_rows'] = [row for row in records if row.get(args.member_column) == args.member]
    print(json.dumps(report, indent=2, default=str), flush=True)
    check_result(report['schema'], records, args.member_column, args.member, args.attribute_column, args.expected)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--init-sql', type=Path, required=True, help='Local SQL to LOAD extensions and create the SAP secret')
    parser.add_argument('--cube', required=True)
    parser.add_argument('--query', default='ZMAT_Q002')
    parser.add_argument('--member-column', default='ZMATERIAL')
    parser.add_argument('--member', default='M1 Material1', help='Exact formatted member returned by BICS')
    parser.add_argument('--attribute-column', default='ZMATERIAL_ZMAT_GRP', help='Agreed output name; default is a proposal, not an existing API')
    parser.add_argument('--expected', default='GRP1', help='Exact formatted attribute value')
    args = parser.parse_args()
    import duckdb

    with duckdb.connect() as connection:
        connection.execute(args.init_sql.read_text())
        run(connection, args)
    print('PASS: display attribute schema and fixture value match', file=sys.stderr)


if __name__ == '__main__':
    main()
