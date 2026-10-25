#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
python3 -m venv .venv
.venv/bin/pip install -r requirements.txt
.venv/bin/python - <<'PY'
from pathlib import Path
r = Path.cwd()
(r / 'arduino-cli.yaml').write_text(f'''directories:
  data: {r}/.arduino/data
  downloads: {r}/.arduino/downloads
  user: {r}/.arduino/user
board_manager:
  additional_urls:
    - https://resource.heltec.cn/download/package_CubeCell_index.json
''')
PY
arduino-cli --config-file arduino-cli.yaml core update-index
arduino-cli --config-file arduino-cli.yaml core install CubeCell:CubeCell@1.4.0
arduino-cli --config-file arduino-cli.yaml lib install Crypto@0.4.0
