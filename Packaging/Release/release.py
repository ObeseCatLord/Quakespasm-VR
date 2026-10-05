#!/usr/bin/env python3
"""Local 2.0 releases using the existing native recipes and publisher.

Normal workflow (push the committed revision before publication):
  python3 Packaging/Release/release.py --config CONFIG --root COHORT run \
      --revision HEAD --build --stage --deploy --publish-r2 --publish-gh

CONFIG is local JSON, not credentials. Required: output_base (outside source).
Optional sections: arm.ssh_host; straight.directory and straight.runtime_base;
r2.remote and r2.public; github.repository, github.tag, optional github.notes_file; windows (passed to
windows/build.py). Paths must be absolute. COHORT is below output_base.
Builds are clean. Resume rechecks full inventories; partial/conflicting build
outputs are retained and refused, never silently deleted or reused.
To retry a failed target, manually rename its directory outside the cohort
and repeat build/run. Successfully verified targets are retained.
Import copies only immutable source/package inputs, never changes the original
cohort or its publication. An imported engine revision remains its own revision.
"""
import argparse
from contextlib import contextmanager
import fcntl
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import urllib.parse
import urllib.request

try:
    from . import unix
except ImportError:
    import unix


REPO = Path(__file__).resolve().parents[2]
PLATFORMS = ('linux', 'arm', 'windows')
RUNTIME_NAMES = ('quakespasm-vr-2.0-linux-x64.tar.gz',
                 'quakespasm-vr-2.0-linux-arm64.tar.gz',
                 'quakespasm-vr-2.0-win-x64.zip')
require, read, save, sha = unix.require, unix.read, unix.save, unix.sha


def git(repo, *args):
    return subprocess.check_output(['git', '-C', str(repo), *args], text=True).strip()


def revision(repo, value):
    require(git(repo, 'branch', '--show-current') == '2.0', 'Source checkout must be on branch 2.0')
    require(value and not value.startswith('-'), 'Invalid revision')
    resolved = git(repo, 'rev-parse', '--verify', value + '^{commit}')
    require(re.fullmatch('[0-9a-f]{40}', resolved), 'Expected full committed revision')
    subprocess.run(['git', '-C', str(repo), 'merge-base', '--is-ancestor', resolved, '2.0'], check=True)
    return resolved


def absolute(value):
    path = Path(value).expanduser()
    require(path.is_absolute(), 'Config/output paths must be absolute: ' + str(value))
    require(path.resolve() == path, 'Symlink or noncanonical path: ' + str(value))
    return path


def output_root(config, value, repo=REPO):
    base, root = absolute(config['output_base']), absolute(value)
    source = Path(repo).resolve()
    require(not base.is_relative_to(source) and not source.is_relative_to(base),
            'output_base must be disjoint from the source checkout')
    require(root != base and root.is_relative_to(base), 'Cohort must be below output_base')
    # Never treat another checkout (including a worktree) as release output.
    for path in [root, *root.parents]:
        require(not (path / '.git').exists(), 'Output may not be in a source checkout')
    return root


def empty_output(root):
    require(not root.exists() or (root.is_dir() and not any(root.iterdir())),
            'Output conflict: directory must be empty: ' + str(root))


@contextmanager
def cohort_lock(root, base):
    """One local operator per cohort; never reserve an otherwise empty root."""
    base = Path(base)
    base.mkdir(parents=True, exist_ok=True)
    name = '.release-' + hashlib.sha256(str(root).encode()).hexdigest()[:24] + '.lock'
    descriptor = os.open(base / name, os.O_RDWR | os.O_CREAT | os.O_NOFOLLOW, 0o600)
    try:
        try:
            fcntl.flock(descriptor, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError as error:
            raise RuntimeError('Another local release command owns this cohort') from error
        yield
    finally:
        os.close(descriptor)


def verify_source(root, repo=REPO, expected_revision=None):
    """Check canonical committed archive, manifest, and any extracted source."""
    root = Path(root)
    entry = read(unix.regular(root / 'entry.json'))
    rev = entry['production_revision']
    require(re.fullmatch('[0-9a-f]{40}', rev), 'Invalid cohort revision')
    revision(repo, rev)
    if expected_revision is not None:
        require(rev == revision(repo, expected_revision), 'Requested revision differs from cohort engine revision')
    archive = unix.regular(unix.contained(root, 'archives/product.tar'))
    require(archive.stat().st_size == entry['archive_bytes'] and sha(archive) == entry['archive_sha256'],
            'Source archive hash/size mismatch')
    with archive.open('rb') as stream:
        require(subprocess.check_output(['git', 'get-tar-commit-id'], stdin=stream, text=True).strip() == rev,
                'Archive commit identity mismatch')
    # A PAX commit header alone cannot prove the archive contains that commit.
    process = subprocess.Popen(['git', '-C', str(repo), 'archive', '--format=tar', rev], stdout=subprocess.PIPE)
    try:
        checksum = hashlib.file_digest(process.stdout, 'sha256').hexdigest()
    finally:
        process.stdout.close()
        status = process.wait()
    require(status == 0 and checksum == entry['archive_sha256'], 'Archive differs from committed git archive')
    manifest_path = unix.regular(root / 'production-source-manifest.json')
    manifest = read(manifest_path)
    files = unix.archive_manifest(archive)
    require(manifest['revision'] == rev and manifest['archive_sha256'] == entry['archive_sha256']
            and manifest['files'] == files and manifest['regular_file_count'] == len(files),
            'Source manifest differs from complete archive inventory')
    if (root / 'source').exists():
        require(unix.inventory(root / 'source') == {row['path']: {'file': row['sha256']} for row in files},
                'Extracted source inventory mismatch')
        # Recipes must retain their archive executable bits; hashes alone omit modes.
        import tarfile
        with tarfile.open(archive) as bundle:
            for member in bundle:
                if member.isfile():
                    required_mode = member.mode & 0o111
                    require((root / 'source' / member.name).stat().st_mode & required_mode == required_mode,
                            'Extracted source mode mismatch: ' + member.name)
    state_path = root / 'coordinator.json'
    if state_path.exists():
        state = read(unix.regular(state_path))
        require(state['entry'] == entry and state['source_manifest_sha256'] == sha(manifest_path),
                'Recorded source identity mismatch')
    return entry


def initial_state(root, entry, imported=False):
    return {'format': 1, 'entry': entry,
            'source_manifest_sha256': sha(root / 'production-source-manifest.json'),
            'imported_qualified': imported, 'platforms': {}}


def prepare(root, rev, repo=REPO):
    rev = revision(repo, rev)
    empty_output(root)
    root.mkdir(parents=True, exist_ok=True)
    (root / 'archives').mkdir()
    archive = root / 'archives/product.tar'
    with archive.open('xb') as stream:
        subprocess.run(['git', '-C', str(repo), 'archive', '--format=tar', rev], stdout=stream, check=True)
    entry = {'production_revision': rev, 'archive_sha256': sha(archive), 'archive_bytes': archive.stat().st_size}
    files = unix.archive_manifest(archive)
    save(root / 'entry.json', entry)
    save(root / 'production-source-manifest.json', {'revision': rev, 'archive_sha256': entry['archive_sha256'],
                                                  'regular_file_count': len(files), 'files': files})
    save(root / 'coordinator.json', initial_state(root, entry))
    verify_source(root, repo, rev)
    return entry


def verify_platform(root, name, entry):
    if name == 'windows':
        return unix.verify_windows(root, entry)
    result = unix.verify_unix(root, name, entry)
    if (root / name / 'qualification.json').exists():
        result['qualification_sha256'] = unix.verify_qualification(root, name, entry)
    return result


def verify_windows_adapter(root, config_path, repo=REPO):
    require(config_path is not None, 'Windows verification requires --config for the offline adapter')
    subprocess.run([sys.executable, str(repo / 'Packaging/Release/windows/build.py'), '--root', str(root),
                    '--config', str(config_path), '--verify-only'], stdout=subprocess.PIPE,
                   stderr=subprocess.PIPE, check=True)


def verify(root, platforms, repo=REPO, expected_revision=None, require_record=False, config_path=None):
    entry = verify_source(root, repo, expected_revision)
    state_path = root / 'coordinator.json'
    require(not require_record or state_path.exists(), 'Import/prepare cohort before resuming builds')
    state = read(state_path) if state_path.exists() else {'platforms': {}}
    results = {}
    for name in platforms:
        if name == 'windows':
            verify_windows_adapter(root, config_path, repo)
        result = verify_platform(root, name, entry)
        if require_record and name != 'windows':
            require('qualification_sha256' in result, 'Missing native qualification record')
        recorded = state['platforms'].get(name)
        require(not require_record or recorded is not None, 'Platform has no coordinator record: ' + name)
        require(recorded is None or recorded == result, 'Recorded artifact identity mismatch: ' + name)
        results[name] = result
    return results


def import_cohort(root, original, rev, platforms, repo=REPO, config_path=None):
    original = absolute(original)
    require(not root.is_relative_to(original) and not original.is_relative_to(root),
            'Import source/output must be disjoint')
    empty_output(root)
    entry = verify_source(original, repo, rev)
    original_results = verify(original, platforms, repo, rev, config_path=config_path)
    existing_stage = original / 'r2-stage'
    staged = validate_stage(existing_stage, entry) if existing_stage.exists() else None
    root.mkdir(parents=True, exist_ok=True)
    (root / 'archives').mkdir()
    for name in ('entry.json', 'production-source-manifest.json'):
        shutil.copy2(original / name, root / name)
    shutil.copy2(original / 'archives/product.tar', root / 'archives/product.tar')
    for name in platforms:
        if name == 'windows':
            destination = root / 'windows/release'
            destination.mkdir(parents=True)
            # Preserve native/independent verification and pinned input evidence.
            shutil.copytree(original / 'windows/release', destination, dirs_exist_ok=True)
        else:
            shutil.copytree(original / name / 'package-final', root / name / 'package-final', symlinks=True)
            unix.import_qualification(original, root, name, entry)
    results = verify(root, platforms, repo, rev, config_path=config_path)
    for name in platforms:
        require(all(results[name].get(key) == value for key, value in original_results[name].items()),
                'Imported artifacts/evidence differ from qualified input')
    state = initial_state(root, entry, imported=True)
    state['platforms'] = results
    if staged:
        temporary = Path(tempfile.mkdtemp(prefix='.r2-stage-import-', dir=root))
        shutil.copytree(existing_stage, temporary, dirs_exist_ok=True)
        require(validate_stage(temporary, entry) == staged, 'Imported stage changed')
        temporary.rename(root / 'r2-stage')
        state['stage_manifest_sha256'] = sha(root / 'r2-stage/release.json')
    save(root / 'coordinator.json', state)
    return state


def build(root, platforms, config, config_path, repo=REPO):
    entry = verify_source(root, repo)
    state = read(unix.regular(root / 'coordinator.json'))
    # Validate every completed platform before any new build, not just selected ones.
    verify(root, list(state['platforms']), repo, require_record=True, config_path=config_path)
    if not (root / 'source').exists():
        unix.extract_source(root / 'archives/product.tar', root / 'source')
    verify_source(root, repo)
    for name in platforms:
        if name in state['platforms']:
            continue
        if name == 'windows':
            require(not (root / 'windows').exists(), 'Windows output conflict')
            unix.run([sys.executable, repo / 'Packaging/Release/windows/build.py', '--root', root,
                      '--config', config_path], root / 'windows-build.log', cwd=repo)
        else:
            unix.build(root, name, config, root / 'source')
        if name == 'windows':
            verify_windows_adapter(root, config_path, repo)
        state['platforms'][name] = verify_platform(root, name, entry)
        save(root / 'coordinator.json', state)
    return verify(root, platforms, repo, require_record=True, config_path=config_path)


def validate_stage(stage, entry):
    release_path = unix.regular(stage / 'release.json')
    release = read(release_path)
    require(release['branch'] == '2.0' and release['revision'] == entry['production_revision'],
            'Staged release identity mismatch')
    actual = unix.inventory(stage)
    actual.pop('release.json')
    require(actual == {unix.relpath(name): {'file': digest} for name, digest in release['files'].items()},
            'Staged full inventory mismatch')
    require(sha(unix.regular(stage / 'builds' / release['revision'] / 'product.tar')) == entry['archive_sha256'],
            'Staged source identity mismatch')
    return release


def verify_stage(root):
    stage = unix.contained(root, 'r2-stage')
    state = read(unix.regular(root / 'coordinator.json'))
    result = validate_stage(stage, state['entry'])
    require(state.get('stage_manifest_sha256') == sha(stage / 'release.json'), 'Staged receipt identity mismatch')
    return result


def stage(root, repo=REPO, config_path=None):
    verify(root, PLATFORMS, repo, require_record=True, config_path=config_path)
    output = root / 'r2-stage'
    pending = root / 'stage-pending.json'
    state = read(root / 'coordinator.json')
    if pending.exists() and not output.exists():
        record = read(unix.regular(pending))
        temporary = absolute(record['temporary'])
        require(temporary.parent == root and temporary.name.startswith('.r2-stage-'),
                'Pending stage temporary path escapes cohort')
        require(record['entry'] == state['entry'], 'Pending stage source identity mismatch')
        validate_stage(temporary, state['entry'])
        require(record['release_sha256'] == sha(temporary / 'release.json'), 'Pending stage hash mismatch')
        temporary.rename(output)
    if output.exists():
        # Recover a process interruption between promotion and receipt binding.
        if 'stage_manifest_sha256' not in state and pending.exists():
            record = read(unix.regular(pending))
            validate_stage(output, state['entry'])
            require(record['entry'] == state['entry'] and record['release_sha256'] == sha(output / 'release.json'),
                    'Promoted stage does not match pending identity')
            state['stage_manifest_sha256'] = record['release_sha256']
            save(root / 'coordinator.json', state)
            pending.unlink()
        return verify_stage(root)
    entry = read(root / 'entry.json')
    require(not pending.exists(), 'Pending stage retained; inspect before staging again')
    temporary = Path(tempfile.mkdtemp(prefix='.r2-stage-', dir=root))
    unix.run([sys.executable, repo / 'Packaging/Linux/publish-r2.py', 'stage',
              '--linux', root / 'linux/package-final', '--arm', root / 'arm/package-final',
              '--windows', root / 'windows/release/Release', '--windows-receipt',
              root / 'windows/release/windows-artifact-manifest.json',
              '--source', root / 'archives/product.tar', '--revision', entry['production_revision'],
              '--output', temporary], root / 'r2-stage.log', cwd=repo)
    validate_stage(temporary, entry)
    checksum = sha(temporary / 'release.json')
    save(pending, {'entry': entry, 'release_sha256': checksum, 'temporary': str(temporary)})
    require(not output.exists(), 'Stage destination appeared during generation')
    temporary.rename(output)
    state = read(root / 'coordinator.json')
    state['stage_manifest_sha256'] = checksum
    save(root / 'coordinator.json', state)
    pending.unlink()
    return verify_stage(root)


def publish_r2(root, config, repo=REPO, config_path=None):
    verify(root, PLATFORMS, repo, require_record=True, config_path=config_path)
    verify_stage(root)
    settings = config['r2']
    unix.run([sys.executable, repo / 'Packaging/Linux/publish-r2.py', 'publish', '--stage', root / 'r2-stage',
              '--remote', settings['remote'], '--public', settings['public']], root / 'r2-publish.log', cwd=repo)


def download(url, destination, expected):
    with urllib.request.urlopen(urllib.request.Request(url, headers={'User-Agent': 'QuakeSpasmVR-release/2.0'}),
                                timeout=60) as response, Path(destination).open('xb') as output:
        shutil.copyfileobj(response, output)
    require(sha(destination) == expected, 'Downloaded public artifact hash mismatch: ' + Path(destination).name)


def release_notes(body, settings, rev, public):
    repository = settings['repository']
    zip_url = 'https://github.com/' + repository + '/archive/' + rev + '.zip'
    tar_url = 'https://github.com/' + repository + '/archive/' + rev + '.tar.gz'
    if settings.get('notes_file'):
        notes = unix.regular(absolute(settings['notes_file'])).read_text()
    else:
        # Replace only the historical current-build section or our marked block.
        notes = re.sub(r'(?s)<!-- qsvr-source-revision:start -->.*?<!-- qsvr-source-revision:end -->', '', body)
        notes = re.sub(r'(?ms)^## Current fixed builds\s*\n.*?(?=^## |\Z)', '', notes)
        notes = notes.rstrip() + '\n\n<!-- qsvr-source-revision:start -->\n'
        notes += '## Current fixed builds\n\nRuntime engine revision: `' + rev + '`. The original release tag is retained.\n'
        notes += 'Exact engine source: [ZIP](' + zip_url + ') and [tar.gz](' + tar_url + ').\n'
        for platform_name in ('linux-x64', 'linux-arm64'):
            notes += '[' + platform_name + ' source access](' + public + '/builds/' + rev
            notes += '/source-access-' + platform_name + '.tar.gz).\n'
        notes += '<!-- qsvr-source-revision:end -->\n'
    require(zip_url in notes and tar_url in notes, 'Release notes require exact engine source ZIP and TAR links')
    links = re.findall(r'https://github\.com/' + re.escape(repository) + r'/archive/([0-9a-f]{40})\.', notes)
    links += re.findall(re.escape(public) + r'/builds/([0-9a-f]{40})/', notes)
    require(all(value == rev for value in links), 'Release notes contain conflicting old source revision links')
    return notes


def publish_github(root, config, repo=REPO, config_path=None):
    verify(root, PLATFORMS, repo, require_record=True, config_path=config_path)
    release = verify_stage(root)
    settings = config['github']
    repository, tag = settings['repository'], settings['tag']
    require(re.fullmatch(r'[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+', repository), 'Invalid GitHub repository')
    require(tag and not tag.startswith('-') and not any(char.isspace() for char in tag), 'Invalid original tag')
    public = config['r2']['public'].rstrip('/')
    require(urllib.parse.urlsplit(public).scheme == 'https', 'R2 public URL must use HTTPS')
    # Mirror the existing publisher's exact origin requirement; never create or move a tag.
    remote = git(repo, 'ls-remote', 'origin', 'refs/heads/2.0').split()
    require(remote and remote[0] == release['revision'], 'GitHub publication requires engine revision on origin/2.0')
    gh = ['gh', 'release']
    view = lambda: json.loads(subprocess.check_output(
        gh + ['view', tag, '--repo', repository, '--json', 'tagName,id,body'], text=True))
    before = view()
    require(before['tagName'] == tag, 'Existing GitHub release tag mismatch')
    notes = release_notes(before['body'], settings, release['revision'], public)
    with tempfile.TemporaryDirectory(prefix='qsvr-release-gh-') as temporary:
        directory = Path(temporary)
        rows = []
        for name in RUNTIME_NAMES:
            relative = 'builds/' + release['revision'] + '/' + name
            require(relative in release['archives'], 'Runtime archive missing from staged release')
            expected = release['files'][relative]
            download(public + '/' + relative + '?sha256=' + expected, directory / name, expected)
            rows.append(expected + '  ' + name + '\n')
        checksums = directory / 'SHA256SUMS'
        checksums.write_text(''.join(rows))
        assets = [directory / name for name in (*RUNTIME_NAMES, 'SHA256SUMS')]
        notes_path = directory / 'release-notes.md'
        notes_path.write_text(notes)
        subprocess.run(gh + ['upload', tag, '--repo', repository, '--clobber', *map(str, assets)], check=True)
        subprocess.run(gh + ['edit', tag, '--repo', repository, '--notes-file', str(notes_path)], check=True)
        after = view()
        require(after['tagName'] == before['tagName'] and after['id'] == before['id'] and after['body'] == notes,
                'Original GitHub release identity/notes mismatch')
        downloaded = directory / 'verify'
        downloaded.mkdir()
        patterns = [item for name in (*RUNTIME_NAMES, 'SHA256SUMS') for item in ('--pattern', name)]
        subprocess.run(gh + ['download', tag, '--repo', repository, '--dir', str(downloaded), *patterns], check=True)
        actual = unix.inventory(downloaded)
        require(actual == {path.name: {'file': sha(path)} for path in assets}, 'GitHub attachment inventory/hash mismatch')
    save(root / 'github-publication.json', {'revision': release['revision'], 'repository': repository,
                                          'original_tag': tag, 'release_id': before['id'],
                                          'attachments': [*RUNTIME_NAMES, 'SHA256SUMS']})


def platform_list(value):
    names = value.split(',')
    if len(names) != len(set(names)) or any(name not in PLATFORMS for name in names):
        raise argparse.ArgumentTypeError('Choose comma-separated linux,arm,windows without duplicates')
    return names


def parser():
    result = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    result.add_argument('--config', required=True, type=Path, help='local JSON paths/options; no secrets')
    result.add_argument('--root', required=True, type=Path, help='cohort directory below config.output_base')
    commands = result.add_subparsers(dest='command', required=True)
    for name, help_text in [('prepare', 'archive a committed 2.0 revision'), ('build', 'build/resume verified platforms'),
                            ('verify', 'verify committed archive and complete package inventories'),
                            ('import', 'copy an explicitly qualified cohort to a new output'),
                            ('stage', 'stage all runtime/source-access archives using publish-r2.py'),
                            ('deploy', 'deploy Linux runtime to Straight without restarting anything'),
                            ('publish', 'explicit R2 and/or existing GitHub release publication'),
                            ('run', 'one-command workflow with explicit action flags')]:
        sub = commands.add_parser(name, help=help_text)
        if name in ('prepare', 'verify', 'import', 'run'):
            sub.add_argument('--revision', required=name == 'import', default='HEAD' if name == 'prepare' else None,
                             help='committed 2.0 revision; import requires explicit engine revision')
        if name in ('build', 'verify', 'import', 'run'):
            sub.add_argument('--platforms', type=platform_list, default=list(PLATFORMS))
        if name == 'import':
            sub.add_argument('--from-root', required=True, type=Path)
        if name == 'run':
            for flag in ('build', 'stage', 'deploy', 'publish-r2', 'publish-gh'):
                sub.add_argument('--' + flag, action='store_true')
        if name == 'publish':
            sub.add_argument('--r2', action='store_true')
            sub.add_argument('--github', action='store_true')
    return result


def execute(args, config, config_path, root):
    command = args.command
    if command == 'prepare':
        result = prepare(root, args.revision)
    elif command == 'import':
        result = import_cohort(root, args.from_root, args.revision, args.platforms, config_path=config_path)
    elif command == 'verify':
        result = verify(root, args.platforms, expected_revision=args.revision, config_path=config_path)
    elif command == 'build':
        result = build(root, args.platforms, config, config_path)
    elif command == 'stage':
        result = stage(root, config_path=config_path)
    elif command == 'deploy':
        verify(root, ['linux'], require_record=True)
        result = unix.deploy(root, config)
        save(root / 'straight-deployment.json', result)
    elif command == 'publish':
        require(args.r2 or args.github, 'Select --r2 and/or --github')
        if args.r2:
            publish_r2(root, config, config_path=config_path)
        if args.github:
            publish_github(root, config, config_path=config_path)
        result = {'published': True}
    else:
        require(any((args.build, args.stage, args.deploy, args.publish_r2, args.publish_gh)), 'run requires action flags')
        if not root.exists() or not any(root.iterdir()):
            prepare(root, args.revision or 'HEAD')
        else:
            verify_source(root, expected_revision=args.revision)
        if args.build:
            build(root, args.platforms, config, config_path)
        if args.stage:
            stage(root, config_path=config_path)
        if args.deploy:
            verify(root, ['linux'], require_record=True)
            save(root / 'straight-deployment.json', unix.deploy(root, config))
        if args.publish_r2:
            publish_r2(root, config, config_path=config_path)
        if args.publish_gh:
            publish_github(root, config, config_path=config_path)
        result = {'revision': read(root / 'entry.json')['production_revision'], 'completed': True}
    if command == 'import':
        result = {'revision': result['entry']['production_revision'], 'imported': True, 'platforms': list(result['platforms'])}
    print(json.dumps(result, sort_keys=True))
    return 0


def main(argv=None):
    args = parser().parse_args(argv)
    config_path = absolute(args.config.absolute())
    config = read(unix.regular(config_path))
    root = output_root(config, args.root.absolute())
    # Recheck branch even for import/verify; never derive import identity from HEAD.
    require(git(REPO, 'branch', '--show-current') == '2.0', 'Source checkout must be on branch 2.0')
    with cohort_lock(root, absolute(config['output_base'])):
        return execute(args, config, config_path, root)


if __name__ == '__main__':
    try:
        sys.exit(main())
    except (RuntimeError, OSError, ValueError, KeyError, subprocess.CalledProcessError) as error:
        print('release: ' + str(error), file=sys.stderr)
        sys.exit(1)
