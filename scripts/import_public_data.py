"""Reproduce and verify an unmodified Intel Lab sensor excerpt from MIT's archive."""
import argparse
import csv
from datetime import datetime, timezone
import gzip
import hashlib
import io
import json
import math
from pathlib import Path

ARCHIVE_SHA256 = 'd99288c8f406ca6604d359ceaa0d8adfffa79e7095061a1e27dc4399f48c7225'
HEADER = ['deviceId', 'sequence', 'timestampMs', 'temperature', 'humidity', 'voltage']
ROOT = Path(__file__).resolve().parents[1]

def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()

def parse(line):
    fields = line.split()
    if len(fields) != 8 or fields[3] != '1':
        raise ValueError('Not a complete mote-1 record')
    # The publisher supplies a local clock without a timezone. UTC here is an
    # encoding convention, not a claim about the original measurement timezone.
    clock = datetime.fromisoformat(fields[0] + 'T' + fields[1]).replace(tzinfo=timezone.utc)
    delta = clock - datetime(1970, 1, 1, tzinfo=timezone.utc)
    timestamp = (delta.days * 86400 + delta.seconds) * 1000 + delta.microseconds // 1000
    values = [float(fields[i]) for i in (4, 5, 7)]
    if not all(map(math.isfinite, values)) or not (-100 <= values[0] <= 200 and 0 <= values[1] <= 100 and 0 <= values[2] <= 1000):
        raise ValueError('Outside the workbench measurement contract')
    int(fields[2])
    return timestamp, fields

def encode(lines):
    stream = io.StringIO(newline='')
    writer = csv.writer(stream, lineterminator='\n')
    writer.writerow(HEADER)
    for sequence, line in enumerate(lines, 1):
        timestamp, fields = parse(line)
        writer.writerow(['intel-lab-mote1', sequence, timestamp, fields[4], fields[5], fields[7]])
    return stream.getvalue().encode('utf-8')

def verify(folder):
    metadata = json.loads((folder / 'provenance.json').read_text(encoding='utf-8'))
    assert metadata['archiveSha256'] == ARCHIVE_SHA256
    lines = (folder / 'intel-lab-mote1.source.txt').read_text(encoding='ascii').splitlines(keepends=True)
    assert len(lines) == metadata['rows'] == 4096
    clocks = [parse(line)[0] for line in lines]
    assert clocks == sorted(clocks)
    assert (folder / 'intel-lab-mote1.csv').read_bytes() == encode(lines)
    for name, sha in metadata['files'].items():
        assert digest(folder / name) == sha, name
    print(f'PASS: {len(lines)} public rows match their original temperature/humidity/voltage and recorded clock.')

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--archive', type=Path, help='MIT data.txt.gz; pinned SHA256 required')
    parser.add_argument('--output', type=Path, default=ROOT / 'data')
    args = parser.parse_args()
    if args.archive:
        assert digest(args.archive) == ARCHIVE_SHA256, 'Archive differs from the reviewed MIT download'
        selected = []
        stats = {'totalSourceRows': 0, 'mote1SourceRows': 0, 'mote1RejectedRows': 0}
        # Read the complete archive so gzip CRC/truncation is checked as well.
        with gzip.open(args.archive, 'rt', encoding='ascii', newline='') as source:
            for index, line in enumerate(source):
                stats['totalSourceRows'] += 1
                fields = line.split()
                if len(fields) < 4 or fields[3] != '1':
                    continue
                stats['mote1SourceRows'] += 1
                try:
                    timestamp, _ = parse(line)
                except (ValueError, OverflowError):
                    stats['mote1RejectedRows'] += 1
                    continue
                selected.append((timestamp, index, line))
        selected.sort(key=lambda item: (item[0], item[1]))
        lines = [item[2] for item in selected[:4096]]
        assert len(lines) == 4096
        args.output.mkdir(parents=True, exist_ok=True)
        (args.output / 'intel-lab-mote1.source.txt').write_bytes(''.join(lines).encode('ascii'))
        (args.output / 'intel-lab-mote1.csv').write_bytes(encode(lines))
        metadata = {
            'title': 'Intel Berkeley Research Lab sensor data',
            'publisherPage': 'http://db.csail.mit.edu/labdata/labdata.html',
            'downloadUrl': 'http://db.csail.mit.edu/labdata/data.txt.gz',
            'archiveSha256': ARCHIVE_SHA256,
            'archiveBytes': args.archive.stat().st_size,
            'retrievedDate': '2026-10-06',
            'kind': 'public measured records replayed by a simulated TCP device',
            'selection': 'First 4096 chronological complete, finite, in-contract readings of mote 1; raw numeric values retained; no interpolation or clipping',
            'timestampConvention': 'Original timezone unspecified; encode naive source clock as UTC milliseconds, truncate sub-millisecond precision; display UTC to retain source clock',
            'sequenceConvention': 'Transport sequence numbers are new counters; source epochs remain in source.txt',
            'rows': len(lines),
            'firstSourceClock': ' '.join(lines[0].split()[:2]),
            'lastSourceClock': ' '.join(lines[-1].split()[:2]),
            'units': {'temperature': 'degrees Celsius', 'humidity': 'relative humidity percent', 'voltage': 'volts'},
            'statistics': stats,
            'attribution': ['Peter Bodik', 'Wei Hong', 'Carlos Guestrin', 'Sam Madden', 'Mark Paskin', 'Romain Thibaux'],
            'rights': 'Publisher permits use and reproduction; acknowledge original contributors. Data is separate from project MIT code license.',
            'files': {name: digest(args.output / name) for name in ['intel-lab-mote1.csv', 'intel-lab-mote1.source.txt']},
        }
        (args.output / 'provenance.json').write_bytes((json.dumps(metadata, indent=2) + '\n').encode('utf-8'))
    verify(args.output)

if __name__ == '__main__':
    main()
