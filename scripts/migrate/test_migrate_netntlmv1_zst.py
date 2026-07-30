#!/usr/bin/env python3
"""Tests for migrate_netntlmv1_zst.

Uses stdlib unittest (not pytest) so this suite runs on the migration hosts
with a bare python3, the same way the script itself does.

Run: python3 scripts/migrate/test_migrate_netntlmv1_zst.py
"""
import os
import shutil
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import migrate_netntlmv1_zst as m


class FakeResult(object):
    def __init__(self, returncode=0, stderr=''):
        self.returncode = returncode
        self.stderr = stderr


class FakeTools(object):
    """Stands in for crackalack_rtc2rt / crackalack_rt2zst.

    Models the real byte behaviour closely enough to exercise the round-trip
    check: 'rtc' payloads carry a marker that decompress expands and compress
    reverses, so a corrupted step produces a genuine byte mismatch.
    """

    def __init__(self, fail_on=None, corrupt_roundtrip=False):
        self.fail_on = fail_on or set()
        self.corrupt_roundtrip = corrupt_roundtrip
        self.calls = []

    def run(self, argv):
        binary = os.path.basename(argv[0])
        self.calls.append(argv)

        if binary == 'rtc2rt':
            if 'rtc2rt' in self.fail_on:
                return FakeResult(1, 'boom')
            src, dst = argv[1], argv[2]
            # The real binary exits nonzero on a missing source rather than
            # raising; this is exactly the production path that destroyed 32
            # outputs, so the double has to reproduce it faithfully.
            if not os.path.exists(src):
                return FakeResult(1, 'cannot open %s' % src)
            with open(src, 'rb') as f:
                data = f.read()
            with open(dst, 'wb') as f:
                f.write(data + b'-EXPANDED')
            return FakeResult()

        if binary == 'rt2zst':
            if argv[1] == '-d':
                if 'decompress' in self.fail_on:
                    return FakeResult(1, 'boom')
                src, dst = argv[2], argv[3]
                with open(src, 'rb') as f:
                    data = f.read()
                payload = data.replace(b'-ZSTD', b'')
                if self.corrupt_roundtrip:
                    payload += b'-TAMPERED'
                with open(dst, 'wb') as f:
                    f.write(payload)
                return FakeResult()
            if 'compress' in self.fail_on:
                return FakeResult(1, 'boom')
            src, dst = argv[3], argv[4]
            with open(src, 'rb') as f:
                data = f.read()
            with open(dst, 'wb') as f:
                f.write(data + b'-ZSTD')
            return FakeResult()

        raise AssertionError('unexpected binary: ' + binary)


class MigrateTestCase(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.mkdtemp(prefix='migrate-test-')
        self.base = os.path.join(self.tmp, 'src')
        self.dest = os.path.join(self.tmp, 'dst')
        self.scratch = os.path.join(self.tmp, 'scratch')
        os.makedirs(self.scratch)
        self.tools = FakeTools()

    def tearDown(self):
        shutil.rmtree(self.tmp, ignore_errors=True)

    def make_part(self, rel, payload=b'RTCDATA'):
        path = os.path.join(self.base, rel)
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, 'wb') as f:
            f.write(payload)
        return path

    def convert(self, rtc_path, zst_path=None, delete_source=True, tools=None):
        tools = tools or self.tools
        return m.convert_one_part(
            rtc_path, 'rtc2rt', 'rt2zst', self.scratch,
            zst_path=zst_path, delete_source=delete_source, runner=tools.run)


class TestDestZstPath(MigrateTestCase):
    def test_mirrors_relative_layout_under_dest(self):
        rtc = self.make_part('1001-2000/1500/part_0.rtc')
        got = m.dest_zst_path(rtc, self.base, self.dest)
        self.assertEqual(
            got, os.path.join(self.dest, '1001-2000/1500/part_0.rt.zst'))

    def test_handles_trailing_slash_and_relative_base(self):
        rtc = self.make_part('0-1000/7/part_1.rtc')
        got = m.dest_zst_path(rtc, self.base + '/', self.dest)
        self.assertEqual(
            got, os.path.join(self.dest, '0-1000/7/part_1.rt.zst'))

    def test_none_dest_means_in_place_sibling(self):
        rtc = self.make_part('0-1000/7/part_1.rtc')
        self.assertEqual(m.dest_zst_path(rtc, self.base, None),
                         rtc[:-4] + '.rt.zst')


class TestNasSourceMode(MigrateTestCase):
    """dest_dir set => write to dest tree, and NEVER delete the source."""

    def test_writes_output_to_dest_tree(self):
        rtc = self.make_part('1001-2000/1500/part_0.rtc')
        zst = m.dest_zst_path(rtc, self.base, self.dest)

        result = self.convert(rtc, zst_path=zst, delete_source=False)

        self.assertEqual(result['status'], 'done')
        self.assertTrue(os.path.exists(zst), 'output not written to dest tree')

    def test_creates_missing_dest_directories(self):
        rtc = self.make_part('2001-3000/2400/part_2.rtc')
        zst = m.dest_zst_path(rtc, self.base, self.dest)
        self.assertFalse(os.path.isdir(os.path.dirname(zst)))

        result = self.convert(rtc, zst_path=zst, delete_source=False)

        self.assertEqual(result['status'], 'done')
        self.assertTrue(os.path.exists(zst))

    def test_source_is_preserved(self):
        rtc = self.make_part('1001-2000/1500/part_0.rtc')
        zst = m.dest_zst_path(rtc, self.base, self.dest)

        self.convert(rtc, zst_path=zst, delete_source=False)

        self.assertTrue(os.path.exists(rtc),
                        'source .rtc was deleted -- this would destroy the NAS backup')

    def test_no_output_written_beside_the_source(self):
        rtc = self.make_part('1001-2000/1500/part_0.rtc')
        zst = m.dest_zst_path(rtc, self.base, self.dest)

        self.convert(rtc, zst_path=zst, delete_source=False)

        self.assertFalse(os.path.exists(rtc[:-4] + '.rt.zst'),
                         'wrote .rt.zst into the read-only source tree')

    def test_scratch_is_cleaned_up(self):
        rtc = self.make_part('1001-2000/1500/part_0.rtc')
        zst = m.dest_zst_path(rtc, self.base, self.dest)

        self.convert(rtc, zst_path=zst, delete_source=False)

        self.assertEqual(os.listdir(self.scratch), [])

    def test_failure_preserves_source_and_removes_partial_output(self):
        rtc = self.make_part('1001-2000/1500/part_0.rtc')
        zst = m.dest_zst_path(rtc, self.base, self.dest)
        tools = FakeTools(corrupt_roundtrip=True)

        result = self.convert(rtc, zst_path=zst, delete_source=False, tools=tools)

        self.assertEqual(result['status'], 'error')
        self.assertEqual(result['detail'], 'round-trip byte mismatch')
        self.assertTrue(os.path.exists(rtc), 'source deleted on failure')
        self.assertFalse(os.path.exists(zst), 'partial output left behind')
        self.assertEqual(os.listdir(self.scratch), [])

    def test_records_byte_counts(self):
        rtc = self.make_part('1001-2000/1500/part_0.rtc')
        zst = m.dest_zst_path(rtc, self.base, self.dest)

        result = self.convert(rtc, zst_path=zst, delete_source=False)

        self.assertEqual(result['orig_bytes'], os.path.getsize(rtc))
        self.assertEqual(result['new_bytes'], os.path.getsize(zst))


class TestFailureNeverDestroysAnotherWorkersOutput(MigrateTestCase):
    """Regression: fail() used to remove zst_path unconditionally.

    On 2026-07-30 two drivers ran against range 3001-4095. Worker A converted a
    part and deleted its .rtc; worker B, still holding that part in its file
    list, ran rtc2rt against the missing source, failed, and fail() deleted the
    good .rt.zst worker A had just written. 32 parts ended up with neither a
    .rtc nor a .rt.zst and had to be rebuilt from the NAS backup.

    An output that already existed when this invocation started belongs to
    someone else and must survive our failure.
    """

    def test_preexisting_output_survives_missing_source(self):
        rtc = os.path.join(self.base, '3001-4095/3802/part_0.rtc')
        os.makedirs(os.path.dirname(rtc))
        # The exact production shape: source already gone, output already there.
        zst = rtc[:-4] + '.rt.zst'
        with open(zst, 'wb') as f:
            f.write(b'GOOD-OUTPUT-FROM-ANOTHER-WORKER')

        result = self.convert(rtc, zst_path=zst, delete_source=True)

        self.assertEqual(result['status'], 'error')
        self.assertTrue(os.path.exists(zst),
                        'fail() destroyed an output it did not create')
        with open(zst, 'rb') as f:
            self.assertEqual(f.read(), b'GOOD-OUTPUT-FROM-ANOTHER-WORKER')

    def test_preexisting_output_survives_compress_failure(self):
        rtc = self.make_part('1001-2000/1500/part_0.rtc')
        zst = m.dest_zst_path(rtc, self.base, self.dest)
        os.makedirs(os.path.dirname(zst))
        with open(zst, 'wb') as f:
            f.write(b'PRE-EXISTING')
        tools = FakeTools(fail_on={'compress'})

        result = self.convert(rtc, zst_path=zst, delete_source=False, tools=tools)

        self.assertEqual(result['status'], 'error')
        self.assertTrue(os.path.exists(zst))
        with open(zst, 'rb') as f:
            self.assertEqual(f.read(), b'PRE-EXISTING')

    def test_output_created_by_this_call_is_still_removed_on_failure(self):
        """The cleanup we DO want must keep working."""
        rtc = self.make_part('1001-2000/1500/part_0.rtc')
        zst = m.dest_zst_path(rtc, self.base, self.dest)
        self.assertFalse(os.path.exists(zst))
        tools = FakeTools(corrupt_roundtrip=True)

        result = self.convert(rtc, zst_path=zst, delete_source=False, tools=tools)

        self.assertEqual(result['status'], 'error')
        self.assertFalse(os.path.exists(zst),
                         'our own partial output should be cleaned up')


class TestDriverLock(MigrateTestCase):
    """Two drivers on one range is what caused the loss; make it impossible."""

    def test_second_driver_on_same_manifest_refuses_to_start(self):
        self.make_part('1001-2000/1500/part_0.rtc')
        manifest = os.path.join(self.tmp, 'manifest.jsonl')

        with m.driver_lock(manifest):
            with self.assertRaises(SystemExit):
                with m.driver_lock(manifest):
                    pass

    def test_lock_is_released_after_use(self):
        manifest = os.path.join(self.tmp, 'manifest.jsonl')

        with m.driver_lock(manifest):
            pass
        # Must be re-acquirable; a stale lock would strand the campaign.
        with m.driver_lock(manifest):
            pass

    def test_lock_released_even_if_run_raises(self):
        manifest = os.path.join(self.tmp, 'manifest.jsonl')

        try:
            with m.driver_lock(manifest):
                raise RuntimeError('boom')
        except RuntimeError:
            pass

        with m.driver_lock(manifest):
            pass

    def test_different_manifests_do_not_block_each_other(self):
        a = os.path.join(self.tmp, 'a.jsonl')
        b = os.path.join(self.tmp, 'b.jsonl')

        with m.driver_lock(a):
            with m.driver_lock(b):
                pass


class TestLegacyInPlaceMode(MigrateTestCase):
    """dest_dir absent => unchanged behaviour, including deleting the source."""

    def test_writes_sibling_and_deletes_source(self):
        rtc = self.make_part('0-1000/5/part_0.rtc')

        result = self.convert(rtc, zst_path=None, delete_source=True)

        self.assertEqual(result['status'], 'done')
        self.assertTrue(os.path.exists(rtc[:-4] + '.rt.zst'))
        self.assertFalse(os.path.exists(rtc), 'legacy mode must delete the .rtc')

    def test_failure_keeps_source(self):
        rtc = self.make_part('0-1000/5/part_0.rtc')
        tools = FakeTools(fail_on={'compress'})

        result = self.convert(rtc, zst_path=None, delete_source=True, tools=tools)

        self.assertEqual(result['status'], 'error')
        self.assertTrue(os.path.exists(rtc))


class TestResumeSkipsCompletedOutputs(MigrateTestCase):
    """With an immutable source, an existing dest .rt.zst is the resume signal."""

    def test_pending_parts_excludes_parts_with_existing_output(self):
        done_rtc = self.make_part('1001-2000/1/part_0.rtc')
        todo_rtc = self.make_part('1001-2000/2/part_0.rtc')
        done_zst = m.dest_zst_path(done_rtc, self.base, self.dest)
        os.makedirs(os.path.dirname(done_zst))
        with open(done_zst, 'wb') as f:
            f.write(b'already-converted')

        pending = m.pending_parts([done_rtc, todo_rtc], {}, self.base, self.dest)

        self.assertEqual(pending, [todo_rtc])

    def test_zero_length_output_is_not_treated_as_done(self):
        rtc = self.make_part('1001-2000/1/part_0.rtc')
        zst = m.dest_zst_path(rtc, self.base, self.dest)
        os.makedirs(os.path.dirname(zst))
        open(zst, 'wb').close()

        pending = m.pending_parts([rtc], {}, self.base, self.dest)

        self.assertEqual(pending, [rtc])

    def test_manifest_done_still_skips(self):
        rtc = self.make_part('1001-2000/1/part_0.rtc')
        manifest = {rtc: {'status': 'done'}}

        pending = m.pending_parts([rtc], manifest, self.base, self.dest)

        self.assertEqual(pending, [])

    def test_manifest_error_does_not_skip(self):
        rtc = self.make_part('1001-2000/1/part_0.rtc')
        manifest = {rtc: {'status': 'error', 'detail': 'transient'}}

        pending = m.pending_parts([rtc], manifest, self.base, self.dest)

        self.assertEqual(pending, [rtc])


class TestCliWiring(MigrateTestCase):
    def test_dest_dir_implies_source_is_kept(self):
        rtc = self.make_part('1001-2000/1500/part_0.rtc')
        m.main(['--base-dir', self.base, '--dirs', '1001-2000',
                '--manifest', os.path.join(self.tmp, 'manifest.jsonl'),
                '--rtc2rt-bin', 'rtc2rt', '--rt2zst-bin', 'rt2zst',
                '--scratch-dir', self.scratch, '--dest-dir', self.dest,
                '--workers', '1'], runner=self.tools.run)

        self.assertTrue(os.path.exists(rtc), 'source deleted in --dest-dir mode')
        self.assertTrue(os.path.exists(
            m.dest_zst_path(rtc, self.base, self.dest)))

    def test_manifest_records_absolute_source_path(self):
        rtc = self.make_part('1001-2000/1500/part_0.rtc')
        manifest_path = os.path.join(self.tmp, 'manifest.jsonl')
        m.main(['--base-dir', self.base, '--dirs', '1001-2000',
                '--manifest', manifest_path,
                '--rtc2rt-bin', 'rtc2rt', '--rt2zst-bin', 'rt2zst',
                '--scratch-dir', self.scratch, '--dest-dir', self.dest,
                '--workers', '1'], runner=self.tools.run)

        entries = m.load_manifest(manifest_path)
        self.assertIn(rtc, entries)
        self.assertEqual(entries[rtc]['status'], 'done')

    def test_rerun_is_idempotent_and_does_no_work(self):
        self.make_part('1001-2000/1500/part_0.rtc')
        argv = ['--base-dir', self.base, '--dirs', '1001-2000',
                '--manifest', os.path.join(self.tmp, 'manifest.jsonl'),
                '--rtc2rt-bin', 'rtc2rt', '--rt2zst-bin', 'rt2zst',
                '--scratch-dir', self.scratch, '--dest-dir', self.dest,
                '--workers', '1']
        m.main(argv, runner=self.tools.run)
        calls_after_first = len(self.tools.calls)

        m.main(argv, runner=self.tools.run)

        self.assertEqual(len(self.tools.calls), calls_after_first,
                         'second run reprocessed already-converted parts')


if __name__ == '__main__':
    unittest.main(verbosity=2)
