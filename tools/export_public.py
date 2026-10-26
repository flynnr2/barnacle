#!/usr/bin/env python3
"""Create a fresh source-only publication tree. Never exports Git history."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import zipfile

ROOT = Path(__file__).resolve().parents[1]
FILES = [
    'LICENSE', 'README.md', 'PUBLICATION.md', 'THIRD_PARTY_NOTICES.md', 'requirements.txt',
    'docs/PROFILES.md', 'docs/timing-validation-20261026.txt', 'docs/gnss-firmware-provenance.json',
    'config/profiles.json', 'config/dependencies.json',
    'docs/images/device-front.jpg', 'docs/images/device-rear.jpg', 'docs/images/device-antenna.jpg',
    'tools/node.py', 'tools/setup.sh', 'tools/check_profiles.sh', 'tools/export_public.py',
    'scripts/setup.sh', 'scripts/check_relay.sh', 'scripts/check_flash_layout.py', 'scripts/decode_relay.py',
    'firmware/Beacon/MeshTime.h', 'firmware/Beacon/Location.h',
    'firmware/Barnacle/Barnacle.h', 'firmware/Barnacle/RadioPolicy.h',
    'firmware/Wardrive/Wardrive.h', 'firmware/Wardrive/FlashJournal.h',
    'tests/relay.cpp', 'tests/profiles.cpp', 'tests/arduino_stub/Arduino.h',
    'tests/arduino_stub/Stream.h', 'tests/relay_stubs/LoRaWan_APP.h', 'tests/relay_stubs/sx126x.h',
]

def private_patterns():
    patterns = [str(Path.home()).encode()]
    legacy = ROOT / 'firmware/Beacon/identity.h'
    seeds = []
    if legacy.exists():
        values = re.findall(r'0x([0-9a-fA-F]{2})', legacy.read_text())
        if len(values) == 32:
            seeds.append(bytes(int(x, 16) for x in values))
    for path in (ROOT / '.secrets/devices').glob('*.json'):
        seeds.append(bytes.fromhex(json.loads(path.read_text())['seed_hex']))
    for seed in seeds:
        patterns.extend([seed, seed.hex().encode(), ','.join(f'0x{x:02x}' for x in seed).encode()])
    return patterns

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--destination', type=Path, required=True)
    args = parser.parse_args()
    out = args.destination.resolve()
    archive = out.with_suffix('.zip')
    if out == ROOT or out.exists() or archive.exists():
        parser.error('Destination and ZIP must be new paths')
    files = FILES + [str(p.relative_to(ROOT)) for p in sorted((ROOT / 'firmware/Relay').glob('*')) if p.suffix in ('.h', '.ino')]
    files += [str(p.relative_to(ROOT)) for p in sorted((ROOT / 'licenses').glob('*.txt'))]
    pending = {}
    for name in files:
        source = ROOT / name
        if source.is_symlink() or not source.is_file():
            raise ValueError('Expected an ordinary allowlisted source file: ' + name)
        data = source.read_bytes()
        if name == 'firmware/Relay/BarnacleIdentity.h':
            data = b'#pragma once\n#error "Use tools/node.py with an explicitly selected private device identity"\n'
        if name == 'tests/relay.cpp':
            # Use synthetic positions in release fixtures, without altering the
            # workshop regression tests or embedding their original positions.
            count = [0]
            def position(_match):
                count[0] += 1
                return f'barnacle::coarse(51.50168{count[0]},-0.10912{count[0]},1000'
            data = re.sub(r'barnacle::coarse\(-?\d+\.\d+,-?\d+\.\d+,1000', position, data.decode()).encode()
            # Precision-rejection assertions also contained the original fix
            # as integer microdegrees. Keep the guard with synthetic values.
            data = re.sub(rb'(?<!\d)\d{8}(?!\d)',
                          lambda m: b'51501681' if m.group().startswith(b'42') else
                          b'109121' if m.group().startswith(b'70') else m.group(), data)
        pending[name] = data
    pending['.gitignore'] = b'.arduino/\n.venv/\n.secrets/\nvendor/\nbuild/\nrelease/\nlogs/\nresearch/\narduino-cli.yaml\n__pycache__/\n*.pyc\n.DS_Store\n'
    patterns = private_patterns()
    for name, data in pending.items():
        if any(pattern and pattern in data for pattern in patterns):
            raise ValueError('Private key or local path detected in export candidate: ' + name)
    # Validation metadata omits identities, local paths, logs and binary hashes.
    validation = {}
    for name in json.loads((ROOT / 'config/profiles.json').read_text()):
        report = ROOT / 'build/profiles/validation' / name / 'build-report.json'
        if report.exists():
            r = json.loads(report.read_text())
            validation[name] = dict(compiled=True, flashed=False, ram_gap_bytes=r['ram_gap_bytes'],
                                    flash_margin_bytes=r['flash']['journal_start']-r['flash']['image_checksum_end'])
    pending['VALIDATION.json'] = (json.dumps(dict(profiles=validation, hardware_validation='One repeater-clock device has bounded timing/UART and 125 kHz RF bench evidence; see docs/timing-validation-20261026.txt. Public profile images remain unflashed; production 62.5 kHz RF reception is untested.', original_project_license='MIT'), indent=2)+'\n').encode()
    pending['SOURCE_MANIFEST.json'] = (json.dumps(dict(files={n:hashlib.sha256(d).hexdigest() for n,d in sorted(pending.items())}, git_history_included=False, captures_included=False, binaries_included=False, private_identities_included=False), indent=2)+'\n').encode()
    out.parent.mkdir(parents=True, exist_ok=True)
    out.mkdir()
    for name, data in pending.items():
        path = out / name; path.parent.mkdir(parents=True, exist_ok=True); path.write_bytes(data)
        if path.suffix == '.sh' or name == 'tools/node.py':
            path.chmod(0o755)
    with zipfile.ZipFile(archive, 'x', zipfile.ZIP_DEFLATED) as z:
        for name in sorted(pending):
            z.write(out/name, str(Path(out.name)/name))
    with zipfile.ZipFile(archive) as z:
        if z.testzip() is not None:
            raise ValueError('ZIP integrity check failed')
    print(json.dumps(dict(source_tree=str(out), zip=str(archive), source_files=len(pending), known_private_patterns_checked=len(patterns), profiles_compiled=list(validation), publication_license='MIT', published=False), indent=2))

if __name__ == '__main__':
    main()
