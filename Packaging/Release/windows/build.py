#!/usr/bin/env python3
"""Thin host adapter for the qualified native WinBoat Release recipe."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import posixpath
from pathlib import Path, PurePosixPath
import re
import shutil
import struct
import subprocess
import sys
import tarfile
import uuid
import xml.etree.ElementTree as ET

HERE = Path(__file__).resolve().parent
SCRIPTS = ('prepare-build.ps1', 'build-release.ps1', 'independent-guest-verification.ps1')
INPUTS = ('dependency-reference.json', 'dependency-input-manifest.json',
          'dependency-baseline-hashes-0f277d1f.json', 'shader-build-plan.json')
FEATURES = ('USE_SDL3', 'USE_CODEC_MP3', 'USE_CODEC_VORBIS', 'USE_CODEC_WAVE',
            'USE_CODEC_FLAC', 'USE_CODEC_OPUS', 'USE_VOICECHAT', 'USE_CODEC_XMP',
            'USE_CODEC_UMX', 'USE_STEAMAUDIO')


def require(ok, message):
    if not ok:
        raise ValueError(message)


def read(path):
    require(path.is_file() and not path.is_symlink(), f'Missing regular input: {path}')
    return json.loads(path.read_text(encoding='utf-8-sig'))


def sha(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def save(path, value):
    temp = path.with_name(path.name + '.tmp')
    temp.write_text(json.dumps(value, indent=2) + '\n', encoding='utf-8')
    temp.replace(path)


def safe_path(name):
    require(isinstance(name, str) and name and '\\' not in name and ':' not in name,
            'Invalid manifest path')
    p = PurePosixPath(name)
    require(not p.is_absolute() and str(p) == name and '..' not in p.parts,
            f'Unsafe manifest path: {name}')
    require(all(not part.endswith((' ', '.')) and not any(ord(c) < 32 for c in part)
                for part in p.parts), f'Unsafe Windows path: {name}')
    return name


def inventory(rows, key):
    result = {}
    seen = set()
    for row in rows:
        name = safe_path(row[key])
        require(name.casefold() not in seen, f'Duplicate manifest path: {name}')
        require(re.fullmatch('[0-9a-f]{64}', row['sha256']) is not None,
                f'Invalid hash: {name}')
        seen.add(name.casefold())
        result[name] = row
    require(result, 'Empty manifest')
    return result


def identity(document, entry):
    require(document['revision'] == entry['production_revision'] and
            document['archive_sha256'] == entry['archive_sha256'], 'Source identity mismatch')


def source_inputs(root):
    entry = read(root / 'entry.json')
    require(re.fullmatch('[0-9a-f]{40}', entry['production_revision']) is not None and
            re.fullmatch('[0-9a-f]{64}', entry['archive_sha256']) is not None,
            'Invalid source revision/archive checksum')
    archive = root / 'archives/product.tar'
    require(archive.is_file() and not archive.is_symlink(), 'Missing source archive')
    require(archive.stat().st_size == entry['archive_bytes'] and sha(archive) == entry['archive_sha256'],
            'Source archive bytes/checksum mismatch')
    manifest = read(root / 'production-source-manifest.json')
    identity(manifest, entry)
    files = inventory(manifest['files'], 'path')
    require(manifest.get('regular_file_count', len(files)) == len(files), 'Source count mismatch')
    actual = {}
    with tarfile.open(archive) as bundle:
        for member in bundle:
            name = safe_path(member.name.rstrip('/') if member.isdir() else member.name)
            require(member.isfile() or member.isdir(), f'Unsupported archive member: {name}')
            if member.isdir():
                continue
            require(name in files and name not in actual, f'Unexpected/duplicate archive member: {name}')
            with bundle.extractfile(member) as stream:
                digest = hashlib.file_digest(stream, 'sha256').hexdigest()
            require(digest == files[name]['sha256'] and
                    member.size == files[name].get('bytes', member.size), f'Source hash mismatch: {name}')
            actual[name] = digest
    require(set(actual) == set(files), 'Archive/full source manifest inventory mismatch')
    # Scripts use this count even when the coordinator supplies only files/path/sha256.
    manifest['regular_file_count'] = len(files)
    return entry, manifest, archive


def transfer_receipts(receipts):
    rows = inventory(read(receipts / 'receipt-transfer-manifest.json'), 'file')
    for name, row in rows.items():
        require('/' not in name, 'Nested receipt filename')
        p = receipts / name
        require(p.is_file() and not p.is_symlink() and p.stat().st_size == row['bytes'] and
                sha(p) == row['sha256'], f'Receipt transfer mismatch: {name}')
    required = {'preparation.json', 'native-msbuild-status.json', 'final-status.json',
                'fresh-compile-inventory.json', 'artifact-manifest.json', 'tool-paths.json',
                'independent-guest-verification.json', 'production-source-manifest.json',
                'dependency-input-manifest.json', 'shader-build-plan.json', 'Release.log', 'Release.binlog'}
    require(required <= rows.keys(), 'Incomplete transferred build receipts')


def optimized_commands(receipts):
    lines = (receipts / 'Release.log').read_text(encoding='utf-8-sig', errors='replace').splitlines()
    cl = [line for line in lines if re.search(r'cl\.exe"?\s+/', line, re.I) and
          all(re.search(r'/' + flag + r'\b', line, re.I) for flag in ('O2', 'GL', 'W4', 'WX'))]
    link = [line for line in lines if re.search(r'link\.exe"?\s+/', line, re.I) and
            re.search(r'/LTCG\b', line, re.I)]
    require(cl and link, 'Missing actual /O2 /GL /W4 /WX /LTCG commands')
    for feature in FEATURES:
        require(any(re.search(r'/D\s*' + feature + r'\b', line) for line in cl),
                f'Missing full-feature compile define: {feature}')
    return len(cl), len(link)


def baseline_inputs(baseline):
    values = {name: read(baseline / name) for name in INPUTS}
    receipts = baseline / 'receipts'
    transfer_receipts(receipts)
    final = read(receipts / 'final-status.json')
    independent = read(receipts / 'independent-guest-verification.json')
    require(final['native_exit_code'] == 0 and final['package_verified'] and
            not final['postbuild_error'] and not final['launcher_exception'] and
            independent['status'] == 'passed', 'Baseline is not a qualified native build')
    optimized_commands(receipts)
    prep = read(receipts / 'preparation.json')
    for name in ('dependency-input-manifest.json', 'shader-build-plan.json'):
        require(read(receipts / name) == values[name], f'Baseline receipt/input mismatch: {name}')
    values['dependency-reference.json']['external_sha256'] = {
        row['path']: row['sha256'] for row in prep['external_dependency_and_tool_facts']}
    require(len(values['dependency-reference.json']['external_sha256']) == 13,
            'Incomplete pinned external prerequisite facts')
    return values


def check_dependencies(manifest, baseline, inputs):
    source = inventory(manifest['files'], 'path')
    deps = inputs['dependency-input-manifest.json']
    require(deps['dependency_tree_equal_to_accepted_baseline'], 'Dependency baseline not accepted')
    expected = inventory(deps['files'], 'path')
    require(len(expected) == deps['dependency_file_count'], 'Dependency input count mismatch')
    prefixes = ('Windows/SDL3/', 'Windows/codecs/', 'Windows/misc/')
    require({p for p in source if p.startswith(prefixes)} == set(expected),
            'Pinned dependency tree inventory changed; requalification required')
    for name, row in expected.items():
        require(source[name]['sha256'] == row['sha256'], f'Pinned dependency input changed: {name}')


NS = '{http://schemas.microsoft.com/developer/msbuild/2003}'


def release_condition(condition):
    """Evaluate only the established project's simple configuration conditions."""
    if not condition:
        return True
    for macro, value in (('$(Configuration)', 'Release'), ('$(Platform)', 'x64'),
                         ('$(SteamAudioSdkDir)', 'pinned-sdk')):
        condition = condition.replace(macro, value)
    match = re.fullmatch(r"\s*'([^']*)'\s*(==|!=)\s*'([^']*)'\s*", condition)
    require(match is not None and '$(' not in condition,
            f'Unsupported project condition; compatibility review required: {condition}')
    left, operation, right = match.groups()
    return (left == right) if operation == '==' else (left != right)


def project_path(value, filename=None):
    value = value.replace('$(SolutionDir)', 'Windows/VisualStudio/').replace('\\', '/')
    if not value.startswith('Windows/VisualStudio/'):
        value = 'Windows/VisualStudio/' + value
    if filename is not None:
        value = value.replace('%(Filename)', PurePosixPath(filename).stem).replace(
            '%(Extension)', PurePosixPath(filename).suffix)
    value = posixpath.normpath(value)
    require('$(' not in value and '%(' not in value and '*' not in value and ';' not in value,
            f'Unsupported project path; compatibility review required: {value}')
    return safe_path(value)


def derive_project_plan(archive, manifest, baseline, inputs):
    """Derive inventories, keeping configuration/import/link policy at its qualified reference.

    This is an inventory reader for the existing recipe, not an MSBuild evaluator.
    New compile/shader items work; unfamiliar configuration constructs fail explicitly.
    """
    source = inventory(manifest['files'], 'path')
    previous = inventory(read(baseline / 'production-source-manifest.json')['files'], 'path')
    recipes = {p for p in previous if p.startswith('Windows/VisualStudio/') and
               (p.endswith(('.vcxproj', '.props', '.sln')))}
    require(recipes == {p for p in source if p.startswith('Windows/VisualStudio/') and
                       p.endswith(('.vcxproj', '.props', '.sln'))},
            'Project/import topology changed; compatibility review required')
    old_archive = baseline / 'product.tar'
    old_entry = read(baseline / 'entry.json')
    require(sha(old_archive) == old_entry['archive_sha256'], 'Baseline source archive checksum mismatch')
    with tarfile.open(archive) as current, tarfile.open(old_archive) as old:
        trees = {}
        for name in sorted(recipes):
            now = current.extractfile(name).read()
            before = old.extractfile(name).read()
            if not name.endswith('.vcxproj'):
                require(now == before, f'Build/link/import policy changed; compatibility review required: {name}')
                continue
            tree = ET.fromstring(now)
            reference = ET.fromstring(before)
            trees[name] = tree
            # Item inventories may evolve. Configuration, flags, imports and project references
            # remain the qualified behavior, including LinkIncremental and LTCG defaults.
            def policy(t):
                clone = ET.fromstring(ET.tostring(t))
                for group in clone.findall(NS + 'ItemGroup'):
                    for item in list(group):
                        if 'Include' in item.attrib and (item.tag == NS + 'ClCompile' or
                           item.tag == NS + 'CustomBuild' and item.attrib['Include'].endswith(('.frag', '.vert', '.comp'))):
                            group.remove(item)
                    if len(group) == 0:
                        clone.remove(group)
                for element in clone.iter():
                    element.text = (element.text or '').strip()
                    element.tail = ''
                return ET.tostring(clone)
            require(policy(tree) == policy(reference),
                    f'Build/link/import policy changed; compatibility review required: {name}')
    object_paths, generated_c, shader_inputs = [], [], []
    for name, tree in trees.items():
        project = PurePosixPath(name).stem
        project_name = 'vkQuake' if project == 'vkquake' else project
        defaults = {}
        for group in tree.findall(NS + 'ItemDefinitionGroup'):
            if release_condition(group.get('Condition')):
                custom = group.find(NS + 'CustomBuild')
                if custom is not None:
                    defaults.update({e.tag.removeprefix(NS): e.text or '' for e in custom
                                     if release_condition(e.get('Condition'))})
        for group in tree.findall(NS + 'ItemGroup'):
            if not release_condition(group.get('Condition')):
                continue
            for item in group:
                if item.tag not in (NS + 'ClCompile', NS + 'CustomBuild') or 'Include' not in item.attrib:
                    continue
                if not release_condition(item.get('Condition')):
                    continue
                metadata = {}
                for element in item:
                    if release_condition(element.get('Condition')):
                        key = element.tag.removeprefix(NS)
                        metadata[key] = (element.text or '').replace('%(' + key + ')', defaults.get(key, ''))
                if metadata.get('ExcludedFromBuild', 'false').lower() == 'true':
                    continue
                path = project_path(item.attrib['Include'])
                if item.tag == NS + 'ClCompile':
                    require('ObjectFileName' not in metadata,
                            'Custom object filename requires compatibility review')
                    require(path in source or path.startswith('Shaders/Compiled/Release/') or
                            path == 'Quake/embedded_pak.c',
                            f'Missing compile input: {path}')
                    object_paths.append('Windows/VisualStudio/Build-' + project_name +
                                        '/x64/Release/' + PurePosixPath(path).stem + '.obj')
                else:
                    if not path.startswith('Shaders/'):
                        continue  # The existing embedded-pak rule remains pinned by policy().
                    require(path in source and path.startswith('Shaders/') and
                            path.endswith(('.frag', '.vert', '.comp')),
                            f'Unsupported shader CustomBuild input: {path}')
                    shader_inputs.append(path)
                    outputs = metadata.get('Outputs', defaults.get('Outputs', ''))
                    require(outputs, f'Missing shader outputs: {path}')
                    for output in outputs.split(';'):
                        if not output.strip():
                            continue
                        target = project_path(output.strip(), path)
                        require(target.startswith('Shaders/Compiled/Release/') and target.endswith('.c'),
                                f'Unsupported shader output: {target}')
                        generated_c.append(target)
    require(len(set(object_paths)) == len(object_paths), 'Colliding native object filenames')
    require(len(set(generated_c)) == len(generated_c), 'Duplicate generated shader output')
    compiled = [p for p in object_paths if PurePosixPath(p).name in
                {PurePosixPath(c).stem + '.obj' for c in generated_c} and 'Build-vkQuake/' in p]
    require(len(compiled) == len(generated_c) and generated_c, 'Generated/compiled shader inventories differ')
    inputs['dependency-reference.json']['expected_native_object_count'] = len(object_paths)
    inputs['dependency-reference.json']['expected_native_objects'] = sorted(object_paths)
    inputs['shader-build-plan.json'] = {
        'revision': manifest['revision'], 'source_archive_sha256': manifest['archive_sha256'],
        'configuration': 'Release', 'shader_source_inputs': sorted(shader_inputs),
        'generated_c_expected': sorted(generated_c),
        'generated_spv_expected': sorted(p[:-2] + '.spv' for p in generated_c),
        'expected_shader_objects': sorted(compiled),
        'compiled_shader_c_input_count': len(generated_c),
        'expected_generated_spv_count': len(generated_c)}


def verify_release(release, entry, manifest, inputs, require_publisher=True):
    receipts = release / 'receipts'
    transfer_receipts(receipts)
    source = read(receipts / 'production-source-manifest.json')
    identity(source, entry)
    require({name: row['sha256'] for name, row in inventory(source['files'], 'path').items()} ==
            {name: row['sha256'] for name, row in inventory(manifest['files'], 'path').items()},
            'Receipt full source inventory mismatch')
    deps = read(receipts / 'dependency-input-manifest.json')
    require(deps['files'] == inputs['dependency-input-manifest.json']['files'],
            'Receipt pinned dependency inventory mismatch')
    documents = {name: read(receipts / (name + '.json')) for name in
                 ('final-status', 'native-msbuild-status', 'independent-guest-verification',
                  'artifact-manifest', 'fresh-compile-inventory', 'preparation')}
    for document in documents.values():
        identity(document, entry)
    final, native, independent, art, inv, prep = (documents[name] for name in documents)
    require(final['native_exit_code'] == native['native_exit_code'] == 0 and
            not final['launcher_exception'] and not final['postbuild_error'] and
            final['package_verified'] and independent['status'] == 'passed', 'Native verification failed')
    require(native['source_manifest_sha256'] == sha(receipts / 'production-source-manifest.json') and
            native['dependency_manifest_sha256'] == sha(receipts / 'dependency-input-manifest.json'),
            'Native source/dependency receipt hashes mismatch')
    require(prep['status'] == 'ready' and prep['archive_bytes'] == entry['archive_bytes'] and
            prep['forbidden_cache_file_count'] == 0 and prep['release_output_absent_before_build'] and
            not inv['accepted_object_cache_copied'], 'Fresh namespace preparation mismatch')
    require(independent['original_source_files_rehashed'] == len(manifest['files']) ==
            prep['extracted_file_count'], 'Independent full source count mismatch')
    require(independent['unchanged_dependency_hash_checks'] == len(deps['files']) ==
            prep['pinned_archive_dependencies_verified'], 'Independent dependency count mismatch')
    reference = inputs['dependency-reference.json']
    require({f['path']: f['sha256'] for f in prep['external_dependency_and_tool_facts']} ==
            reference['external_sha256'], 'Pinned external tools/SDK changed')
    plan = inputs['shader-build-plan.json']
    require(inv['all_object_count'] == len(inv['object_files']) == reference['expected_native_object_count'] and
            all(inv[k] for k in ('all_objects_fresh', 'all_shader_c_fresh', 'all_shader_spv_fresh',
                                'all_shader_objects_fresh')), 'Fresh object inventory mismatch')
    require({f['path'] for f in inv['object_files']} == set(reference['expected_native_objects']),
            'Native object path inventory mismatch')
    for key, planned in (('shader_c_files', 'generated_c_expected'),
                         ('shader_spv_files', 'generated_spv_expected'),
                         ('shader_object_files', 'expected_shader_objects')):
        require({f['path'] for f in inv[key]} == set(inputs['shader-build-plan.json'][planned]),
                f'Current project shader inventory mismatch: {key}')
    for key, expected in (('fresh_shader_c_generated', plan['compiled_shader_c_input_count']),
                          ('fresh_shader_spv_generated', plan['expected_generated_spv_count']),
                          ('fresh_shader_objects', plan['compiled_shader_c_input_count'])):
        require(inv[key] == expected, f'Fresh shader count mismatch: {key}')
    receipt_plan = read(receipts / 'shader-build-plan.json')
    for key in ('generated_c_expected', 'generated_spv_expected', 'expected_shader_objects', 'shader_source_inputs'):
        require(set(receipt_plan[key]) == set(plan[key]), f'Receipt/current project shader plan mismatch: {key}')
    require(independent['compile_file_hash_checks'] == sum(len(inv[k]) for k in
            ('object_files', 'shader_c_files', 'shader_spv_files', 'shader_object_files')),
            'Independent compile hash count mismatch')
    tool = read(receipts / 'tool-paths.json')
    require(tool['cl_hostx64_observed'] and tool['link_hostx64_observed'] and
            independent['native_tool_hashes_unchanged'] == len(tool['installed_x64_tool_facts']) == 5,
            'Actual native HostX64 tools not verified')
    for f in tool['installed_x64_tool_facts']:
        require(reference['external_sha256'].get(f['path']) == f['sha256'], 'Native tool pin mismatch')
    require(art['buildtree_equals_staged'] and art['engine_buildtree_sha256'] ==
            art['engine_staged_sha256'], 'Buildtree/staging engine mismatch')
    all_files = inventory(art['files'], 'file')
    pins = inputs['dependency-baseline-hashes-0f277d1f.json']['Release']
    require(set(all_files) == set(pins) and independent['artifact_file_hash_checks'] == len(all_files),
            'Qualified runtime/symbol/license inventory mismatch')
    runtime = {name: row for name, row in all_files.items() if not name.lower().endswith('.pdb')}
    stage = release / 'Release'
    require(stage.is_dir() and not stage.is_symlink() and
            {p.name for p in stage.iterdir()} == set(runtime), 'Runtime directory inventory mismatch')
    pe_count = 0
    for name, row in runtime.items():
        require('/' not in name, 'Nested runtime filename')
        p = stage / name
        require(p.is_file() and not p.is_symlink() and p.stat().st_size == row['bytes'] and
                sha(p) == row['sha256'], f'Retrieved artifact hash mismatch: {name}')
        if name != 'vkQuake.exe':
            require(row['sha256'] == pins[name], f'Pinned runtime/license changed: {name}')
        if p.suffix.lower() in ('.exe', '.dll'):
            data = p.read_bytes()
            require(data[:2] == b'MZ', f'Invalid PE: {name}')
            offset = struct.unpack_from('<I', data, 0x3c)[0]
            require(data[offset:offset+4] == b'PE\0\0' and
                    struct.unpack_from('<H', data, offset+4)[0] == 0x8664, f'Non-x64 PE: {name}')
            pe_count += 1
    require(pe_count == art['pe_count'] == sum(name.lower().endswith(('.exe', '.dll')) for name in pins) and
            sha(stage / 'vkQuake.exe') == art['engine_buildtree_sha256'], 'Runtime engine/inventory mismatch')
    cl_count, link_count = optimized_commands(receipts)
    require(independent['actual_O2_GL_compiler_commands'] > 0 and
            independent['actual_LTCG_link_commands'] == link_count, 'Independent optimization evidence missing')
    publisher = {'revision': entry['production_revision'], 'archive_sha256': entry['archive_sha256'],
                 'files': [{'file': name, 'sha256': runtime[name]['sha256']} for name in sorted(runtime)]}
    if require_publisher:
        existing = read(release / 'windows-artifact-manifest.json')
        identity(existing, entry)
        require(inventory(existing['files'], 'file') == inventory(publisher['files'], 'file'),
                'Publisher manifest mismatch')
    return publisher, {'status': 'passed', 'revision': entry['production_revision'],
                       'archive_sha256': entry['archive_sha256'], 'native_exit_code': 0,
                       'retrieved_file_hash_checks': len(runtime), 'pe_x64_count': pe_count,
                       'actual_O2_GL_W4_WX_compiler_commands': cl_count,
                       'actual_LTCG_link_commands': link_count, 'runtime_executed': False}


def ps_string(value):
    return "'" + str(value).replace("'", "''") + "'"


def run_tool(argv, log):
    # No host shell, command text logging, SSH credential export, or public transport.
    with log.open('wb') as output:
        result = subprocess.run([str(arg) for arg in argv], stdout=output, stderr=subprocess.STDOUT)
    require(result.returncode == 0, f'Private WinBoat operation failed ({result.returncode}); see {log}')


def fresh_build(root, entry, manifest, archive, inputs, tools):
    windows = root / 'windows'
    windows.mkdir()  # Exclusive namespace reservation, also rejects a concurrent/partial build.
    control = windows / 'control'
    release = windows / 'release'
    control.mkdir()
    release.mkdir()
    save(control / 'adapter-state.json', {'status': 'reserved', **entry})
    powershell = tools / 'winboat-powershell'
    scp = tools / 'winboat-scp'
    namespace = 'qsvr-release-' + entry['production_revision'][:12] + '-' + entry['archive_sha256'][:12] + '-' + uuid.uuid4().hex
    expression = ("$ErrorActionPreference='Stop'; $p=Join-Path $env:USERPROFILE " +
                  ps_string('Documents\\Codex\\' + namespace) +
                  "; if(Test-Path -LiteralPath $p){throw 'Guest namespace exists'}; "
                  "New-Item -ItemType Directory -Path $p | Out-Null; "
                  "[Console]::WriteLine((ConvertTo-Json -Compress @{path=$p}))")
    run_tool([powershell, expression], control / 'guest-namespace.log')
    guest = json.loads((control / 'guest-namespace.log').read_text(encoding='utf-8-sig').strip())['path']
    require(re.fullmatch(r'[A-Za-z]:\\[^\r\n";]*\\' + re.escape(namespace), guest),
            'Unexpected guest namespace path')
    save(control / 'adapter-state.json', {'status': 'transferring', 'guest_workspace': guest, **entry})
    shutil.copyfile(archive, release / 'product.tar')
    save(release / 'entry.json', entry)
    save(release / 'production-source-manifest.json', manifest)
    for name, value in inputs.items():
        if name in ('dependency-input-manifest.json', 'shader-build-plan.json'):
            value = {**value, 'revision': entry['production_revision'],
                     'source_archive_sha256': entry['archive_sha256']}
        save(release / name, value)
    for name in SCRIPTS:
        shutil.copyfile(HERE / name, release / name)
    handoff = [*SCRIPTS, *INPUTS, 'product.tar', 'entry.json', 'production-source-manifest.json']
    facts = [{'file': name, 'sha256': sha(release / name), 'bytes': (release / name).stat().st_size}
             for name in handoff]
    save(control / 'handoff-manifest.json', facts)
    for name in handoff:
        run_tool([scp, 'to-guest', release / name, guest.replace('\\', '/') + '/' + name],
                 control / ('transfer-' + name + '.log'))
    encoded = json.dumps(facts)
    expression = ("$ErrorActionPreference='Stop'; $p=" + ps_string(guest) +
                  '; $files=ConvertFrom-Json ' + ps_string(encoded) +
                  "; foreach($f in $files){$x=Join-Path $p $f.file; "
                  "if((Get-Item -LiteralPath $x).Length -ne $f.bytes -or "
                  "(Get-FileHash -Algorithm SHA256 -LiteralPath $x).Hash.ToLowerInvariant() -ne $f.sha256)"
                  "{throw 'Guest handoff mismatch'}}; "
                  "$bad=@(); foreach($f in $files){if($f.file.EndsWith('.ps1')){"
                  "$tokens=$null;$errors=$null;[void][Management.Automation.Language.Parser]::ParseFile("
                  "(Join-Path $p $f.file),[ref]$tokens,[ref]$errors);$bad+=@($errors)}}; "
                  "if($bad.Count){throw 'PowerShell syntax failure'}; Write-Output 'HANDOFF_VERIFIED'")
    run_tool([powershell, expression], control / 'guest-handoff-verification.log')
    try:
        for name in SCRIPTS:
            save(control / 'adapter-state.json', {'status': name, 'guest_workspace': guest, **entry})
            expression = ("$ErrorActionPreference='Stop'; & powershell.exe -NoLogo -NoProfile "
                          '-NonInteractive -ExecutionPolicy Bypass -File ' +
                          ps_string(guest + '\\' + name) + '; exit $LASTEXITCODE')
            run_tool([powershell, expression], control / (name + '.log'))
    finally:
        # Preserve failure evidence too. Retrieval failure does not remove the guest workspace.
        run_tool([scp, 'from-guest', guest.replace('\\', '/') + '/receipts', release, '--recursive'],
                 control / 'retrieve-receipts.log')
    art = read(release / 'receipts/artifact-manifest.json')
    stage = release / 'Release'
    stage.mkdir()
    for name in inventory(art['files'], 'file'):
        require('/' not in name, 'Nested artifact filename')
        if not name.lower().endswith('.pdb'):
            run_tool([scp, 'from-guest', guest.replace('\\', '/') + '/artifacts/Release/' + name, stage / name],
                     control / ('retrieve-' + name + '.log'))
    publisher, verification = verify_release(release, entry, manifest, inputs, require_publisher=False)
    save(release / 'windows-artifact-manifest.json', publisher)
    save(release / 'receipts/local-verification.json', verification)
    save(control / 'adapter-state.json', {'status': 'verified', 'guest_workspace': guest, **entry})
    return verification


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', required=True, type=Path, help='Immutable prepared source cohort')
    parser.add_argument('--config', required=True, type=Path, help='Local JSON with windows.baseline_release')
    parser.add_argument('--verify-only', action='store_true', help='Offline rehash; never access the guest or build')
    args = parser.parse_args(argv)
    root = args.root.resolve()
    config_path = args.config.resolve()
    config = read(config_path)['windows']
    baseline = Path(config['baseline_release']).expanduser()
    if not baseline.is_absolute():
        baseline = config_path.parent / baseline
    baseline = baseline.resolve()
    entry, manifest, archive = source_inputs(root)
    inputs = baseline_inputs(baseline)
    check_dependencies(manifest, baseline, inputs)
    derive_project_plan(archive, manifest, baseline, inputs)
    release = root / 'windows/release'
    if (root / 'windows').exists():
        try:
            _, result = verify_release(release, entry, manifest, inputs)
        except (ValueError, KeyError, OSError, struct.error, TypeError) as error:
            raise ValueError('Existing Windows namespace is invalid/partial; preserved. '
                             'Retry explicitly in a new cohort root. ' + str(error)) from error
        print(json.dumps({**result, 'action': 'verified-existing'}, indent=2))
        return 0
    require(not args.verify_only, 'No completed Windows cohort to verify')
    tools = Path(config.get('winboat_tools', '~/.codex/skills/winboat-ssh/scripts')).expanduser()
    if not tools.is_absolute():
        tools = config_path.parent / tools
    for name in ('winboat-powershell', 'winboat-scp'):
        require(os.access(tools / name, os.X_OK), f'Missing private WinBoat skill transport: {name}')
    result = fresh_build(root, entry, manifest, archive, inputs, tools)
    print(json.dumps({**result, 'action': 'built-and-verified'}, indent=2))
    return 0


if __name__ == '__main__':
    try:
        sys.exit(main())
    except (ValueError, KeyError, OSError, struct.error, TypeError, tarfile.TarError) as error:
        print(f'Windows adapter: {error}', file=sys.stderr)
        sys.exit(2)
