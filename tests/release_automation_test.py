#!/usr/bin/env python3
"""Bounded local release identity/recovery tests; no engine, SSH or publication."""
import io
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tarfile
import tempfile
import unittest
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'Packaging/Release'))
import release
import unix


class ReleaseTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='release-fixture-')
        self.addCleanup(self.temporary.cleanup)
        self.base = Path(self.temporary.name)
        self.repo = self.base / 'source-repo'
        self.repo.mkdir()
        self.command('init', '--quiet', '-b', '2.0')
        self.command('config', 'user.name', 'Release fixture')
        self.command('config', 'user.email', 'fixture@example.invalid')
        (self.repo / 'engine.c').write_text('committed engine\n')
        self.command('add', 'engine.c')
        self.command('commit', '--quiet', '-m', 'fixture')
        self.rev = release.git(self.repo, 'rev-parse', 'HEAD')
        self.root = self.base / 'outputs/cohort'
        release.prepare(self.root, 'HEAD', self.repo)
        self.entry = unix.read(self.root / 'entry.json')
        self.config = {'output_base': str(self.base / 'outputs'),
                       'straight': {'directory': str(self.base / 'straight'),
                                    'runtime_base': str(self.base / 'runtimes')}}

    def command(self, *args):
        subprocess.run(['git', '-C', str(self.repo), *args], check=True, stdout=subprocess.PIPE,
                       stderr=subprocess.PIPE)

    def package(self, name='linux'):
        package = self.root / name / 'package-final'
        (package / 'bin').mkdir(parents=True)
        (package / 'sources').mkdir()
        (package / 'lib').mkdir()
        (package / 'share').mkdir()
        engine = package / 'bin/vkquake'
        engine.write_bytes(b'fixture-runtime')
        engine.chmod(0o755)
        (package / 'lib/provider.so.1').write_bytes(b'provider')
        (package / 'lib/provider.so').symlink_to('provider.so.1')
        shutil.copy2(self.root / 'archives/product.tar', package / 'sources/product.tar')
        (package / 'share/license.txt').write_text('fixture license')
        unix.save(package / 'artifact-manifest.json', {
            'architecture': 'x86_64' if name == 'linux' else 'aarch64',
            'product': {'revision': self.rev, 'archive_sha256': self.entry['archive_sha256'],
                        'archive_path': 'sources/product.tar'}, 'inventory': unix.inventory(package)})
        (self.root / name / 'verify.log').write_text('fixture verifier evidence\n')
        unix.save(self.root / name / 'qualification.json', {
            'revision': self.rev, 'archive_sha256': self.entry['archive_sha256'],
            'manifest_sha256': unix.sha(package / 'artifact-manifest.json'), 'verified': True,
            'receipts': {'verify.log': unix.sha(self.root / name / 'verify.log')}})
        state = unix.read(self.root / 'coordinator.json')
        state['platforms'][name] = release.verify_platform(self.root, name, self.entry)
        unix.save(self.root / 'coordinator.json', state)
        return package

    def straight(self, symlinks=False):
        straight = Path(self.config['straight']['directory'])
        (straight / 'bin').mkdir(parents=True)
        for index, path in enumerate([straight / 'quakespasm-openvr.bin', straight / 'bin/quakespasm-openvr.bin']):
            if symlinks:
                original = self.base / ('old-' + str(index))
                original.write_bytes(b'old-engine-' + str(index).encode())
                original.chmod(0o755)
                path.symlink_to(original)
            else:
                path.write_bytes(b'old-engine-' + str(index).encode())
                path.chmod(0o755)
        (straight / 'QuakeSpasmLauncher.py').write_text('launcher untouched\n')
        (straight / 'quakespasm-openvr').write_text('wrapper untouched\n')
        (straight / 'save.sav').write_bytes(b'game state')
        return straight

    def staged_fixture(self, destination):
        destination.mkdir(exist_ok=True)
        prefix = destination / 'builds' / self.rev
        prefix.mkdir(parents=True)
        shutil.copy2(self.root / 'archives/product.tar', prefix / 'product.tar')
        archives = []
        for name in (*release.RUNTIME_NAMES, 'source-access-linux-x64.tar.gz', 'source-access-linux-arm64.tar.gz'):
            (prefix / name).write_bytes(('qualified immutable bytes ' + name).encode())
            archives.append((prefix / name).relative_to(destination).as_posix())
        (destination / 'manifest.tsv').write_text('fixture updater manifest\n')
        unix.save(destination / 'release.json', {'branch': '2.0', 'revision': self.rev, 'archives': archives,
                  'files': {name: row['file'] for name, row in unix.inventory(destination).items()}})
        return unix.read(destination / 'release.json')

    def test_committed_archive_ignores_dirty_source_and_refuses_wrong_branch_conflict(self):
        (self.repo / 'engine.c').write_text('uncommitted edits')
        release.verify_source(self.root, self.repo, self.rev)
        with self.assertRaisesRegex(RuntimeError, 'Output conflict'):
            release.prepare(self.root, self.rev, self.repo)
        self.command('checkout', '--quiet', '-b', 'other')
        with self.assertRaisesRegex(RuntimeError, 'branch 2.0'):
            release.verify_source(self.root, self.repo)

    def test_revision_mismatch_and_forged_archive_rejected(self):
        (self.repo / 'engine.c').write_text('next revision')
        self.command('add', 'engine.c')
        self.command('commit', '--quiet', '-m', 'next')
        with self.assertRaisesRegex(RuntimeError, 'differs from cohort'):
            release.verify_source(self.root, self.repo, 'HEAD')
        archive = self.root / 'archives/product.tar'
        data = archive.read_bytes().replace(b'committed engine', b'corrupted engine')
        archive.write_bytes(data)
        entry = unix.read(self.root / 'entry.json')
        entry.update(archive_sha256=unix.sha(archive), archive_bytes=len(data))
        unix.save(self.root / 'entry.json', entry)
        with self.assertRaisesRegex(RuntimeError, 'committed git archive'):
            release.verify_source(self.root, self.repo)

    def test_output_containment_rejects_checkout_and_symlink(self):
        with self.assertRaises(RuntimeError):
            release.output_root({'output_base': str(self.repo)}, self.repo / 'release', self.repo)
        with self.assertRaises(RuntimeError):
            release.output_root(self.config, self.base / 'arbitrary', self.repo)
        (self.base / 'outputs/link').symlink_to(self.repo, target_is_directory=True)
        with self.assertRaises(RuntimeError):
            release.output_root(self.config, self.base / 'outputs/link/release', self.repo)

    def test_resume_checks_added_removed_changed_files_links_and_qualification(self):
        package = self.package()
        release.verify(self.root, ['linux'], self.repo, require_record=True)
        for action in ('added', 'modified', 'removed', 'link'):
            with self.subTest(action=action):
                path = package / 'lib/provider.so.1'
                original = path.read_bytes()
                if action == 'added':
                    (package / 'extra').write_bytes(b'not inventoried')
                elif action == 'modified':
                    path.write_bytes(b'changed')
                elif action == 'removed':
                    path.unlink()
                else:
                    (package / 'lib/provider.so').unlink()
                    (package / 'lib/provider.so').symlink_to('../bin/vkquake')
                with self.assertRaises((RuntimeError, FileNotFoundError)):
                    release.verify(self.root, ['linux'], self.repo, require_record=True)
                (package / 'extra').unlink(missing_ok=True)
                path.write_bytes(original)
                (package / 'lib/provider.so').unlink()
                (package / 'lib/provider.so').symlink_to('provider.so.1')
        (self.root / 'linux/verify.log').write_text('changed evidence')
        with self.assertRaisesRegex(RuntimeError, 'receipt changed'):
            release.verify(self.root, ['linux'], self.repo, require_record=True)

    def test_resume_does_not_rebuild_completed_or_delete_failed_target(self):
        self.package()
        failed = self.root / 'arm'
        failed.mkdir()
        (failed / 'failure.log').write_text('preserved')
        with mock.patch.object(unix, 'build', wraps=unix.build) as adapter:
            release.build(self.root, ['linux'], self.config, self.base / 'config.json', self.repo)
            adapter.assert_not_called()
            with self.assertRaisesRegex(RuntimeError, 'Build output conflict'):
                release.build(self.root, ['linux', 'arm'], self.config, self.base / 'config.json', self.repo)
        self.assertEqual((failed / 'failure.log').read_text(), 'preserved')

    def test_import_binds_qualification_and_preserves_staged_bytes(self):
        self.package()
        staged = self.staged_fixture(self.root / 'r2-stage')
        before = unix.inventory(self.root / 'r2-stage')
        imported = self.base / 'outputs/imported'
        state = release.import_cohort(imported, self.root, self.rev, ['linux'], self.repo)
        self.assertTrue(state['imported_qualified'])
        self.assertEqual(release.verify_stage(imported), staged)
        self.assertEqual(unix.inventory(imported / 'r2-stage'), before)
        release.verify(imported, ['linux'], self.repo, require_record=True)
        self.assertEqual(unix.inventory(self.root / 'r2-stage'), before)

    def test_windows_import_keeps_evidence_and_calls_offline_adapter(self):
        path = self.root / 'windows/release'
        (path / 'Release').mkdir(parents=True)
        (path / 'receipts').mkdir()
        (path / 'Release/vkQuake.exe').write_bytes(b'PE fixture')
        (path / 'receipts/native.json').write_text('native fixture evidence')
        for name in ('dependency-input-manifest.json', 'shader-build-plan.json',
                     'dependency-reference.json', 'dependency-baseline-hashes-0f277d1f.json'):
            (path / name).write_text('{}')
        unix.save(path / 'windows-artifact-manifest.json', {'revision': self.rev,
                  'archive_sha256': self.entry['archive_sha256'],
                  'files': [{'file': 'vkQuake.exe', 'sha256': unix.sha(path / 'Release/vkQuake.exe')}]})
        imported = self.base / 'outputs/windows-import'
        with mock.patch.object(release, 'verify_windows_adapter') as verifier:
            release.import_cohort(imported, self.root, self.rev, ['windows'], self.repo,
                                  config_path=self.base / 'config.json')
            self.assertEqual(verifier.call_count, 2)
        self.assertEqual(unix.inventory(path), unix.inventory(imported / 'windows/release'))
        (imported / 'windows/release/receipts/native.json').write_text('changed')
        with mock.patch.object(release, 'verify_windows_adapter'), self.assertRaisesRegex(RuntimeError, 'Recorded artifact'):
            release.verify(imported, ['windows'], self.repo, require_record=True)

    def test_stage_is_atomic_and_existing_stage_is_never_regenerated(self):
        self.package()
        def producer(argv, log, cwd=None):
            self.staged_fixture(Path(argv[argv.index('--output') + 1]))
        with mock.patch.object(release, 'verify'), mock.patch.object(unix, 'run', side_effect=producer) as run:
            release.stage(self.root, self.repo)
            self.assertEqual(run.call_count, 1)
            before = unix.inventory(self.root / 'r2-stage')
            release.stage(self.root, self.repo)
            self.assertEqual(run.call_count, 1)
            self.assertEqual(before, unix.inventory(self.root / 'r2-stage'))
        (self.root / 'r2-stage/manifest.tsv').write_text('changed')
        with self.assertRaisesRegex(RuntimeError, 'inventory mismatch'):
            release.verify_stage(self.root)

    def test_stage_failure_retains_partial_output_without_promoting(self):
        def fail(argv, log, cwd=None):
            (Path(argv[argv.index('--output') + 1]) / 'partial').write_bytes(b'evidence')
            raise subprocess.CalledProcessError(1, 'fixture')
        with mock.patch.object(release, 'verify'), mock.patch.object(unix, 'run', side_effect=fail):
            with self.assertRaises(subprocess.CalledProcessError):
                release.stage(self.root, self.repo)
        self.assertFalse((self.root / 'r2-stage').exists())
        self.assertEqual(len(list(self.root.glob('.r2-stage-*/partial'))), 1)

    def test_stage_recovers_promoted_receipt_before_binding(self):
        self.staged_fixture(self.root / 'r2-stage')
        unix.save(self.root / 'stage-pending.json', {'entry': self.entry,
                  'release_sha256': unix.sha(self.root / 'r2-stage/release.json')})
        with mock.patch.object(release, 'verify'), mock.patch.object(unix, 'run') as run:
            release.stage(self.root, self.repo)
            run.assert_not_called()
        release.verify_stage(self.root)
        self.assertFalse((self.root / 'stage-pending.json').exists())

    def test_stage_recovers_validated_temporary_before_promotion(self):
        temporary = self.root / '.r2-stage-fixture'
        self.staged_fixture(temporary)
        before = unix.inventory(temporary)
        unix.save(self.root / 'stage-pending.json', {'entry': self.entry, 'temporary': str(temporary),
                  'release_sha256': unix.sha(temporary / 'release.json')})
        with mock.patch.object(release, 'verify'), mock.patch.object(unix, 'run') as run:
            release.stage(self.root, self.repo)
            run.assert_not_called()
        self.assertEqual(before, unix.inventory(self.root / 'r2-stage'))
        self.assertFalse(temporary.exists())

    def test_local_cohort_lock_is_nonblocking_and_outside_empty_root(self):
        with release.cohort_lock(self.root, self.base / 'outputs'):
            with self.assertRaisesRegex(RuntimeError, 'owns this cohort'):
                with release.cohort_lock(self.root, self.base / 'outputs'):
                    self.fail('second owner acquired lock')
        empty = self.base / 'outputs/unprepared'
        with release.cohort_lock(empty, self.base / 'outputs'):
            self.assertFalse(empty.exists())

    def test_straight_regular_and_symlink_engines_idempotent_launcher_preserved(self):
        self.package()
        straight = self.straight(symlinks=True)
        preserved = {name: unix.sha(straight / name) for name in ('QuakeSpasmLauncher.py', 'quakespasm-openvr', 'save.sav')}
        first = unix.deploy(self.root, self.config)
        second = unix.deploy(self.root, self.config)
        self.assertTrue(first['changed'])
        self.assertFalse(second['changed'])
        self.assertEqual(len(list((straight / '.engine-backups').iterdir())), 1)
        self.assertEqual(preserved, {name: unix.sha(straight / name) for name in preserved})
        self.assertEqual((self.base / 'old-0').read_bytes(), b'old-engine-0')

    def test_straight_prevalidates_second_entry_before_mutations(self):
        self.package()
        straight = self.straight()
        second = straight / 'bin/quakespasm-openvr.bin'
        second.unlink()
        second.symlink_to(self.base / 'missing')
        original = unix.sha(straight / 'quakespasm-openvr.bin')
        with self.assertRaisesRegex(RuntimeError, 'must be executable'):
            unix.deploy(self.root, self.config)
        self.assertFalse(Path(self.config['straight']['runtime_base']).exists())
        self.assertFalse((straight / '.engine-backups').exists())
        self.assertEqual(unix.sha(straight / 'quakespasm-openvr.bin'), original)

    def interrupted_deployment(self):
        self.package()
        straight = self.straight()
        actual_replace = os.replace
        def interrupt(source, destination):
            destination = Path(destination)
            if destination.name == 'quakespasm-openvr.bin':
                records = list((straight / '.engine-backups').glob('*/deployment.json'))
                self.assertEqual(len(records), 1)
                self.assertEqual(unix.read(records[0])['status'], 'prepared')
            if destination == straight / 'bin/quakespasm-openvr.bin':
                raise OSError('fixture interruption')
            return actual_replace(source, destination)
        with mock.patch.object(unix.os, 'replace', side_effect=interrupt):
            with self.assertRaisesRegex(OSError, 'interruption'):
                unix.deploy(self.root, self.config)
        return straight

    def test_straight_interrupted_pair_resumes_with_same_backups(self):
        straight = self.interrupted_deployment()
        backup = next((straight / '.engine-backups').iterdir())
        before = {p.name: unix.sha(p) for p in backup.glob('*.bin')}
        result = unix.deploy(self.root, self.config)
        self.assertEqual(result['backup'], str(backup))
        self.assertEqual(before, {p.name: unix.sha(p) for p in backup.glob('*.bin')})
        self.assertEqual(len(list((straight / '.engine-backups').iterdir())), 1)
        self.assertEqual(unix.read(backup / 'deployment.json')['status'], 'applied')
        self.assertFalse(unix.deploy(self.root, self.config)['changed'])

    def test_straight_pending_launcher_or_backup_tamper_refuses(self):
        straight = self.interrupted_deployment()
        launcher = straight / 'QuakeSpasmLauncher.py'
        original = launcher.read_text()
        launcher.write_text('unexpected launcher')
        with self.assertRaisesRegex(RuntimeError, 'Launcher changed'):
            unix.deploy(self.root, self.config)
        launcher.write_text(original)
        backup = next((straight / '.engine-backups').iterdir())
        (backup / 'root-engine.bin').write_bytes(b'changed backup')
        with self.assertRaisesRegex(RuntimeError, 'backup changed'):
            unix.deploy(self.root, self.config)
        self.assertFalse((straight / 'bin/quakespasm-openvr.bin').is_symlink())

    def test_safe_archives_reject_traversal_duplicate_link_device_before_writes(self):
        for bad in ('../escape', '/absolute', 'duplicate', 'link', 'device'):
            with self.subTest(bad=bad):
                archive = self.base / ('bad-' + bad.replace('/', '_') + '.tar')
                with tarfile.open(archive, 'w') as bundle:
                    first = tarfile.TarInfo('good'); first.size = 1
                    bundle.addfile(first, io.BytesIO(b'x'))
                    member = tarfile.TarInfo('good' if bad == 'duplicate' else bad)
                    if bad == 'link':
                        member.type = tarfile.SYMTYPE; member.linkname = '../escape'
                    elif bad == 'device':
                        member.type = tarfile.CHRTYPE
                    bundle.addfile(member)
                destination = self.base / ('extract-' + bad.replace('/', '_'))
                destination.mkdir()
                with self.assertRaises(RuntimeError):
                    unix.extract_runtime(archive, destination)
                self.assertEqual(list(destination.iterdir()), [])
                with self.assertRaises(RuntimeError):
                    unix.extract_source(archive, self.base / 'source-extract')
                self.assertFalse((self.base / 'source-extract').exists())

    def test_release_notes_update_current_source_links_without_tag_change(self):
        old = '0' * 40
        body = 'Maintainer introduction\n\n## Current fixed builds\nold engine ' + old + '\n'
        settings = {'repository': 'owner/repo'}
        notes = release.release_notes(body, settings, self.rev, 'https://fixture.invalid/2.0')
        self.assertIn('Maintainer introduction', notes)
        self.assertNotIn(old, notes)
        self.assertIn('/archive/' + self.rev + '.zip', notes)
        self.assertIn('/archive/' + self.rev + '.tar.gz', notes)
        self.assertEqual(notes, release.release_notes(notes, settings, self.rev, 'https://fixture.invalid/2.0'))
        notes_path = self.base / 'notes.md'
        notes_path.write_text(notes + '\nhttps://github.com/owner/repo/archive/' + old + '.zip')
        with self.assertRaisesRegex(RuntimeError, 'conflicting old'):
            release.release_notes('', {**settings, 'notes_file': str(notes_path)}, self.rev, 'https://fixture.invalid/2.0')

    def test_github_uses_verified_public_bytes_only_four_assets_and_original_tag(self):
        self.staged_fixture(self.root / 'r2-stage')
        state = unix.read(self.root / 'coordinator.json')
        state['stage_manifest_sha256'] = unix.sha(self.root / 'r2-stage/release.json')
        unix.save(self.root / 'coordinator.json', state)
        config = {'github': {'repository': 'owner/repo', 'tag': 'original-tag'},
                  'r2': {'public': 'https://fixture.invalid/2.0'}}
        body = {'value': 'Maintainer notes'}
        uploaded = {}
        mutations = []
        def view(argv, text=True):
            self.assertEqual(argv[:4], ['gh', 'release', 'view', 'original-tag'])
            return json.dumps({'tagName': 'original-tag', 'id': 'original-id', 'body': body['value']})
        def download(url, destination, expected):
            self.assertTrue(url.startswith('https://fixture.invalid/2.0/builds/' + self.rev))
            payload = self.root / 'r2-stage/builds' / self.rev / destination.name
            destination.write_bytes(payload.read_bytes())
            self.assertEqual(unix.sha(destination), expected)
        def gh_command(argv, check=True):
            self.assertEqual(argv[3], 'original-tag')
            mutations.append(argv[2])
            if argv[2] == 'upload':
                paths = [Path(p) for p in argv[7:]]
                self.assertEqual({p.name for p in paths}, {*release.RUNTIME_NAMES, 'SHA256SUMS'})
                uploaded.update({p.name: p.read_bytes() for p in paths})
            elif argv[2] == 'edit':
                body['value'] = Path(argv[argv.index('--notes-file') + 1]).read_text()
            elif argv[2] == 'download':
                destination = Path(argv[argv.index('--dir') + 1])
                for name, payload in uploaded.items():
                    (destination / name).write_bytes(payload)
            else:
                self.fail('Unexpected GH action')
        with mock.patch.object(release, 'verify'), mock.patch.object(release, 'git', return_value=self.rev), \
             mock.patch.object(release, 'download', side_effect=download), \
             mock.patch.object(release.subprocess, 'check_output', side_effect=view), \
             mock.patch.object(release.subprocess, 'run', side_effect=gh_command):
            release.publish_github(self.root, config, self.repo)
        self.assertEqual(mutations, ['upload', 'edit', 'download'])
        self.assertEqual(unix.read(self.root / 'github-publication.json')['original_tag'], 'original-tag')
        with mock.patch.object(release, 'verify'), mock.patch.object(release, 'git', return_value=self.rev), \
             mock.patch.object(release, 'download', side_effect=RuntimeError('Downloaded public artifact hash mismatch')), \
             mock.patch.object(release.subprocess, 'check_output', side_effect=view), \
             mock.patch.object(release.subprocess, 'run') as mutation:
            with self.assertRaisesRegex(RuntimeError, 'hash mismatch'):
                release.publish_github(self.root, config, self.repo)
            mutation.assert_not_called()


if __name__ == '__main__':
    unittest.main()
