#!/system/bin/sh
# Light Vibes: makes the ambient light follow the music. The work is done by lightvibesd.
# Settings are one-value files the app writes in its own folder; until the app has
# been opened, the shipped defaults apply.
MOD=/data/hmgapi/mods/${HMGAPI_MOD:-light-vibes}
chmod 755 "$MOD/lightvibesd"
exec "$MOD/lightvibesd" /data/data/lightvibes.app/files/settings "$MOD/defaults"
