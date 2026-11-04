"""Offline tests of the regression oracle; these do not simulate the BICS fix."""
import importlib.util
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


if __name__ == '__main__':
    unittest.main()
