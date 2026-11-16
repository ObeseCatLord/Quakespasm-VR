#!/usr/bin/env python3
"""Publisher manifest merge proofs; no network, rclone or builds."""
import ast
from pathlib import Path
import re
import unittest


PUBLISHER = Path(__file__).resolve().parents[1] / 'Packaging/Linux/publish-r2.py'
WANTED = {'require', 'parse_manifest', 'format_row', 'check_destinations'}
CONSTANTS = {'RUNTIME_PREFIX', 'RUNTIMES'}


def load_helpers():
    """Execute only the manifest helpers, never the publisher CLI."""
    tree = ast.parse(PUBLISHER.read_text(), filename=str(PUBLISHER))
    nodes = [node for node in tree.body
             if (isinstance(node, ast.FunctionDef) and node.name in WANTED) or
             (isinstance(node, ast.Assign) and any(getattr(t, 'id', None) in CONSTANTS for t in node.targets))]
    namespace = {'re': re}
    exec(compile(ast.Module(nodes, []), str(PUBLISHER), 'exec'), namespace)
    return namespace


H = load_helpers()
TAG = H['RUNTIME_PREFIX']


def digest(n):
    return '%064x' % n


def row(n, source, destination, runtime=None):
    fields = [digest(n), 'file', source, destination]
    return (TAG + '\t'.join(fields + [runtime]) if runtime else '\t'.join(fields)) + '\n'


BUILD = ''.join([
    '# sha256\taction\tsource_path\tdestination_path\n',
    row(1, 'builds/r/linux-x64/run', 'run', 'linux-x64'),
    row(2, 'builds/r/linux-arm64/run', 'run', 'linux-arm64'),
    row(3, 'builds/r/win-x64/run.exe', 'run.exe', 'win-x64'),
])


class ManifestMerge(unittest.TestCase):
    def setUp(self):
        self.build = H['parse_manifest'](BUILD)

    def test_round_trip_keeps_old_clients_compatible(self):
        text = row(9, 'content/a.pak', 'id1/a.pak') + row(8, 'tool/arm/t', 't', 'linux-arm64')
        rows = H['parse_manifest'](text)
        self.assertEqual(''.join(H['format_row'](r) for r in rows), text)
        self.assertFalse(rows[0]['runtimes'])
        self.assertEqual(rows[1]['runtimes'], ['linux-arm64'])

    def test_untagged_runtime_column_is_rejected(self):
        with self.assertRaisesRegex(RuntimeError, 'older clients'):
            H['parse_manifest']('\t'.join([digest(1), 'file', 'a', 'a', 'linux-x64']) + '\n')

    def test_shared_content_and_arm_override_are_accepted(self):
        extras = H['parse_manifest'](row(9, 'content/a.pak', 'id1/a.pak') + row(7, 'tool/x64/t', 't') +
                                     row(8, 'tool/arm/t', 't', 'linux-arm64'))
        H['check_destinations'](extras, self.build)

    def test_untagged_extra_cannot_shadow_a_build_file(self):
        extras = H['parse_manifest'](row(9, 'old/run', 'run'))
        with self.assertRaisesRegex(RuntimeError, 'Two manifest rows install run'):
            H['check_destinations'](extras, self.build)

    def test_duplicate_tagged_rows_are_rejected(self):
        extras = H['parse_manifest'](row(9, 'other/run', 'RUN', 'linux-arm64'))
        with self.assertRaisesRegex(RuntimeError, 'for linux-arm64'):
            H['check_destinations'](extras, self.build)

    def test_x64_tagged_extra_cannot_override_untagged(self):
        extras = H['parse_manifest'](row(7, 'tool/any/t', 't') + row(8, 'tool/x64/t', 't', 'linux-x64'))
        with self.assertRaisesRegex(RuntimeError, 'for linux-x64'):
            H['check_destinations'](extras, self.build)


if __name__ == '__main__':
    unittest.main()
