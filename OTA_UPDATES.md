# Over-the-air firmware updates

Once the device runs a firmware built from this setup (flashed over USB one
last time), every later firmware version can be delivered over Thread through
Home Assistant — no cable, no interruption beyond one automatic reboot, and
the Matter pairing, Thread credentials and user settings all survive.

## How it works

- The firmware has the Matter **OTA Requestor** role enabled
  (`CONFIG_ENABLE_OTA_REQUESTOR=y`) and reports a numeric software version
  (`CONFIG_DEVICE_SOFTWARE_VERSION_NUMBER`).
- Every build also produces `build/light-ota.bin`
  (`CONFIG_CHIP_OTA_IMAGE_BUILD=y`): the app image wrapped in a Matter OTA
  header carrying VID/PID (0xFFF1/0x8000), the numeric version and the
  version string.
- The Home Assistant **Matter Server add-on** doubles as a Matter **OTA
  Provider**. With *Enable Test Net DCL* switched on (already required for
  this device's test certificate), it serves update files from its
  `updates/` config folder — checked *before* the official DCL, which is what
  makes updates for a test-VID device possible at all.
- When you click **Install** on the update entity in HA, the Matter Server
  announces itself to the device as OTA provider; the device downloads the
  image over Thread into its inactive OTA slot (`ota_0`/`ota_1`, 3.87 MB
  each), verifies it, reboots into it, and confirms.

Matter only compares the **numeric** version: an update is offered when the
file's number is strictly greater than what the device runs. The version
string ("1.1") is just a human-readable label. `scripts/release.sh` keeps the
number, the string, and all the files that store them in sync.

## Cutting a release

```sh
# from the project directory, with esp-idf + esp-matter exported:
scripts/release.sh 1.1          # bumps the numeric version by 1
scripts/release.sh 2.0 20       # or pick the number explicitly
```

The script updates `CMakeLists.txt` (`PROJECT_VER`, `PROJECT_VER_NUMBER`) and
`sdkconfig`/`sdkconfig.defaults` (`CONFIG_DEVICE_SOFTWARE_VERSION_NUMBER`),
rebuilds, and drops two files into `releases/`:

- `light-v1.1.ota` — the update image
- `light-v1.1.json` — the descriptor the Matter Server needs (DCL
  `modelVersion` format, with a `file:///` URL pointing at the image)

Commit the version bump afterwards so the repo matches what's in the field.

## Delivering it with Home Assistant

1. Copy **both** files into the Matter Server add-on's updates folder:
   - via the Samba add-on: `\\homeassistant\addon_configs\core_matter_server\updates\`
   - or via the SSH add-on: `/addon_configs/core_matter_server/updates/`
   (create the `updates` folder the first time)
2. **Restart the Matter Server add-on** so it re-reads the folder.
   Note: restarting it briefly disconnects all Matter devices in HA — they
   recover on their own within a minute.
3. Open the air-quality station's device page in HA. A **firmware update**
   entity appears (updates are checked every 12 h; to force it, run the
   `homeassistant.update_entity` action on that entity).
4. Click **Install**. The transfer takes a few minutes over Thread; the LCD
   keeps running during the download, then the device reboots into the new
   firmware.
5. Verify: the device page shows the new firmware version, and the LCD's
   System page shows the new `FW` string.

## Bootstrapping (first time only)

The firmware currently on the device must itself contain the OTA-enabled
config and a known version number. So the very first update after setting
this up goes over USB:

```sh
idf.py flash
```

After that, `scripts/release.sh` + the copy step is the whole workflow.

## Troubleshooting

- **No update entity appears**: check the add-on log after restart — it
  logs what it loaded from the updates folder. Make sure *both* the `.json`
  and `.ota` files are there and that `softwareVersion` in the JSON is
  greater than the version the device page currently shows.
- **Update offered but install fails immediately**: VID/PID mismatch —
  the JSON and the `.ota` header must both say vid 65521 / pid 32768
  (decimal for 0xFFF1/0x8000). `scripts/release.sh` guarantees this.
- **Device says the image is not applicable**: its running numeric version
  is ≥ the update's. Bump higher and re-release.
- **Inspecting an .ota file**: `python
  $ESP_MATTER_PATH/connectedhomeip/connectedhomeip/src/app/ota_image_tool.py
  show releases/light-v1.1.ota` prints the header (VID, PID, version,
  payload digest).
- The Matter Server project is transitioning from the Python implementation
  to `matterjs-server`; the add-on keeps the same `updates/` folder and
  arguments, but if updates stop being picked up after an add-on major
  update, re-check the add-on documentation for the current custom-update
  mechanism.
