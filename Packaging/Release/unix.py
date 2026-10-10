"""Small filesystem, existing Unix recipe, and Straight deployment adapters."""
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import platform
import posixpath
import re
import shlex
import shutil
import subprocess
import sys
import tarfile
import tempfile
import uuid


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def sha(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def read(path):
    return json.loads(Path(path).read_text(encoding='utf-8-sig'))


def save(path, value):
    path = Path(path)
    with tempfile.NamedTemporaryFile(mode='w', dir=path.parent, delete=False) as stream:
        temporary = Path(stream.name)
        json.dump(value, stream, indent=2, sort_keys=True)
        stream.write('\n')
    try:
        os.replace(temporary, path)
    finally:
        temporary.unlink(missing_ok=True)


def relpath(value):
    require(isinstance(value, str) and value and '\\' not in value and '\x00' not in value,
            'Invalid relative path')
    path = PurePosixPath(value)
    require(not path.is_absolute() and '..' not in path.parts and path.parts
            and path.as_posix() == value and ':' not in value,
            'Unsafe relative path: ' + value)
    return value


def contained(root, value):
    path = Path(root) / relpath(value)
    require(path.resolve().is_relative_to(Path(root).resolve()), 'Path escapes root: ' + value)
    return path


def regular(path):
    path = Path(path)
    require(path.is_file() and not path.is_symlink(), 'Expected regular file: ' + str(path))
    return path


def inventory(root):
    """Compatible with package.py, but reject special files and escaping links."""
    root = Path(root)
    require(root.is_dir() and not root.is_symlink(), 'Expected regular directory: ' + str(root))
    result = {}
    for base, dirs, files in os.walk(root, followlinks=False):
        for name in sorted(dirs + files):
            path = Path(base) / name
            relative = path.relative_to(root).as_posix()
            relpath(relative)
            if path.is_symlink():
                target = os.readlink(path)
                require(not Path(target).is_absolute() and '\\' not in target,
                        'Unsafe symlink: ' + relative)
                require(path.resolve(strict=True).is_relative_to(root.resolve()),
                        'Symlink escapes inventory: ' + relative)
                result[relative] = {'link': target}
                if name in dirs:
                    dirs.remove(name)
            elif path.is_file():
                result[relative] = {'file': sha(path)}
            else:
                require(path.is_dir(), 'Unsupported filesystem entry: ' + relative)
    return dict(sorted(result.items()))


def archive_manifest(archive):
    """Inspect all members before extraction; links, devices and duplicates fail."""
    files, names = [], set()
    with tarfile.open(regular(archive)) as bundle:
        for member in bundle:
            name = relpath(member.name.rstrip('/'))
            require(name not in names, 'Duplicate archive member: ' + name)
            names.add(name)
            require(member.isdir() or member.isfile(), 'Unsupported archive member: ' + name)
            if member.isfile():
                with bundle.extractfile(member) as stream:
                    checksum = hashlib.file_digest(stream, 'sha256').hexdigest()
                files.append({'path': name, 'bytes': member.size, 'sha256': checksum})
    # A file must never also be a parent directory (irrespective of member order).
    regular_names = {row['path'] for row in files}
    for name in names:
        require(not any(parent.as_posix() in regular_names for parent in PurePosixPath(name).parents),
                'Archive file used as a directory: ' + name)
    return sorted(files, key=lambda row: row['path'])


def extract_source(archive, destination):
    archive_manifest(archive)
    destination = Path(destination)
    require(not destination.exists(), 'Source output already exists')
    destination.mkdir(parents=True)
    with tarfile.open(archive) as bundle:
        for member in bundle:
            path = contained(destination, member.name.rstrip('/'))
            if member.isdir():
                path.mkdir(parents=True, exist_ok=True)
            else:
                path.parent.mkdir(parents=True, exist_ok=True)
                with bundle.extractfile(member) as source, path.open('xb') as output:
                    shutil.copyfileobj(source, output)
                path.chmod(member.mode & 0o777)  # No ownership, setuid or special bits.


def extract_runtime(archive, destination):
    """Safe local extraction for build-foundry.sh's retrieved package tar."""
    destination = Path(destination)
    require(destination.is_dir() and not destination.is_symlink() and not any(destination.iterdir()),
            'Runtime extraction destination must be empty')
    with tarfile.open(regular(archive)) as bundle:
        members, names, leaves = [], set(), set()
        for member in bundle:
            name = member.name
            if name.startswith('./'):
                name = name[2:]
            if name in ('.', '') and member.isdir():
                continue
            name = relpath(name.rstrip('/'))
            require(name not in names, 'Duplicate runtime archive member: ' + name)
            names.add(name)
            require(member.isfile() or member.isdir() or member.issym(), 'Unsupported runtime archive member')
            if not member.isdir():
                leaves.add(name)
            if member.issym():
                link = member.linkname
                require(link and not link.startswith('/') and '\\' not in link and ':' not in link,
                        'Unsafe runtime archive symlink')
                relpath(posixpath.normpath(posixpath.join(posixpath.dirname(name), link)))
            members.append((name, member))
        for name in names:
            require(not any(parent.as_posix() in leaves for parent in PurePosixPath(name).parents),
                    'Runtime file/link used as parent directory')
        # No write occurs until every member and link has passed validation.
        for name, member in members:
            path = contained(destination, name)
            path.parent.mkdir(parents=True, exist_ok=True)
            if member.isdir():
                path.mkdir(exist_ok=True)
            elif member.isfile():
                with bundle.extractfile(member) as source, path.open('xb') as output:
                    shutil.copyfileobj(source, output)
                path.chmod(member.mode & 0o777)
        for name, member in members:
            if member.issym():
                contained(destination, name).symlink_to(member.linkname)
    inventory(destination)  # Also reject dangling links and cycles.


def verify_unix(root, name, entry):
    package = contained(root, name + '/package-final')
    manifest_path = regular(package / 'artifact-manifest.json')
    manifest = read(manifest_path)
    require(manifest['architecture'] == {'linux': 'x86_64', 'arm': 'aarch64'}[name],
            'Unix architecture mismatch')
    product = manifest['product']
    require(product['revision'] == entry['production_revision']
            and product['archive_sha256'] == entry['archive_sha256'], 'Unix source identity mismatch')
    actual = inventory(package)
    actual.pop('artifact-manifest.json')
    require(actual == manifest['inventory'], name + ' full inventory mismatch')
    source = regular(contained(package, product['archive_path']))
    require(sha(source) == entry['archive_sha256'], 'Packaged source archive mismatch')
    engine = regular(package / 'bin/vkquake')
    require(engine.stat().st_mode & 0o111, 'Unix engine is not executable')
    return {'manifest_sha256': sha(manifest_path), 'inventory_entries': len(actual)}


def verify_qualification(root, name, entry):
    """Bind accepted native verification receipts to this exact package."""
    record_path = regular(contained(root, name + '/qualification.json'))
    record = read(record_path)
    require(record['revision'] == entry['production_revision']
            and record['archive_sha256'] == entry['archive_sha256'] and record['verified'] is True,
            'Native qualification identity mismatch')
    require(record['manifest_sha256'] == sha(root / name / 'package-final/artifact-manifest.json'),
            'Qualification package manifest mismatch')
    require(record['receipts'], 'Missing qualification evidence')
    for relative, checksum in record['receipts'].items():
        require(sha(regular(contained(root / name, relative))) == checksum, 'Qualification receipt changed')
    return sha(record_path)


def import_qualification(original, destination, name, entry):
    """Normalize the accepted cohort's native package-verification evidence."""
    original, destination = Path(original), Path(destination)
    existing = original / name / 'qualification.json'
    if existing.exists():
        verify_qualification(original, name, entry)
        record = read(existing)
        for relative in record['receipts']:
            target = contained(destination / name, relative)
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(regular(contained(original / name, relative)), target)
        shutil.copy2(existing, destination / name / 'qualification.json')
        return
    # Current production ARM receipt is at the cohort root; Linux has its own.
    receipt_path = original / name / 'cohort-result.json'
    if name == 'arm' and not receipt_path.exists():
        receipt_path = original / 'cohort-result.json'
    receipt = read(regular(receipt_path))
    require(receipt['revision'] == entry['production_revision']
            and receipt['archive_sha256'] == entry['archive_sha256']
            and receipt['platform'] == {'linux': 'x86_64', 'arm': 'aarch64'}[name]
            and receipt['status'] == 'passed', 'Native cohort was not qualified')
    for step in ('engine-build', 'package-stage', 'package-verify'):
        status = receipt['stages'][step]
        require(status['exit'] == 0 and status['revision'] == entry['production_revision']
                and status['archive_sha256'] == entry['archive_sha256'], 'Native verification stage failed')
        require(status['image_identity'].endswith(' linux/' + ('amd64' if name == 'linux' else 'arm64')),
                'Native verification image architecture mismatch')
    freshness = receipt['freshness']
    require(freshness['package_verify_exit'] == 0 and freshness['actual_inventory_matches_manifest'] is True
            and freshness['engine_cache_copied'] is False and freshness['fresh_engine_object_files'] > 0
            and freshness['engine_staged_sha256'] == sha(original / name / 'package-final/bin/vkquake'),
            'Native qualification does not bind the packaged engine')
    manifest = read(original / name / 'package-final/artifact-manifest.json')
    require(freshness['inventory_entries_excluding_manifest'] == len(manifest['inventory']),
            'Native qualification inventory count mismatch')
    target = destination / name / 'qualification/cohort-result.json'
    target.parent.mkdir(parents=True)
    shutil.copy2(receipt_path, target)
    save(destination / name / 'qualification.json', {
        'revision': entry['production_revision'], 'archive_sha256': entry['archive_sha256'],
        'manifest_sha256': sha(original / name / 'package-final/artifact-manifest.json'),
        'verified': True, 'verifier': 'accepted-native-cohort-package.py',
        'receipts': {'qualification/cohort-result.json': sha(target)}})


def verify_windows(root, entry):
    package = contained(root, 'windows/release/Release')
    receipt = regular(contained(root, 'windows/release/windows-artifact-manifest.json'))
    manifest = read(receipt)
    require(manifest['revision'] == entry['production_revision']
            and manifest['archive_sha256'] == entry['archive_sha256'], 'Windows source identity mismatch')
    expected = {}
    for row in manifest['files']:
        name = relpath(row['file'])
        require(name not in expected, 'Duplicate Windows artifact: ' + name)
        expected[name] = {'file': row['sha256']}
    require('vkQuake.exe' in expected, 'Windows engine missing')
    require(inventory(package) == expected, 'Windows full inventory mismatch')
    evidence = inventory(contained(root, 'windows/release/receipts'))
    inputs = {name: sha(regular(contained(root, 'windows/release/' + name))) for name in
              ('dependency-input-manifest.json', 'shader-build-plan.json',
               'dependency-reference.json', 'dependency-baseline-hashes-0f277d1f.json')}
    return {'manifest_sha256': sha(receipt), 'inventory_entries': len(expected),
            'native_receipts': evidence, 'inputs_sha256': inputs}


def run(argv, log, cwd=None, env=None):
    """No shell; preserve a bounded, per-command build log and fail on native exit."""
    with Path(log).open('w') as stream:
        subprocess.run([str(value) for value in argv], cwd=cwd, stdout=stream,
                       stderr=subprocess.STDOUT, check=True, env=env)


def build(root, name, config, recipe_root):
    """Clean builds only. Existing native recipes own dependency/compiler policy."""
    root, recipe_root = Path(root), Path(recipe_root)
    output = root / name
    require(not output.exists(), 'Build output conflict; retain/inspect failed output: ' + str(output))
    entry = read(root / 'entry.json')
    archive = root / 'archives/product.tar'
    if name == 'arm':
        host = config.get('arm', {}).get('ssh_host', 'foundry')
        require(re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9_.@:-]*', host), 'Invalid ARM SSH alias')
        output.mkdir()
        # The recipe owns native remote building/verification. Interpose only its
        # final local tar extraction, which otherwise accepts unsafe tar members.
        with tempfile.TemporaryDirectory(prefix='qsvr-safe-tar-') as tools:
            wrapper = Path(tools) / 'tar'
            wrapper.write_text('#!/bin/sh\nexec ' + shlex.quote(sys.executable) + ' '
                               + shlex.quote(str(Path(__file__).resolve())) + ' "$@"\n')
            wrapper.chmod(0o755)
            environment = dict(os.environ, PATH=tools + os.pathsep + os.environ.get('PATH', ''))
            run(['bash', recipe_root / 'Packaging/Linux/build-foundry.sh', archive,
                 entry['production_revision'], output / 'package-final', host], output / 'build.log',
                env=environment)
    else:
        require(name == 'linux' and platform.machine() == 'x86_64', 'Native x86_64 host required')
        output.mkdir()
        (output / 'native').mkdir()
        (output / 'package-final').mkdir()
        image = 'qsvr-local-2.0:' + entry['production_revision'][:12] + '-' + uuid.uuid4().hex[:8]
        run(['docker', 'build', '--platform', 'linux/amd64', '-t', image,
             recipe_root / 'Packaging/Linux'], output / 'image.log')
        identity = subprocess.check_output(['docker', 'image', 'inspect', image, '--format',
                                            '{{.Id}} {{.Os}}/{{.Architecture}}'], text=True).strip()
        require(identity.endswith(' linux/amd64'), 'Builder architecture mismatch')
        save(output / 'image.json', {'identity': identity, 'dependency_reuse': False})
        docker = ['docker', 'run', '--rm', '--platform', 'linux/amd64']
        run(docker + ['-v', str(root / 'archives') + ':/input:ro',
                      '-v', str(output / 'native') + ':/output', image,
                      '/input/product.tar', entry['production_revision'], '/output'], output / 'native.log')
        package = docker + ['--entrypoint', 'python3', '-v', str(output / 'package-final') + ':/package']
        run(package + ['-v', str(output / 'native') + ':/output:ro', image,
                       '/usr/local/bin/package.py', 'stage', '/output', '/package'], output / 'stage.log')
        run(package + ['--network', 'none', image, '/usr/local/bin/package.py', 'verify', '/package'],
            output / 'verify.log')
    result = verify_unix(root, name, entry)
    receipt = 'build.log' if name == 'arm' else 'verify.log'
    save(output / 'qualification.json', {
        'revision': entry['production_revision'], 'archive_sha256': entry['archive_sha256'],
        'manifest_sha256': result['manifest_sha256'], 'verified': True,
        'verifier': 'existing-native-recipe-package.py', 'receipts': {receipt: sha(output / receipt)}})
    return result


def deploy(root, config):
    """Validate first, retain immutable runtime, replace only two engine entries."""
    root = Path(root)
    entry = read(regular(root / 'entry.json'))
    verify_unix(root, 'linux', entry)
    settings = config['straight']
    straight = Path(settings['directory']).expanduser().absolute()
    runtime_base = Path(settings['runtime_base']).expanduser().absolute()
    require(straight.is_dir() and straight.resolve() == straight, 'Straight must be a real directory')
    require(runtime_base.resolve() == runtime_base and not runtime_base.is_relative_to(straight)
            and not straight.is_relative_to(runtime_base), 'Runtime and Straight roots must be disjoint')
    require(not runtime_base.is_relative_to(root) and not root.is_relative_to(runtime_base),
            'Runtime and cohort roots must be disjoint')
    revision = entry['production_revision']
    require(re.fullmatch('[0-9a-f]{40}', revision), 'Invalid deployment revision')
    target = runtime_base / revision / 'linux'
    require(target.resolve() == target, 'Symlink in runtime destination')
    package = root / 'linux/package-final'
    wanted = inventory(package)
    if target.exists():
        require(inventory(target) == wanted, 'Immutable runtime inventory conflict')
        require(regular(target / 'bin/vkquake').stat().st_mode & 0o111, 'Runtime engine is not executable')
    engines = [straight / 'quakespasm-openvr.bin', straight / 'bin/quakespasm-openvr.bin']
    launchers = {path: sha(path) for path in straight.iterdir()
                 if path.is_file() and (path.name.startswith('QuakeSpasmLauncher')
                                        or path.name == 'quakespasm-openvr')}
    backup_base = straight / '.engine-backups'
    require(backup_base.resolve() == backup_base, 'Symlink in backup destination')
    engine_hash = sha(package / 'bin/vkquake')
    manifest_hash = sha(package / 'artifact-manifest.json')
    intended = str(target / 'bin/vkquake')
    for path in engines:
        require(path.parent.resolve() == path.parent and path.parent.is_dir(),
                'Engine parent must be a real directory')
        require(path.is_file() and path.stat().st_mode & 0o111, 'Existing engine must be executable')
    def installed(path):
        return path.is_symlink() and os.readlink(path) == intended and sha(path) == engine_hash
    def original_matches(path, row):
        return (path.is_symlink() == (row['original_type'] == 'symlink') and sha(path) == row['sha256']
                and (os.readlink(path) if path.is_symlink() else None) == row['previous_link'])
    pending = []
    if backup_base.exists():
        require(backup_base.is_dir(), 'Backup root is not a directory')
        for folder in backup_base.iterdir():
            require(not folder.is_symlink(), 'Symlink in backup directory')
            record_path = folder / 'deployment.json'
            if record_path.is_file():
                record = read(regular(record_path))
                if record.get('runtime') == str(target) and record.get('status') == 'prepared':
                    pending.append((folder, record))
    require(len(pending) <= 1, 'Multiple pending deployments for this revision')
    if pending:
        backup, result = pending[0]
        require(target.exists(), 'Prepared deployment runtime is missing; retained for inspection')
        require(result['revision'] == revision and result['engine_sha256'] == engine_hash
                and result['manifest_sha256'] == manifest_hash, 'Pending deployment identity mismatch')
        require(result['launcher_hashes'] == {p.name: h for p, h in launchers.items()},
                'Launcher changed since prepared deployment')
        rows = result['executables']
        require({row['path'] for row in rows} == {str(path) for path in engines}, 'Pending engine set mismatch')
        for row in rows:
            path = Path(row['path'])
            saved = Path(row['backup'])
            require(saved.parent == backup and saved.resolve() == saved and sha(regular(saved)) == row['sha256'],
                    'Pending executable backup changed')
            require(original_matches(path, row) or installed(path),
                    'Engine is neither recorded old nor intended new state')
    elif all(installed(path) for path in engines):
        require(target.exists(), 'Engine points at missing runtime')
        return {'revision': revision, 'runtime': str(target), 'changed': False, 'launcher_preserved': True}
    else:
        # Prepare immutable runtime and all executable backups before entry-point changes.
        # Every path/manifest/engine/launcher check above precedes these mutations.
        target.parent.mkdir(parents=True, exist_ok=True)
        if not target.exists():
            temporary = Path(tempfile.mkdtemp(prefix='.linux-', dir=target.parent))
            try:
                shutil.copytree(package, temporary, dirs_exist_ok=True, symlinks=True)
                require(inventory(temporary) == wanted, 'Copied runtime inventory mismatch')
                require(not target.exists(), 'Runtime appeared during deployment')
                temporary.rename(target)
            finally:
                if temporary.exists():
                    shutil.rmtree(temporary)
        backup_base.mkdir(exist_ok=True)
        backup = Path(tempfile.mkdtemp(prefix=revision[:12] + '-', dir=backup_base))
        rows = []
        for index, path in enumerate(engines):
            checksum = sha(path)
            saved = backup / ('root-engine.bin' if index == 0 else 'bin-engine.bin')
            previous = os.readlink(path) if path.is_symlink() else None
            shutil.copy2(path, saved, follow_symlinks=True)
            require(sha(saved) == checksum and sha(path) == checksum, 'Engine changed during backup')
            rows.append({'path': str(path), 'original_type': 'symlink' if path.is_symlink() else 'file',
                         'previous_link': previous, 'sha256': checksum, 'backup': str(saved)})
        result = {'revision': revision, 'runtime': str(target), 'status': 'prepared',
                  'engine_sha256': engine_hash, 'manifest_sha256': manifest_hash,
                  'backup': str(backup), 'executables': rows,
                  'launcher_hashes': {p.name: h for p, h in launchers.items()}}
        # Persist the recovery contract BEFORE replacing either entry point.
        save(backup / 'deployment.json', result)
    for row in rows:
        path = Path(row['path'])
        require(original_matches(path, row) or installed(path), 'Engine changed before replacement')
    for path, checksum in launchers.items():
        require(sha(path) == checksum, 'Launcher changed before replacement')
    changed = False
    for row in rows:
        path = Path(row['path'])
        if installed(path):
            continue
        temporary = path.with_name('.' + path.name + '.' + uuid.uuid4().hex)
        try:
            temporary.symlink_to(target / 'bin/vkquake')
            os.replace(temporary, path)
            changed = True
        finally:
            temporary.unlink(missing_ok=True)
    require(inventory(target) == wanted, 'Deployed runtime changed')
    for path, checksum in launchers.items():
        require(sha(path) == checksum, 'Launcher changed during deployment')
    result.update(status='applied', changed=changed, launcher_preserved=True)
    save(backup / 'deployment.json', result)
    return result


if __name__ == '__main__':
    # Private tar shim used only at the existing Foundry recipe's extraction boundary.
    require(len(sys.argv) == 7 and sys.argv[1:4] == ['--no-same-owner', '--no-same-permissions', '-xf']
            and sys.argv[5] == '-C', 'Unsupported local tar invocation')
    extract_runtime(sys.argv[4], sys.argv[6])
