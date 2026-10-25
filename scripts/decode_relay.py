#!/usr/bin/env python3
"""Decode a saved relay flash dump offline; never connects to a device."""
import argparse
import datetime
import json
from pathlib import Path
import re
import struct

NAMES = {32: 'relay_boot', 33: 'hourly_claim', 34: 'advert_claim', 35: 'own_tx_done',
         36: 'own_tx_fail', 37: 'summary', 38: 'error_summary', 39: 'own_tx_skip', 63: 'relay_state'}

def checksum(raw):
    value = 2166136261
    for byte in raw:
        value = ((value ^ byte) * 16777619) & 0xffffffff
    return value

def decode(raw):
    if len(raw) != 28160:
        raise ValueError('Expected exactly 110 saved 256-byte log rows')
    events, invalid = [], []
    for index in range(110):
        page = raw[index * 256:(index + 1) * 256]
        magic, generation, count, check = struct.unpack_from('<IIII', page)
        if magic != 0x57444c32:
            continue
        if not generation or not 1 <= count <= 7 or checksum(page[:12] + bytes(4) + page[16:]) != check:
            invalid.append(index)
            continue
        for offset in range(count):
            seq, utc, lat, lon, stamp, peer, rssi, age, snr, typ, flags, detail = struct.unpack_from('<IIiiIIhHbBBB', page, 32 + 32 * offset)
            event = dict(sequence=seq, utc=utc, utc_iso=datetime.datetime.fromtimestamp(utc, datetime.timezone.utc).isoformat(),
                         event=NAMES.get(typ, 'legacy_' + str(typ)), row=index, generation=generation,
                         clock_source={0: 'unset', 1: 'estimate', 2: 'mesh_estimate', 3: 'USB', 4: 'GNSS'}.get(flags & 7, 'unknown'))
            if typ == 63:
                event.update(identity_checksum=f'{peer:08x}', last_hour=lat, last_advert=lon, last_stamp=stamp, schema=age,
                             advert_format=(detail & 1) if age==2 else 0)
            elif typ == 37:
                event.update(forwarded=stamp, received=peer, errors=lat)
            elif typ == 38:
                event.update(tx_failures=stamp, crc_errors=peer, drops_or_malformed=lat)
            elif typ in (33, 34):
                event['claimed_timestamp'] = stamp
            elif typ in (35, 36, 39):
                event.update(message_kind={1: 'hourly_UTC', 2: 'repeater_advert'}.get(stamp, 'unknown'), claimed_timestamp=peer)
            elif typ == 32:
                event['build_epoch'] = stamp
            else:
                event['legacy_fields'] = dict(latitude_micro=lat, longitude_micro=lon, stamp=stamp, peer=peer,
                                              rssi=rssi, fix_age=age, snr=snr, flags=flags, detail=detail)
            events.append(event)
    events.sort(key=lambda e: (e['generation'], e['sequence']))
    return dict(events=events, invalid_or_torn_rows=invalid,
                note='Counters restart at boot. Own TX completion is local radio evidence; forwarded counts do not prove downstream reception.')

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('saved_dump', type=Path)
    args = parser.parse_args()
    raw = args.saved_dump.read_bytes()
    if len(raw) != 28160:
        rows = {int(i): bytes.fromhex(value) for i, value in re.findall(r'^FLASH_ROW index=(\d+) data=([0-9a-f]+)', raw.decode(), re.M)}
        if set(rows) != set(range(110)) or any(len(p) != 256 for p in rows.values()):
            raise ValueError('Incomplete saved transcript; expected all 110 rows')
        raw = b''.join(rows[i] for i in range(110))
    print(json.dumps(decode(raw), indent=2))

if __name__ == '__main__':
    main()
