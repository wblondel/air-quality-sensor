# Air Quality Station — User Guide

A Matter-over-Thread air quality monitor built around a Sensirion SEN66 sensor, with a
20×4 LCD display, a color LED indicator and two control buttons. It measures CO2,
particulate matter (PM1 / PM2.5 / PM4 / PM10), VOC, NOx, temperature and relative
humidity, shows everything locally on the display, and integrates with Home Assistant
(or any other Matter controller) over a Thread network.

---

## 1. At a glance

| Part | What it does |
|---|---|
| LCD display (20×4) | Shows live readings, history, min/max and system status on 6 pages |
| Color LED | Glows in a color matching the current overall air quality |
| Button 1 (display button) | Page navigation, refresh, settings menu |
| Button 2 (control button) | LED on/off, fan cleaning, Matter pairing |
| BOOT button (on the board) | Factory reset |
| USB port | Power (and serial console for technical users) |

New measurements are taken **once per minute** by default — adjustable from 10 s
to 5 min in the settings menu. Everything on the display and in Home Assistant
updates at that rhythm.

---

## 2. Getting started

1. Power the device over USB.
2. The display shows **"Waiting for data"** for up to a minute — this is normal; the
   first measurement cycle hasn't completed yet.
3. After the first cycle, readings appear and the display starts rotating through its
   pages automatically.
4. If the device has already been paired with Home Assistant before, it reconnects to
   the Thread network by itself — no action needed after a power cut.

---

## 3. The display

### The six pages

The display cycles automatically through six pages (one every 7 seconds):

1. **Live values** — temperature, humidity, CO2, VOC, PM2.5, PM10, and the overall
   air-quality verdict (e.g. `Air: Good`). Small ▲/▼ arrows next to a value mean it
   went up or down since the previous measurement; no arrow means it is stable.
2. **Particles** — the full particulate breakdown in µg/m³: PM1, PM2.5, PM4 and PM10,
   plus the NOx index. (PM4 is shown *only* here — Matter has no way to report it.)
3. **Min/Max** — the lowest and highest temperature, humidity and CO2 seen since the
   device was last powered on.
4. **CO2 chart** — a bar chart of the last 20 measurements of CO2, one bar each
   (20 minutes at the default refresh rate), auto-scaled; the range is shown in
   the header and the current value below.
5. **Big CO2** — the CO2 value in large digits readable from across the room, with
   the air-quality verdict underneath.
6. **System status** — uptime, free memory and firmware version. A `*` in the top
   right corner means auto-rotation is currently paused.

### Navigating

- **Press button 1** to go to the next page. Manual navigation **pauses** the
  automatic rotation, so the display stays on the page you chose. (Turn rotation
  back on in the settings menu.)
- **Hold button 1 (~1.5 s)** to open the **settings menu** (see below).
- **Double-press button 1** to trigger a measurement immediately instead of waiting
  for the next cycle.

### Settings menu

Hold **button 1** (~1.5 s) to open the settings menu; hold it again to **save and
leave**. The menu also saves and closes itself after 30 s without a press.

| Setting | Range | What it does |
|---|---|---|
| Refresh | 10 s / 30 s / 1 min / 2 min / 5 min | How often a new measurement is taken |
| Altitude | 0 – 3000 m, in 25 m steps | Your elevation, used by the CO2 sensor for pressure compensation |
| Rotate every | 3 – 30 s | How long each display page stays on screen |
| Auto-rotate | ON / OFF | Whether the pages rotate automatically |

While the menu is open:

- **Button 1** moves to the next setting (the `>` marks the current one).
- **Button 2** increases the value. **Double-press** decreases it, and **keeping
  it held** scrolls fast — handy for the altitude.
- Values wrap around at the end of their range.

Settings are stored permanently and survive power cuts and reboots. A factory
reset returns them to their defaults. Setting your altitude once is worth it:
the CO2 sensor assumes sea level unless told otherwise, so readings get more
accurate the closer the setting is to your real elevation.

### Backlight

- After **5 minutes** without a button press, the backlight turns off and the screen
  goes blank.
- **Any button press wakes the display.** A wake-up press *only* wakes the display —
  it never triggers the button's normal action, so you can't accidentally toggle the
  light or start a fan cleaning while waking the screen. Press again for the action.

---

## 4. The color LED indicator

The LED color always reflects the current overall air quality:

| Color | Meaning | Brightness |
|---|---|---|
| Green | Good | low |
| Yellow-green | Fair / Moderate | low |
| Yellow | Poor | medium |
| Orange | Very poor | medium |
| Red | Extremely poor | higher (to catch your attention) |

- **Press button 2** (or use Home Assistant) to switch the LED on or off. It is a
  normal Matter light, so it also works in HA scenes and automations.
- The device **respects your choice**: a light you switched off stays off. The color
  keeps being updated in the background, so the moment you switch it back on it shows
  the current air quality.
- After a reboot the light restores its previous on/off state. If you want it as an
  always-on indicator, switch it on once and leave it.

---

## 5. Buttons reference

| Gesture | Button 1 (display) | Button 2 (control) |
|---|---|---|
| Single press | Next page (pauses rotation) | Toggle the LED light |
| Double press | Measure now | Start sensor fan cleaning |
| Long press (~1.5 s) | Settings menu (open / save & close) | Open Matter pairing window |

Inside the settings menu the buttons change roles — see "Settings menu" above.

**BOOT button** (small button on the circuit board): hold for about 5 seconds to
**factory reset** the device. This erases all Matter pairings — the device will need
to be paired again. Use it only when instructed to, or when giving the device away.

Reminder: if the screen is asleep, the first press only wakes it.

---

## 6. How the air-quality verdict is computed

The verdict (shown on the display, in Home Assistant, and as the LED color) is the
**worst** of three ratings: CO2 (latest value), PM2.5 and PM10 (both averaged over
the last hour).

| Verdict | CO2 (ppm) | PM2.5 (µg/m³, 1 h avg) | PM10 (µg/m³, 1 h avg) |
|---|---|---|---|
| Good | 400 – 600 | ≤ 15 | ≤ 30 |
| Fair | ≤ 700 | ≤ 30 | ≤ 60 |
| Moderate | ≤ 800 | ≤ 50 | ≤ 120 |
| Poor | ≤ 950 | ≤ 100 | ≤ 260 |
| Very poor | ≤ 1200 | ≤ 150 | ≤ 400 |
| Extremely poor | > 1200 | > 150 | > 400 |

Practical reading: green means the room is well ventilated; yellow means "open a
window soon"; orange/red means "open a window now".

---

## 7. Home Assistant / Matter

### What you need

- Home Assistant with the **Matter integration** (Matter Server add-on).
- A **Thread border router** known to Home Assistant (e.g. the OpenThread Border
  Router add-on with a compatible USB dongle), with its credentials synced to your
  phone (HA app → Settings → Companion App → Thread).
- Because this device uses Matter **test certificates**, the Matter Server add-on
  needs the **"Enable Test Net DCL"** option turned on (add-on Configuration tab).
  Pairing is rejected at the attestation step without it.

### Pairing (first time)

1. In the Home Assistant app: **Add device → Add Matter device**.
2. Enter the pairing code: **3497-011-2332** (`34970112332`).
3. Keep the phone close to the device — the first phase runs over Bluetooth.
4. Pairing takes up to a minute: the phone connects, hands the device the Thread
   network credentials, then the Home Assistant server takes over.

### What appears in Home Assistant

- A **light** (the LED indicator) — full color and brightness control.
- An **air quality** device with: the overall air-quality verdict, CO2, PM1, PM2.5,
  PM10, VOC index and NOx index — each with current value, plus 1-hour average and
  peak values for the concentrations.
- **Temperature** and **humidity** sensors.

All values refresh once per minute.

### Pairing a second controller (another app or hub)

1. Hold **button 2** for ~1.5 s — the display switches to the pairing page showing
   the code and a 5-minute countdown.
2. On the new controller, add a Matter device using that code.
3. The pairing window closes automatically after 5 minutes or once pairing completes.
   (Pressing button 1 dismisses the pairing page from the display; the window itself
   stays open.)

### Removing / re-pairing

Remove the device from Home Assistant like any other Matter device. If pairing ever
gets stuck (e.g. the device was removed from HA while offline), factory reset the
device (hold BOOT ~5 s) and pair again from scratch. On iPhone, also remove stale
entries under iOS Settings → General → Matter Accessories.

---

## 8. Maintenance and placement

### Fan cleaning

The particulate sensor contains a small fan that accumulates dust.

- **Double-press button 2** to run a cleaning cycle. The display shows
  "Fan cleaning…" for ~10 seconds. The fan revs to maximum speed — it is quiet, so
  don't worry if you barely hear it.
- The sensor also cleans itself automatically at regular intervals, so the manual
  button is mainly useful after dusty activities (renovation, vacuuming, etc.).
- PM readings may be briefly odd during a cleaning cycle.

### CO2 accuracy

Automatic self-calibration is enabled: the sensor assumes it sees fresh outdoor-level
air (~400 ppm) at least once a week. **Air the room regularly** — if the device lives
in a permanently closed room, CO2 readings will slowly drift.

Also set your **altitude** in the settings menu (hold button 1): the sensor uses it
for pressure compensation, which matters increasingly the higher you live.

### Placement tips

- Breathing height, away from direct sunlight, radiators, and drafts.
- Not directly next to a window or door (readings would swing with every gust).
- Keep at least a few centimeters of clearance around the sensor's air openings.
- Indoor use only; avoid condensing humidity.

---

## 9. Troubleshooting

| Symptom | What to do |
|---|---|
| Screen is blank | It's probably asleep — press any button. If it stays blank, check power; if the backlight is on but no text, adjust the contrast screw on the display's back board. |
| "Waiting for data" for more than 2 minutes | The sensor isn't responding. Power-cycle the device. If it persists, check the sensor wiring (technical). |
| A button "does nothing" | First press after 5 idle minutes only wakes the screen — press again. |
| LED never lights up | It was switched off. Press button 2 once, or turn the light on in Home Assistant. |
| Device shows "unavailable" in Home Assistant | Check that the Thread border router is up. Power-cycle the device — it rejoins on its own. |
| Pairing fails on the phone | Verify the Matter Server option "Enable Test Net DCL" is on, the Thread network is set up in HA, and Thread credentials are synced to the phone. Then factory reset the device and retry. |
| CO2 seems too high/low all the time | Ventilate the room thoroughly (15 min, windows open) and give the self-calibration a week of normal use with regular airing. |
| PM values seem stuck or noisy | Run a fan cleaning (double-press button 2). |
| Want to start fresh | Hold BOOT ~5 s (factory reset), remove the device from HA and pair again. |

Note: min/max values, the CO2 chart and trend arrows reset at every reboot — that's
by design; long-term history belongs to Home Assistant.

---

## 10. Technical reference

### Matter identity

| Property | Value |
|---|---|
| Vendor / Product ID | 0xFFF1 / 0x8000 (Matter *test* identity) |
| Setup passcode | 20202021 |
| Discriminator | 3840 |
| Manual pairing code | 34970112332 |
| Transport | Thread (device acts as a full Thread router), BLE for commissioning |
| Commissioning window (button) | 300 s, opened with the original passcode |

Endpoints: 1 = Extended Color Light · 2 = Air Quality Sensor (air quality,
CO2/PM1/PM2.5/PM10/VOC/NOx concentrations with 1-hour average and peak, temperature,
humidity) · 3 = Temperature Sensor · 4 = Humidity Sensor.

### Hardware map (ESP32-C6)

| GPIO | Function |
|---|---|
| 6 / 7 | I2C SDA / SCL (shared bus: SEN66 at address 0x6B, LCD backpack at 0x27) |
| 8 | WS2812 RGB LED |
| 9 | BOOT button (factory reset) |
| 22 | Button 2 (control) — active low, to GND |
| 23 | Button 1 (display) — active low, to GND |
| 16 / 17 | Serial console (UART0) |

The LCD is a 2004A (HD44780) behind a PCF8574 I2C backpack, powered at 3.3 V.

### Timings

| Setting | Value |
|---|---|
| Measurement cycle | 60 s default (10 s – 5 min, settings menu) |
| Display page rotation | 7 s default (3 – 30 s, settings menu) |
| Backlight timeout | 5 min |
| Settings menu timeout | 30 s (saves and closes) |
| Trend-arrow deadbands | ±0.2 °C, ±1 %RH, ±25 ppm CO2, ±0.3 µg/m³ PM2.5 |
| CO2 chart window | 20 samples (= 20 min at default refresh) |
| Fan cleaning duration | ~10 s |

### Serial console

Connect over USB at **115200 baud** (`idf.py monitor` from the project directory, or
any serial terminal). The log shows boot progress, every measurement cycle, button
events, Matter commissioning steps and Thread state changes — the first place to look
when diagnosing anything. Firmware is flashed with `idf.py flash` (ESP-IDF v5.5.x
with the esp-matter SDK), and later versions can be installed over the air from
Home Assistant — see `OTA_UPDATES.md`.

### Factory reset details

Holding BOOT ~5 s erases the Matter fabric table and NVS configuration (including
the settings-menu values, which return to defaults), then reboots
into pairing mode (BLE advertising, commissioning window open). Thread credentials
are removed with the fabric — the device must be re-commissioned to rejoin the
network. A full `idf.py erase-flash` + reflash achieves the same from a computer.
