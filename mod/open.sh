#!/system/bin/sh
# Open the Light Vibes app from NEMESIS Manager (Launch on the Light Vibes package),
# installing its APK first if needed. Needs no hooks, so it opens the app on a car
# without an HMG-API hook map too.
MOD=${ROOT:-}/data/hmgapi/mods/light-vibes
say() { echo "Light Vibes: $1"; }
[ "$(getprop sys.boot_completed)" = 1 ] || { say "Android isn't up yet: try again in a minute"; exit 1; }
case "$(pm path lightvibes.app 2>/dev/null)" in
    package:*) ;;
    *)
        A=$MOD/app/LightVibes.apk
        [ -f "$A" ] || { say "the app isn't on this unit: install Light Vibes again"; exit 1; }
        setprop sys.boot_completed 0
        r=$(pm install -r "$A" 2>&1)
        setprop sys.boot_completed 1
        case "$r" in
            *Success*) say "installed the app" ;;
            *) say "the app didn't install: $r"; exit 1 ;;
        esac ;;
esac
am start -n lightvibes.app/.MainActivity >/dev/null 2>&1 || { say "couldn't open the app"; exit 1; }
