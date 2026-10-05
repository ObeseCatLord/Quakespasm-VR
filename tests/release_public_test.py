#!/usr/bin/env python3
"""Public archive/refresh proofs using local fixtures; no network or builds."""
import hashlib
import io
import json
from pathlib import Path
import stat
import sys
import tarfile
import tempfile
import unittest
from unittest import mock
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'Packaging/Release'))
import release


def archive_fixture(path):
    files = {'package/README.md': b'private release instructions',
             'package/readme.txt': b'another root readme',
             'package/bin/engine': b'qualified engine\x00bytes',
             'package/share/README.md': b'nested component notice',
             'package/LICENSE.txt': b'required license'}
    if path.suffix == '.zip':
        with zipfile.ZipFile(path, 'w') as bundle:
            bundle.comment = b'archive comment'
            for name, payload in files.items():
                info = zipfile.ZipInfo(name, (2020, 2, 3, 4, 5, 6))
                info.create_system = 3
                info.external_attr = (stat.S_IFREG | 0o755) << 16
                info.internal_attr = 1
                info.compress_type = zipfile.ZIP_DEFLATED
                info.comment = b'member comment'
                info.extra = b'\xfe\xca\x02\x00ok'
                bundle.writestr(info, payload)
            info = zipfile.ZipInfo('package/bin/link')
            info.create_system = 3
            info.external_attr = (stat.S_IFLNK | 0o777) << 16
            bundle.writestr(info, b'engine')
            bundle.writestr('package/README-tools/', b'')
    else:
        with tarfile.open(path, 'w:gz', pax_headers={'comment': 'global metadata'}) as bundle:
            for name, payload in files.items():
                info = tarfile.TarInfo(name)
                info.size = len(payload)
                info.mode = 0o755 if name.endswith('engine') else 0o640
                info.uid, info.gid = 123, 456
                info.uname, info.gname = 'owner', 'group'
                info.mtime = 123456789
                info.pax_headers = {'mtime': '123456789.125', 'comment': 'member metadata'}
                bundle.addfile(info, io.BytesIO(payload))
            for name, kind, target in [('package/bin/link', tarfile.SYMTYPE, 'engine'),
                                       ('package/license-link', tarfile.LNKTYPE, 'package/LICENSE.txt'),
                                       ('package/README-tools', tarfile.DIRTYPE, '')]:
                info = tarfile.TarInfo(name)
                info.type, info.linkname, info.mode = kind, target, 0o751
                bundle.addfile(info)


def archive_inventory(path):
    if path.suffix == '.zip':
        with zipfile.ZipFile(path) as bundle:
            fields = ('filename', 'date_time', 'external_attr', 'internal_attr', 'extra',
                      'comment', 'create_system', 'create_version', 'extract_version', 'compress_type')
            return bundle.comment, {m.filename: (tuple(getattr(m, f) for f in fields), bundle.read(m))
                                    for m in bundle.infolist()}
    with tarfile.open(path) as bundle:
        fields = ('name', 'mode', 'uid', 'gid', 'uname', 'gname', 'mtime', 'type',
                  'linkname', 'size', 'devmajor', 'devminor', 'pax_headers')
        return bundle.pax_headers, {m.name: (tuple(getattr(m, f) for f in fields),
                                            bundle.extractfile(m).read() if m.isfile() else None)
                                    for m in bundle}


class PublicReleaseTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix='release-public-test-')
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.rev = 'c' * 40
        self.config = {'github': {'repository': 'owner/repo', 'tag': 'original-tag'},
                       'r2': {'public': 'https://distribution.invalid/2.0'}}
        self.notes = release.release_notes('Maintainer notes', self.config['github'], self.rev,
                                           self.config['r2']['public'])
        self.notes_path = self.root / 'notes.md'
        self.notes_path.write_text(self.notes)
        self.config['github']['notes_file'] = str(self.notes_path)
        self.identity = {'revision': self.rev, 'repository': 'owner/repo',
                         'original_tag': 'original-tag', 'release_id': 'original-id'}
        release.save(self.root / 'github-publication.json', self.identity)
        stage = self.root / 'r2-stage'
        prefix = stage / 'builds' / self.rev
        prefix.mkdir(parents=True)
        (prefix / 'product.tar').write_bytes(b'qualified source fixture')
        for name in release.RUNTIME_NAMES:
            archive_fixture(prefix / name)
        entry = {'production_revision': self.rev,
                 'archive_sha256': release.sha(prefix / 'product.tar')}
        self.staged = {'branch': '2.0', 'revision': self.rev,
                       'archives': ['builds/' + self.rev + '/' + n for n in release.RUNTIME_NAMES],
                       'files': {n: v['file'] for n, v in release.unix.inventory(stage).items()}}
        release.save(stage / 'release.json', self.staged)
        release.save(self.root / 'coordinator.json', {'entry': entry,
                     'stage_manifest_sha256': release.sha(stage / 'release.json')})
        self.before_stage = release.unix.inventory(stage)
        self.body = 'Historical launcher reference replaced by supplied notes'
        originals = {n: release.sha(prefix / n) for n in release.RUNTIME_NAMES}
        rows = ''.join(originals[n] + '  ' + n + '\n' for n in release.RUNTIME_NAMES)
        originals['SHA256SUMS'] = hashlib.sha256(rows.encode()).hexdigest()
        self.metadata = {'id': 42, 'node_id': 'original-id', 'tag_name': 'original-tag',
                         'assets': [{'name': n, 'digest': 'sha256:' + d} for n, d in originals.items()]}
        self.uploaded = {}
        self.commands = []
        self.corrupt_download = False

    def check_output(self, argv, text=True):
        self.assertEqual(argv, ['gh', 'release', 'view', 'original-tag', '--repo', 'owner/repo',
                                '--json', 'tagName,id,body,assets'])
        return json.dumps({'tagName': self.metadata['tag_name'], 'id': self.metadata['node_id'],
                           'body': self.body, 'assets': self.metadata['assets']})

    def gh_command(self, argv, check=True):
        self.assertEqual(argv[:2], ['gh', 'release'])
        self.assertEqual(argv[3:6], ['original-tag', '--repo', 'owner/repo'])
        action = argv[2]
        self.commands.append(action)
        if action == 'upload':
            self.uploaded = {Path(p).name: Path(p).read_bytes() for p in argv[7:]}
            self.assertEqual(set(self.uploaded), {*release.RUNTIME_NAMES, 'SHA256SUMS'})
            self.metadata['assets'] = [{'name': n, 'digest': 'sha256:' + hashlib.sha256(b).hexdigest()}
                                       for n, b in self.uploaded.items()]
        elif action == 'edit':
            self.body = Path(argv[argv.index('--notes-file') + 1]).read_text()
        elif action == 'download':
            for name, payload in self.uploaded.items():
                destination = Path(argv[argv.index('--dir') + 1]) / name
                destination.write_bytes(payload + (b'corrupt' if self.corrupt_download else b''))
        else:
            self.fail('Unexpected action: ' + action)

    def publish(self, refresh=True, remote=None):
        with mock.patch.object(release, 'verify') as qualification, \
             mock.patch.object(release, 'git', return_value=remote or 'd' * 40) as git, \
             mock.patch.object(release, 'download', side_effect=AssertionError('Private download attempted')), \
             mock.patch.object(release.subprocess, 'check_output', side_effect=self.check_output), \
             mock.patch.object(release.subprocess, 'run', side_effect=self.gh_command):
            release.publish_github(self.root, self.config, config_path=self.root / 'config.json',
                                   refresh_existing=refresh)
            qualification.assert_called_once_with(self.root, release.PLATFORMS, release.REPO,
                                                   require_record=True, config_path=self.root / 'config.json')
            if refresh:
                git.assert_not_called()
            else:
                git.assert_called_once_with(release.REPO, 'ls-remote', 'origin', 'refs/heads/2.0')

    def test_archive_cleanup_preserves_payloads_modes_links_notices_and_originals(self):
        for extension in ('tar.gz', 'zip'):
            with self.subTest(extension=extension):
                original = self.root / ('original.' + extension)
                archive_fixture(original)
                digest = release.sha(original)
                comment, inventory = archive_inventory(original)
                del inventory['package/README.md']
                del inventory['package/readme.txt']
                outputs = [self.root / (name + '.' + extension) for name in ('first', 'second')]
                for output in outputs:
                    release.public_archive(original, output)
                    self.assertEqual(archive_inventory(output), (comment, inventory))
                self.assertEqual(outputs[0].read_bytes(), outputs[1].read_bytes())
                self.assertEqual(release.sha(original), digest)

    def test_notes_keep_exact_source_links_and_default_has_no_distribution_links(self):
        notes = release.release_notes('Intro\n## Current fixed builds\nold section',
                                      {'repository': 'owner/repo'}, self.rev, self.config['r2']['public'])
        self.assertIn('Intro', notes)
        self.assertNotIn('distribution.invalid', notes)
        self.assertIn('/archive/' + self.rev + '.zip', notes)
        self.assertIn('/archive/' + self.rev + '.tar.gz', notes)
        self.assertEqual(notes, release.release_notes(notes, {'repository': 'owner/repo'}, self.rev,
                                                     self.config['r2']['public']))

    def test_generated_and_supplied_notes_reject_private_urls_and_app_mentions(self):
        host = '.'.join(('distribution', 'invalid'))
        private_root = '.'.join(('shrubdragon', 'studio'))
        forbidden = ['QuakeSpasmLaUnChEr', 'https://' + host + '/file',
                     'http://deep.sub.' + host + '/file', '//' + host.upper() + './file',
                     'https://user@sub.' + host + ':443/file',
                     'https://' + private_root + '/file',
                     'https://quack.' + private_root + '/file',
                     'https://sibling.' + private_root + '/file',
                     'http://deep.sibling.' + private_root + '/file',
                     '//' + private_root.upper() + './file']
        for index, text in enumerate(forbidden):
            for provided in (False, True):
                with self.subTest(case=index, provided=provided):
                    settings = {'repository': 'owner/repo'}
                    if provided:
                        self.notes_path.write_text(self.notes + '\n' + text)
                        settings['notes_file'] = str(self.notes_path)
                    with self.assertRaises(RuntimeError) as error:
                        release.release_notes(text, settings, self.rev, self.config['r2']['public'])
                    self.assertNotIn(text, str(error.exception))
        for allowed_host in (host, private_root):
            allowed = 'sub.' + allowed_host + '.evil.invalid'
            self.assertIn(allowed, release.release_notes(
                'https://' + allowed + '/', {'repository': 'owner/repo'}, self.rev,
                self.config['r2']['public']))
        prose = 'Follow the straight path to the exit.'
        self.assertIn(prose, release.release_notes(prose, {'repository': 'owner/repo'}, self.rev,
                                                   self.config['r2']['public']))
        self.notes_path.write_text(self.notes + '\n' + prose)
        self.assertIn(prose, release.release_notes('', self.config['github'], self.rev,
                                                   self.config['r2']['public']))

    def test_refresh_original_then_cleaned_assets_is_deterministic_and_proven(self):
        self.publish()
        first = dict(self.uploaded)
        self.assertEqual(self.commands, ['upload', 'edit', 'download'])
        receipt = release.read(self.root / 'github-publication.json')
        self.assertEqual({k: receipt[k] for k in self.identity}, self.identity)
        self.assertEqual(receipt['public_sha256'], {n: hashlib.sha256(b).hexdigest() for n, b in first.items()})
        self.assertEqual(receipt['removed_readme_policy'], 'package-root/README* files only')
        self.assertEqual(first['SHA256SUMS'].decode(), ''.join(
            receipt['public_sha256'][n] + '  ' + n + '\n' for n in release.RUNTIME_NAMES))
        for name in release.RUNTIME_NAMES:
            archive = self.root / name
            archive.write_bytes(first[name])
            members = archive_inventory(archive)[1]
            self.assertNotIn('package/README.md', members)
            self.assertIn('package/LICENSE.txt', members)
        self.publish()
        self.assertEqual(self.uploaded, first)
        self.assertEqual(release.unix.inventory(self.root / 'r2-stage'), self.before_stage)

    def test_refresh_rejects_wrong_or_missing_receipt_before_mutation(self):
        for key in self.identity:
            with self.subTest(key=key):
                release.save(self.root / 'github-publication.json', {**self.identity, key: 'wrong'})
                with self.assertRaisesRegex(RuntimeError, 'receipt identity mismatch'):
                    self.publish()
        (self.root / 'github-publication.json').unlink()
        with self.assertRaises((RuntimeError, FileNotFoundError)):
            self.publish()
        self.assertEqual(self.commands, [])

    def test_refresh_rejects_unknown_missing_duplicate_or_unqualified_assets(self):
        valid = json.loads(json.dumps(self.metadata))
        for problem in ('unknown', 'missing', 'duplicate', 'wrong_digest', 'no_digest', 'wrong_id', 'wrong_tag'):
            with self.subTest(problem=problem):
                self.metadata = json.loads(json.dumps(valid))
                assets = self.metadata['assets']
                if problem == 'unknown':
                    assets.append({'name': 'README.md', 'digest': 'sha256:' + '0' * 64})
                elif problem == 'missing':
                    assets.pop()
                elif problem == 'duplicate':
                    assets[-1] = dict(assets[0])
                elif problem == 'wrong_digest':
                    assets[0]['digest'] = 'sha256:' + '0' * 64
                elif problem == 'no_digest':
                    assets[0].pop('digest')
                elif problem == 'wrong_id':
                    self.metadata.update(id=99, node_id='wrong')
                else:
                    self.metadata['tag_name'] = 'wrong'
                with self.assertRaisesRegex(RuntimeError, 'mismatch|assets|digest'):
                    self.publish()
                self.assertEqual(self.commands, [])
                self.assertEqual(release.read(self.root / 'github-publication.json'), self.identity)

    def test_notes_failure_blocks_upload_and_final_hash_failure_blocks_receipt(self):
        self.notes_path.write_text(self.notes + '\nPrivate LAUNCHER instructions')
        with self.assertRaisesRegex(RuntimeError, 'private app'):
            self.publish()
        self.assertEqual(self.commands, [])
        self.notes_path.write_text(self.notes)
        self.corrupt_download = True
        with self.assertRaisesRegex(RuntimeError, 'attachment inventory/hash'):
            self.publish()
        self.assertEqual(release.read(self.root / 'github-publication.json'), self.identity)

    def test_normal_publication_origin_gate_is_unchanged(self):
        with self.assertRaisesRegex(RuntimeError, 'engine revision on origin/2.0'):
            self.publish(refresh=False)
        self.assertEqual(self.commands, [])
        self.publish(refresh=False, remote=self.rev)
        self.assertEqual(self.commands, ['upload', 'edit', 'download'])

    def test_refresh_cli_requires_github_and_forbids_r2_and_run_flag(self):
        for flags in (['--refresh-existing'], ['--refresh-existing', '--r2'],
                      ['--refresh-existing', '--r2', '--github']):
            args = release.parser().parse_args(['--root', str(self.root), '--config', str(self.root / 'config.json'),
                                                'publish', *flags])
            with mock.patch.object(release, 'publish_r2') as r2, mock.patch.object(release, 'publish_github') as gh:
                with self.assertRaisesRegex(RuntimeError, 'requires --github and forbids --r2'):
                    release.execute(args, self.config, self.root / 'config.json', self.root)
                r2.assert_not_called()
                gh.assert_not_called()
        with mock.patch('sys.stderr', new_callable=io.StringIO), self.assertRaises(SystemExit):
            release.parser().parse_args(['--root', str(self.root), '--config', 'unused',
                                          'run', '--publish-gh', '--refresh-existing'])


if __name__ == '__main__':
    unittest.main()
