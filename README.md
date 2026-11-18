# Light Vibes (HMG-API mod)

Makes the head unit's ambient lighting follow the music, or the car's speed, power or efficiency. Colour
and brightness react to the beat and to the sound's frequency bands, with no microphone. When the music
stops, or Siri or a phone call comes in, your own colour and brightness come back.

What's new in each version is in the [changelog](CHANGELOG.md).

**[Download the latest release](https://github.com/dibble9012/light-vibes/releases/latest)**

Supports the Kia EV6, Kia Niro EV (2023), Hyundai Ioniq 5 and Hyundai Ioniq 6 (USA Gen5W head units). Light Vibes detects
the car and uses its own factory ambient colours (64 custom wheel colours on Kia; 64 custom wheel colours plus all 10 official `0x01xx` mood-theme stock colours—including Healing Forest emerald green, Mindful Teal, Ocean Blue, Marigold Gold, Vivid Orchid and Dream Purple—on Hyundai).

## Install

You need HMG-API 1.0.0 or later on the car first. Both methods use a USB flash drive. Download
the file for your method from the
[latest release](https://github.com/dibble9012/light-vibes/releases/latest), listed under **Assets**.

ADB over USB has to be off, or the port can't read the drive.

### With the HMG-API app

1. Download `light-vibes-v0.7.0-hmgapi-usb.zip`.
2. Unzip it onto the top level of a FAT32 flash drive, so the drive has
   `HMGAPI/updates/light-vibes-v0.7.0.zip`.
3. Plug the drive into the car and install the mod from the updates page in the HMG-API app.
4. Tap **Apply changes** in the app, then **Restart unit**. The Light Vibes tile appears on the
   home screen after the restart.

To remove the mod, use the HMG-API app.

### With NEMESIS

1. Download `light-vibes-v0.7.0-nemesis-usb.zip`.
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

The Material Design 3 split-pane interface adapts automatically to the head unit's Day and Night themes and fits the wide 1280×480 Gen5W screen without vertical clutter:

- **Master switch** at the top of the left preset rail switches the effect on or off without uninstalling the mod, alongside **+** (new preset) and **⋮** (LED testing & color capture, and preset reset/restore).
- **Presets:** Synesthesia (the default), Phase Pulse, Phase Drift, Duet, Ember, Night Drive, Lively,
  Bold Flash, Epic, Glow, Speed, Power and Efficiency—each with a live color badge. Tap one to use it. If you switch presets
  with unsaved changes, the app asks whether to save them, save them as a new preset, or discard them.
- **Single-row header bar:** tap the **Mode** chip to open the categorized **Music Modes** / **Hybrid & Driving** mode sheet, or switch between the **Color**, **Brightness** and **Tuning** segmented tabs.

### Music & hybrid modes

- **Synesthesia** maps continuous log-frequency spectral centroid (55 Hz–8 kHz) directly to colour
  hue, gliding smoothly from deep bass hues through vocal mids to bright highs, with a live **Color
  rotation** slider.
- **Spectrum** gives each sound band its own colour: tap a band, then a colour. **+** splits the
  selected band in two (up to 8 bands), and **−** removes it, with its neighbour taking over its range.
  A 115 ms minimum dwell lets the car's LIN lamp finish each colour transition cleanly without mid-fade mud.
- **Beat** steps around the colour wheel on each beat, by the **Color step** you choose, with a live 16-beat colour jump preview strip.
- **Pulse** pulses the brightness to the beat in one colour. The top card shows the active colour alongside **Drive Mode** (which uses the car's Eco, Normal and Sport colours live) and **Set as ambient color** (which makes a picked colour stay as your car's own ambient colour).
- **Duet** layers two musical roles onto one lamp: a smooth midrange/vocal **Melody Bed** punctuated
  by crisp **Beat Accent** flashes in the 180° opposite complementary colour (or any picked colour),
  with a **Pair walk** slider (`Off`, `every beat`, or `2–16 beats`) that steps complementary pairs
  around the wheel on beat.
- **Ember** models thermal glow with percussion sparks: each beat ignites a bright hot flare while
  snare and clap transients (250 Hz–8 kHz) re-ignite cooling embers across **Fire**, **Plasma** or
  **Aurora** gradients.
- **Phase Drift** steps through curated mood palettes (**Forest & Aurora**, **Cyberpunk**, **Sunset**,
  **Ocean Deep**) or the **Full Wheel** every **Beats per color** (`2–16` beats)—from steady 8-beat
  night-highway mood shifts (**Phase Drift**) to energetic 2-beat rainbow beat-pulses (**Phase Pulse**).
- **Night Drive** fuses EV driving telemetry with music: **Motor Power & Regen** (emerald green on
  regen, ocean blue cruising, red under throttle) or **Vehicle Speed** sets the colour while music
  beats pulse the brightness—and when music is paused, it cruises calmly at your telemetry colour.
- **Brightness:** **Follow music** sets a range (`0` to `10`, where `0` turns the lamp completely off
  between hits for full strobe contrast) that the brightness moves within, by **Loudness**,
  smooth **Beat pulse** envelope, or crisp **Flash on beat**. **Fixed level** keeps one brightness.
  **Dimmed in Dark** is the car's own setting, on every mode.
- **Tuning:** how quickly colours change (down to 20 ms), how easily they switch, **Auto-adapt** vs
  **Manual only** band sensitivity in Spectrum, **Auto BPM** vs **Manual** tempo sync (using a
  3-band log-spectral comb tracker with CarPlay track-skip silence reset), and **Record 20s clip
  to USB** (`Light Vibes tuning/`, RAM-buffered during playback for zero light jitter). Music modes
  automatically snap and filter colours to rich, hue-aware vivid factory swatches (including Hyundai's
  `0x01xx` stock greens/golds/teals and Kia's greens while skipping pale pinks, tans and whites).
  The live meter at the top shows each band's level, the locked BPM, what a band or beat has to reach
  (the marker lines), and where the brightness sits in its range.

### Driving modes

These ignore the music. Each fades through the factory colours between a low and a high colour, and
the slider's **Now** pill shows the current reading.

- **Speed** fades between **Low Speed** and **High Speed** over the **Speed Range**, such as 0 mph
  green and 80 mph red. Miles or kilometres follow the head unit's setting.
- **Power** follows the total power the motors are using, with separate colours for **Regen**,
  **Zero** and **Full Power** across the **Power Range** (-150 to 350 kW).
- **Efficiency** fades between **Low Efficiency** and **High Efficiency** across the **Efficiency
  Range**, in mi/kWh or km/kWh as the head unit is set. **Efficiency based on** chooses **Current
  Trip**, worked out by Light Vibes since the car was started, or **Car Average**, the same average
  the cluster shows.
- Each has its own brightness, or the car's **Dimmed in Dark**.
- The **Tuning** tab (and **Night Drive**'s Tuning tab) shows a **Current Drive** graph: the reading over the
  drive so far, in the colour the light had, or would have had, at each moment with the colours and range set
  now. It updates every second while the page is open. Light Vibes records speed, power and efficiency while
  the car is on, whatever mode you use.

### Presets

- **+** in the preset header starts a new preset from Synesthesia as it came. When a preset has unsaved edits, **Save** keeps
  them in the current preset, **Save as…** copies them to a new one, and **Undo** reverts them immediately.
- **Reset modified built-in presets** appears in the **⋮** menu whenever any built-in preset has saved overrides,
  restoring all built-in presets to their shipped settings in one tap.
- **Delete** removes the current preset. For a built-in preset you've changed, the same prompt offers
  **Reset instead**. Deleted built-in presets come back anytime via **Restore deleted built-in presets** in the **⋮** menu.

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

Tried on the car (Kia EV6 & Hyundai Ioniq 5):

- CarPlay music across Synesthesia, Phase Pulse, Phase Drift, Duet, Ember, Night Drive, Spectrum, Beat and Pulse, presets and colours, and the live meter
- Hyundai Ioniq 5 custom 64-colour wheel plus all 10 official `0x01xx` mood-theme stock colours, USB diagnostic report exports, and 20-second USB audio/CSV tuning captures
- Speed and Power while driving, and Efficiency's car average (it matches the cluster)
- Pulse's Drive Mode and Set as ambient light color, and putting your settings back when Light Vibes stops
- Sounds of Nature
- Adding and removing bands, and making and deleting presets

Not tried on the car yet:

- **Bringing your colour back** after the car is switched off mid-song. The logic has been tested off the car.
- **Hyundai Ioniq 6** on-vehicle verification (shares the Gen5W Hyundai palette and NEMESIS launch).
- **Efficiency on the current trip** over a long drive.

## LED testing & color capture

Open **⋮ > LED testing & color capture** at the top of the left preset rail while parked. Everything runs locally and saves directly to a USB stick in `Light Vibes reports/` (with no QR codes or external links):

- **15-second LED & brightness test:** cycles Red, Green and Blue, then Low (`1`), High (`10`) and Mid (`5`) brightness while listing each step live, restores your own ambient colour and brightness, shows an on-screen diagnosis, and saves `light-vibes-report-YYYYMMDD-HHMM.txt` to USB (also kept locally at `/data/hmgapi/public/light-vibes/report.txt`).
- **Capture current:** reads the lamp's live 16-bit RGB (`0x01xx` stock or wheel), brightness, Dimmed-in-Dark and Drive Mode link in ~0.4 s, displays a live swatch on screen, and appends a timestamped line to `Light Vibes reports/captured-colors.txt` on USB.
- **Auto-capture:** polls `SYS_SML_STATUS_M_RESP` at 1 Hz in the background so you can switch to the car's native **Ambient Light** menu and tap through stock swatches—or cycle **Eco / Normal / Sport** Drive Modes—and automatically log every unique colour to `captured-colors.txt` on USB.

## How it works

- **Audio tap.** CarPlay and the head unit's own audio play out on ALSA `card0` device 2 (`I2S_3`),
  at 48 kHz and 44.1 kHz. The I2S controller's mixer switch `Device2 Tx to Rx Internal Loopback Mode`
  copies the outgoing audio into the same link's capture side, `pcmC0D2c`. The service switches it
  on and reads it. What you hear is unchanged, and nothing is recorded or saved unless you tap
  **Record 20s clip to USB** on the Tuning tab.
- **Hands off Siri and calls.** They record from `pcmC0D0c` and switch the playback side to a lower
  rate. The service lets go of `pcmC0D2c` within about 80 ms when the playback side isn't running at
  44.1 or 48 kHz.
- **Lamp control** (`service call com.hkmc.micom.system`, `com.hkmc.micom.ISystemService`):
  `76` requestSMLStatus, `77` setSMLConfiguration(mode, data) with mode 5 for brightness and mode 9 for
  drive-mode-linked colour, and `78` setSMLRGB(byte[2] r, byte[2] g, byte[2] b). The lamp only reliably
  accepts its factory 64-colour custom wheel (plus `0x01xx` stock mood colours on Hyundai), which differs between Kia and Hyundai (`VehicleSettings.apk`);
  both `lightvibesd` and the app auto-detect the vehicle palette (`kia` or `hyundai`, overridable via a
  `palette` setting file). Custom colours are ignored while drive-mode colour is on, so the service turns
  it off while it drives the lamp and restores it afterwards. Commands go through a persistent helper
  (`LampHelper`, run with `app_process`) that keeps the service's binder, falling back to `service call`.
- **Analysis.** Zero-lag time-domain bandpass onset detection plus a right-aligned 1024-sample Hann window
  (2048-point FFT) every 512 samples (10.7 ms at 48 kHz), for beats, auto-adapting bands and loudness.
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

This writes the two release files to `dist/`, along with `light-vibes-v0.7.0.hmgapi.zip`, the signed
mod inside them, and `light-vibes-v0.7.0-nemesis-package.zip`, the file to upload to the NEMESIS
Package Store. It signs the mod with `~/.hmgapi/author.key` and the app with
`~/.hmgapi/light-vibes-app.keystore`, which it makes on the first build. Updates must be signed with
the same keys as the first install.

## Test

```bash
HMGAPI_DIR=../HMG-API ./build.sh test
```

This also runs the analysis on synthetic music on the host (it needs numpy), and `test/test_lamp.c`,
which checks the speed fade, the vehicle service's replies, the saved colour, and USB diagnostic/color capture off the car.

## Authors

- **Kyle ([dibble9012](https://github.com/dibble9012))**
- **[lemoyneiv](https://github.com/lemoyneiv)**

## License

AGPL-3.0. See [LICENSE](LICENSE).
