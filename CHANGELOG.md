# Changelog

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
