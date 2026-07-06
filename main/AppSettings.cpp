#include "AppSettings.h"

#include <esp_log.h>
#include <nvs.h>

static const char* TAG = "AppSettings";
static const char* kNamespace = "appcfg";

// Values read from NVS are validated so a corrupted or stale entry can never
// produce an out-of-range timer period or altitude.
static uint32_t ValidRefresh(uint32_t value)
{
    for (uint32_t choice : AppSettings::kRefreshChoices) {
        if (value == choice) {
            return value;
        }
    }
    return 60;
}

void AppSettings::Load()
{
    nvs_handle_t handle;
    if (nvs_open(kNamespace, NVS_READONLY, &handle) != ESP_OK) {
        return; // nothing saved yet, keep the defaults
    }

    uint32_t u32;
    uint16_t u16;
    uint8_t u8;
    if (nvs_get_u32(handle, "refresh", &u32) == ESP_OK) {
        refreshSeconds = ValidRefresh(u32);
    }
    if (nvs_get_u16(handle, "altitude", &u16) == ESP_OK) {
        altitudeMeters = u16 > kAltitudeMaxMeters ? kAltitudeMaxMeters : u16;
    }
    if (nvs_get_u8(handle, "rotate", &u8) == ESP_OK) {
        rotateSeconds = u8 < kRotateMinSec ? kRotateMinSec : (u8 > kRotateMaxSec ? kRotateMaxSec : u8);
    }
    if (nvs_get_u8(handle, "autorot", &u8) == ESP_OK) {
        autoRotate = u8 != 0;
    }
    if (nvs_get_u8(handle, "netlog", &u8) == ESP_OK) {
        netlogEnabled = u8 != 0;
    }
    nvs_close(handle);

    ESP_LOGI(TAG, "Loaded: refresh %us, altitude %um, rotate %us (%s)",
             (unsigned)refreshSeconds, altitudeMeters, rotateSeconds, autoRotate ? "on" : "off");
}

void AppSettings::Save() const
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(kNamespace, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS: %s", esp_err_to_name(err));
        return;
    }

    nvs_set_u32(handle, "refresh", refreshSeconds);
    nvs_set_u16(handle, "altitude", altitudeMeters);
    nvs_set_u8(handle, "rotate", rotateSeconds);
    nvs_set_u8(handle, "autorot", autoRotate ? 1 : 0);
    nvs_set_u8(handle, "netlog", netlogEnabled ? 1 : 0);

    err = nvs_commit(handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to commit settings: %s", esp_err_to_name(err));
    }
    nvs_close(handle);
}
