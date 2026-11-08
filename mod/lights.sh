#!/system/bin/sh
# Light Vibes: makes the ambient light follow the music. The work is done by lightvibesd.
# Settings are one-value files the app writes in its own folder; until the app has
# been opened, the shipped defaults apply.
MOD=/data/hmgapi/mods/${HMGAPI_MOD:-light-vibes}
NEM=/data/nemesis/apps/hmgapi.${HMGAPI_MOD:-light-vibes}/bin/open.sh
if [ -f "$MOD/open.sh" ] && [ -d "${NEM%/*}" ]; then
    cat "$MOD/open.sh" > "$NEM" 2>/dev/null
    chmod 755 "$NEM" 2>/dev/null
fi
chmod 755 "$MOD/lightvibesd"
exec "$MOD/lightvibesd" /data/data/lightvibes.app/files/settings "$MOD/defaults"
