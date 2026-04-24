#!/bin/bash
set -e

CORE_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

python3 "$CORE_DIR/script/config/config_summary.py" "$CORE_DIR" "$CORE_DIR/build" >/dev/null
cat "$CORE_DIR/build/config_summary.txt"

