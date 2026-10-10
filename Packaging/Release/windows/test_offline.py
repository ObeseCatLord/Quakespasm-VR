#!/usr/bin/env python3
"""Offline regression checks against a qualified cohort; never contacts WinBoat."""
import argparse
import contextlib
import copy
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import shutil
import tarfile
import tempfile
import unittest
import xml.etree.ElementTree as ET

spec = importlib.util.spec_from_file_location('windows_build', Path(__file__).with_name('build.py'))
adapter = importlib.util.module_from_spec(spec)
spec.loader.exec_module(adapter)


class OfflineChecks(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.entry, cls.manifest, cls.archive = adapter.source_inputs(COHORT)
        cls.baseline = COHORT / 'windows/release'
        cls.inputs = adapter.baseline_inputs(cls.baseline)
        adapter.check_dependencies(cls.manifest, cls.baseline, cls.inputs)
        adapter.derive_project_plan(cls.archive, cls.manifest, cls.baseline, cls.inputs)

    def test_complete_current_cohort_rehashes_and_skips_without_transport(self):
        before = adapter.sha(self.baseline / 'windows-artifact-manifest.json')
        with tempfile.TemporaryDirectory(prefix='windows config spaces ') as folder:
            config = Path(folder) / 'config with spaces.json'
            adapter.save(config, {'windows': {'baseline_release': str(self.baseline),
                                             'winboat_tools': '/missing-transport-must-not-run'}})
            with contextlib.redirect_stdout(io.StringIO()) as output:
                self.assertEqual(adapter.main(['--root', str(COHORT), '--config', str(config)]), 0)
            self.assertEqual(json.loads(output.getvalue())['action'], 'verified-existing')
            with contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(adapter.main(['--root', str(COHORT), '--config', str(config),
                                               '--verify-only']), 0)
        self.assertEqual(before, adapter.sha(self.baseline / 'windows-artifact-manifest.json'))

    def test_wrong_identity_rejected(self):
        entry = {**self.entry, 'production_revision': 'f' * 40}
        with self.assertRaisesRegex(ValueError, 'identity'):
            adapter.verify_release(self.baseline, entry, self.manifest, self.inputs)

    def test_import_manifest_accepts_order_independent_path_hash_schema(self):
        manifest = copy.deepcopy(self.manifest)
        manifest['files'] = [{'path': f['path'], 'sha256': f['sha256']}
                             for f in reversed(manifest['files'])]
        adapter.verify_release(self.baseline, self.entry, manifest, self.inputs)

    def test_pinned_dependency_mutation_rejected(self):
        manifest = copy.deepcopy(self.manifest)
        pinned = self.inputs['dependency-input-manifest.json']['files'][0]['path']
        next(f for f in manifest['files'] if f['path'] == pinned)['sha256'] = '0' * 64
        with self.assertRaisesRegex(ValueError, 'Pinned dependency input changed'):
            adapter.check_dependencies(manifest, self.baseline, self.inputs)

    def test_partial_namespace_rejected_and_retained(self):
        with tempfile.TemporaryDirectory(prefix='windows partial ') as folder:
            root = Path(folder)
            (root / 'archives').mkdir()
            shutil.copyfile(self.archive, root / 'archives/product.tar')
            adapter.save(root / 'entry.json', self.entry)
            adapter.save(root / 'production-source-manifest.json', self.manifest)
            (root / 'windows').mkdir()
            config = root / 'config.json'
            adapter.save(config, {'windows': {'baseline_release': str(self.baseline)}})
            with self.assertRaisesRegex(ValueError, 'invalid/partial.*Retry explicitly'):
                adapter.main(['--root', str(root), '--config', str(config)])
            self.assertTrue((root / 'windows').exists())

    def test_artifact_and_receipt_mutations_rejected(self):
        with tempfile.TemporaryDirectory(prefix='windows rehash ') as folder:
            release = Path(folder) / 'release'
            release.mkdir()
            shutil.copytree(self.baseline / 'receipts', release / 'receipts')
            shutil.copytree(self.baseline / 'Release', release / 'Release')
            shutil.copyfile(self.baseline / 'windows-artifact-manifest.json',
                            release / 'windows-artifact-manifest.json')
            artifact = release / 'Release/LICENSE.txt'
            original = artifact.read_bytes()
            artifact.write_bytes(original + b'changed')
            with self.assertRaisesRegex(ValueError, 'artifact hash mismatch'):
                adapter.verify_release(release, self.entry, self.manifest, self.inputs)
            artifact.write_bytes(original)
            receipt = release / 'receipts/independent-guest-verification.json'
            receipt.write_bytes(receipt.read_bytes() + b' ')
            with self.assertRaisesRegex(ValueError, 'Receipt transfer mismatch'):
                adapter.verify_release(release, self.entry, self.manifest, self.inputs)

    def changed_archive(self, destination, policy_change=False):
        """Add a C input and shader through the real reference XML, without building."""
        changes = {'Quake/future_adapter_test.c': b'int adapter_future;\n',
                   'Shaders/future_adapter_test.frag': b'// offline inventory fixture\n'}
        with tarfile.open(self.archive) as original:
            engine_name = 'Windows/VisualStudio/vkquake.vcxproj'
            engine = ET.fromstring(original.extractfile(engine_name).read())
            if policy_change:
                group = next(g for g in engine.findall(adapter.NS + 'PropertyGroup') if
                             g.get('Condition') and adapter.release_condition(g.get('Condition')))
                ET.SubElement(group, adapter.NS + 'LinkIncremental').text = 'true'
            else:
                group = ET.SubElement(engine, adapter.NS + 'ItemGroup')
                ET.SubElement(group, adapter.NS + 'ClCompile', Include='..\\..\\Quake\\future_adapter_test.c')
                item = ET.SubElement(group, adapter.NS + 'ClCompile',
                                     Include='..\\..\\Shaders\\Compiled\\Release\\future_adapter_test.frag.c')
                ET.SubElement(item, adapter.NS + 'ExcludedFromBuild',
                              Condition="'$(Configuration)|$(Platform)'=='Debug|x64'").text = 'true'
                embedded_name = 'Windows/VisualStudio/embedded.vcxproj'
                embedded = ET.fromstring(original.extractfile(embedded_name).read())
                ET.SubElement(ET.SubElement(embedded, adapter.NS + 'ItemGroup'),
                              adapter.NS + 'CustomBuild', Include='..\\..\\Shaders\\future_adapter_test.frag')
                changes[embedded_name] = ET.tostring(embedded)
            changes[engine_name] = ET.tostring(engine)
            manifest = copy.deepcopy(self.manifest)
            rows = {f['path']: f for f in manifest['files']}
            with tarfile.open(destination, 'w') as target:
                for member in original:
                    if member.name in changes:
                        continue
                    stream = original.extractfile(member) if member.isfile() else None
                    target.addfile(member, stream)
                for name, content in changes.items():
                    member = tarfile.TarInfo(name)
                    member.size = len(content)
                    target.addfile(member, io.BytesIO(content))
                    rows[name] = {'path': name, 'bytes': len(content),
                                  'sha256': hashlib.sha256(content).hexdigest()}
            manifest['files'] = list(rows.values())
            manifest['regular_file_count'] = len(rows)
            manifest['archive_sha256'] = adapter.sha(destination)
            return manifest

    def test_new_c_and_shader_counts_derived_from_current_project(self):
        with tempfile.TemporaryDirectory(prefix='windows evolve ') as folder:
            archive = Path(folder) / 'product.tar'
            manifest = self.changed_archive(archive)
            inputs = adapter.baseline_inputs(self.baseline)
            adapter.derive_project_plan(archive, manifest, self.baseline, inputs)
            self.assertEqual(inputs['dependency-reference.json']['expected_native_object_count'],
                             self.inputs['dependency-reference.json']['expected_native_object_count'] + 2)
            self.assertEqual(inputs['shader-build-plan.json']['compiled_shader_c_input_count'],
                             self.inputs['shader-build-plan.json']['compiled_shader_c_input_count'] + 1)
            self.assertNotIn('Windows/VisualStudio/Build-vkQuake/x64/Release/pl_linux.obj',
                             inputs['dependency-reference.json']['expected_native_objects'])

    def test_link_policy_change_requires_explicit_compatibility_review(self):
        with tempfile.TemporaryDirectory(prefix='windows policy ') as folder:
            archive = Path(folder) / 'product.tar'
            manifest = self.changed_archive(archive, policy_change=True)
            with self.assertRaisesRegex(ValueError, 'Build/link/import policy changed'):
                adapter.derive_project_plan(archive, manifest, self.baseline,
                                            adapter.baseline_inputs(self.baseline))

    def test_powershell_literal_and_manifest_paths(self):
        self.assertEqual(adapter.ps_string("path with space' ; command"), "'path with space'' ; command'")
        for path in ('../outside', 'Windows/A:stream', '/absolute', 'Windows/file.', 'a\\b'):
            with self.assertRaises(ValueError):
                adapter.safe_path(path)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cohort', type=Path, required=True)
    args, remaining = parser.parse_known_args()
    COHORT = args.cohort.resolve()
    unittest.main(argv=['test_offline.py', *remaining])
