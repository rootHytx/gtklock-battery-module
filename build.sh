#!/usr/bin/env bash
# Build the gtklock battery module into a loadable .so.
# Requires: pkg-config, libgtk-3-dev (which pulls libglib2.0-dev for gmodule-export-2.0).
set -euo pipefail
cd "$(dirname "$0")"

gcc -shared -fPIC -Wall -O2 \
    $(pkg-config --cflags gtk+-3.0 gmodule-export-2.0) \
    source.c -o battery-module.so \
    $(pkg-config --libs gtk+-3.0 gmodule-export-2.0)

echo "Built: $(pwd)/battery-module.so"
