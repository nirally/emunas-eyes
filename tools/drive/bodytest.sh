#!/bin/sh
# Eye-sync body check. Usage: bodytest.sh start [forward]  -> launch game, skip intros/notices, look down (or not)
#                             bodytest.sh run <label> <json-settings>  -> apply settings, run forward, capture 2 sequences
S="$(cd "$(dirname "$0")" && pwd)"
D="/e/SteamLibrary/steamapps/common/Mirrors Edge Catalyst/mecvr"
send() { powershell -ExecutionPolicy Bypass -File "$(cygpath -w "$S/send.ps1")" "$@"; }
case "$1" in
start)
  start_ts=$(date +%H:%M)
  cmd //c start "" "steam://rungameid/1233570"
  for i in $(seq 1 150); do grep -q "SMOKE OK" "$D/mecvr.log" 2>/dev/null && [ "$(head -c 5 "$D/mecvr.log")" \> "$(date -d '-3 min' +%H:%M)" ] && break; sleep 1; done
  sleep 15
  # intros, "servers shut down" + "EA Online" notices, then Resume Game on the main menu
  for k in 39 39 1C 39 1C 1C 39 1C; do send key $k; sleep 3; done
  sleep 5; send key 1C; sleep 25
  [ "$2" = "forward" ] || send mouse 0 900 20
  echo started ;;
run)
  label="$2"; settings="$3"
  # apply settings: replace values in config.json (keys must already exist)
  echo "$settings" | tr ',' '\n' | while IFS='=' read k v; do
    [ -n "$k" ] && sed -i "s|\"$k\": *[^,}]*|\"$k\": $v|" "$D/config.json"
  done
  sleep 2
  n0=$(grep -c "pair " "$D/mecvr.log")
  send key 11 4500 & sleep 1.5; touch "$D/PAIR"; sleep 1.2; touch "$D/PAIR"; wait
  sleep 1
  echo "== $label ($settings)"
  grep "pair " "$D/mecvr.log" | tail -n +$((n0 + 1)) | sed 's/.*pair /pair /' ;;
esac
