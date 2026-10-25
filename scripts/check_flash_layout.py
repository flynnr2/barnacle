#!/usr/bin/env python3
"""Fail the build if the loadable image or its checksum touches log rows."""
import argparse
import json
from pathlib import Path
import re
root=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--build-dir',default='wardrive-build')
p.add_argument('--report',default='logs/wardrive-flash-layout.json')
args=p.parse_args()
header=(root/'firmware/Wardrive/FlashJournal.h').read_text()
start=int(re.search(r'FLASH_LOG_START = (0x[0-9a-f]+)',header)[1],16)
rows={}
for line in (root/args.build_dir/'CubeCell_GPS_REGION_US915_RGB_0.cyacd').read_text().splitlines()[1:]:
    assert line.startswith(':')
    b=bytes.fromhex(line[1:]); array=b[0]; row=int.from_bytes(b[1:3],'big'); size=int.from_bytes(b[3:5],'big')
    assert array == 0 and size == 256 and len(b)==262
    assert sum(b) % 256 == 0, 'row checksum'
    rows[row]=b[5:5+size]
    assert not start//256 <= row < 510, f'image overlaps journal row {row}'
meta=rows[511][-64:]
last_boot_row=int.from_bytes(meta[5:9],'little'); length=int.from_bytes(meta[9:13],'little')
checksum_end=(last_boot_row+1)*256+length
assert 0 < length and checksum_end <= start
# Verify actual CYACD data against bootloader additive checksum, including any gaps.
app_start=(last_boot_row+1)*256
app=bytes(rows.get(addr//256,bytes(256))[addr%256] for addr in range(app_start,checksum_end))
assert (-sum(app)) & 255 == meta[0], 'bootloader application checksum mismatch'
result={'image_checksum_end':checksum_end,'journal_start':start,'journal_end':510*256,
        'journal_bytes':510*256-start,'records':(510-start//256)*7,
        'reserved_rows':[510,511],'application_checksum_verified':True}
(root/args.report).write_text(json.dumps(result,indent=2)+'\n')
print('PASS flash layout and bootloader checksum:',result)
