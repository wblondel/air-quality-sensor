#pragma once

#include <stdint.h>

// User-adjustable settings, persisted in NVS ("appcfg" namespace) and edited
// on the LCD settings page. A factory reset erases them like the rest of NVS.
struct AppSettings {
    static constexpr uint32_t kRefreshChoices[] = {10, 30, 60, 120, 300};
    static constexpr uint16_t kAltitudeMaxMeters = 3000; // SEN66 valid range
    static constexpr uint16_t kAltitudeStepMeters = 25;
    static constexpr uint8_t kRotateMinSec = 3;
    static constexpr uint8_t kRotateMaxSec = 30;

    uint32_t refreshSeconds = 60; // sensor poll period, one of kRefreshChoices
    uint16_t altitudeMeters = 25; // CO2 pressure-compensation altitude
    uint8_t rotateSeconds = 7;    // display auto-rotation period
    bool autoRotate = true;
    bool netlogEnabled = false;   // stream logs over Thread (debug), off by default

    void Load();
    void Save() const;
};
