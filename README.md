# Light Vibes (HMG-API mod)

Makes the head unit's ambient lighting follow the music, or the car's speed, power or efficiency. Colour
and brightness react to the beat and to the sound's frequency bands, with no microphone. When the music
stops, or Siri or a phone call comes in, your own colour and brightness come back.

What's new in each version is in the [changelog](CHANGELOG.md).

**[Download the latest release](https://github.com/dibble9012/light-vibes/releases/latest)**

Supports the Kia EV6, Hyundai Ioniq 5 and Hyundai Ioniq 6 (USA Gen5W head units). Light Vibes detects
the car and uses its own 64 factory ambient colours, which differ between Kia and Hyundai. It's been
used most on a 2023 Kia EV6 GT-Line. This is an early version, and some parts
haven't been tried on the car yet: see [What's been tested](#whats-been-tested).

## Install

You need HMG-API 1.0.0 or later on the car first. Both methods use a USB flash drive. Download
the file for your method from the
[latest release](https://github.com/dibble9012/light-vibes/releases/latest), listed under **Assets**.

ADB over USB has to be off, or the port can't read the drive.

### With the HMG-API app

1. Download `light-vibes-v0.6.0-hmgapi-usb.zip`.
2. Unzip it onto the top level of a FAT32 flash drive, so the drive has
   `HMGAPI/updates/light-vibes-v0.6.0.zip`.
3. Plug the drive into the car and install the mod from the updates page in the HMG-API app.
4. Tap **Apply changes** in the app, then **Restart unit**. The Light Vibes tile appears on the
   home screen after the restart.

To remove the mod, use the HMG-API app.

### With NEMESIS

1. Download `light-vibes-v0.6.0-nemesis-usb.zip`.
2. Unzip it onto the top level of a FAT32 or exFAT flash drive, so the drive has `NEMESIS.data`
   and the `packages` folder at its top level. Keep only one NEMESIS package on the drive.
3. Once Android is up, plug the drive into the car and confirm the install of "Light Vibes".
4. Restart the head unit, then take the drive out. The Light Vibes tile appears on the home screen
   after the restart.

HMG-API installs and applies the mod for you. To remove it, uninstall the package in NEMESIS
Manager.

## Using it

Open the **Light Vibes** tile on the home screen, or tap **Launch** on **Light Vibes** in NEMESIS
Manager, which opens the app on any car, with or without the tile. On cars HMG-API doesn't have a
built-in hook map for yet, such as the Ioniq 6, you can also turn the tile on: **HMG-API > Settings >
Hook map > Map this car from its files**, then **Check the map on this car**, and restart the head unit.
Every change applies right away.

The app follows the head unit's light and dark themes.

- **On/Off** switches the effect off without uninstalling the mod.
- **Presets:** Lively (the default), Marigold, Contrast, Bold Flash, Glow, Epic, Speed, Power and
  Efficiency. Tap one to use it. If you switch presets with unsaved changes, the app asks whether to
  save them, save them as a new preset, or discard them.
- **Tabs:** every mode has **Color**, **Brightness** and **Tuning** tabs.

### Music modes

- **Spectrum** gives each sound band its own colour: tap a band, then a colour. **+** splits the
  selected band in two (up to 8 bands), and **−** removes it, with its neighbour taking over its range.
- **Beat** steps around the colour wheel on each beat, by the **Color step** you choose.
- **Pulse** pulses the brightness to the beat in one colour. The **Pulse** card shows the colour in
  use. Pick any colour, or **Drive Mode** at the bottom of the picker to use the car's drive-mode
  colours, which change live as you switch Eco, Normal and Sport. Picking Drive Mode turns on the car's
  **Link to Drive Mode** setting, and picking any other colour turns it off, so the colour shows.
  **Set as ambient light color** also makes the picked colour your car's own ambient colour.
- **Brightness:** **Follow the music** sets a range the brightness moves within, either with the
  **Average loudness** or with a **Flash on beat**. **Fixed level** keeps one brightness.
  **Dimmed in Dark** is the car's own setting, on every mode.
- **Tuning:** how quickly colours change, how easily they switch, each band's sensitivity, and how
  beats are detected. The live meter at the top shows each band's level, what a band or beat has to
  reach (the marker lines), and where the brightness sits in its range.

### Driving modes

These ignore the music. Each fades through the factory colours between a low and a high colour, and
the slider's **Now** shows the current reading.

- **Speed** fades between **Low Speed** and **High Speed** over the **Speed Range**, such as 0 mph
  green and 80 mph red. Miles or kilometres follow the head unit's setting.
- **Power** follows the total power the motors are using, with separate colours for **Regen**,
  **Zero** and **Full Power** across the **Power Range** (-150 to 350 kW).
- **Efficiency** fades between **Low Efficiency** and **High Efficiency** across the **Efficiency
  Range**, in mi/kWh or km/kWh as the head unit is set. **Efficiency Based On** chooses **Current
  Trip**, worked out by Light Vibes since the car was started, or **Car Average**, the same average
  the cluster shows.
- Each has its own brightness, or the car's **Dimmed in Dark**.

### Presets

- **+ New preset**, at the end of the list, starts a preset from Lively as it came. **Save** keeps
  your changes in the current preset, and **Save as…** copies them to a new one. Presets can have
  any name.
- **Delete** removes the current preset. For a built-in preset you've changed, the same prompt offers
  **Reset instead**. Deleted built-in presets come back with **Restore built-in presets** at the end
  of the list.

### Your own light settings

When Light Vibes is turned on, it keeps a copy of your ambient light settings: colour, brightness,
Dimmed in Dark and Link to Drive Mode. When it's turned off, uninstalled or updated, or switched off in
the app, those settings come back, whatever Light Vibes changed meanwhile. The one exception is
**Set as ambient light color** in Pulse: that colour becomes the one that comes back. A change you
make in the car's own settings while Light Vibes is on is undone when it's turned off.

If the car is switched off during a song, or the mod stops unexpectedly, your colour comes back at
the next start.

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

Tried on the car (EV6):

- CarPlay music, Spectrum, Beat and Pulse, presets and colours, and the live meter
- Speed and Power while driving, and Efficiency's car average (it matches the cluster)
- Pulse's Drive Mode and Set as ambient light color, and putting your settings back when Light Vibes stops
- Sounds of Nature
- Adding and removing bands, and making and deleting presets

Not tried on the car yet:

- **Bringing your colour back** after the car is switched off mid-song. The logic has only been
  tested off the car.
- **CarPlay since the 44.1 kHz change** that added the head unit's own audio.
- **The Hyundai Ioniq 5 and Ioniq 6.** Their colour palette and the NEMESIS launch come from
  [lemoyneiv](https://github.com/lemoyneiv)'s work. Everything else has been tried on the EV6 only.
- **Efficiency on the current trip** over a long drive.
- **Report a problem** saving to a USB stick.

## Ideas and requests

Tap **Feedback** at the top of the app and scan the code with your phone. It opens a GitHub issue for
your idea, already tagged as an enhancement.

## If something isn't working

Open **Report a problem** at the top of the app, while parked. For about 15 seconds the light changes
colour and brightness while you watch, with each command listed as it's sent, then your own settings
come back. **Test again** runs it once more. The app says what it found and shows
a code. Scan it with your phone to open a filled-in [GitHub issue](https://github.com/dibble9012/light-vibes/issues/new?template=problem.yml),
add your car, and submit it.

The code holds the versions of Light Vibes and HMG-API, the head unit's software version, your other mods, the
lamp's answers, your light settings and how loud the music has been. It doesn't include your serial number, addresses or
location, and nothing is sent unless you submit it. With a USB stick plugged in, the app also saves the full report
in a `Light Vibes reports` folder that you can attach. Over ADB it's at `/data/hmgapi/public/light-vibes/report.txt`.

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
  it off while it drives the lamp and restores it afterwards. Commands go through a persistent helper
  (`LampHelper`, run with `app_process`) that keeps the service's binder, falling back to `service call`.
- **Analysis.** A 2048-point FFT every 512 samples (10.7 ms at 48 kHz), for beats, bands and loudness.
- **Speed.** Every 500 ms the service asks HMG-API's vehicle service for `speed`, then walks around
  the colour wheel between your two colours, leaving out the pale ones.
- **Settings.** The app writes one-value files in its own folder, and `lightvibesd` applies them
  within half a second. The app's tile is added by HMG-API's launcher hook.

- **Lamp check.** Lamp commands get no answer, so the service asks the lamp what it's showing about 5 seconds
  after it takes over and writes the result to its log, along with how loud each session was and how many commands it sent.

The log is at `/data/hmgapi/logs/light-vibes/lightvibesd.log`.

## Build

You need `zig` (`brew install zig`), a JDK, the Android SDK with `build-tools` and
`platforms;android-19`, git, python3, openssl, unzip and zip, plus a checkout of
[HMG-API](https://github.com/VysseCodes/HMG-API), whose `tools/mknemesismod.sh` makes the NEMESIS
package. tinyalsa is cloned into `third_party/` at a pinned commit.

```bash
HMGAPI_DIR=../HMG-API ./build.sh
```

This writes the two release files to `dist/`, along with `light-vibes-v0.6.0.hmgapi.zip`, the signed
mod inside them, and `light-vibes-v0.6.0-nemesis-package.zip`, the file to upload to the NEMESIS
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
