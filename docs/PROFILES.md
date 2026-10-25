# Firmware profiles and device identities

The hardware target is the Heltec HTCC-AB02S / CubeCell GPS: ASR6502, SX1262,
Air530 and 128×64 OLED. This is a device-specific project using a pinned
MeshCore routing core, not the stock full-featured repeater distribution.

| Profile          | Forwards mesh packets | Hourly UTC on #test | Signed advert role |
| ---------------- | --------------------- | ------------------- | ------------------ |
| repeater         | Yes                   | No                  | Repeater           |
| repeater-clock   | Yes                   | Yes                 | Repeater           |
| time-publisher   | No                    | Yes                 | Sensor             |

All three retain GNSS time, a single OLED health screen, sparse circular
logging, approximately 1 km location cells, and the existing journal layout.
The time publisher listens for diagnostics but does not relay other nodes'
traffic. It publishes ordinary channel text; it is not a precision timing
service or an implemented MeshCore sensor-query API.

The repeater profiles implement forwarding, duplicate suppression, supported
scope policy and queue handling. They do not implement stock remote admin,
Bluetooth companion connectivity, a room server, or an OTA management service.

## Identity is separate from role

A device file in `.secrets/devices/<label>.json` holds its name and Ed25519
seed. Create one identity per physical node. Reuse that device file when
switching its profile; the key and name stay the same. Do not run two physical
nodes with a copied private identity. The build never silently generates a new
identity, never reuses the installed Barnacle key automatically, and never
overwrites an existing device file.

```sh
.venv/bin/python tools/node.py profiles
.venv/bin/python tools/node.py identity create --device garden --name GardenNode
.venv/bin/python tools/node.py identity show --device garden
.venv/bin/python tools/node.py build --device garden --profile repeater-clock
```

The last two commands display only public metadata. Private files and compiled
firmware must remain private: the binary contains the device seed. Build
outputs are isolated under `build/profiles/<device>/<profile>/`, with an image
hash, build fingerprint, pinned dependency versions and flash/RAM checks in
`build-report.json`. These commands compile only and do not open a serial port
or flash hardware.

For the existing private workshop device, legacy build scripts still retain
their old identity path and default behavior. The profile builder is a separate,
explicit path for future builds. Historic Beacon, Barnacle and Wardrive sketches
remain available locally; they are not additional supported public profiles.

## Radio and scheduling

The available RF setting is fixed Boston: 910.525 MHz, BW 62.5 kHz, SF7,
CR 4/5, requested TX power 22 dBm, private sync 0x1424, 32 preamble symbols,
explicit header, CRC and normal IQ. There is no automatic US/UK switching.
Confirm a matching local network profile before operating this hardware.

Originated packets use two-byte paths. Forwarded packets retain the incoming
path width and append or consume the appropriate hop. A completed local send
does not establish that another node received it.

Hourly text is `<name>: YYYY-MM-DD HH:00 UTC`, with a holdover label when
appropriate. Slots are claimed durably before transmission, have a bounded
receive-aware service window, and are not replayed after reboot. A missed slot
is skipped. Repeater adverts normally use a 47-hour interval and fresh coarse
location; maps depend on observers receiving and uploading the advert.

GNSS qualification requires fresh fields, at least four satellites, HDOP at
most 3 and ten consistent distinct seconds. Later bounded clock jumps require
thirty seconds of consistency. The observed Air530 1024-week date rollover is
handled separately from UTC corrections. Initial position acquisition does not
guarantee immediate UTC accuracy; a startup offset was observed and later
corrected automatically. Packet stamps and saved hourly claims remain monotonic
when UTC corrects backwards.

## Storage and diagnostics

The 128 KB flash is shared with code. The journal is 110 × 256-byte rows
(28,160 bytes), not megabytes of track storage. A build is rejected if its image
or bootloader checksum crosses the journal boundary at 0x19000. The legacy
764-byte EEPROM layout remains readable and is not rewritten by these profiles.
Journal checkpoints legitimately rotate old onboard records. Download logs
before changing firmware; changing identity cannot preserve the meaning of
identity-bound counters automatically.

The 115200-baud console accepts read-only `status`, `screen`, `storage`,
`flash dump`, `trace on`, and `trace off`. A complete flash download ends with
`FLASH_END`. `scripts/decode_relay.py` decodes a saved dump offline. Opening a
serial port can reset this board; use one collector at a time. There is no
automatic uploader in the public build tool.
