#!/usr/bin/env python3
"""Stage verified 2.0 runtimes and publish an isolated updater channel."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import tarfile
import urllib.request
import zipfile
from package import scan


def sha(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def copy(source, destination):
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source, destination)


def stage(args):
    out = args.output.resolve()
    require(not out.exists() or not any(out.iterdir()), 'Staging directory must be empty')
    require(re.fullmatch('[0-9a-f]{40}', args.revision), 'Expected a full source revision')
    require(subprocess.check_output(['git', 'get-tar-commit-id'], stdin=args.source.open('rb'), text=True).strip() == args.revision,
            'Source archive revision mismatch')
    out.mkdir(parents=True, exist_ok=True)
    prefix = Path('builds') / args.revision
    rows, archives = [], []
    with tarfile.open(args.source) as source:
        source_notices = {member.name: source.extractfile(member).read() for member in source
                          if member.isfile() and (member.name.startswith('LICENSES/') or
                          member.name in ['LICENSE-GPL-3.0.txt', 'LICENSE-COMPONENTS.txt', 'Packaging/Linux/quakespasm-openvr'])}
    for runtime, package in [('linux-x64', args.linux), ('linux-arm64', args.arm)]:
        manifest = json.loads((package / 'artifact-manifest.json').read_text())
        require(manifest['architecture'] == ('x86_64' if runtime == 'linux-x64' else 'aarch64'), 'Native architecture mismatch')
        require(manifest['product']['revision'] == args.revision, 'Native package revision mismatch')
        require(manifest['product']['archive_sha256'] == sha(args.source), 'Native source archive mismatch')
        actual = scan(package)
        actual.pop('artifact-manifest.json', None)
        require(actual == manifest['inventory'], 'Native package inventory differs from manifest')
        root = out / prefix / runtime
        copy(package / 'bin/vkquake', root / 'bin/quakespasm-openvr.bin')
        (root / 'quakespasm-openvr').write_bytes(source_notices['Packaging/Linux/quakespasm-openvr'])
        (root / 'quakespasm-openvr').chmod(0o755)
        for source in sorted((package / 'lib').rglob('*')):
            if source.is_file():
                # The updater has no symlink action: materialize SONAME aliases.
                copy(source, root / 'lib' / source.relative_to(package / 'lib'))
        shutil.copytree(package / 'share', root / 'share')
        for path in sorted(root.rglob('*')):
            if path.is_file():
                rows.append((sha(path), path.relative_to(out).as_posix(), path.relative_to(root).as_posix(), runtime))
        archive = out / prefix / ('quakespasm-vr-2.0-' + runtime + '.tar.gz')
        with tarfile.open(archive, 'w:gz') as bundle:
            bundle.add(root, arcname='quakespasm-vr-2.0-' + runtime)
        archives.append(archive.relative_to(out).as_posix())
        source_access = out / prefix / ('source-access-' + runtime + '.tar.gz')
        with tarfile.open(source_access, 'w:gz') as bundle:
            bundle.add(package / 'sources', arcname='sources')
            bundle.add(package / 'receipts', arcname='receipts')
            bundle.add(package / 'artifact-manifest.json', arcname='artifact-manifest.json')
        archives.append(source_access.relative_to(out).as_posix())
    if args.windows:
        receipt = json.loads(args.windows_receipt.read_text(encoding='utf-8-sig'))
        require(receipt['revision'] == args.revision and receipt['archive_sha256'] == sha(args.source), 'Windows source mismatch')
        root = out / prefix / 'win-x64'
        for entry in receipt['files']:
            name = entry['file']
            source = args.windows / name
            require(sha(source) == entry['sha256'], 'Windows artifact changed: ' + name)
            if source.suffix.lower() == '.pdb':
                continue
            copy(source, root / ('quakespasm-openvr.exe' if name == 'vkQuake.exe' else name))
        for name, data in source_notices.items():
            if name.startswith('LICENSE'):
                target = root / name
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(data)
        for path in sorted(root.rglob('*')):
            if path.is_file():
                rows.append((sha(path), path.relative_to(out).as_posix(), path.relative_to(root).as_posix(), 'win-x64'))
        archive = out / prefix / 'quakespasm-vr-2.0-win-x64.zip'
        with zipfile.ZipFile(archive, 'w', zipfile.ZIP_DEFLATED) as bundle:
            for path in sorted(root.rglob('*')):
                if path.is_file():
                    bundle.write(path, 'quakespasm-vr-2.0-win-x64/' + path.relative_to(root).as_posix())
        archives.append(archive.relative_to(out).as_posix())
    copy(args.source, out / prefix / 'product.tar')
    archives.append((prefix / 'product.tar').as_posix())
    (out / 'manifest.tsv').write_text('# sha256\taction\tsource_path\tdestination_path\n' + ''.join(
        '#qsvr-runtime-v1\t' + digest + '\tfile\t' + source + '\t' + destination + '\t' + runtime + '\n'
        for digest, source, destination, runtime in rows))
    files = {path.relative_to(out).as_posix(): sha(path) for path in out.rglob('*') if path.is_file()}
    (out / 'release.json').write_text(json.dumps({'branch': '2.0', 'revision': args.revision,
        'archives': archives, 'files': files, 'headset_appearance_tested': False}, indent=2) + '\n')
    print(json.dumps({'status': 'staged', 'revision': args.revision, 'runtime_files': len(rows), 'archives': archives}))


def publish(args):
    out = args.stage.resolve()
    release = json.loads((out / 'release.json').read_text())
    revision = release['revision']
    remote_revision = subprocess.check_output(['git', 'ls-remote', 'origin', 'refs/heads/2.0'], text=True).split()[0]
    require(remote_revision == revision, 'Publishing requires source revision on origin/2.0')
    require(args.remote.rstrip('/').endswith('/2.0'), 'Publisher only writes the isolated 2.0 channel')
    actual = scan(out)
    actual.pop('release.json', None)
    require(actual == {name: {'file': digest} for name, digest in release['files'].items()}, 'Staged inventory differs from release')
    if args.check:
        print(json.dumps({'status': 'verified', 'revision': revision}))
        return
    prefix = 'builds/' + revision
    bucket, channel = args.remote.rstrip('/').rsplit('/', 1)
    remote_files = json.loads(subprocess.check_output(['rclone', 'lsjson', bucket, '--recursive', '--files-only',
        '--include', channel + '/' + prefix + '/**'], text=True))
    # A revision namespace is immutable. Verify every existing object before
    # uploading anything, so a failed/repeated run cannot damage a live build.
    for entry in remote_files:
        name = entry['Path'].removeprefix(channel + '/')
        require(name in release['files'], 'Existing revision has an unexpected object: ' + name)
        with subprocess.Popen(['rclone', 'cat', bucket + '/' + entry['Path']], stdout=subprocess.PIPE, stderr=subprocess.PIPE) as stream:
            digest = hashlib.file_digest(stream.stdout, 'sha256').hexdigest()
            require(stream.wait() == 0, 'Cannot verify existing revision object')
        require(digest == release['files'][name], 'Refusing to replace a differing revision object: ' + name)
    subprocess.run(['rclone', 'copy', str(out / prefix), args.remote.rstrip('/') + '/' + prefix,
                    '--s3-no-check-bucket', '--immutable'], check=True)
    # Read public bytes before exposing the manifest. No runtime logs/configs or
    # user game data are staged. R2 credentials remain in the existing rclone owner.
    for name, digest in release['files'].items():
        if name == 'manifest.tsv':
            continue
        url = args.public.rstrip('/') + '/' + name + '?sha256=' + digest
        with urllib.request.urlopen(url, timeout=120) as response:
            actual = hashlib.file_digest(response, 'sha256').hexdigest()
        require(actual == digest, 'Public object mismatch: ' + name)
    for name in ['release.json', 'manifest.tsv']:
        subprocess.run(['rclone', 'copyto', str(out / name), args.remote.rstrip('/') + '/' + name,
                        '--s3-no-check-bucket'], check=True)
        with urllib.request.urlopen(args.public.rstrip('/') + '/' + name + '?revision=' + revision, timeout=30) as response:
            require(hashlib.file_digest(response, 'sha256').hexdigest() == sha(out / name), 'Public metadata mismatch')
    print(json.dumps({'status': 'published', 'revision': revision, 'channel': args.public}))


parser = argparse.ArgumentParser()
sub = parser.add_subparsers(dest='command', required=True)
s = sub.add_parser('stage')
for name in ['linux', 'arm', 'source', 'output']:
    s.add_argument('--' + name, required=True, type=Path)
s.add_argument('--revision', required=True)
s.add_argument('--windows', type=Path)
s.add_argument('--windows-receipt', type=Path)
p = sub.add_parser('publish')
p.add_argument('--stage', required=True, type=Path)
p.add_argument('--remote', default='r2:quack/2.0')
p.add_argument('--public', default='https://quack.shrubdragon.studio/2.0')
p.add_argument('--check', action='store_true')
args = parser.parse_args()
stage(args) if args.command == 'stage' else publish(args)
