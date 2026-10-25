# Barnacle — CubeCell MeshCore node

Device-specific firmware for the **Heltec HTCC-AB02S / CubeCell GPS**
(ASR6502 + SX1262 + Air530 + OLED), using a pinned MeshCore routing core.

Barnacle began by repurposing an **IoWE Helium Mapper** device built around
the Heltec HTCC-AB02S / CubeCell GPS. This project replaces its original
Helium mapping firmware with MeshCore firmware.

Choose a role explicitly; keep the device identity separate from the role.

| Profile        | Forwarding | Hourly UTC text |
| -------------- | ---------- | --------------- |
| repeater       | Yes        | No              |
| repeater-clock | Yes        | Yes, on #test   |
| time-publisher | No         | Yes, on #test   |

All profiles include GNSS time, one OLED health screen, approximately 1 km
location cells and sparse logging in the existing flash journal. The time
publisher advertises a sensor role. The repeater profiles are compact custom
forwarders; stock remote administration, Bluetooth and room hosting are not
implemented.

## Build

The currently validated host is macOS with Arduino CLI and the CubeCell vendor
tools available. Setup pins CubeCell 1.4.0, Crypto 0.4.0 and a specific MeshCore
revision. Other host platforms have not been validated.

```sh
sh tools/setup.sh
.venv/bin/python tools/node.py identity create --device garden --name GardenNode
.venv/bin/python tools/node.py build --device garden --profile repeater-clock
```

Identity creation is explicit and refuses to overwrite a key. Build output is
isolated per device/profile and includes flash/checksum/RAM reports. Building
never opens USB or flashes a device. Keep `.secrets/` **and compiled firmware**
private; the image contains its node's private seed. Use one identity per
physical device, retaining it when switching that device's role.

The radio is fixed to the Boston profile: 910.525 MHz, BW 62.5 kHz, SF7,
CR 4/5, requested 22 dBm, private sync and 32 preamble symbols. GNSS does not
automatically retune the radio for another region. Originated paths use two
bytes. Local transmission completion is not proof of downstream reception.

## Documentation and verification

- [Profiles, identity, scheduling and storage](docs/PROFILES.md)
- [Publication workflow and private history](PUBLICATION.md)
- [Third-party notices and project-license decision](THIRD_PARTY_NOTICES.md)

```sh
sh tools/check_profiles.sh
```

Host tests exercise the actual pinned routing core, profile forwarding policy,
advertised role, two-byte originated packets, malformed input, journal recovery,
GNSS discipline and the radio adapter. They do not measure RF power or prove
that a distant receiver heard a packet. New profiles need physical commissioning.

## Working repository versus public source

This workshop also retains earlier Beacon, Barnacle and Wardrive sketches and
private field-test history. The previous GeoSpoof README is preserved locally
under `docs/history/`. Those generations are not additional supported public
profiles. Existing private build and capture paths remain available.

**Publish a fresh source export, not this workshop's Git history.** The export
tool includes only reviewed source/docs/tests and excludes identities, firmware
images, captures and old history. Original project code is [MIT licensed](LICENSE). Third-party components retain
their own notices. Public source: <https://github.com/flynnr2/barnacle>.
