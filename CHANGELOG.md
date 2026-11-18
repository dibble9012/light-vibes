# Changelog

## 0.7.0

### Authors
- Co-authored by **Kyle ([dibble9012](https://github.com/dibble9012))** and **[lemoyneiv](https://github.com/lemoyneiv)**.

### Material Design 3 interface & presets
- Full **Material Design 3** split-pane interface tailored for wide 1280×480 Gen5W screens in both Day and Night themes, replacing the tall mode button grid with a compact single-row header, categorized **Mode** sheet, segmented tabs, and M3 Expressive sliders.
- **Synesthesia** is now the default preset, alongside **Phase Pulse**, **Phase Drift**, **Duet**, **Ember**, **Night Drive**, **Lively**, **Bold Flash**, **Epic**, **Glow**, **Speed**, **Power** and **Efficiency**.
- Integrated the live **Current Drive** telemetry graph directly into the Material 3 **Tuning** tab for **Speed**, **Power**, **Efficiency** and **Night Drive**.

### New music & hybrid modes
- **Synesthesia:** maps continuous log-frequency spectral centroid (55 Hz–8 kHz) directly around the vivid colour ring, with a live **Color wheel rotation** slider.
- **Phase Drift & Phase Pulse:** steps cleanly through curated mood palettes (**Forest & Aurora**, **Cyberpunk**, **Sunset**, **Ocean Deep**) or the **Full Wheel** every **Beats per color** (`2–16` beats), with optional beat-pulse brightness.
- **Duet:** smooth midrange/vocal **Melody Bed** punctuated by crisp **Beat Accent** flashes in the 180° complementary colour (or any picked colour), with an optional beat-synced **Pair walk** (`Off`, `every beat`, or `2–16 beats`).
- **Ember:** thermal beat flares with snare/clap transient sparks (250 Hz–8 kHz) re-igniting cooling embers across **Fire**, **Plasma** or **Aurora** gradients.
- **Night Drive:** fuses EV telemetry (**Motor Power & Regen** or **Vehicle Speed**) for colour with music beat-pulsing brightness, cruising calmly at your telemetry colour when playback pauses.

### Hyundai `0x01xx` stock mood palette & audio DSP
- Added all 10 official Hyundai `0x01xx` mood-theme stock colours—including Healing Forest emerald green, Mindful Teal, Ocean Blue, Marigold Gold, Vivid Orchid and Dream Purple—tested live on the Hyundai Ioniq 5, plus hue-aware vivid swatch filtering on both Kia and Hyundai.
- Zero-lag time-domain bandpass beat onset detection, 115 ms minimum Spectrum dwell (preventing LIN lamp mid-fade mud), full `0–10` brightness floor (where `0` blacks out between hits), **Beat pulse** brightness envelope, and 3-band **Auto BPM** tempo tracking with CarPlay track-skip silence reset.
- RAM-buffered 20-second USB audio/CSV tuning captures (`Light Vibes tuning/`) for zero light jitter during recording.

### USB LED testing & stock color capture
- Redesigned **LED testing & color capture** (`⋮ > LED testing & color capture`) as a two-card local USB tool (`Light Vibes reports/`):
  - **15-second LED & brightness test** with live step tracking, on-screen diagnosis, and direct **Save to USB**.
  - **Capture current** and 1 Hz **Auto-capture** for logging live 16-bit RGB + hex values (`captured-colors.txt`) while tapping swatches in the car's Ambient Light settings or cycling Drive Modes.

## 0.6.3

- **Kia Niro EV (2023)** is now listed as compatible in the NEMESIS Package Store, and uses the Kia colours.
- **Current Drive graph** on the Speed, Power and Efficiency **Tuning** tabs: the drive so far, drawn in the
  colour the ambient light had, or would have had, at each moment with your current colours and range. It
  updates live, every second, while the page is open, and recolours as you move the range. Light Vibes records
  speed, power and efficiency while the car is on, whatever mode you use.

## 0.6.0

### New cars
- **Hyundai Ioniq 5 and Ioniq 6** (US Gen5W), alongside the Kia EV6. Light Vibes detects the car and
  uses its own 64 factory ambient colours, which differ between Kia and Hyundai. Thanks to
  [lemoyneiv](https://github.com/lemoyneiv).
- **Launch from NEMESIS Manager:** tapping **Launch** on Light Vibes opens the app directly, also on cars
  without the home screen tile, such as the Ioniq 6.

### New modes
- **Power:** the light follows the total power the motors are using, with separate colours for regen,
  zero and full power.
- **Efficiency:** the light follows mi/kWh (or km/kWh), based on the **Current Trip** or the **Car
  Average** the cluster shows.
- **Speed** gets the same new look, and follows the head unit's miles or kilometres setting.

### Pulse
- A **Pulse** card shows the colour in use, and the picker ends with **Drive Mode**: the car's own
  drive-mode colours, which change live with Eco, Normal and Sport while the music plays.
- Picking Drive Mode turns on the car's **Link to Drive Mode** setting, and any other colour turns it
  off, so the colour shows. Colour changes apply instantly.
- **Set as ambient light color** makes the picked colour your car's own ambient colour.

### Your settings stay yours
- When Light Vibes is turned on, it keeps a copy of your ambient light settings (colour, brightness,
  Dimmed in Dark, Link to Drive Mode). They come back when it's turned off, uninstalled or updated.

### Faster, tighter response
- Lights react to the music sooner: twice as many analysis steps a second, an instant response to
  beats, and lamp commands sent through a helper that stays connected instead of starting a new
  command each time.
- **Fastest light change** now goes down to 50 ms, and the built-in presets are quicker.

### Easier to use
- Every mode has **Color**, **Brightness** and **Tuning** tabs.
- New brightness choices: **Follow the music** (with **Average loudness** or **Flash on beat**) or a
  **Fixed level**, and the car's **Dimmed in Dark** on every mode.
- One slider with two handles for each range (speed, power, efficiency, brightness).
- The app follows the head unit's light and dark themes.
- Switching presets with unsaved changes asks whether to save, save as new, or discard. Presets can
  have any name.

### Help and feedback
- **Report a problem** runs a lamp test while you watch, lists each command live, and gives a code
  you scan to open a filled-in GitHub issue. **Test again** runs it once more.
- **Feedback** gives a code for suggesting an idea on GitHub.

## 0.5.0

- First release: Spectrum, Beat and Pulse music modes, Speed mode, presets, and a live meter.
