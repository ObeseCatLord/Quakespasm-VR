"""Adopt the win-x64 runtime that GitHub Actions built for a release revision.

The Windows workflow (.github/workflows/windows.yml) builds every pushed commit
with MSVC and uploads `qsvr-win-x64-<revision>`: `Release/` plus a
`windows-artifact-manifest.json` naming the revision and each file's SHA-256.
This module finds the successful run for exactly the release revision, checks
the artifact against its manifest and the expected runtime file set, and writes
the cohort's `windows/release/` in the layout the stage step consumes. The run
identity is kept in `windows/release/receipts/actions-run.json`.
"""
import json
import shutil
import subprocess
import tempfile
import time
from pathlib import Path

import unix

RUNTIME_FILES = frozenset((
    'vkQuake.exe', 'SDL3.dll', 'phonon.dll', 'libcurl.dll', 'nghttp2.dll', 'z.dll',
    'libcrypto-3-x64.dll', 'libssl-3-x64.dll', 'libFLAC-8.dll', 'libmad-0.dll', 'libmikmod-3.dll',
    'libmpg123-0.dll', 'libogg-0.dll', 'libopus-0.dll', 'libopusfile-0.dll', 'libvorbis-0.dll',
    'libvorbisfile-3.dll', 'libxmp.dll', 'LICENSE.opus.txt', 'LICENSE.txt'))
WORKFLOW = 'windows.yml'
RECEIPT = 'windows/release/receipts/actions-run.json'


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def gh(*args):
    return subprocess.run(['gh', *args], check=True, stdout=subprocess.PIPE, text=True).stdout


def find_run(repository, revision, wait_seconds):
    """Return the successful Windows run for revision, waiting while one is in progress."""
    deadline = time.monotonic() + wait_seconds
    while True:
        runs = json.loads(gh('run', 'list', '-R', repository, '--workflow', WORKFLOW, '--commit', revision,
                             '--event', 'push', '--json', 'databaseId,status,conclusion,url,headSha',
                             '--limit', '20'))
        runs = [run for run in runs if run['headSha'] == revision]
        done = [run for run in runs if run['status'] == 'completed' and run['conclusion'] == 'success']
        if done:
            return done[0]
        pending = [run for run in runs if run['status'] != 'completed']
        require(runs, 'No Windows Actions run for ' + revision + '; push the commit first')
        require(pending, 'Windows Actions run for ' + revision + ' did not succeed: ' + runs[0]['url'])
        require(time.monotonic() < deadline, 'Timed out waiting for ' + pending[0]['url'])
        time.sleep(30)


def fetch(root, entry, repository, wait_seconds=3600):
    root = Path(root)
    destination = root / 'windows/release'
    require(not (root / 'windows').exists(), 'Windows output exists; inspect it before fetching again')
    revision = entry['production_revision']
    run = find_run(repository, revision, wait_seconds)
    name = 'qsvr-win-x64-' + revision
    with tempfile.TemporaryDirectory(prefix='.windows-actions-', dir=root) as temporary:
        download = Path(temporary) / 'artifact'
        gh('run', 'download', str(run['databaseId']), '-R', repository, '-n', name, '-D', str(download))
        manifest = json.loads(unix.regular(download / 'windows-artifact-manifest.json').read_text('utf-8-sig'))
        require(manifest.get('revision') == revision, 'Actions artifact revision mismatch')
        require(str(manifest.get('run_id')) == str(run['databaseId']), 'Actions artifact run mismatch')
        listed = {row['file']: row['sha256'] for row in manifest['files']}
        require(len(listed) == len(manifest['files']), 'Duplicate file in Actions manifest')
        require(set(listed) == RUNTIME_FILES, 'Actions runtime file set differs: '
                + ', '.join(sorted(set(listed) ^ RUNTIME_FILES)))
        package = download / 'Release'
        present = {path.name for path in package.iterdir()}
        require(present == RUNTIME_FILES, 'Actions artifact contents differ from its manifest')
        for file, digest in listed.items():
            require(unix.sha(unix.regular(package / file)) == digest, 'Actions artifact hash mismatch: ' + file)
        staging = Path(temporary) / 'release'
        (staging / 'receipts').mkdir(parents=True)
        shutil.copytree(package, staging / 'Release')
        unix.save(staging / 'windows-artifact-manifest.json', {
            'revision': revision, 'archive_sha256': entry['archive_sha256'],
            'files': [{'file': file, 'sha256': listed[file]} for file in sorted(listed)]})
        unix.save(staging / 'receipts/actions-run.json', {
            'repository': repository, 'run_id': run['databaseId'], 'url': run['url'],
            'artifact': name, 'revision': revision,
            'artifact_manifest_sha256': unix.sha(download / 'windows-artifact-manifest.json')})
        destination.parent.mkdir(parents=True)
        staging.rename(destination)
    return verify(root, entry)


def adopted(root):
    return (Path(root) / RECEIPT).is_file()


def verify(root, entry):
    root = Path(root)
    receipt = unix.read(unix.regular(unix.contained(root, RECEIPT)))
    require(receipt['revision'] == entry['production_revision'], 'Actions receipt revision mismatch')
    manifest_path = unix.regular(unix.contained(root, 'windows/release/windows-artifact-manifest.json'))
    manifest = unix.read(manifest_path)
    require(manifest['revision'] == entry['production_revision']
            and manifest['archive_sha256'] == entry['archive_sha256'], 'Windows source identity mismatch')
    expected = {row['file']: {'file': row['sha256']} for row in manifest['files']}
    require(set(expected) == RUNTIME_FILES, 'Windows runtime file set mismatch')
    require(unix.inventory(unix.contained(root, 'windows/release/Release')) == expected,
            'Windows full inventory mismatch')
    return {'manifest_sha256': unix.sha(manifest_path), 'inventory_entries': len(expected),
            'builder': 'github-actions', 'run_id': receipt['run_id']}
