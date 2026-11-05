#!/bin/zsh
# loadpreset.sh <name> [debug]: replace the head unit's light-vibes settings with presets/<name>.conf (tuning from the Mac).
# Applies live. The app shows the new values next time it opens; open the app once first, so the folder belongs to it.
set -e
here=${0:A:h}; conf=$here/presets/$1.conf; dbg=${2:-0}
[[ -f $conf ]] || { echo "no preset $conf"; ls $here/presets; exit 1; }
D=/data/data/lightvibes.app/files/settings
cmd="cd $D && for f in *; do [ \$f = enabled ] || rm -f \$f; done; echo $dbg > debug"
while IFS='=' read -r k v; do [[ -z $k || $k == \#* ]] && continue; cmd+="; echo '$v' > $k"; done < $conf
adb shell "hmgapid sh \"$cmd\""
