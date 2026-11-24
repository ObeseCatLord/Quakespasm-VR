#!/usr/bin/env python3
"""Stage verified 2.0 runtimes and publish them to the single update channel.

The bucket holds exactly one set of files: the `latest` channel. Its manifest is
the staged build's runtime rows plus the channel's other rows (shared content),
carried forward from the live manifest or replaced with --extras. Only objects
that are new or changed against the live manifest are read back publicly, and
--content-only skips the build entirely when it is already the live release
(use it to change shared content). After the new
metadata is live, every object the new release does not reference is deleted,
including the previous build."""
import argparse
import hashlib
import http.client
import json
from pathlib import Path
import re
import shutil
import ssl
import subprocess
import tarfile
import tempfile
import time
import urllib.error
import urllib.request
import zipfile
from package import scan



# Runtime files every Windows package needs beside the engine but that the
# native MSBuild recipe does not produce: the Khronos OpenXR loader (VR is
# mandatory) and the MSVC C++ runtime the engine imports. Each is read from the
# exact source archive and must match its pinned hash; see the PROVENANCE files.
WINDOWS_RUNTIME_FILES = {
    'openxr_loader.dll': ('Windows/openxr/x64/openxr_loader.dll',
                          '7f07a9ae5b02f6dc9d667673fbc7a309fdabe136c3b24c0d4f54b5c1d544dd3b'),
    'msvcp140.dll': ('Windows/msvc/x64/msvcp140.dll',
                     '0f885b509a685d2bbfa652fed26b5fb31d88fbdab0a978c641d1c7b8aa460aa9'),
    'vcruntime140.dll': ('Windows/msvc/x64/vcruntime140.dll',
                         'd5e4d9a3e835fa679450145d6a7d94e36573a509317111904d9b3712c30d9066'),
}
WINDOWS_RUNTIME_NOTICES = {
    'Windows/openxr/LICENSE.txt': 'LICENSES/openxr-loader/LICENSE.txt',
    'Windows/openxr/PROVENANCE.md': 'LICENSES/openxr-loader/PROVENANCE.md',
    'Windows/msvc/PROVENANCE.md': 'LICENSES/msvc-runtime/PROVENANCE.md',
    'Windows/net/PROVENANCE.md': 'LICENSES/net/PROVENANCE.md',
    'Windows/net/licenses/curl.txt': 'LICENSES/net/curl.txt',
    'Windows/net/licenses/openssl.txt': 'LICENSES/net/openssl.txt',
    'Windows/net/licenses/nghttp2.txt': 'LICENSES/net/nghttp2.txt',
    'Windows/net/licenses/zlib.txt': 'LICENSES/net/zlib.txt',
}

def sha(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


STALL_SECONDS = 30
STALL_MIN_RATE = 256 * 1024


def stream_sha256(response):
    # A connection can trickle bytes forever without tripping the socket
    # timeout; give up once the average rate stays below a usable floor.
    digest = hashlib.sha256()
    start = time.monotonic()
    received = 0
    while chunk := response.read1(1 << 20):
        digest.update(chunk)
        received += len(chunk)
        elapsed = time.monotonic() - start
        if elapsed > STALL_SECONDS and received < STALL_MIN_RATE * elapsed:
            raise TimeoutError('download stalled at %d bytes/s' % (received / elapsed))
    return digest.hexdigest()


def public_sha256(url, timeout, attempts=4):
    # Retry only downloads, from byte zero with a fresh digest. Publication and
    # hash comparison stay outside this helper; certificate failures and HTTP
    # client errors are fatal, while server errors (5xx) are transient.
    for attempt in range(attempts):
        try:
            request = urllib.request.Request(url, headers={'User-Agent': 'QuakeSpasmVR-Publisher/2.0'})
            with urllib.request.urlopen(request, timeout=timeout) as response:
                return stream_sha256(response)
        except (urllib.error.URLError, ConnectionError, TimeoutError, ssl.SSLError,
                http.client.IncompleteRead, http.client.RemoteDisconnected) as error:
            reason = error.reason if isinstance(error, urllib.error.URLError) else error
            if ((isinstance(error, urllib.error.HTTPError) and error.code < 500) or
                    isinstance(reason, ssl.SSLCertVerificationError) or attempt + 1 == attempts):
                raise
            print('Public download interrupted; retry %d/%d' % (attempt + 2, attempts), flush=True)
            time.sleep(2 ** attempt)


RUNTIME_PREFIX = '#qsvr-runtime-v1\t'
RUNTIMES = ('linux-x64', 'linux-arm64', 'win-x64')
CHANNEL = 'latest'


def parse_manifest(text):
    rows = []
    for line in text.splitlines():
        if not line.strip():
            continue
        tagged = line.startswith(RUNTIME_PREFIX)
        if tagged:
            line = line[len(RUNTIME_PREFIX):]
        elif line.startswith('#'):
            continue
        parts = line.split('\t')
        require(3 <= len(parts) <= 5, 'Malformed manifest row: ' + line)
        runtimes = parts[4].split(',') if len(parts) == 5 else []
        require(re.fullmatch('[0-9a-f]{64}', parts[0]) and parts[1] in ('file', 'extract'), 'Malformed manifest row: ' + line)
        require(not tagged or runtimes, 'Tagged manifest row without runtimes: ' + line)
        require(not runtimes or tagged, 'Runtime column without the runtime tag would break older clients: ' + line)
        rows.append({'sha256': parts[0], 'action': parts[1], 'source': parts[2],
                     'destination': parts[3] if len(parts) > 3 and parts[3] else parts[2], 'runtimes': runtimes})
    return rows


def format_row(row):
    fields = [row['sha256'], row['action'], row['source'], row['destination']]
    if row['runtimes']:
        return RUNTIME_PREFIX + '\t'.join(fields + [','.join(row['runtimes'])]) + '\n'
    return '\t'.join(fields) + '\n'


def check_destinations(extras, build_rows):
    # Two rows writing one path would leave clients permanently "out of date"
    # or install the wrong platform's file, so refuse such a manifest. Untagged
    # rows apply to every platform and must not collide with any build row.
    # ARM64 clients skip untagged executables, so there an ARM-tagged extra row
    # may stand in for an untagged extra with the same destination.
    untagged = [row for row in extras if not row['runtimes']]
    for runtime in RUNTIMES:
        seen = {}
        tagged = [row for row in extras + build_rows if runtime in row['runtimes'] or 'all' in row['runtimes']]
        for row in untagged + tagged:
            key = row['destination'].lower()
            # seen maps a destination to (source, is an untagged extra).
            replaces = (runtime == 'linux-arm64' and key in seen and seen[key][1] and
                        row['runtimes'] and any(row is extra for extra in extras))
            require(key not in seen or replaces, 'Two manifest rows install %s for %s: %s and %s' %
                    (row['destination'], runtime, seen.get(key, ('',))[0], row['source']))
            seen[key] = (row['source'], not row['runtimes'])


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
                          member.name in ['LICENSE-GPL-3.0.txt', 'LICENSE-COMPONENTS.txt', 'Packaging/Linux/quakespasm-openvr', 'docs/release-notes-2.0.md',
                          *[path for path, _ in WINDOWS_RUNTIME_FILES.values()], *WINDOWS_RUNTIME_NOTICES])}
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
                # The update manifest has no symlink action: materialize SONAME aliases.
                copy(source, root / 'lib' / source.relative_to(package / 'lib'))
        shutil.copytree(package / 'share', root / 'share')
        if 'docs/release-notes-2.0.md' in source_notices:
            (root / 'README.md').write_bytes(source_notices['docs/release-notes-2.0.md'])
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
        for target, (name, digest) in WINDOWS_RUNTIME_FILES.items():
            data = source_notices.get(name)
            require(data is not None and hashlib.sha256(data).hexdigest() == digest,
                    'Windows runtime file missing or not its pinned build: ' + name)
            require(not (root / target).exists(), 'Windows runtime file already present: ' + target)
            (root / target).write_bytes(data)
        for name, target in WINDOWS_RUNTIME_NOTICES.items():
            require(name in source_notices, 'Windows runtime notice missing: ' + name)
            (root / target).parent.mkdir(parents=True, exist_ok=True)
            (root / target).write_bytes(source_notices[name])
        if 'docs/release-notes-2.0.md' in source_notices:
            (root / 'README.md').write_bytes(source_notices['docs/release-notes-2.0.md'])
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
    # A new build must be origin/main; content-only keeps the build already live.
    if not args.content_only:
        remote_revision = subprocess.check_output(['git', 'ls-remote', 'origin', 'refs/heads/main'], text=True).split()[0]
        require(remote_revision == revision, 'Publishing requires source revision on origin/main')
    require(args.remote.rstrip('/').endswith('/' + CHANNEL) and args.public.rstrip('/').endswith('/' + CHANNEL),
            'Publisher only writes the single %s channel' % CHANNEL)
    actual = scan(out)
    actual.pop('release.json', None)
    require(actual == {name: {'file': digest} for name, digest in release['files'].items()}, 'Staged inventory differs from release')
    if args.check:
        print(json.dumps({'status': 'verified', 'revision': revision}))
        return
    prefix = 'builds/' + revision
    # The channel's other rows (shared content), never a build.
    live = subprocess.run(['rclone', 'cat', args.remote.rstrip('/') + '/manifest.tsv'], capture_output=True, text=True)
    live_rows = parse_manifest(live.stdout) if live.returncode == 0 else []
    # Objects already live under the same source and hash were verified when
    # they were published; only new or changed ones are read back below.
    live_objects = {(row['source'], row['sha256']) for row in live_rows}
    if args.content_only:
        live_release = subprocess.run(['rclone', 'cat', args.remote.rstrip('/') + '/release.json'],
                                      capture_output=True)
        require(live_release.returncode == 0 and
                hashlib.sha256(live_release.stdout).hexdigest() == sha(out / 'release.json'),
                '--content-only needs this exact build to be the live release')
    if args.extras:
        extras = parse_manifest(args.extras.read_text())
    else:
        extras = live_rows
    extras = [row for row in extras if not row['source'].startswith('builds/')]
    if args.extras_root:
        root = args.extras_root.resolve()
        wanted = {row['source']: row['sha256'] for row in extras}
        for path in root.rglob('*'):
            if path.is_file():
                name = path.relative_to(root).as_posix()
                require(wanted.get(name) == sha(path), 'Extra file not in --extras with this hash: ' + name)
    build_rows = parse_manifest((out / 'manifest.tsv').read_text())
    require(all(row['source'].startswith(prefix + '/') for row in build_rows), 'Staged manifest row outside the build')
    check_destinations(extras, build_rows)
    manifest_text = ('# sha256\taction\tsource_path\tdestination_path\n' +
                     ''.join(format_row(row) for row in extras + build_rows))
    bucket, channel = args.remote.rstrip('/').rsplit('/', 1)
    remote_files = [] if args.content_only else json.loads(subprocess.check_output(
        ['rclone', 'lsjson', bucket, '--recursive', '--files-only',
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
    if not args.content_only:
        subprocess.run(['rclone', 'copy', str(out / prefix), args.remote.rstrip('/') + '/' + prefix,
                        '--s3-no-check-bucket', '--immutable'], check=True)
    # Read public bytes before exposing the manifest. No runtime logs/configs or
    # user game data are staged. R2 credentials remain in the existing rclone owner.
    # A content-only publish keeps the live build, which was verified then.
    for name, digest in ([] if args.content_only else release['files'].items()):
        if name == 'manifest.tsv':
            continue
        url = args.public.rstrip('/') + '/' + name + '?sha256=' + digest
        actual = public_sha256(url, timeout=120)
        require(actual == digest, 'Public object mismatch: ' + name)
    if args.extras_root:
        subprocess.run(['rclone', 'copy', str(args.extras_root), args.remote.rstrip('/'),
                        '--s3-no-check-bucket', '--immutable'], check=True)
    for source, digest in sorted({(row['source'], row['sha256']) for row in extras} - live_objects):
        actual = public_sha256(args.public.rstrip('/') + '/' + source + '?sha256=' + digest, timeout=120)
        require(actual == digest, 'Public extra object mismatch: ' + source)
    with tempfile.TemporaryDirectory() as temporary:
        merged = Path(temporary) / 'manifest.tsv'
        merged.write_text(manifest_text)
        for name, path in [('release.json', out / 'release.json'), ('manifest.tsv', merged)]:
            subprocess.run(['rclone', 'copyto', str(path), args.remote.rstrip('/') + '/' + name,
                            '--s3-no-check-bucket'], check=True)
            actual = public_sha256(args.public.rstrip('/') + '/' + name + '?revision=' + revision, timeout=30)
            require(actual == sha(path), 'Public metadata mismatch')
        # One set only: delete everything the live release does not reference.
        keep = {'release.json', 'manifest.tsv', *release['files'], *(row['source'] for row in extras)}
        listing = json.loads(subprocess.check_output(['rclone', 'lsjson', args.remote.rstrip('/'), '--recursive',
                                                      '--files-only'], text=True))
        stale = sorted(entry['Path'] for entry in listing if entry['Path'] not in keep)
        if stale:
            (Path(temporary) / 'stale').write_text(''.join(path + '\n' for path in stale))
            subprocess.run(['rclone', 'delete', args.remote.rstrip('/'), '--files-from-raw',
                            str(Path(temporary) / 'stale')], check=True)
    print(json.dumps({'status': 'published', 'revision': revision, 'channel': args.public,
                      'extras': len(extras), 'deleted': len(stale)}))


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
# Endpoints come from the private release configuration, never from this file.
p.add_argument('--remote', required=True, help='rclone path of the channel, ending in /latest')
p.add_argument('--public', required=True, help='public HTTPS URL of the same channel')
p.add_argument('--extras', type=Path, help='non-build manifest rows (default: keep the live ones)')
p.add_argument('--extras-root', type=Path, help='new extra files to upload, laid out at their source paths')
p.add_argument('--check', action='store_true')
p.add_argument('--content-only', action='store_true',
               help='the staged build is already live: skip re-verifying it, publish only the extras')
args = parser.parse_args()
stage(args) if args.command == 'stage' else publish(args)
