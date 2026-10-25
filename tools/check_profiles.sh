#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
umask 077
# Shared host objects contain no device identities and exercise the default role.
sh scripts/check_relay.sh
out="$PWD/build/relay-first/host"
crypto="$PWD/.arduino/user/libraries/Crypto/src"
ed="$PWD/vendor/MeshCore/lib/ed25519"
for profile in 1 2 3; do
  c++ -std=c++11 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer \
    -ffunction-sections -fdata-sections -Wl,-dead_strip -include cstdio \
    -DCUBECELL_PROFILE="$profile" '-DCUBECELL_NODE_NAME="TestNode"' \
    -Itests/relay_stubs -Itests/arduino_stub -Ivendor/MeshCore/src -Ifirmware/Relay \
    -Ifirmware/Barnacle -Ifirmware/Wardrive -I"$out" -I"$crypto" -I"$ed" \
    tests/profiles.cpp vendor/MeshCore/src/Mesh.cpp vendor/MeshCore/src/Dispatcher.cpp \
    vendor/MeshCore/src/Identity.cpp vendor/MeshCore/src/Packet.cpp vendor/MeshCore/src/Utils.cpp \
    vendor/MeshCore/src/helpers/AdvertDataHelpers.cpp "$out"/*.o \
    "$crypto/Ed25519.cpp" "$crypto/Curve25519.cpp" "$crypto/BigNumberUtil.cpp" \
    "$crypto/SHA512.cpp" "$crypto/SHA256.cpp" "$crypto/Hash.cpp" "$crypto/Crypto.cpp" \
    "$crypto/AES128.cpp" "$crypto/AESCommon.cpp" "$crypto/BlockCipher.cpp" \
    -o "$out/check-profile-$profile"
  ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 "$out/check-profile-$profile"
done
