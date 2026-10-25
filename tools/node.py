#!/usr/bin/env python3
"""Explicit device identities and compile-only profiles. Never opens USB."""
import argparse
import datetime as dt
import hashlib
import json
import os
from pathlib import Path
import re
import secrets
import shutil
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[1]
PROFILES = json.loads((ROOT / 'config/profiles.json').read_text())
DEPS = json.loads((ROOT / 'config/dependencies.json').read_text())

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def device_path(label):
    if not re.fullmatch(r'[a-z][a-z0-9-]{0,31}', label):
        raise ValueError('Device label: lowercase letters, numbers and hyphens, starting with a letter')
    return ROOT / '.secrets/devices' / (label + '.json')

def public_key(seed):
    from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey
    from cryptography.hazmat.primitives.serialization import Encoding, PublicFormat
    return Ed25519PrivateKey.from_private_bytes(seed).public_key().public_bytes(Encoding.Raw, PublicFormat.Raw).hex()

def load_device(label):
    value = json.loads(device_path(label).read_text())
    seed = bytes.fromhex(value['seed_hex'])
    if len(seed) != 32 or public_key(seed) != value['public_key']:
        raise ValueError('Invalid private device file or mismatched public key')
    if not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9 _.-]{0,17}', value['name']):
        raise ValueError('Node name: 1..18 ASCII letters, digits, spaces, underscore, dot or hyphen')
    return value, seed

def make_identity(args):
    path = device_path(args.device)
    if not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9 _.-]{0,17}', args.name):
        raise ValueError('Node name must be 1..18 safe ASCII characters')
    if getattr(args, 'from_header', None):
        text = args.from_header.read_text()
        match = re.search(r'IDENTITY_SEED\s*\[32\]\s*=\s*\{([^}]+)\}', text)
        if not match:
            raise ValueError('Input is not an IDENTITY_SEED[32] header')
        seed = bytes(int(x, 16) for x in re.findall(r'0x([0-9a-fA-F]{2})', match[1]))
        if len(seed) != 32:
            raise ValueError('Expected exactly 32 seed bytes in the input header')
    else:
        seed = secrets.token_bytes(32)
    value = dict(name=args.name, seed_hex=seed.hex(), public_key=public_key(seed),
                 created_utc=dt.datetime.now(dt.timezone.utc).isoformat())
    path.parent.mkdir(parents=True, exist_ok=True, mode=0o700)
    # Exclusive creation: never silently replace an existing node identity.
    with path.open('x') as f:
        json.dump(value, f, indent=2); f.write('\n')
    path.chmod(0o600)
    print(json.dumps(dict(device=args.device, name=args.name, public_key=value['public_key'],
                         private_file=str(path.relative_to(ROOT)), keep_private=True), indent=2))

def dependencies(_args):
    vendor = ROOT / 'vendor/MeshCore'
    if not vendor.exists():
        vendor.parent.mkdir(exist_ok=True)
        subprocess.run(['git', 'clone', '--no-checkout', DEPS['meshcore_url'], str(vendor)], check=True)
        subprocess.run(['git', '-C', str(vendor), 'checkout', '--detach', DEPS['meshcore_commit']], check=True)
    verify_dependency()
    print('Pinned MeshCore dependency verified')

def verify_dependency():
    vendor = ROOT / 'vendor/MeshCore'
    head = subprocess.check_output(['git', '-C', str(vendor), 'rev-parse', 'HEAD'], text=True).strip()
    dirty = subprocess.check_output(['git', '-C', str(vendor), 'status', '--porcelain'], text=True).strip()
    if head != DEPS['meshcore_commit'] or dirty:
        raise ValueError('MeshCore must be clean and at the pinned revision; existing checkouts are never reset automatically')
    return vendor

def build(args):
    device, seed = load_device(args.device)
    profile = PROFILES[args.profile]
    vendor = verify_dependency()
    # Device/profile outputs cannot overwrite the deployed legacy build images.
    out = ROOT / 'build/profiles' / args.device / args.profile
    out.mkdir(parents=True, exist_ok=True)
    sketch = out / 'Relay'
    if sketch.exists():
        shutil.rmtree(sketch)
    sketch.mkdir()
    inputs = sorted((ROOT / 'firmware/Relay').glob('*')) + [ROOT / p for p in (
        'firmware/Beacon/MeshTime.h', 'firmware/Beacon/Location.h',
        'firmware/Barnacle/Barnacle.h', 'firmware/Barnacle/RadioPolicy.h',
        'firmware/Wardrive/Wardrive.h', 'firmware/Wardrive/FlashJournal.h')]
    digest = hashlib.sha256()
    for p in inputs:
        if p.suffix not in ('.h', '.ino') or p.name in ('build_info.h', 'BarnacleIdentity.h'):
            continue
        digest.update(str(p.relative_to(ROOT)).encode() + p.read_bytes())
        shutil.copy2(p, sketch / p.name)
    digest.update(json.dumps(dict(profile=args.profile, name=device['name'], public_key=device['public_key'],
                                  dependencies=DEPS), sort_keys=True).encode())
    build_id = digest.hexdigest()[:12]
    epoch = int(os.environ.get('SOURCE_DATE_EPOCH', time.time()))
    (sketch / 'build_info.h').write_text(f'#pragma once\n#define BUILD_ID "{build_id}"\n#define BUILD_EPOCH {epoch}UL\n')
    (sketch / 'device_config.h').write_text('#pragma once\n#define CUBECELL_PROFILE ' + str(profile['id']) +
                                           '\n#define CUBECELL_NODE_NAME ' + json.dumps(device['name']) + '\n')
    (sketch / 'BarnacleIdentity.h').write_text('#pragma once\n#include "device_identity.h"\n')
    private = sketch / 'device_identity.h'
    private.write_text('#pragma once\nstatic const uint8_t IDENTITY_SEED[32] = {' +
                       ','.join(f'0x{x:02x}' for x in seed) + '};\n')
    private.chmod(0o600)
    units = {'MC_Mesh.cpp': 'src/Mesh.cpp', 'MC_Dispatcher.cpp': 'src/Dispatcher.cpp',
             'MC_Identity.cpp': 'src/Identity.cpp', 'MC_Packet.cpp': 'src/Packet.cpp',
             'MC_Utils.cpp': 'src/Utils.cpp', 'MC_Pool.cpp': 'src/helpers/StaticPoolPacketManager.cpp',
             'MC_Advert.cpp': 'src/helpers/AdvertDataHelpers.cpp'}
    for name in ('fe', 'ge', 'sc', 'sha512', 'key_exchange', 'keypair', 'sign'):
        units['MC_' + name + '.c'] = 'lib/ed25519/' + name + '.c'
    for name, source in units.items():
        prefix = '#include "' + str(ROOT / '.arduino/user/libraries/Crypto/src/AES.h') + '"\n' if name == 'MC_Utils.cpp' else ''
        (sketch / name).write_text(prefix + '#include "' + str(vendor / source) + '"\n')
    sdk = ROOT / '.arduino/data/packages/CubeCell/hardware/CubeCell' / DEPS['cubecell_core']
    ld = (sdk / 'cores/asr650x/projects/PSoC4/cm0plusgcc.ld').read_text()
    if ld.count('0x0800') != 3:
        raise ValueError('Unexpected linker layout; refusing to guess the stack reservation')
    linker = out / 'stack4k.ld'
    linker.write_text(ld.replace('0x0800', '0x1000'))
    recipe = next(s.split('=', 1)[1] for s in (sdk / 'platform.txt').read_text().splitlines()
                  if s.startswith('recipe.c.combine.flags.asr650x='))
    recipe = recipe.replace('"-T{compiler.sdk.path}/projects/PSoC4/cm0plusgcc.ld"', '"-T' + str(linker) + '"')
    if str(linker) not in recipe:
        raise ValueError('Could not select the checked linker script')
    flags = '-flto -fno-exceptions -fno-unwind-tables -fno-asynchronous-unwind-tables -DDISPLAY_REDUCE_MEMORY -DCUBECELL_DEVICE_CONFIG'
    flags += ' -I' + str(sketch) + ' -I' + str(vendor / 'src') + ' -I' + str(vendor / 'lib/ed25519')
    cmd = ['arduino-cli', '--config-file', str(ROOT / 'arduino-cli.yaml'), 'compile', '--fqbn', DEPS['fqbn'],
           '--build-property', 'compiler.cpp.extra_flags=' + flags, '--build-property', 'compiler.c.extra_flags=-flto',
           '--build-property', 'compiler.ldflags=-flto -Wl,-u,__wrap_snprintf,-u,__wrap_sprintf,-u,__wrap_printf,-u,__wrap_fflush',
           '--build-property', 'recipe.c.combine.flags.asr650x=' + recipe, '--build-path', str(out / 'target'), str(sketch)]
    with (out / 'compile.log').open('w') as log:
        subprocess.run(cmd, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, check=True)
    subprocess.run([sys.executable, str(ROOT / 'scripts/check_flash_layout.py'), '--build-dir', str(out / 'target'),
                    '--report', str(out / 'flash-layout.json')], check=True)
    sections = {}
    for m in re.finditer(r'^\.(text|rodata|data|bss|heap|stack)\s+0x([0-9a-f]+)\s+0x([0-9a-f]+)',
                         (out / 'target/Relay.ino.map').read_text(), re.M):
        sections[m[1]] = dict(address=int(m[2], 16), bytes=int(m[3], 16))
    gap = sections['stack']['address'] - sections['heap']['address'] - sections['heap']['bytes']
    if sections['stack']['bytes'] != 4096 or sections['heap']['bytes'] != 4096 or gap < 0:
        raise ValueError('RAM or stack reservation failed')
    image = out / 'target/CubeCell_GPS_REGION_US915_RGB_0.cyacd'
    report = dict(build=build_id, profile=args.profile, device=args.device, name=device['name'], public_key=device['public_key'],
                  build_epoch=epoch, image=str(image.relative_to(ROOT)), image_sha256=sha(image),
                  flash=json.loads((out / 'flash-layout.json').read_text()), ram_gap_bytes=gap,
                  sections=sections, dependencies=DEPS, device_accessed=False, flashed=False,
                  hardware_validation='Unflashed profile build; host/compile validation is not RF reception evidence')
    (out / 'build-report.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps({k: report[k] for k in ('build', 'profile', 'device', 'public_key', 'image', 'ram_gap_bytes', 'flashed')}, indent=2))

def main():
    os.umask(0o077)
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest='command', required=True)
    commands.add_parser('profiles').set_defaults(func=lambda _a: print(json.dumps(PROFILES, indent=2)))
    commands.add_parser('dependencies').set_defaults(func=dependencies)
    identities = commands.add_parser('identity').add_subparsers(dest='identity_command', required=True)
    create = identities.add_parser('create'); create.add_argument('--device', required=True); create.add_argument('--name', required=True); create.set_defaults(func=make_identity)
    migrate = identities.add_parser('import'); migrate.add_argument('--device', required=True); migrate.add_argument('--name', required=True); migrate.add_argument('--from-header', type=Path, required=True); migrate.set_defaults(func=make_identity)
    show = identities.add_parser('show'); show.add_argument('--device', required=True)
    show.set_defaults(func=lambda a: print(json.dumps({k: v for k, v in load_device(a.device)[0].items() if k != 'seed_hex'}, indent=2)))
    compile_cmd = commands.add_parser('build'); compile_cmd.add_argument('--profile', choices=PROFILES, required=True); compile_cmd.add_argument('--device', required=True); compile_cmd.set_defaults(func=build)
    args = parser.parse_args()
    try:
        args.func(args)
    except (ValueError, FileNotFoundError, FileExistsError, subprocess.CalledProcessError) as exc:
        # Never include a private device record in an error message.
        parser.exit(1, f'Error: {exc}\n')

if __name__ == '__main__':
    main()
