#!/usr/bin/env python3
"""Extracts the names the companion shows (characters, classes, items, terrain, chapter titles,
victory conditions) from your own Path of Radiance disc into text.json.

    python3 tools/extract_text.py --dolphin-tool path/to/dolphin-tool --disc GFEE01.rvz

It runs `dolphin-tool extract` for Mess/common.m and decodes the message table:

    0x00 u32 file size     0x04 u32 text data size     0x0C u32 entry count
    0x20 text data (cp1252, NUL-terminated)
    then `count` entries of (u32 text offset, u32 key offset), then the key strings (Shift-JIS)
"""
import argparse
import json
import os
import struct
import subprocess
import tempfile

KEEP_PREFIXES = ('MPID_', 'MJID_', 'MIID_', 'MT_', 'MCT', 'MW_', 'MSID_')


def parse_messages(data):
    _size, text_size, _unused, count = struct.unpack_from('>4I', data, 0)
    table = 0x20 + text_size
    keys = table + count * 8

    def cstr(offset, encoding):
        end = data.index(b'\0', offset)
        return data[offset:end].decode(encoding, 'replace')

    messages = {}
    for i in range(count):
        text, key = struct.unpack_from('>II', data, table + i * 8)
        messages[cstr(keys + key, 'cp932')] = cstr(0x20 + text, 'cp1252')
    return messages


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--dolphin-tool', required=True)
    parser.add_argument('--disc', required=True)
    parser.add_argument('--out', default=os.path.join(os.path.dirname(__file__), '..', 'text.json'))
    args = parser.parse_args()

    with tempfile.TemporaryDirectory() as tmp:
        subprocess.run([args.dolphin_tool, 'extract', '-i', args.disc, '-s', 'Mess/common.m', '-o', tmp, '-q'],
                       check=True)
        path = next(os.path.join(root, f) for root, _, files in os.walk(tmp) for f in files if f == 'common.m')
        with open(path, 'rb') as f:
            messages = parse_messages(f.read())

    kept = {k: v for k, v in sorted(messages.items()) if k.startswith(KEEP_PREFIXES) and '\n' not in v}
    with open(args.out, 'w', encoding='utf-8') as f:
        json.dump(kept, f, ensure_ascii=False, indent=0, sort_keys=True)
    print(f'wrote {len(kept)} of {len(messages)} messages to {args.out}')


if __name__ == '__main__':
    main()
