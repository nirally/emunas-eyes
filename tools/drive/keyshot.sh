#!/bin/sh
# Presses keys in order (scancodes, e0 prefix = extended), then saves a screenshot as PNG.
# Usage: keyshot.sh <out.png> [key ...]
T="$(cd "$(dirname "$0")" && pwd)"
D="/e/SteamLibrary/steamapps/common/Mirrors Edge Catalyst/mecvr"
out="$1"; shift
for k in "$@"; do powershell -ExecutionPolicy Bypass -File "$(cygpath -w "$T/send.ps1")" key "$k"; sleep 1.2; done
before=$(ls "$D"/shot_*.bmp 2>/dev/null | wc -l)
touch "$D/SHOT"
for i in $(seq 1 20); do [ "$(ls "$D"/shot_*.bmp 2>/dev/null | wc -l)" -gt "$before" ] && break; sleep 0.5; done
sleep 0.5
powershell -ExecutionPolicy Bypass -File "$(cygpath -w "$T/topng.ps1")" "$(cygpath -w "$(ls -t "$D"/shot_*.bmp | head -1)")" "$(cygpath -w "$out")"
