"""Check full-memory dump structure without loading game memory into the checker."""

import argparse
import json
from pathlib import Path
import struct


def validate_dump(path):
    path = Path(path)
    size = path.stat().st_size
    result = {'path': str(path), 'bytes': size, 'valid': False, 'error': None}
    try:
        with path.open('rb') as source:
            def read(offset, length):
                if offset < 0 or length < 0 or offset + length > size:
                    raise ValueError('Dump range exceeds file bounds')
                source.seek(offset)
                data = source.read(length)
                if len(data) != length:
                    raise ValueError('Dump changed or is truncated')
                return data

            header = read(0, 32)
            if header[:4] != b'MDMP':
                raise ValueError('Not a minidump')
            count, directory = struct.unpack_from('<II', header, 8)
            if not 1 <= count <= 256 or directory < 32:
                raise ValueError('Missing or invalid stream directory')
            streams = {}
            for index in range(count):
                kind, length, offset = struct.unpack('<III', read(directory + 12 * index, 12))
                if offset + length > size:
                    raise ValueError('Stream exceeds file bounds')
                if kind == 0:
                    continue
                if kind in streams:
                    raise ValueError('Duplicate stream type')
                streams[kind] = (offset, length)
            for kind, record_size, label in ((3, 48, 'threads'), (4, 108, 'modules')):
                if kind not in streams:
                    raise ValueError(f'Missing {label} stream')
                offset, length = streams[kind]
                if length < 4:
                    raise ValueError(f'Truncated {label} stream')
                records = struct.unpack('<I', read(offset, 4))[0]
                if not records or 4 + records * record_size > length:
                    raise ValueError(f'Empty or truncated {label} records')
                result[label] = records
            if 9 not in streams:
                raise ValueError('Missing full-memory stream')
            offset, length = streams[9]
            if length < 16:
                raise ValueError('Truncated full-memory descriptor')
            ranges, base = struct.unpack('<QQ', read(offset, 16))
            if not ranges or ranges > 1000000 or 16 + 16 * ranges > length:
                raise ValueError('Invalid full-memory range table')
            memory_bytes = 0
            for index in range(ranges):
                address, span = struct.unpack('<QQ', read(offset + 16 + index * 16, 16))
                if address + span > 1 << 64:
                    raise ValueError('Virtual memory range overflow')
                memory_bytes += span
            if not memory_bytes or base < 32 or base + memory_bytes > size:
                raise ValueError('Full-memory payload is empty or truncated')
            if path.stat().st_size != size:
                raise ValueError('Dump changed during validation')
            result.update(valid=True, memory_ranges=ranges, memory_bytes=memory_bytes)
    except (OSError, ValueError, struct.error) as error:
        result['error'] = str(error)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--run-root', type=Path, required=True)
    args = parser.parse_args()
    if not args.run_root.is_dir():
        parser.error('Run directory does not exist')
    results = [validate_dump(path) for path in sorted(args.run_root.glob('debugger.[0-3]/*.dmp'))]
    print(json.dumps({'dump_count': len(results), 'dumps': results,
                      'incomplete': any(not row['valid'] for row in results),
                      'limitation': 'Structural completeness only; not fault attribution or guaranteed exception context.'}, indent=2))


if __name__ == '__main__':
    main()
