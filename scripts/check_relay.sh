#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
umask 077
out="$PWD/build/relay-first/host"
crypto="$PWD/.arduino/user/libraries/Crypto/src"
ed="$PWD/vendor/MeshCore/lib/ed25519"
mkdir -p "$out"
cp firmware/Beacon/MeshTime.h firmware/Beacon/Location.h "$out/"
# Reference C crypto contains known signed-shift UB. Keep address checks on it,
# and full address/undefined checks on our C++ and the reference routing core.
for source in fe ge sc sha512 keypair sign key_exchange; do
  cc -O1 -g -fsanitize=address -ffunction-sections -fdata-sections \
    -c "$ed/$source.c" -o "$out/$source.o"
done
c++ -std=c++11 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer \
  -ffunction-sections -fdata-sections -Wl,-dead_strip -include cstdio \
  -Itests/relay_stubs -Itests/arduino_stub -Ivendor/MeshCore/src -Ifirmware/Relay -Ifirmware/Barnacle -Ifirmware/Wardrive \
  -I"$out" -I"$crypto" -I"$ed" \
  tests/relay.cpp vendor/MeshCore/src/Mesh.cpp vendor/MeshCore/src/Dispatcher.cpp \
  vendor/MeshCore/src/Identity.cpp vendor/MeshCore/src/Packet.cpp vendor/MeshCore/src/Utils.cpp \
  vendor/MeshCore/src/helpers/AdvertDataHelpers.cpp "$out"/*.o \
  "$crypto/Ed25519.cpp" "$crypto/Curve25519.cpp" "$crypto/BigNumberUtil.cpp" \
  "$crypto/SHA512.cpp" "$crypto/SHA256.cpp" "$crypto/Hash.cpp" "$crypto/Crypto.cpp" \
  "$crypto/AES128.cpp" "$crypto/AESCommon.cpp" "$crypto/BlockCipher.cpp" \
  -o "$out/check-relay"
ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 "$out/check-relay" "$out/synthetic-flash.bin"
.venv/bin/python scripts/decode_relay.py "$out/synthetic-flash.bin" > "$out/decoded-synthetic-flash.json"
.venv/bin/python - <<'PY'
import json
from pathlib import Path
r=json.loads(Path('build/relay-first/host/decoded-synthetic-flash.json').read_text())
assert len(r['invalid_or_torn_rows'])==1
assert len(r['events'])==218
assert all(e['last_hour']==42 for e in r['events'] if e['event']=='relay_state')
print('PASS offline decoder reads candidate-generated rows, retained claims and torn-row warning')
PY
