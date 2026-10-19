#!/usr/bin/env python3
import importlib.util
from pathlib import Path
import unittest

path = Path(__file__).resolve().parents[2] / 'packaging/compatibility/report.py'
spec = importlib.util.spec_from_file_location('compatibility_report', path)
report = importlib.util.module_from_spec(spec)
spec.loader.exec_module(report)


def release(name, qt, pdf, zip_version, **extra):
    return {'version': name, 'architecture': 'x86_64', 'sources': ['https://example.invalid/fixture'],
            'libraries': {'qt_base': qt, 'qt_pdf': pdf, 'libzip': zip_version},
            'coinstallable': True, 'sonames': ['libzip.so.5'], **extra}


def distribution(*releases):
    return {'id': 'fixture', 'repositories': ['main'], 'coverage': 'fixture only', 'releases': list(releases)}


class CompatibilityReportTest(unittest.TestCase):
    requirements = {'qt_base': '6.4', 'qt_pdf': '6.4', 'libzip': '1.7'}

    def test_highest_lower_bound_selects_joint_release(self):
        result = report.calculate(distribution(
            release('10', '6.4', '6.3', '1.6'),
            release('11', '6.4', '6.4', '1.6'),
            release('12', '6.4', '6.4', '1.7')), self.requirements, 'x86_64')
        self.assertEqual(result['per_library_first_reviewed_release'], {'qt_base': '10', 'qt_pdf': '11', 'libzip': '12'})
        self.assertEqual(result['highest_lower_bound'], '12')
        self.assertEqual(result['earliest_reviewed_candidate'], '12')
        self.assertIsNone(result['supported_minimum'])

    def test_individual_minima_do_not_prove_a_joint_package_set(self):
        result = report.calculate(distribution(
            release('old', '6.4', '6.3', '1.7'),
            release('new', '6.3', '6.4', '1.7')), self.requirements, 'x86_64')
        self.assertEqual(result['highest_lower_bound'], 'new')
        self.assertIsNone(result['earliest_reviewed_candidate'])

    def test_unknown_or_unreviewed_data_cannot_become_support(self):
        for extra in ({'coinstallable': None}, {'sources': []}, {'architecture': 'aarch64'}):
            result = report.calculate(distribution(release('12', '6.8', '6.8', '1.11', **extra)),
                                      self.requirements, 'x86_64')
            self.assertIsNone(result['earliest_reviewed_candidate'])
            self.assertIsNone(result['supported_minimum'])

    def test_binary_soname_excludes_an_older_native_package_abi(self):
        candidate = release('12', '6.4', '6.4', '1.7', sonames=['libzip.so.4'])
        source = report.calculate(distribution(candidate), self.requirements, 'x86_64')
        binary = report.calculate(distribution(candidate), self.requirements, 'x86_64', ['libzip.so.5'])
        self.assertEqual(source['earliest_reviewed_candidate'], '12')
        self.assertIsNone(binary['earliest_reviewed_candidate'])

    def test_rolling_versions_follow_reviewed_order_and_unknown_distros_stay_unknown(self):
        result = report.calculate(distribution(release('rolling@2026-10-19', '6.11', '6.11', '1.11')),
                                  self.requirements, 'x86_64')
        self.assertEqual(result['earliest_reviewed_candidate'], 'rolling@2026-10-19')
        self.assertIsNone(report.calculate(distribution(), self.requirements, 'x86_64')['earliest_reviewed_candidate'])

    def test_only_matching_complete_release_validation_promotes_support(self):
        evidence = {'dzurkel_version': '0.7.0', 'architecture': 'x86_64', 'requirements': self.requirements,
                    'profile': 'baseline-native', 'passed_checks': ['package_install', 'text', 'image', 'pdf', 'csv', 'xlsx', 'docx', 'epub', 'desktop'],
                    'evidence_url': 'https://example.invalid/fixture-run'}
        candidate = release('12', '6.4', '6.4', '1.7', validation=[evidence])
        result = report.calculate(distribution(candidate), self.requirements, 'x86_64', dzurkel_version='0.7.0')
        self.assertEqual(result['supported_minimum'], '12')
        self.assertIsNone(report.calculate(distribution(candidate), self.requirements, 'x86_64', dzurkel_version='0.7.1')['supported_minimum'])
        evidence['passed_checks'] = ['package_install', 'text']
        self.assertIsNone(report.calculate(distribution(candidate), self.requirements, 'x86_64', dzurkel_version='0.7.0')['supported_minimum'])

    def test_numeric_versions_and_invalid_input(self):
        self.assertGreater(report.version('6.10'), report.version('6.9.9'))
        self.assertEqual(report.version('6.4'), report.version('6.4.0'))
        for invalid in ('', 'unknown', '6.4~rc1', '>=6.4'):
            with self.assertRaises(ValueError): report.version(invalid)


if __name__ == '__main__':
    unittest.main()
