#!/usr/bin/env bash
# Cut a new firmware release: bump the version everywhere it lives, rebuild,
# and emit the Matter OTA image plus the update descriptor for the Home
# Assistant Matter Server into releases/.
#
# Usage: scripts/release.sh <version-string> [<version-number>]
#   scripts/release.sh 1.1       # numeric version auto-increments
#   scripts/release.sh 2.0 20    # explicit numeric version
#
# Matter compares the *numeric* version; the device only accepts an update
# whose number is strictly greater than what it is running.

set -euo pipefail

PROJECT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
cd "$PROJECT_DIR"

if [[ $# -lt 1 || $# -gt 2 ]]; then
    sed -n 's/^# \{0,1\}//p' "$0" | sed -n '2,9p'
    exit 1
fi

VERSION_STR="$1"

CURRENT_NUMBER=$(sed -n 's/^set(PROJECT_VER_NUMBER \([0-9]*\))/\1/p' CMakeLists.txt)
if [[ -z "$CURRENT_NUMBER" ]]; then
    echo "error: PROJECT_VER_NUMBER not found in CMakeLists.txt" >&2
    exit 1
fi
VERSION_NUMBER="${2:-$((CURRENT_NUMBER + 1))}"

if (( VERSION_NUMBER <= CURRENT_NUMBER )); then
    echo "warning: version number $VERSION_NUMBER is not greater than the current $CURRENT_NUMBER;" >&2
    echo "         devices already on $CURRENT_NUMBER will refuse this update." >&2
fi

if ! command -v idf.py > /dev/null; then
    echo "error: idf.py not in PATH; source esp-idf's and esp-matter's export.sh first" >&2
    exit 1
fi

echo "Releasing version $VERSION_STR (number $VERSION_NUMBER, previously $CURRENT_NUMBER)"

# Keep every place that stores the version in sync
sed -i '' "s/^set(PROJECT_VER \".*\")/set(PROJECT_VER \"$VERSION_STR\")/" CMakeLists.txt
sed -i '' "s/^set(PROJECT_VER_NUMBER .*)/set(PROJECT_VER_NUMBER $VERSION_NUMBER)/" CMakeLists.txt
sed -i '' "s/^CONFIG_DEVICE_SOFTWARE_VERSION_NUMBER=.*/CONFIG_DEVICE_SOFTWARE_VERSION_NUMBER=$VERSION_NUMBER/" \
    sdkconfig sdkconfig.defaults

idf.py build

OTA_FILE="light-v$VERSION_STR.ota"
mkdir -p releases
cp build/light-ota.bin "releases/$OTA_FILE"

# Update descriptor for the Matter Server's local-updates directory. The
# schema mirrors a DCL modelVersion entry; vid/pid are the decimal form of
# the Matter test identity 0xFFF1/0x8000 used by this device.
cat > "releases/light-v$VERSION_STR.json" << EOF
{
  "modelVersion": {
    "vid": 65521,
    "pid": 32768,
    "softwareVersion": $VERSION_NUMBER,
    "softwareVersionString": "$VERSION_STR",
    "otaUrl": "file:///$OTA_FILE"
  }
}
EOF

echo
echo "Release ready:"
echo "  releases/$OTA_FILE"
echo "  releases/light-v$VERSION_STR.json"
echo
echo "Next: copy both files into the Matter Server add-on's updates folder"
echo "(Samba: \\\\homeassistant\\addon_configs\\core_matter_server\\updates),"
echo "restart the add-on, then install the update from the device page in HA."
echo "Details: OTA_UPDATES.md"
