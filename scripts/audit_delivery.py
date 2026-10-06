"""Validate delivered real PE binaries, module dependencies and SHA256 manifest."""
import argparse
import hashlib
import json
from pathlib import Path
import struct

parser = argparse.ArgumentParser()
parser.add_argument('--root', type=Path, required=True)
args = parser.parse_args()
root = args.root.resolve()
manifest = json.loads((root / 'manifest.json').read_text(encoding='utf-8-sig'))
expected_programs = {'device-workbench', 'device-simulator', 'result-receiver'}
expected_modules = {'contracts', 'wire_protocol', 'device_simulator', 'device_source', 'frontend', 'record_store', 'result_push', 'result_receiver'}
assert set(manifest['programs']) == expected_programs
assert set(manifest['modules']) == expected_modules

def check_pe(path, is_dll):
    data = path.read_bytes()
    assert len(data) > 4096 and data[:2] == b'MZ', f'Not real Windows binary: {path}'
    offset = struct.unpack_from('<I', data, 0x3c)[0]
    assert data[offset:offset+4] == b'PE\0\0', path
    assert struct.unpack_from('<H', data, offset+4)[0] == 0x8664, f'Not x64: {path}'
    characteristics = struct.unpack_from('<H', data, offset+22)[0]
    assert bool(characteristics & 0x2000) == is_dll, f'Wrong executable/library type: {path}'

for program in expected_programs:
    folder = root / 'programs' / program
    check_pe(folder / (program+'.exe'), False)
    for library in ('wb_contracts.dll', 'Qt6Core.dll', 'Qt6Gui.dll', 'Qt6Widgets.dll', 'Qt6Network.dll', 'vcruntime140.dll', 'msvcp140.dll'):
        check_pe(folder / library, True)
    assert (folder / 'platforms/qwindows.dll').is_file(), program
    assert (folder / 'platforms/qoffscreen.dll').is_file(), program

for module in expected_modules:
    folder = root / 'modules' / module
    info = json.loads((folder / 'module.json').read_text(encoding='utf-8-sig'))
    assert info['module'] == module and info['version'] == manifest['version']
    check_pe(folder / info['binary'], True)
    assert (folder / info['library']).read_bytes().startswith(b'!<arch>\n'), module
    assert (folder / info['header']).is_file(), module
    for dependency in info['dependencies']:
        assert (root / 'shared/bin' / ('wb_'+dependency+'.dll')).is_file(), (module, dependency)

for item in manifest['files']:
    path = (root / item['path']).resolve()
    assert path.is_relative_to(root), item
    data = path.read_bytes()
    assert len(data) == item['size'], item['path']
    assert hashlib.sha256(data).hexdigest() == item['sha256'], item['path']

assert json.loads((root / 'test-evidence/binary-only-summary.json').read_text(encoding='utf-8'))['passed']
assert json.loads((root / 'test-evidence/dll-consumer.json').read_text(encoding='utf-8-sig'))['modulesLoaded'] == 7
import subprocess
import sys
subprocess.run([sys.executable, str(Path(__file__).with_name('import_public_data.py')), '--output', str(root / 'programs/device-simulator/data')], check=True)
for license in ('LGPL-3.0-only.txt', 'GPL-3.0-only.txt', 'Qt-GPL-exception-1.0.txt'):
    assert (root / 'licenses' / license).stat().st_size > 500
print(f'PASS: {len(expected_programs)} real EXEs, {len(expected_modules)} module DLLs/import libraries, runtime dependencies, binary-only tests, and {len(manifest["files"])} SHA256 checks.')
