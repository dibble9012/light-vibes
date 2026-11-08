#!/bin/bash
# Build the light-vibes HMG-API mod on the Mac.
#
#   ./build.sh          ARM binaries, settings app, signed dist/ zips
#   ./build.sh test     also build a host copy and run the analysis on synthetic music
#
# Needs: zig (brew install zig), git, python3, openssl, unzip, zip, a JDK, the Android SDK with build-tools
# and platforms;android-19 (ANDROID_HOME, default: Homebrew's android-commandlinetools), and a checkout of
# HMG-API (HMGAPI_DIR, default ../HMG-API). The test step also needs numpy.
# The mod is signed with ~/.hmgapi/author.key, the app with ~/.hmgapi/light-vibes-app.keystore (made on
# first build). Keep both: updates need the same keys as the first install.
# Output binaries are static ARMv7 (musl), so they run on the head unit's Android 4.4 without bionic.
set -euo pipefail
cd "$(dirname "$0")"
TINY=third_party/tinyalsa
TINY_REV=a7cd26a17fe2add2ff38d2f281ed0b919187a068   # tinyalsa master as built and tested on the car (2026-11-01)
SDK=${ANDROID_HOME:-/opt/homebrew/share/android-commandlinetools}
BT=$(ls -d "$SDK"/build-tools/* | sort -V | tail -1)
ANDROID_JAR=$SDK/platforms/android-19/android.jar
AUTHOR_KEY=${HMGAPI_KEY:-$HOME/.hmgapi/author.key}
if [ -z "${HMGAPI_DIR:-}" ]; then     # a checkout of github.com/VysseCodes/HMG-API, for tools/mknemesismod.sh
    if [ -d third_party/HMG-API ]; then HMGAPI_DIR=third_party/HMG-API; else HMGAPI_DIR=../HMG-API; fi
fi
APP_KEYSTORE=$HOME/.hmgapi/light-vibes-app.keystore

if [ ! -d "$TINY" ]; then
    mkdir -p third_party
    git clone -q https://github.com/tinyalsa/tinyalsa "$TINY"
    git -C "$TINY" checkout -q "$TINY_REV"
fi

if command -v zig >/dev/null 2>&1; then ZIGCC="zig cc"
elif python3 -c "import ziglang" 2>/dev/null; then ZIGCC="python3 -m ziglang cc"
else echo "need zig: brew install zig   (or: pip3 install ziglang)"; exit 1; fi

ARM="-target arm-linux-musleabihf -mcpu=cortex_a7 -static -Os -s -I$TINY/include"
PCM="$TINY/src/pcm.c $TINY/src/pcm_hw.c $TINY/src/limits.c $TINY/src/snd_card_plugin.c"
MIX="$TINY/src/mixer.c $TINY/src/mixer_hw.c $TINY/src/snd_card_plugin.c"

mkdir -p build dist
echo "== ARM binaries"
$ZIGCC $ARM -o build/lightvibesd src/lightvibesd.c $PCM $TINY/src/mixer.c $TINY/src/mixer_hw.c -lm
$ZIGCC $ARM -o build/ambcap    src/tools/ambcap.c $PCM -lm
$ZIGCC $ARM -o build/ambmix    src/tools/ambmix.c $MIX -lm
ls -l build/lightvibesd build/ambcap build/ambmix

echo "== settings app"
A=build/app
rm -rf $A
mkdir -p $A/classes $A/dex $A/assets/presets
for p in lively marigold contrast pulse3 glow epic speed power efficiency; do cp presets/$p.conf $A/assets/presets/; done
javac --release 8 -nowarn -Xlint:-options -cp "$ANDROID_JAR" -d $A/classes app/src/lightvibes/app/*.java
"$BT/d8" --release --min-api 19 --lib "$ANDROID_JAR" --output $A/dex $(find $A/classes -name '*.class')
"$BT/aapt" package -f -M app/AndroidManifest.xml -S app/res -A $A/assets -I "$ANDROID_JAR" -F $A/unsigned.apk
(cd $A/dex && zip -q -X ../unsigned.apk classes.dex)
"$BT/zipalign" -f 4 $A/unsigned.apk $A/aligned.apk
if [ ! -f "$APP_KEYSTORE" ]; then
    mkdir -p "$(dirname "$APP_KEYSTORE")"
    keytool -genkeypair -keystore "$APP_KEYSTORE" -storepass light-vibes-app -keypass light-vibes-app -alias app \
        -keyalg RSA -keysize 2048 -validity 15000 -dname "CN=Light Vibes" >/dev/null 2>&1
fi
"$BT/apksigner" sign --ks "$APP_KEYSTORE" --ks-pass pass:light-vibes-app --min-sdk-version 19 --v1-signing-enabled true \
    --v2-signing-enabled false --out $A/LightVibes.apk $A/aligned.apk
ls -l $A/LightVibes.apk

echo "== mod"
M=build/mod
rm -rf $M
mkdir -p $M/app $M/defaults
cp mod/mod.json mod/lights.sh mod/open.sh build/lightvibesd $M/
cp $A/LightVibes.apk $M/app/
python3 tools/make_icons.py $M/icons
# defaults until the app has been opened: the lively preset, one value per file
while IFS='=' read -r k v; do
    case "$k" in ''|\#*) continue ;; esac
    echo "$v" > "$M/defaults/$k"
done < presets/lively.conf
python3 tools/sign.py $M dist "$AUTHOR_KEY" "$HMGAPI_DIR"

if [ "${1:-}" = test ]; then
    echo "== host test"
    # the analysis never calls tinyalsa, whose sources need Linux headers: leave its symbols unresolved
    cc -O2 -I$TINY/include -o build/lightvibesd-host src/lightvibesd.c -lm -lpthread -Wl,-undefined,dynamic_lookup
    cc -O1 -I$TINY/include -o build/test_lamp test/test_lamp.c -lm -lpthread -Wl,-undefined,dynamic_lookup
    build/test_lamp
    mkdir -p build/rt
    javac --release 8 -nowarn -Xlint:-options -d build/rt app/src/lightvibes/app/QrCode.java app/src/lightvibes/app/Report.java test/ReportTest.java
    java -cp build/rt lightvibes.app.ReportTest
    [ -f build/song.raw ] || python3 test/make_test_audio.py build/song.raw
    beats=$(build/lightvibesd-host --analyze build/song.raw | grep -c BEAT || true)
    echo "beat mode: $beats beats detected (expect ~24)"
    for p in test/presets/*; do
        [ -d "$p" ] || continue
        n=$(build/lightvibesd-host --analyze build/song.raw "$p" | grep -c DRY || true)
        echo "$(basename "$p"): $n light commands"
    done
fi
