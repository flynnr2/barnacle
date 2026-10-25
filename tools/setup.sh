#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
sh scripts/setup.sh
.venv/bin/python tools/node.py dependencies
