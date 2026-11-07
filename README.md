# Light Vibes (HMG-API mod)

Makes the head unit's ambient lighting follow the music, or the car's speed. Colour and brightness
react to the beat and to the sound's frequency bands, with no microphone. When the music stops, or
Siri or a phone call comes in, your own colour and brightness come back.

**[Download the latest release](https://github.com/lemoyneiv/light-vibes/releases/latest)**

Tested on a 2023 Kia EV6 GT-Line (USA) and supports the Hyundai Ioniq 5 (USA Gen5W), auto-detecting the
vehicle's factory 64-colour ambient light palette at runtime. This is an early version, and some parts
haven't been tried on the car yet: see [What's been tested](#whats-been-tested).

## Install

You need HMG-API 1.0.0 or later on the car first. Both methods use a USB flash drive. Download
the file for your method from the
[latest release](https://github.com/lemoyneiv/light-vibes/releases/latest), listed under **Assets**.

ADB over USB has to be off, or the port can't read the drive.

### With the HMG-API app

1. Download `light-vibes-v0.5.0-hmgapi-usb.zip`.
2. Unzip it onto the top level of a FAT32 flash drive, so the drive has
   `HMGAPI/updates/light-vibes-v0.5.0.zip`.
3. Plug the drive into the car and install the mod from the updates page in the HMG-API app.
4. Tap **Apply changes** in the app, then **Restart unit**. The Light Vibes tile appears on the
   home screen after the restart.

To remove the mod, use the HMG-API app.

### With NEMESIS

1. Download `light-vibes-v0.5.0-nemesis-usb.zip`.
2. Unzip it onto the top level of a FAT32 or exFAT flash drive, so the drive has `NEMESIS.data`
   and the `packages` folder at its top level. Keep only one NEMESIS package on the drive.
3. Once Android is up, plug the drive into the car and confirm the install of "Light Vibes".
4. Restart the head unit, then take the drive out. The Light Vibes tile appears on the home screen
   after the restart.

HMG-API installs and applies the mod for you. To remove it, uninstall the package in NEMESIS
Manager.

## Using it

Open the **Light Vibes** tile. Every change applies right away.

- **On/Off** switches the effect off without uninstalling the mod.
- **Presets:** Lively (the default), Marigold, Contrast, Bold Flash, Glow, Epic and Speed. Tap one to
  use it. Tapping the current preset again undoes your unsaved edits.
- **Spectrum** gives each sound band its own colour: tap a band, then a colour. **+** splits the
  selected band in two (up to 8 bands), and **−** removes it, with its neighbour taking over its range.
- **Beat** steps around the colour wheel on each beat. **Pulse** keeps your own colour and pulses
  the brightness.
- **Speed** ignores the music and fades between a colour for a low speed and a colour for a high
  speed, such as 0 mph green and 80 mph red. The colour steps through the factory colours between
  them, and the strip above the sliders shows the steps. Your own brightness stays as it is.
- **Dimmest and Brightest** set the brightness range.
- **Response** is the sensitivity and speed page: how quickly colours change, how easily they switch,
  each band's sensitivity, and how beats are detected. The live meter at the top shows each band's
  level, what a band or beat has to reach (the white lines), and where the brightness sits in its range.
- **+ New preset**, at the end of the list, starts a preset from Lively as it came. **Save** keeps
  your changes in the current preset, and **Save as…** copies them to a new one.
- **Delete** removes the current preset. For a built-in preset you've changed, the same prompt offers
  **Reset instead**. Deleted built-in presets come back with **Restore built-in presets** at the end
  of the list.

The mod saves your ambient colour and brightness when it starts and before it changes the lamp. If
the car is switched off during a song or the mod stops unexpectedly, your colour comes back at the
next start. A colour you change in the car's menu during a song is overwritten when the song ends.

## Audio sources

| Source | Status |
|---|---|
| Apple CarPlay | Works |
| Audio the head unit plays itself, such as Sounds of Nature | Works |
| USB and Bluetooth music | Expected to work, not measured |
| Android Auto | Not measured |
| AM/FM radio, SiriusXM | Not supported |

AM/FM and SiriusXM never reach any part of the head unit the mod can read, except the cabin
microphone hearing the speakers, and the mod doesn't use the microphone.

## What's been tested

Tried on the car:

- CarPlay music, Spectrum, Beat and Pulse, presets and colours, and the live meter
- Sounds of Nature
- Adding and removing bands, and making and deleting presets

Not tried on the car yet:

- **Speed mode.** The speed reading and how it behaves while driving are untested, including
  whether the reading is in km/h on a US car.
- **Bringing your colour back** after the car is switched off mid-song. The logic has only been
  tested off the car.
- **CarPlay since the 44.1 kHz change** that added the head unit's own audio.

## How it works

- **Audio tap.** CarPlay and the head unit's own audio play out on ALSA `card0` device 2 (`I2S_3`),
  at 48 kHz and 44.1 kHz. The I2S controller's mixer switch `Device2 Tx to Rx Internal Loopback Mode`
  copies the outgoing audio into the same link's capture side, `pcmC0D2c`. The service switches it
  on and reads it. What you hear is unchanged, and nothing is recorded or saved.
- **Hands off Siri and calls.** They record from `pcmC0D0c` and switch the playback side to a lower
  rate. The service lets go of `pcmC0D2c` within about 80 ms when the playback side isn't running at
  44.1 or 48 kHz.
- **Lamp control** (`service call com.hkmc.micom.system`, `com.hkmc.micom.ISystemService`):
  `76` requestSMLStatus, `77` setSMLConfiguration(mode, data) with mode 5 for brightness and mode 9 for
  drive-mode-linked colour, and `78` setSMLRGB(byte[2] r, byte[2] g, byte[2] b). The lamp only reliably
  accepts its factory 64-colour custom wheel, which differs between Kia and Hyundai (`VehicleSettings.apk`);
  both `lightvibesd` and the app auto-detect the vehicle palette (`kia` or `hyundai`, overridable via a
  `palette` setting file). Custom colours are ignored while drive-mode colour is on, so the service turns
  it off while it drives the lamp and restores it afterwards.
- **Analysis.** A 2048-point FFT every 1024 samples, for beats, bands and loudness.
- **Speed.** Every 500 ms the service asks HMG-API's vehicle service for `speed`, then walks around
  the colour wheel between your two colours, leaving out the pale ones.
- **Settings.** The app writes one-value files in its own folder, and `lightvibesd` applies them
  within half a second. The app's tile is added by HMG-API's launcher hook.

The log is at `/data/hmgapi/logs/light-vibes/lightvibesd.log`.

## Build

You need `zig` (`brew install zig`), a JDK, the Android SDK with `build-tools` and
`platforms;android-19`, git, python3, openssl, unzip and zip, plus a checkout of
[HMG-API](https://github.com/VysseCodes/HMG-API), whose `tools/mknemesismod.sh` makes the NEMESIS
package. tinyalsa is cloned into `third_party/` at a pinned commit.

```bash
HMGAPI_DIR=../HMG-API ./build.sh
```

This writes the two release files to `dist/`, along with `light-vibes-v0.5.0.hmgapi.zip`, the signed
mod inside them, and `light-vibes-v0.5.0-nemesis-package.zip`, the file to upload to the NEMESIS
Package Store. It signs the mod with `~/.hmgapi/author.key` and the app with
`~/.hmgapi/light-vibes-app.keystore`, which it makes on the first build. Updates must be signed with
the same keys as the first install.

## Test

```bash
HMGAPI_DIR=../HMG-API ./build.sh test
```

This also runs the analysis on synthetic music on the Mac (it needs numpy), and `test/test_lamp.c`,
which checks the speed fade, the vehicle service's replies and the saved colour off the car.

## License

AGPL-3.0. See [LICENSE](LICENSE).
