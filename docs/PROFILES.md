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

## UART buffering, flash scheduling and PPS timestamps

Checkpoints wait for a fresh, regular PPS rise, an empty GNSS receive buffer,
at least 1 ms of UART silence and an idle radio. The window closes about
1.95 ms after the rise. A missed window defers the write without declaring a
storage failure. A packet waiting for its durable transmission claim stays
outside the outbound queue so forwarding can continue. Failed writes never
authorize transmission and restore the prior in-memory claim state.

Without PPS, optional logging and originated packets wait; mesh forwarding
continues. An hourly packet that misses its service window is skipped.

The custom GNSS ISR and reader use a 1,023-byte receive buffer, enlarged once
from the SDK's 255-byte usable capacity. If allocation fails, the original
buffer remains in use; check `uart_capacity` in `status`. Signing an advert
still occupies the main loop for about one second. The larger buffer covered
the measured NMEA burst, but does not make signing nonblocking or guarantee
lossless reception of a dense RF burst.

`TIMING` reports PPS period and high time, flagged delayed edges, UART hardware
overruns, discarded bytes, UART errors, buffer capacity/peak, flash duration,
longest loop interval and deferred-write count. Tick duration fields use the
nominal 32,768 Hz MCU RTC. PPS timestamps anchor a free-running counter at the
next raw RTC tick; they avoid the SDK `micros()` epoch inconsistency. They are
ISR-entry timestamps with uncalibrated latency, not hardware edge captures or
calibrated UTC phase measurements. `RFSTAT` exposes the SX1262 packet, CRC and
header-error counters.

The normal builder keeps the production 62.5 kHz PHY and read-only console.
The internal `RELAY_BENCH` compile flag selects a 125 kHz test PHY and enables
serial-requested flash/signing stress tests; do not use it for normal service.
See [the bounded hardware validation](timing-validation-20261026.txt), which
distinguishes the bench PHY, the production run and remaining limitations.

## Optional Air530 GNSS firmware investigation

The GNSS firmware is separate from this relay application and from CubeCell
support libraries. Flashing the relay does not update the Air530.

An Ebyte E108/GK9501 update linked by [QRP Labs](https://qrp-labs.com/qmxp/e108fix.html)
was tested on one HTCC-AB02S Air530. Its reported version changed from
`GOKE9501_1.4_18022400` to `GK9501_3.0_Dec 31 2024`. In the immediately preceding
old-firmware sample, valid raw dates required a 1,024-week correction. In the
updated 20-minute steady GPS+BeiDou capture, all 1,199 RMC fixes were valid,
all raw dates were correct without that adjustment, 13,495 NMEA sentences
passed their checksums, and 1,199 PPS rising edges were counted. Acquisition,
reset and PPS-control transitions had separate gaps. These observations
establish bounded operation on that device, not approval for every Air530
revision or guaranteed long-term availability.

The unmodified download contains `7402-0-11.bin` (581,644 bytes):

```text
SHA-256 335fa544459eab47488c2210aae6b0f6329a18b6b578ed13e4cfebf6eb799d08
```

Package provenance and the ZIP checksum are recorded in
[gnss-firmware-provenance.json](gnss-firmware-provenance.json). Obtain the
package through the QRP Labs instructions and check the hashes before use.
Their connection and flashing instructions concern Ebyte/QRP Labs hardware;
they are not a drop-in Air530 procedure. This repository provides no automatic
GNSS flasher. The CubeCell USB bridge must handle the GNSS baud-rate change,
and receiver-specific flash contents need preservation before a write.

Before the experimental update, two independent reads of the complete
1 MiB normal flash agreed. OTP/security areas and battery-backed state were
outside that backup. Programming and readback were checked, but a complete
rollback was not exercised because the new firmware passed the bounded tests.
Do not treat an untested rollback path as guaranteed recovery.

The downloaded ZIP contains the binary and Windows flasher, with no separate
redistribution license. QRP Labs states that its hosted copy has permission
from Ebyte; that statement does not establish permission for this repository
to mirror it. The firmware and flasher are therefore not bundled here and
are not covered by this project's MIT license. Local archival copies were
retained; publishing another mirror should first establish redistribution
permission from the rights holder.
