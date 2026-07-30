#!/usr/bin/env python3
"""Migrates NetNTLMv1 .rtc tables to .rt.zst.

For each .rtc part: decompress to raw .rt, recompress to .rt.zst, verify the
round trip is byte-identical, then clean up the scratch files. Progress is
tracked in a JSONL manifest so an interrupted run can resume without
reprocessing finished parts.

Two modes:

  in-place (no --dest-dir)
      The .rt.zst is written beside the .rtc and the .rtc is DELETED once the
      round-trip check passes. This is the original behaviour, used when the
      source tree is the working copy being converted.

  separate destination (--dest-dir)
      The .rt.zst is written under --dest-dir, mirroring the source's layout
      relative to --base-dir, and the source .rtc is NEVER deleted. Use this
      when --base-dir is a read-only second copy (e.g. the NAS): the sources
      stay intact as a backup and the destination host only ever receives
      output. Because the source is immutable in this mode, an existing
      non-empty .rt.zst in the destination is what marks a part as already
      done, alongside the manifest.
"""
import argparse
import filecmp
import hashlib
import json
import multiprocessing
import os
import subprocess
import sys


def load_manifest(manifest_path):
    """Reads a JSONL manifest, returns {part_path: entry_dict}."""
    entries = {}
    if not os.path.exists(manifest_path):
        return entries
    with open(manifest_path, 'r') as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            entry = json.loads(line)
            entries[entry['part']] = entry
    return entries


def append_manifest(manifest_path, entry):
    """Appends one JSON-encoded line to the manifest file."""
    with open(manifest_path, 'a') as f:
        f.write(json.dumps(entry) + '\n')


def discover_parts(base_dir, dir_names):
    """Returns a sorted list of absolute paths to every *.rtc file
    found under base_dir/<name> for each name in dir_names."""
    parts = []
    for name in dir_names:
        top = os.path.join(base_dir, name)
        if not os.path.isdir(top):
            continue
        for root, _dirs, files in os.walk(top):
            for fn in files:
                if fn.endswith('.rtc'):
                    parts.append(os.path.abspath(os.path.join(root, fn)))
    return sorted(parts)


def dest_zst_path(rtc_path, base_dir, dest_dir):
    """Returns the .rt.zst output path for one .rtc part.

    With dest_dir None the output is a sibling of the .rtc. Otherwise the
    source's path relative to base_dir is mirrored under dest_dir, so a
    read-only source tree (the NAS) maps onto a separate output tree.
    """
    if dest_dir is None:
        return rtc_path[:-4] + '.rt.zst'
    rel = os.path.relpath(rtc_path, os.path.abspath(base_dir))
    return os.path.join(os.path.abspath(dest_dir), rel[:-4] + '.rt.zst')


def pending_parts(all_parts, manifest, base_dir, dest_dir):
    """Filters all_parts down to the ones still needing conversion.

    A part is done if the manifest says so, or if its output already exists
    and is non-empty. The second check matters because in --dest-dir mode the
    source .rtc is never deleted, so the source tree cannot indicate progress
    on its own -- and a zero-length output means an interrupted write, not a
    finished part.
    """
    todo = []
    for part in all_parts:
        if manifest.get(part, {}).get('status') == 'done':
            continue
        out = dest_zst_path(part, base_dir, dest_dir)
        if os.path.exists(out) and os.path.getsize(out) > 0:
            continue
        todo.append(part)
    return todo


def convert_one_part(rtc_path, rtc2rt_bin, rt2zst_bin, scratch_dir, zst_path=None,
                     delete_source=True, zst_level=19, runner=subprocess.run):
    """Runs rtc -> rt -> zst -> round-trip-check for one part.

    zst_path defaults to a sibling of the .rtc. delete_source removes the
    source .rtc after a verified round trip; pass False when the source tree
    is a backup that must survive.
    """
    base = os.path.splitext(os.path.basename(rtc_path))[0]
    # scratch_rt lives under scratch_dir (not beside the .rtc), so the large
    # transient decompressed .rt can be written to fast local scratch storage
    # even when the .rtc tree itself is on slower/remote storage (e.g. NFS).
    # Part basenames repeat identically across many source subdirectories, so
    # a hash of the full rtc_path is mixed in to keep scratch filenames unique
    # across the whole tree (avoids collisions between parallel workers).
    path_hash = hashlib.md5(rtc_path.encode()).hexdigest()[:12]
    scratch_rt = os.path.join(scratch_dir, base + '.' + path_hash + '.rt')
    scratch_check = os.path.join(scratch_dir, base + '.' + path_hash + '.rt.check')
    if zst_path is None:
        zst_path = rtc_path[:-4] + '.rt.zst'  # sibling of the .rtc, same directory
    # The destination tree mirrors the source layout, so the part's directory
    # may not exist yet on the first part landing in it.
    os.makedirs(os.path.dirname(zst_path), exist_ok=True)

    def fail(detail):
        for p in (scratch_rt, scratch_check):
            if os.path.exists(p):
                os.remove(p)
        if os.path.exists(zst_path):
            os.remove(zst_path)
        return {'part': rtc_path, 'status': 'error', 'detail': detail}

    r = runner([rtc2rt_bin, rtc_path, scratch_rt])
    if r.returncode != 0:
        return fail('rtc2rt failed: ' + str(r.stderr))

    r = runner([rt2zst_bin, '-l', str(zst_level), scratch_rt, zst_path])
    if r.returncode != 0:
        return fail('rt2zst compress failed: ' + str(r.stderr))

    r = runner([rt2zst_bin, '-d', zst_path, scratch_check])
    if r.returncode != 0:
        return fail('rt2zst decompress-check failed: ' + str(r.stderr))

    if not filecmp.cmp(scratch_rt, scratch_check, shallow=False):
        return fail('round-trip byte mismatch')

    orig_bytes = os.path.getsize(rtc_path)
    new_bytes = os.path.getsize(zst_path)
    os.remove(scratch_rt)
    os.remove(scratch_check)
    if delete_source:
        os.remove(rtc_path)
    return {'part': rtc_path, 'status': 'done', 'orig_bytes': orig_bytes, 'new_bytes': new_bytes}


def _convert_worker(args):
    rtc_path, rtc2rt_bin, rt2zst_bin, scratch_dir, zst_path, delete_source, zst_level = args
    return convert_one_part(rtc_path, rtc2rt_bin, rt2zst_bin, scratch_dir,
                            zst_path=zst_path, delete_source=delete_source,
                            zst_level=zst_level)


def main(argv=None, runner=subprocess.run):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--base-dir', required=True, help='e.g. /mnt/nvme/rtc/NetNTLMv1')
    parser.add_argument('--dirs', required=True, help='comma-separated top-level dir names, e.g. 0-1000,1001-2000')
    parser.add_argument('--workers', type=int, default=1)
    parser.add_argument('--manifest', required=True)
    parser.add_argument('--rtc2rt-bin', required=True)
    parser.add_argument('--rt2zst-bin', required=True)
    parser.add_argument('--scratch-dir', required=True)
    parser.add_argument('--zst-level', type=int, default=19)
    parser.add_argument('--dest-dir', default=None,
                        help='write .rt.zst under this root, mirroring the layout '
                             'relative to --base-dir, and never delete the source '
                             '.rtc. Use when --base-dir is a read-only copy.')
    args = parser.parse_args(argv)

    os.makedirs(args.scratch_dir, exist_ok=True)
    dir_names = args.dirs.split(',')
    all_parts = discover_parts(args.base_dir, dir_names)
    done = load_manifest(args.manifest)
    todo = pending_parts(all_parts, done, args.base_dir, args.dest_dir)

    # Deleting the source is only safe when it IS the working copy.
    delete_source = args.dest_dir is None
    print('Discovered %d parts, %d already done, %d to convert. Source .rtc will be %s.'
          % (len(all_parts), len(all_parts) - len(todo), len(todo),
             'DELETED after conversion' if delete_source else 'kept'))

    work = [(p, args.rtc2rt_bin, args.rt2zst_bin, args.scratch_dir,
             dest_zst_path(p, args.base_dir, args.dest_dir), delete_source,
             args.zst_level)
            for p in todo]

    def record(result):
        append_manifest(args.manifest, result)
        print('%s: %s' % (result['status'].upper(), result['part']))

    # A single worker runs in-process: it keeps an injected runner usable
    # (a test double need not be picklable) and makes tracebacks readable.
    if args.workers == 1:
        for item in work:
            rtc_path, rtc2rt_bin, rt2zst_bin, scratch_dir, zst_path, del_src, level = item
            record(convert_one_part(rtc_path, rtc2rt_bin, rt2zst_bin, scratch_dir,
                                    zst_path=zst_path, delete_source=del_src,
                                    zst_level=level, runner=runner))
        return

    with multiprocessing.Pool(processes=args.workers) as pool:
        for result in pool.imap_unordered(_convert_worker, work):
            record(result)


if __name__ == '__main__':
    main(sys.argv[1:])
