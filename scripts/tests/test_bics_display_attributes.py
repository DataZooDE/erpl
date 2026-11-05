"""Offline tests of the diagnostic flow and oracle, not the BICS implementation."""
from contextlib import redirect_stdout, redirect_stderr
import importlib.util
import io
import json
from types import SimpleNamespace
from unittest.mock import MagicMock, patch
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location('check', Path(__file__).parents[1] / 'check_bics_display_attributes.py')
check = importlib.util.module_from_spec(spec)
spec.loader.exec_module(check)


class DisplayAttributeCheckTest(unittest.TestCase):
    def verify(self, names, records):
        check.check_result([{'column_name': name} for name in names], records,
                           'ZMATERIAL', 'M1 Material1', 'ZMATERIAL_ZMAT_GRP', 'GRP1')

    def test_reported_missing_column_fails(self):
        with self.assertRaisesRegex(ValueError, 'Missing result column'):
            self.verify(['ZMATERIAL', 'ZMATERIAL_HIER_LEVEL'], [{'ZMATERIAL': 'M1 Material1'}])

    def test_correct_attribute_passes_without_key_figures(self):
        self.verify(['ZMATERIAL', 'ZMATERIAL_ZMAT_GRP'],
                    [{'ZMATERIAL': 'M1 Material1', 'ZMATERIAL_ZMAT_GRP': 'GRP1'}])

    def test_empty_result_does_not_pass(self):
        with self.assertRaisesRegex(ValueError, 'absent'):
            self.verify(['ZMATERIAL', 'ZMATERIAL_ZMAT_GRP'], [])

    def test_null_wrong_or_inconsistent_attribute_fails(self):
        for value in (None, 'T01', ''):
            with self.subTest(value=value), self.assertRaisesRegex(ValueError, 'does not equal'):
                self.verify(['ZMATERIAL', 'ZMATERIAL_ZMAT_GRP'], [
                    {'ZMATERIAL': 'M1 Material1', 'ZMATERIAL_ZMAT_GRP': 'GRP1'},
                    {'ZMATERIAL': 'M1 Material1', 'ZMATERIAL_ZMAT_GRP': value}])

    def test_wrong_member_does_not_pass(self):
        with self.assertRaisesRegex(ValueError, 'absent'):
            self.verify(['ZMATERIAL', 'ZMATERIAL_ZMAT_GRP'],
                        [{'ZMATERIAL': 'M2', 'ZMATERIAL_ZMAT_GRP': 'GRP1'}])

    def test_literal_quotes_are_escaped(self):
        self.assertEqual(check.literal("Q'1"), "'Q''1'")


class DiagnosticFlowTest(unittest.TestCase):
    def setUp(self):
        self.args = SimpleNamespace(cube="CUBE", query="QUERY", member_column="ZMATERIAL",
                                    member="M1 Material1", attribute_column="ZMATERIAL_ZMAT_GRP",
                                    expected="GRP1")
        self.responses = [
            (['version'], [('v1.5.6',)]),
            (['extension_name', 'extension_version'], [('erpl_bics', 'test')]),
            (['technical_name'], [('QUERY',)]),
            (['state_id'], [("state'1",)]),
            (['n_rows'], [(2,)]),
            (['column_name'], [('ZMATERIAL',), ('ZMATERIAL_ZMAT_GRP',)]),
            (['ZMATERIAL', 'ZMATERIAL_ZMAT_GRP'], [('M1 Material1', 'GRP1'), ('M2', 'OTHER')]),
        ]
        self.output = io.StringIO()

    def connection(self):
        responses = []
        for response in self.responses:
            if isinstance(response, Exception):
                responses.append(response)
            else:
                names, records = response
                cursor = MagicMock()
                cursor.description = [(name,) for name in names]
                cursor.fetchall.return_value = records
                responses.append(cursor)
        connection = MagicMock()
        connection.execute.side_effect = responses
        connection.__enter__.return_value = connection
        return connection

    def test_success_emits_json_and_only_fixture_rows(self):
        connection = self.connection()
        with redirect_stdout(self.output):
            check.run(connection, self.args)
        report = json.loads(self.output.getvalue())
        self.assertEqual(report['row_count'], 2)
        self.assertEqual(report['fixture_rows'], [
            {'ZMATERIAL': 'M1 Material1', 'ZMATERIAL_ZMAT_GRP': 'GRP1'}])
        self.assertNotIn('failed_stage', report)
        self.assertEqual(report['result_stats'], [{'n_rows': 2}])
        self.assertEqual(connection.execute.call_args.args[0],
                         "SELECT * FROM sap_bics_result('state''1')")

    def test_report_file_keeps_stdout_free(self):
        import tempfile
        with tempfile.TemporaryDirectory() as tmp:
            self.args.report = Path(tmp) / 'report.json'
            with redirect_stdout(self.output):
                check.run(self.connection(), self.args)
            self.assertEqual(self.output.getvalue(), '')
            self.assertEqual(json.loads(self.args.report.read_text())['row_count'], 2)

    def test_oracle_rejection_still_emits_report(self):
        self.responses[-1] = (['ZMATERIAL', 'ZMATERIAL_ZMAT_GRP'], [('M1 Material1', None)])
        with redirect_stdout(self.output), self.assertRaisesRegex(ValueError, 'does not equal'):
            check.run(self.connection(), self.args)
        self.assertIsNone(json.loads(self.output.getvalue())['fixture_rows'][0]['ZMATERIAL_ZMAT_GRP'])

    def test_missing_state_id_preserves_metadata_and_stops(self):
        for names, records in [(['state_id'], []), (['state_id'], [(None,)]), (['other'], [('value',)])]:
            with self.subTest(records=records):
                self.responses[3] = (names, records)
                connection = self.connection()
                output = io.StringIO()
                with redirect_stdout(output), self.assertRaisesRegex(ValueError, 'no state_id'):
                    check.run(connection, self.args)
                report = json.loads(output.getvalue())
                self.assertEqual(report['failed_stage'], 'session_metadata')
                self.assertIn('design_metadata', report)
                self.assertIn('session_metadata', report)
                self.assertEqual(connection.execute.call_count, 4)

    def test_execute_failures_preserve_partial_report_and_exception(self):
        stages = ['duckdb_version', 'extensions', 'design_metadata', 'session_metadata',
                  'result_stats', 'schema', 'result']
        originals = self.responses[:]
        for index, stage in enumerate(stages):
            with self.subTest(stage=stage):
                self.responses = originals[:]
                failure = RuntimeError('sensitive SDK detail')
                self.responses[index] = failure
                connection = self.connection()
                output = io.StringIO()
                with redirect_stdout(output), self.assertRaises(RuntimeError) as caught:
                    check.run(connection, self.args)
                self.assertIs(caught.exception, failure)
                report = json.loads(output.getvalue())
                self.assertEqual(report['failed_stage'], stage)
                self.assertNotIn('sensitive SDK detail', output.getvalue())
                for completed in stages[:index]:
                    self.assertIn(completed, report)
                self.assertEqual(connection.execute.call_count, index + 1)

    def test_fetch_failure_preserves_stats(self):
        connection = self.connection()
        cursors = list(connection.execute.side_effect)
        failure = RuntimeError('fetch failed')
        cursors[-1].fetchall.side_effect = failure
        connection.execute.side_effect = cursors
        with redirect_stdout(self.output), self.assertRaises(RuntimeError) as caught:
            check.run(connection, self.args)
        self.assertIs(caught.exception, failure)
        report = json.loads(self.output.getvalue())
        self.assertEqual(report['failed_stage'], 'result')
        self.assertIn('result_stats', report)
        self.assertIn('schema', report)

    def test_main_only_announces_success_after_validation(self):
        for value in ('GRP1', None):
            with self.subTest(value=value):
                self.responses[-1] = (['ZMATERIAL', 'ZMATERIAL_ZMAT_GRP'], [('M1 Material1', value)])
                connection = self.connection()
                # Initialization SQL executes before the diagnostic selects.
                connection.execute.side_effect = [None, *connection.execute.side_effect]
                duckdb = SimpleNamespace(connect=lambda **kwargs: connection)
                output, errors = io.StringIO(), io.StringIO()
                with patch.dict('sys.modules', {'duckdb': duckdb}), \
                     patch('sys.argv', ['check', '--init-sql', '/local/init.sql', '--cube', 'CUBE',
                                         '--member-column', 'ZMATERIAL', '--member', 'M1 Material1',
                                         '--attribute-column', 'ZMATERIAL_ZMAT_GRP', '--expected', 'GRP1']), \
                     patch.object(Path, 'read_text', return_value='SELECT 1;'), \
                     redirect_stdout(output), redirect_stderr(errors):
                    if value is None:
                        with self.assertRaisesRegex(ValueError, 'does not equal'):
                            check.main()
                        self.assertNotIn('PASS:', errors.getvalue())
                    else:
                        check.main()
                        self.assertIn('PASS:', errors.getvalue())
                self.assertEqual(connection.execute.call_args_list[0].args, ('SELECT 1;',))
                self.assertEqual(json.loads(output.getvalue())['row_count'], 1)
                connection.__exit__.assert_called_once()


class InitFromEnvTest(unittest.TestCase):
    ENV = {'ERPL_SAP_ASHOST': 'localhost', 'ERPL_SAP_SYSNR': '00', 'ERPL_SAP_CLIENT': '001',
           'ERPL_SAP_USER': 'DEVELOPER', 'ERPL_SAP_PASSWORD': "pa'ss#00", 'ERPL_SAP_LANG': 'EN'}

    def test_builds_install_and_escaped_secret(self):
        sql = check.env_init_sql(self.ENV)
        self.assertIn("INSTALL erpl FROM 'http://get.erpl.io'; LOAD erpl;", sql)
        self.assertIn("PASSWD 'pa''ss#00'", sql)
        self.assertIn("ASHOST 'localhost'", sql)

    def test_missing_variable_is_named(self):
        env = dict(self.ENV)
        del env['ERPL_SAP_PASSWORD']
        with self.assertRaisesRegex(ValueError, 'ERPL_SAP_PASSWORD'):
            check.env_init_sql(env)


if __name__ == '__main__':
    unittest.main()
