#!/usr/bin/env python3
"""Offline release-time compatibility calculation; never invoked at app startup."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess


def version(value):
    if not re.fullmatch(r"\d+(?:\.\d+)*", value):
        raise ValueError(f"Expected a numeric upstream version, got {value!r}")
    return tuple(int(part) for part in value.split('.')) + (0,) * (4 - len(value.split('.')))


def calculate(distribution, requirements, architecture, required_sonames=(), dzurkel_version=None):
    """Highest per-library lower bound, followed by a joint compatibility check.

    Release ordering belongs to the distribution's reviewed input, not a global
    comparison of unrelated distro version numbers. Unknown evidence fails closed.
    """
    releases = distribution['releases']
    floors = {}
    candidates = []
    checks = []
    for index, release in enumerate(releases):
        reasons = []
        if release['architecture'] != architecture:
            reasons.append('architecture not reviewed')
        if not release.get('sources'):
            reasons.append('package evidence missing')
        if release.get('coinstallable') is not True:
            reasons.append('coinstallable package set not established')
        for library, minimum in requirements.items():
            available = release['libraries'].get(library)
            if available is None or version(available) < version(minimum):
                reasons.append(f'{library} needs {minimum}; reviewed version: {available or "unknown"}')
            elif release['architecture'] == architecture and release.get('sources'):
                floors.setdefault(library, index)
        for soname in required_sonames:
            if soname not in release.get('sonames', []):
                reasons.append(f'required library ABI {soname} not reviewed')
        if not reasons:
            candidates.append(index)
        checks.append({'release': release['version'], 'candidate': not reasons, 'reasons': reasons})
    lower_bound = max(floors.values()) if len(floors) == len(requirements) else None
    first_joint = next((index for index in candidates if lower_bound is not None and index >= lower_bound), None)
    required_checks = {'package_install', 'text', 'image', 'pdf', 'csv', 'xlsx', 'docx', 'epub', 'desktop'}
    validated = []
    for index in candidates:
        for evidence in releases[index].get('validation', []):
            if (dzurkel_version is not None
                    and evidence.get('dzurkel_version') == dzurkel_version
                    and evidence.get('architecture') == architecture
                    and evidence.get('requirements') == requirements
                    and evidence.get('profile') == 'baseline-native'
                    and required_checks <= set(evidence.get('passed_checks', []))
                    and evidence.get('evidence_url', '').startswith('https://')):
                validated.append(index)
                break
    return {
        'distribution': distribution['id'],
        'repositories': distribution['repositories'],
        'coverage': distribution['coverage'],
        'per_library_first_reviewed_release': {name: releases[index]['version'] for name, index in floors.items()},
        'highest_lower_bound': releases[lower_bound]['version'] if lower_bound is not None else None,
        'earliest_reviewed_candidate': releases[first_joint]['version'] if first_joint is not None else None,
        # Only a separately recorded package/desktop validation can establish
        # support. Version arithmetic never promotes a candidate to supported.
        'supported_minimum': releases[min(validated)]['version'] if validated else None,
        'validation': 'Package installation, transitive ABI dependencies and desktop preview checks are required.',
        'releases': checks,
    }


def inspect_elf(binary, readelf):
    dynamic = subprocess.check_output([readelf, '--wide', '--dynamic', str(binary)], text=True)
    headers = subprocess.check_output([readelf, '--wide', '--file-header', str(binary)], text=True)
    symbols = subprocess.check_output([readelf, '--wide', '--version-info', str(binary)], text=True)
    needed = sorted(set(re.findall(r'\(NEEDED\).*?\[([^\]]+)\]', dynamic)))
    abi = {}
    # Inspect requirements only; ignore any definitions exported by a module.
    needs = symbols.split('Version needs section', 1)[-1] if 'Version needs section' in symbols else ''
    for family in ('GLIBC', 'GLIBCXX', 'CXXABI'):
        values = re.findall(r'\b' + family + r'_(\d+(?:\.\d+)+)\b', needs)
        if values:
            abi[family] = max(values, key=version)
    machine = re.search(r'^\s*Machine:\s*(.+)$', headers, re.M)
    machine = machine.group(1).strip() if machine else 'unknown'
    architecture = {'Advanced Micro Devices X86-64': 'x86_64', 'AArch64': 'aarch64'}.get(machine, machine)
    return {'architecture': architecture, 'needed_sonames': needed, 'direct_elf_abi_floor': abi}


def main():
    parser = argparse.ArgumentParser(__doc__)
    parser.add_argument('--catalog', type=Path, required=True)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--readelf', required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--release', required=True)
    parser.add_argument('--baseline-qt', required=True)
    parser.add_argument('--baseline-zip', required=True)
    parser.add_argument('--built-qt', required=True)
    parser.add_argument('--built-pdf', required=True)
    parser.add_argument('--adapter', type=Path, action='append', default=[])
    args = parser.parse_args()
    catalog = json.loads(args.catalog.read_text())
    if catalog['schema_version'] != 1:
        raise ValueError('Unsupported compatibility catalog schema')
    artifact = inspect_elf(args.binary, args.readelf)
    optional = ('Qt6Multimedia', 'KF6SyntaxHighlighting', 'KF6WindowSystem', 'KF6KIO')
    leaked = [name for name in artifact['needed_sonames'] if any(library in name for library in optional)]
    if leaked:
        raise ValueError(f'Optional dependencies leaked into the baseline executable: {leaked}')
    requirements = {'qt_base': args.baseline_qt, 'qt_pdf': args.baseline_qt, 'libzip': args.baseline_zip}
    binary_requirements = {**requirements, 'qt_base': args.built_qt, 'qt_pdf': args.built_pdf}
    zip_sonames = [name for name in artifact['needed_sonames'] if name.startswith('libzip.so.')]
    artifact['build_sha256'] = hashlib.sha256(args.binary.read_bytes()).hexdigest()
    artifact['runtime_version_policy'] = 'Conservative Qt build versions; libzip SONAME must match. Native package dependency solvers remain authoritative.'
    result = {
        'schema_version': 1,
        'dzurkel_version': args.release,
        'reviewed_at': catalog['reviewed_at'],
        'baseline_requirements': requirements,
        'artifact_requirements': binary_requirements,
        'artifact': artifact,
        'baseline_os_candidates': [calculate(d, requirements, artifact['architecture'], dzurkel_version=args.release) for d in catalog['distributions']],
        'artifact_os_candidates': [calculate(d, binary_requirements, artifact['architecture'], zip_sonames, args.release) for d in catalog['distributions']],
        'optional_adapters': {path.name: inspect_elf(path, args.readelf) for path in args.adapter},
        'limitations': [
            'Candidates apply only to the reviewed native repositories and architecture, not all older/newer releases or bundles.',
            'build_sha256 identifies the pre-packaging executable; installed/stripped package checksums are separate.',
            'Direct ELF ABI floors exclude transitive runtime and platform-plugin dependencies; native package installation must resolve those.',
            'No OS support is asserted without package and desktop validation. The build host is not an inferred minimum OS.',
            'This report is generated offline during the build; user machines only check actual capabilities.'
        ],
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2, sort_keys=True) + '\n')
    print('Release compatibility report:', args.output.name, '(dependency candidates; no automatic support claims)')


if __name__ == '__main__':
    main()
