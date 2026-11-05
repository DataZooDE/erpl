#!/usr/bin/env python3
"""Opt-in live regression check for DataZooDE/erpl#179 (see companion README)."""

import argparse
import json
import os
from pathlib import Path
import sys

# The a4h trial fixture ZERPL_Q179_ATTR (scripts/sap/provision.d/41-bics-q179-query.sh):
# 0D_NW_PROD on rows with display attribute 0D_NW_PRDGP active in the query definition.
A4H_DEFAULTS = dict(cube='0D_NW_C01', query='ZERPL_Q179_ATTR', member_column='0D_NW_PROD',
                    member='CN0F17', attribute_column='0D_NW_PROD__0D_NW_PRDGP', expected='MON1')
SECRET_ENV = (('ASHOST', 'ERPL_SAP_ASHOST'), ('SYSNR', 'ERPL_SAP_SYSNR'), ('CLIENT', 'ERPL_SAP_CLIENT'),
              ('USER', 'ERPL_SAP_USER'), ('PASSWD', 'ERPL_SAP_PASSWORD'), ('LANG', 'ERPL_SAP_LANG'))


def literal(value):
    return "'" + value.replace("'", "''") + "'"


def env_init_sql(environ, repository='http://get.erpl.io'):
    """INSTALL/LOAD the published erpl and a default sap_rfc secret from the ERPL_SAP_* variables
    the SQL test suites use, so the check runs against the released binaries."""
    missing = [name for _, name in SECRET_ENV if not environ.get(name)]
    if missing:
        raise ValueError(f"--init-from-env needs {', '.join(missing)}")
    options = ', '.join(f'{key} {literal(environ[name])}' for key, name in SECRET_ENV)
    return (f'INSTALL erpl FROM {literal(repository)}; LOAD erpl;\n'
            f'CREATE SECRET erpl_check (TYPE sap_rfc, {options});')


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
    stage = 'duckdb_version'
    try:
        report[stage] = rows(connection, 'SELECT version() AS version')
        stage = 'extensions'
        report[stage] = rows(connection, "SELECT extension_name, extension_version FROM duckdb_extensions() WHERE loaded")
        stage = 'design_metadata'
        report[stage] = rows(connection, f'SELECT * FROM sap_bics_describe({literal(args.cube)}, {literal(args.query)})')
        stage = 'session_metadata'
        state_rows = rows(connection, f'SELECT * FROM sap_bics_begin({literal(args.query)})')
        report[stage] = state_rows
        if not state_rows or not state_rows[0].get('state_id'):
            raise ValueError('sap_bics_begin returned no state_id')
        state = literal(state_rows[0]['state_id'])
        stage = 'result_stats'
        report[stage] = rows(connection, f'SELECT * FROM sap_bics_result_stats({state})')
        stage = 'schema'
        report[stage] = rows(connection, f'DESCRIBE SELECT * FROM sap_bics_result({state})')
        stage = 'result'
        records = rows(connection, f'SELECT * FROM sap_bics_result({state})')
        report['row_count'] = len(records)
        # Limit persisted data to the fixture member. Use only a small test query.
        report['fixture_rows'] = [row for row in records if row.get(args.member_column) == args.member]
    except Exception:
        # Keep the original exception/traceback; do not copy SDK error text into
        # the report, since it may include connection details or SQL secrets.
        report['failed_stage'] = stage
        raise
    finally:
        # Preserve completed diagnostics even when binding or fetching fails.
        text = json.dumps(report, indent=2, default=str)
        if getattr(args, 'report', None):
            # The erpl trampoline prints its load banner to stdout, ahead of any report there.
            Path(args.report).write_text(text + '\n')
        else:
            print(text, flush=True)
    check_result(report['schema'], records, args.member_column, args.member, args.attribute_column, args.expected)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    init = parser.add_mutually_exclusive_group(required=True)
    init.add_argument('--init-sql', type=Path, help='Local SQL to LOAD extensions and create the SAP secret')
    init.add_argument('--init-from-env', action='store_true',
                      help='INSTALL erpl from --repository and create the secret from ERPL_SAP_*')
    parser.add_argument('--repository', default='http://get.erpl.io', help='Extension repository for --init-from-env')
    parser.add_argument('--report', type=Path, help='Write the JSON report to this file instead of stdout')
    parser.add_argument('--cube', default=A4H_DEFAULTS['cube'])
    parser.add_argument('--query', default=A4H_DEFAULTS['query'])
    parser.add_argument('--member-column', default=A4H_DEFAULTS['member_column'])
    parser.add_argument('--member', default=A4H_DEFAULTS['member'], help='Exact formatted member returned by BICS')
    parser.add_argument('--attribute-column', default=A4H_DEFAULTS['attribute_column'],
                        help='Result column of the attribute: <char>__<attr>')
    parser.add_argument('--expected', default=A4H_DEFAULTS['expected'], help='Exact formatted attribute value')
    args = parser.parse_args()
    init_sql = env_init_sql(os.environ, args.repository) if args.init_from_env else args.init_sql.read_text()
    import duckdb

    # erpl is self-distributed and unsigned.
    with duckdb.connect(config={'allow_unsigned_extensions': 'true'}) as connection:
        connection.execute(init_sql)
        run(connection, args)
    print('PASS: display attribute schema and fixture value match', file=sys.stderr)


if __name__ == '__main__':
    main()
