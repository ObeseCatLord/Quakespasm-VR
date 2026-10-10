#!/usr/bin/env python3
"""Publisher transport retry proofs using in-memory responses; no network."""
import ast
import hashlib
import http.client
import io
from pathlib import Path
import ssl
import time
import unittest
from unittest import mock
import urllib.error
import urllib.request


PUBLISHER = Path(__file__).resolve().parents[1] / 'Packaging/Linux/publish-r2.py'
URL = 'https://publisher-fixture.invalid/object'
PAYLOAD = b'complete response after a transport retry\x00'


def load_helpers():
    """Execute only relevant imports/functions, never the publisher CLI."""
    tree = ast.parse(PUBLISHER.read_text(), filename=str(PUBLISHER))
    imports = {'hashlib', 'http.client', 'ssl', 'time',
               'urllib.error', 'urllib.request'}
    nodes = []
    for node in tree.body:
        if isinstance(node, ast.Import):
            aliases = [alias for alias in node.names if alias.name in imports]
            if aliases:
                nodes.append(ast.copy_location(ast.Import(names=aliases), node))
        elif isinstance(node, ast.FunctionDef) and node.name in {'public_sha256', 'require'}:
            nodes.append(node)
    namespace = {}
    module = ast.fix_missing_locations(ast.Module(body=nodes, type_ignores=[]))
    exec(compile(module, str(PUBLISHER), 'exec'), namespace)
    if 'public_sha256' not in namespace:
        raise AssertionError('Publisher public_sha256 helper is not present yet')
    return namespace, tree


class InterruptedResponse(io.RawIOBase):
    """Deliver bytes to the real hash implementation, then reset the socket."""
    def __init__(self, prefix, error):
        super().__init__()
        self.prefix = prefix
        self.error = error
        self.delivered = 0

    def readable(self):
        return True

    def readinto(self, buffer):
        if self.delivered == len(self.prefix):
            raise self.error
        data = self.prefix[self.delivered:self.delivered + len(buffer)]
        buffer[:len(data)] = data
        self.delivered += len(data)
        return len(data)


class PublisherTransportRetryTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.namespace, cls.tree = load_helpers()

    def setUp(self):
        self.opener = self.enterContext(mock.patch.object(urllib.request, 'urlopen'))
        self.sleep = self.enterContext(mock.patch.object(time, 'sleep'))
        self.file_digest = self.enterContext(
            mock.patch.object(hashlib, 'file_digest', wraps=hashlib.file_digest))
        self.helper = self.namespace['public_sha256']

    def assert_requests(self, count, timeout=17):
        self.assertEqual(self.opener.call_count, count)
        for call in self.opener.call_args_list:
            request, = call.args
            self.assertIsInstance(request, urllib.request.Request)
            self.assertEqual(request.full_url, URL)
            self.assertEqual(request.get_header('User-agent'), 'QuakeSpasmVR-Updater/2.0')
            self.assertEqual(call.kwargs, {'timeout': timeout})

    def test_success_preserves_request_and_hashes_once(self):
        response = io.BytesIO(PAYLOAD)
        self.opener.return_value = response
        self.assertEqual(self.helper(URL, 17), hashlib.sha256(PAYLOAD).hexdigest())
        self.assert_requests(1)
        self.sleep.assert_not_called()
        self.file_digest.assert_called_once_with(response, 'sha256')
        self.assertTrue(response.closed)

    def test_handshake_eof_then_success(self):
        response = io.BytesIO(PAYLOAD)
        self.opener.side_effect = [ssl.SSLEOFError('fixture handshake EOF'), response]
        self.assertEqual(self.helper(URL, 17), hashlib.sha256(PAYLOAD).hexdigest())
        self.assert_requests(2)
        self.sleep.assert_called_once_with(1)
        self.file_digest.assert_called_once_with(response, 'sha256')
        self.assertTrue(response.closed)

    def test_mid_stream_reset_discards_partial_hash(self):
        interrupted = InterruptedResponse(b'bytes from failed response', ConnectionResetError('fixture reset'))
        complete = io.BytesIO(PAYLOAD)
        self.opener.side_effect = [interrupted, complete]
        actual = self.helper(URL, 17)
        self.assertEqual(actual, hashlib.sha256(PAYLOAD).hexdigest())
        self.assertNotEqual(actual, hashlib.sha256(interrupted.prefix + PAYLOAD).hexdigest())
        self.assertEqual(interrupted.delivered, len(interrupted.prefix))
        self.assert_requests(2)
        self.sleep.assert_called_once_with(1)
        self.assertEqual(self.file_digest.call_args_list,
                         [mock.call(interrupted, 'sha256'), mock.call(complete, 'sha256')])
        self.assertTrue(interrupted.closed)
        self.assertTrue(complete.closed)

    def test_each_transient_exception_can_recover(self):
        errors = [ConnectionError('fixture connection'), TimeoutError('fixture timeout'),
                  ssl.SSLError('fixture SSL failure'),
                  urllib.error.URLError(ssl.SSLEOFError('fixture wrapped EOF')),
                  http.client.IncompleteRead(b'partial', 20),
                  http.client.RemoteDisconnected('fixture disconnect')]
        for error in errors:
            with self.subTest(error=type(error).__name__):
                self.opener.reset_mock()
                self.sleep.reset_mock()
                self.opener.side_effect = [error, io.BytesIO(PAYLOAD)]
                self.assertEqual(self.helper(URL, 17), hashlib.sha256(PAYLOAD).hexdigest())
                self.assert_requests(2)
                self.sleep.assert_called_once_with(1)

    def test_exhaustion_is_bounded_and_raises_final_error(self):
        errors = [TimeoutError('fixture attempt ' + str(n)) for n in range(4)]
        self.opener.side_effect = errors
        with self.assertRaises(TimeoutError) as raised:
            self.helper(URL, 17)
        self.assertIs(raised.exception, errors[-1])
        self.assert_requests(4)
        self.assertEqual(self.sleep.call_args_list, [mock.call(1), mock.call(2), mock.call(4)])
        self.file_digest.assert_not_called()

    def test_explicit_attempt_limit(self):
        errors = [ConnectionError('fixture first'), ConnectionError('fixture final')]
        self.opener.side_effect = errors
        with self.assertRaises(ConnectionError) as raised:
            self.helper(URL, 17, attempts=2)
        self.assertIs(raised.exception, errors[-1])
        self.assert_requests(2)
        self.sleep.assert_called_once_with(1)

    def test_certificates_and_http_errors_are_fatal(self):
        certificate = ssl.SSLCertVerificationError('fixture untrusted certificate')
        errors = [certificate, urllib.error.URLError(certificate)]
        errors.extend(urllib.error.HTTPError(URL, code, 'fixture HTTP error', {}, io.BytesIO())
                      for code in (403, 404, 503))
        for error in errors:
            if isinstance(error, urllib.error.HTTPError):
                self.addCleanup(error.close)
            with self.subTest(error=type(error).__name__, code=getattr(error, 'code', None)):
                self.opener.reset_mock()
                self.sleep.reset_mock()
                self.opener.side_effect = [error, io.BytesIO(PAYLOAD)]
                with self.assertRaises(type(error)) as raised:
                    self.helper(URL, 17)
                self.assertIs(raised.exception, error)
                self.assert_requests(1)
                self.sleep.assert_not_called()
        self.file_digest.assert_not_called()

    def test_wrong_hash_remains_fatal_in_publisher_without_retry(self):
        publish = next(node for node in self.tree.body
                       if isinstance(node, ast.FunctionDef) and node.name == 'publish')
        verification = None
        for node in ast.walk(publish):
            body = getattr(node, 'body', None)
            if not isinstance(body, list):
                continue
            for assignment, check in zip(body, body[1:]):
                if (isinstance(assignment, ast.Assign)
                        and isinstance(assignment.value, ast.Call)
                        and isinstance(assignment.value.func, ast.Name)
                        and assignment.value.func.id == 'public_sha256'
                        and any(isinstance(target, ast.Name) and target.id == 'actual'
                                for target in assignment.targets)
                        and isinstance(check, ast.Expr)
                        and isinstance(check.value, ast.Call)
                        and isinstance(check.value.func, ast.Name)
                        and check.value.func.id == 'require'
                        and any(isinstance(part, ast.Name) and part.id == 'digest'
                                for part in ast.walk(check))):
                    verification = [assignment, check]
                    break
            if verification:
                break
        self.assertIsNotNone(verification, 'Expected publisher hash comparison outside public_sha256')
        response = io.BytesIO(PAYLOAD)
        self.opener.return_value = response
        namespace = dict(self.namespace, url=URL, digest=hashlib.sha256(b'expected bytes').hexdigest(),
                         name='fixture-object')
        module = ast.Module(body=verification, type_ignores=[])
        with self.assertRaisesRegex(RuntimeError, 'Public object mismatch'):
            exec(compile(module, str(PUBLISHER), 'exec'), namespace)
        self.assertEqual(namespace['actual'], hashlib.sha256(PAYLOAD).hexdigest())
        self.assert_requests(1, timeout=120)
        self.sleep.assert_not_called()
        self.file_digest.assert_called_once_with(response, 'sha256')


if __name__ == '__main__':
    unittest.main(verbosity=2)
