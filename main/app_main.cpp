/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <esp_err.h>
#include <esp_log.h>
#include <nvs_flash.h>

#include <esp_matter.h>
#include <esp_matter_console.h>
#include <esp_matter_ota.h>
#include <esp_openthread.h>

#include <common_macros.h>
#include <app_priv.h>
#include <app_reset.h>
#if CHIP_DEVICE_CONFIG_ENABLE_THREAD
#include <platform/ESP32/OpenthreadLauncher.h>
#endif

#include <app/server/CommissioningWindowManager.h>
#include <app/server/Server.h>

#ifdef CONFIG_ENABLE_SET_CERT_DECLARATION_API
#include <esp_matter_providers.h>
#include <lib/support/Span.h>
#ifdef CONFIG_SEC_CERT_DAC_PROVIDER
#include <platform/ESP32/ESP32SecureCertDACProvider.h>
#elif defined(CONFIG_FACTORY_PARTITION_DAC_PROVIDER)
#include <platform/ESP32/ESP32FactoryDataProvider.h>
#endif
using namespace chip::DeviceLayer;
#endif

#include "MatterNode.h"
#include "MatterAirQualitySensor.h"
#include "MatterExtendedColorLight.h"
#include "MatterHumiditySensor.h"
#include "MatterTemperatureSensor.h"
#include "SensirionSEN66.h"
#include "LCD2004.h"
#include "AppSettings.h"

#include <driver/i2c_master.h>
#include <cmath>
#include <cstring>
#include <esp_app_desc.h>
#include <iot_button.h>
#include <button_gpio.h>

std::shared_ptr<MatterNode> matterNode;
std::shared_ptr<MatterAirQualitySensor> matterAirQualitySensor;
std::shared_ptr<MatterExtendedColorLight> matterExtendedColorLight;
std::shared_ptr<MatterTemperatureSensor> matterTemperatureSensor;
std::shared_ptr<MatterHumiditySensor> matterHumiditySensor;
esp_timer_handle_t sensor_timer_handle;
static std::shared_ptr<AirQualitySensor> airQualitySensor;
static LCD2004* lcd = nullptr;
static AppSettings s_settings;

static const char *TAG = "app_main";
uint16_t light_endpoint_id = 0;

using namespace esp_matter;
using namespace esp_matter::attribute;
using namespace esp_matter::endpoint;
using namespace chip::app::Clusters;

constexpr auto k_timeout_seconds = 300;

#ifdef CONFIG_ENABLE_SET_CERT_DECLARATION_API
extern const uint8_t cd_start[] asm("_binary_certification_declaration_der_start");
extern const uint8_t cd_end[] asm("_binary_certification_declaration_der_end");

const chip::ByteSpan cdSpan(cd_start, static_cast<size_t>(cd_end - cd_start));
#endif // CONFIG_ENABLE_SET_CERT_DECLARATION_API

#if CONFIG_ENABLE_ENCRYPTED_OTA
extern const char decryption_key_start[] asm("_binary_esp_image_encryption_key_pem_start");
extern const char decryption_key_end[] asm("_binary_esp_image_encryption_key_pem_end");

static const char *s_decryption_key = decryption_key_start;
static const uint16_t s_decryption_key_len = decryption_key_end - decryption_key_start;
#endif // CONFIG_ENABLE_ENCRYPTED_OTA

struct DisplayReadings {
    bool valid = false;
    float temperature = NAN, humidity = NAN, co2 = NAN, voc = NAN, nox = NAN;
    float pm1 = NAN, pm25 = NAN, pm4 = NAN, pm10 = NAN;
};

enum DisplayPage {
    kPageLive = 0,
    kPageParticles,
    kPageMinMax,
    kPageCo2Chart,
    kPageCo2Big,
    kPageSystem,
    kDisplayPageCount,
};

static DisplayReadings s_readings;
static DisplayReadings s_prevReadings;
static DisplayReadings s_minReadings;
static DisplayReadings s_maxReadings;
static int s_displayPage = kPageLive;

static constexpr int32_t kBacklightTimeoutSec = 300; // backlight auto-off after idle
static constexpr int32_t kSettingsTimeoutSec = 30;   // settings page saves and closes after idle

// Written from the Matter thread when the commissioning window opens
static volatile int32_t s_pairingCloseAtSec = 0;
// Written from the Matter thread while the Identify cluster is active
static volatile int32_t s_identifyEndSec = 0;
static esp_timer_handle_t s_identifyBlinkTimer = nullptr;
static bool s_identifyLedOn = false;
static bool s_identifyPageDrawn = false;
static int32_t s_lastActivitySec = 0;
static int32_t s_lastRotateSec = 0;
static bool s_autoRotate = true;
static bool s_pairingPageDrawn = false;

// Transient full-screen message (fan cleaning, rotation toggle, ...)
static char s_message[LCD2004::kColumns + 1] = "";
static int32_t s_messageEndSec = 0;
static bool s_messageDrawn = false;

// Settings editor state; s_editSettings is the working copy until saved
enum SettingsField {
    kFieldRefresh = 0,
    kFieldAltitude,
    kFieldRotatePeriod,
    kFieldAutoRotate,
    kSettingsFieldCount,
};
static bool s_settingsOpen = false;
static int s_settingsField = 0;
static AppSettings s_editSettings;

// CO2 history for the bar chart page, one sample per sensor cycle
static float s_co2History[LCD2004::kColumns];
static int s_co2HistoryCount = 0;

// Custom character sets; only one can live in the HD44780's CGRAM at a time
enum class LcdCharset { None, Trend, Bars, BigDigits };
static LcdCharset s_loadedCharset = LcdCharset::None;

static int32_t NowSec()
{
    return (int32_t)(esp_timer_get_time() / 1000000);
}

static const char* AirQualityText()
{
    AirQualityEnum airQuality = matterAirQualitySensor
        ? matterAirQualitySensor->GetLastAirQuality()
        : AirQualityEnum::kUnknown;
    switch (airQuality) {
    case AirQualityEnum::kGood:          return "Good";
    case AirQualityEnum::kFair:          return "Fair";
    case AirQualityEnum::kModerate:      return "Moderate";
    case AirQualityEnum::kPoor:          return "Poor";
    case AirQualityEnum::kVeryPoor:      return "Very poor";
    case AirQualityEnum::kExtremelyPoor: return "Extremely poor";
    default:                             return "Unknown";
    }
}

static void EnsureCharset(LcdCharset charset)
{
    if (s_loadedCharset == charset) {
        return;
    }

    switch (charset) {
    case LcdCharset::Trend: {
        // Slots 0/1: up/down arrows, printed as \x08/\x09 (CGRAM mirror, avoids NUL)
        static const uint8_t up[8]   = {0x04, 0x0E, 0x1F, 0x04, 0x04, 0x04, 0x00, 0x00};
        static const uint8_t down[8] = {0x00, 0x00, 0x04, 0x04, 0x04, 0x1F, 0x0E, 0x04};
        lcd->DefineChar(0, up);
        lcd->DefineChar(1, down);
        break;
    }
    case LcdCharset::Bars: {
        // Slot n: bar filled from the bottom over n+1 pixel rows
        for (uint8_t slot = 0; slot < 8; slot++) {
            uint8_t pattern[8];
            for (int row = 0; row < 8; row++) {
                pattern[row] = (row >= 7 - slot) ? 0x1F : 0x00;
            }
            lcd->DefineChar(slot, pattern);
        }
        break;
    }
    case LcdCharset::BigDigits: {
        static const uint8_t upperHalf[8] = {0x1F, 0x1F, 0x1F, 0x1F, 0x00, 0x00, 0x00, 0x00};
        static const uint8_t lowerHalf[8] = {0x00, 0x00, 0x00, 0x00, 0x1F, 0x1F, 0x1F, 0x1F};
        static const uint8_t twoBars[8]   = {0x1F, 0x1F, 0x1F, 0x00, 0x00, 0x1F, 0x1F, 0x1F};
        lcd->DefineChar(0, upperHalf);
        lcd->DefineChar(1, lowerHalf);
        lcd->DefineChar(2, twoBars);
        break;
    }
    default:
        break;
    }
    s_loadedCharset = charset;
}

static void TrackMinMax(float value, float& minValue, float& maxValue)
{
    if (std::isnan(value)) {
        return;
    }
    if (std::isnan(minValue) || value < minValue) {
        minValue = value;
    }
    if (std::isnan(maxValue) || value > maxValue) {
        maxValue = value;
    }
}

static char TrendChar(float current, float previous, float deadband)
{
    if (std::isnan(current) || std::isnan(previous)) {
        return ' ';
    }
    if (current - previous > deadband) {
        return '\x08';
    }
    if (previous - current > deadband) {
        return '\x09';
    }
    return ' ';
}

// 3x2-cell digits built from full/upper-half/lower-half/two-bar blocks
static void BigDigitCells(int digit, char* top, char* bottom)
{
    static const char* const kTop[10]    = {"FUF", " F ", "UUF", "UUF", "F F", "FUU", "FUU", "UUF", "FTF", "FTF"};
    static const char* const kBottom[10] = {"FLF", " F ", "FLL", "LLF", "UUF", "LLF", "FLF", "  F", "FLF", "LLF"};

    for (int i = 0; i < 3; i++) {
        const char cells[2] = {kTop[digit][i], kBottom[digit][i]};
        char* out[2] = {&top[i], &bottom[i]};
        for (int j = 0; j < 2; j++) {
            switch (cells[j]) {
            case 'F': *out[j] = '\xFF'; break; // ROM full block
            case 'U': *out[j] = '\x08'; break;
            case 'L': *out[j] = '\x09'; break;
            case 'T': *out[j] = '\x0A'; break;
            default:  *out[j] = ' ';    break;
            }
        }
    }
}

static bool RenderWaitingIfNoData()
{
    if (s_readings.valid) {
        return false;
    }
    lcd->Clear();
    lcd->WriteLine(1, "  Waiting for data");
    return true;
}

static void FormatRefreshValue(char* out, size_t size, uint32_t seconds)
{
    if (seconds < 60) {
        snprintf(out, size, "%us", (unsigned)seconds);
    } else {
        snprintf(out, size, "%umin", (unsigned)(seconds / 60));
    }
}

static void RenderSettingsPage()
{
    static const char* const kLabels[kSettingsFieldCount] = {
        "Refresh", "Altitude", "Rotate every", "Auto-rotate"
    };

    lcd->WriteLine(0, "Settings");

    // 3 visible rows; scroll so the selected field stays on screen
    int firstField = s_settingsField <= 2 ? 0 : s_settingsField - 2;
    for (int row = 0; row < 3; row++) {
        int field = firstField + row;
        char value[16] = "";
        switch (field) {
        case kFieldRefresh:
            FormatRefreshValue(value, sizeof(value), s_editSettings.refreshSeconds);
            break;
        case kFieldAltitude:
            snprintf(value, sizeof(value), "%um", s_editSettings.altitudeMeters);
            break;
        case kFieldRotatePeriod:
            snprintf(value, sizeof(value), "%us", s_editSettings.rotateSeconds);
            break;
        case kFieldAutoRotate:
            snprintf(value, sizeof(value), "%s", s_editSettings.autoRotate ? "ON" : "OFF");
            break;
        }
        char line[36]; // WriteLine truncates to the 20 columns
        snprintf(line, sizeof(line), "%c%-13s%6s", field == s_settingsField ? '>' : ' ', kLabels[field], value);
        lcd->WriteLine(row + 1, line);
    }
}

static void RenderDisplay()
{
    if (lcd == nullptr || !lcd->IsBacklightOn()) {
        return;
    }

    // Larger than one row on purpose: WriteLine() truncates to 20 columns
    char line[48];
    int32_t now = NowSec();

    if (now < s_messageEndSec) {
        if (!s_messageDrawn) {
            lcd->Clear();
            lcd->WriteLine(1, s_message);
            s_messageDrawn = true;
        }
        return;
    }
    s_messageDrawn = false;

    if (s_settingsOpen) {
        RenderSettingsPage();
        return;
    }

    if (now < s_identifyEndSec) {
        if (!s_identifyPageDrawn) {
            lcd->WriteLine(0, "");
            lcd->WriteLine(1, "     Identify!");
            lcd->WriteLine(2, "  LED is blinking");
            s_identifyPageDrawn = true;
        }
        snprintf(line, sizeof(line), "Ends in %ds", (int)(s_identifyEndSec - now));
        lcd->WriteLine(3, line);
        return;
    }
    s_identifyPageDrawn = false;

    if (now < s_pairingCloseAtSec) {
        if (!s_pairingPageDrawn) {
            lcd->WriteLine(0, "Matter pairing open");
            lcd->WriteLine(1, "");
            lcd->WriteLine(2, "Code: 3497-011-2332");
            s_pairingPageDrawn = true;
        }
        snprintf(line, sizeof(line), "Closes in %ds", (int)(s_pairingCloseAtSec - now));
        lcd->WriteLine(3, line);
        return;
    }
    s_pairingPageDrawn = false;

    switch (s_displayPage) {
    case kPageLive: // \xDF is the degree symbol in the HD44780 charset
        if (RenderWaitingIfNoData()) {
            break;
        }
        EnsureCharset(LcdCharset::Trend);
        snprintf(line, sizeof(line), "%.1f\xDF" "C%c %.1f%%RH%c",
                 s_readings.temperature, TrendChar(s_readings.temperature, s_prevReadings.temperature, 0.2f),
                 s_readings.humidity, TrendChar(s_readings.humidity, s_prevReadings.humidity, 1.0f));
        lcd->WriteLine(0, line);
        snprintf(line, sizeof(line), "CO2 %.0fppm%c VOC %.0f",
                 s_readings.co2, TrendChar(s_readings.co2, s_prevReadings.co2, 25.0f), s_readings.voc);
        lcd->WriteLine(1, line);
        snprintf(line, sizeof(line), "PM2.5 %.1f%c PM10 %.1f",
                 s_readings.pm25, TrendChar(s_readings.pm25, s_prevReadings.pm25, 0.3f), s_readings.pm10);
        lcd->WriteLine(2, line);
        snprintf(line, sizeof(line), "Air: %s", AirQualityText());
        lcd->WriteLine(3, line);
        break;

    case kPageParticles:
        if (RenderWaitingIfNoData()) {
            break;
        }
        lcd->WriteLine(0, "Particles \xE4g/m3"); // \xE4 = micro sign
        snprintf(line, sizeof(line), "PM1  %.1f  PM2.5 %.1f", s_readings.pm1, s_readings.pm25);
        lcd->WriteLine(1, line);
        snprintf(line, sizeof(line), "PM4  %.1f  PM10  %.1f", s_readings.pm4, s_readings.pm10);
        lcd->WriteLine(2, line);
        snprintf(line, sizeof(line), "NOx index %.0f", s_readings.nox);
        lcd->WriteLine(3, line);
        break;

    case kPageMinMax:
        if (RenderWaitingIfNoData()) {
            break;
        }
        lcd->WriteLine(0, "        MIN     MAX");
        snprintf(line, sizeof(line), "T\xDF" "C %8.1f %7.1f", s_minReadings.temperature, s_maxReadings.temperature);
        lcd->WriteLine(1, line);
        snprintf(line, sizeof(line), "RH%% %8.1f %7.1f", s_minReadings.humidity, s_maxReadings.humidity);
        lcd->WriteLine(2, line);
        snprintf(line, sizeof(line), "CO2 %8.0f %7.0f", s_minReadings.co2, s_maxReadings.co2);
        lcd->WriteLine(3, line);
        break;

    case kPageCo2Chart: {
        if (RenderWaitingIfNoData() || s_co2HistoryCount == 0) {
            break;
        }
        EnsureCharset(LcdCharset::Bars);

        float lo = s_co2History[0];
        float hi = s_co2History[0];
        for (int i = 1; i < s_co2HistoryCount; i++) {
            if (s_co2History[i] < lo) lo = s_co2History[i];
            if (s_co2History[i] > hi) hi = s_co2History[i];
        }
        if (hi - lo < 100.0f) { // keep a sane scale on flat data
            float mid = (hi + lo) / 2.0f;
            lo = mid - 50.0f;
            hi = mid + 50.0f;
        }

        char top[LCD2004::kColumns + 1];
        char bottom[LCD2004::kColumns + 1];
        for (int col = 0; col < LCD2004::kColumns; col++) {
            int idx = col - (LCD2004::kColumns - s_co2HistoryCount); // right-aligned
            if (idx < 0) {
                top[col] = ' ';
                bottom[col] = ' ';
                continue;
            }
            int level = (int)lroundf((s_co2History[idx] - lo) / (hi - lo) * 16.0f);
            if (level < 1) level = 1;
            if (level > 16) level = 16;
            int lowerFill = level > 8 ? 8 : level;
            int upperFill = level > 8 ? level - 8 : 0;
            bottom[col] = (char)(0x08 + lowerFill - 1);
            top[col] = upperFill == 0 ? ' ' : (char)(0x08 + upperFill - 1);
        }
        top[LCD2004::kColumns] = '\0';
        bottom[LCD2004::kColumns] = '\0';

        snprintf(line, sizeof(line), "CO2 %d-%dppm", (int)lo, (int)hi);
        lcd->WriteLine(0, line);
        lcd->WriteLine(1, top);
        lcd->WriteLine(2, bottom);
        snprintf(line, sizeof(line), "last %umin  now %.0f",
                 (unsigned)((LCD2004::kColumns * s_settings.refreshSeconds + 30) / 60), s_readings.co2);
        lcd->WriteLine(3, line);
        break;
    }

    case kPageCo2Big: {
        if (RenderWaitingIfNoData()) {
            break;
        }
        EnsureCharset(LcdCharset::BigDigits);

        int co2 = (int)lroundf(s_readings.co2);
        if (co2 < 0) co2 = 0;
        char digits[12];
        snprintf(digits, sizeof(digits), "%d", co2);

        char top[LCD2004::kColumns + 1];
        char bottom[LCD2004::kColumns + 1];
        memset(top, ' ', LCD2004::kColumns);
        memset(bottom, ' ', LCD2004::kColumns);
        top[LCD2004::kColumns] = '\0';
        bottom[LCD2004::kColumns] = '\0';

        int col = 1;
        for (const char* d = digits; *d != '\0' && col + 3 <= LCD2004::kColumns; d++) {
            BigDigitCells(*d - '0', &top[col], &bottom[col]);
            col += 4; // 3 cells + 1 gap
        }

        lcd->WriteLine(0, "CO2");
        lcd->WriteLine(1, top);
        lcd->WriteLine(2, bottom);
        snprintf(line, sizeof(line), "ppm   Air: %s", AirQualityText());
        lcd->WriteLine(3, line);
        break;
    }

    case kPageSystem: {
        int64_t uptimeSeconds = esp_timer_get_time() / 1000000;
        int days = uptimeSeconds / 86400;
        int hours = (uptimeSeconds % 86400) / 3600;
        int minutes = (uptimeSeconds % 3600) / 60;

        lcd->WriteLine(0, s_autoRotate ? "System status" : "System status     *");
        snprintf(line, sizeof(line), "Up %dd %02d:%02d", days, hours, minutes);
        lcd->WriteLine(1, line);
        snprintf(line, sizeof(line), "Heap %uk min %uk",
                 (unsigned)(esp_get_free_heap_size() / 1024), (unsigned)(esp_get_minimum_free_heap_size() / 1024));
        lcd->WriteLine(2, line);
        snprintf(line, sizeof(line), "FW %s", esp_app_get_description()->version);
        lcd->WriteLine(3, line);
        break;
    }

    default:
        break;
    }
}

static void ShowMessage(const char* text, int32_t seconds)
{
    snprintf(s_message, sizeof(s_message), "%s", text);
    s_messageEndSec = NowSec() + seconds;
    s_messageDrawn = false;
    RenderDisplay();
}

/*
 * Identify cluster: HA's "Identify" button (on any of the endpoints) makes
 * the LED blink and the LCD show a banner so the device can be spotted.
 * Requests arrive on the Matter thread; the LCD is only touched from the
 * esp_timer task, which picks the state up through s_identifyEndSec -- the
 * same pattern as the pairing page.
 */

static void StartIdentifyIndication(int32_t seconds)
{
    s_identifyEndSec = NowSec() + seconds;
    if (s_identifyBlinkTimer != nullptr) {
        esp_timer_stop(s_identifyBlinkTimer); // no-op unless already blinking
        esp_timer_start_periodic(s_identifyBlinkTimer, 500000);
    }
}

static void StopIdentifyIndication()
{
    s_identifyEndSec = 0;
    if (s_identifyBlinkTimer != nullptr) {
        esp_timer_stop(s_identifyBlinkTimer);
    }
    // Reapply the Matter attribute state the blinking trampled on
    chip::DeviceLayer::SystemLayer().ScheduleLambda([]() {
        if (matterExtendedColorLight) {
            matterExtendedColorLight->Initialize();
        }
    });
}

static void IdentifyBlinkTimerCallback(void *arg)
{
    if (NowSec() >= s_identifyEndSec) {
        StopIdentifyIndication(); // deadline passed without a STOP callback
        return;
    }
    s_identifyLedOn = !s_identifyLedOn;
    if (matterExtendedColorLight) {
        matterExtendedColorLight->SetIdentifyBlink(s_identifyLedOn);
    }
}

esp_err_t app_identification_handle(identification::callback_type_t type, uint16_t endpoint_id,
                                    uint8_t effect_id, uint8_t effect_variant)
{
    switch (type) {
    case identification::START: {
        // IdentifyTime holds the requested duration; the identify server also
        // sends STOP when it expires, the deadline is only a safety net
        uint16_t seconds = 15;
        attribute_t* attr = attribute::get(endpoint_id, Identify::Id, Identify::Attributes::IdentifyTime::Id);
        if (attr != nullptr) {
            esp_matter_attr_val_t val = esp_matter_invalid(nullptr);
            if (attribute::get_val(attr, &val) == ESP_OK && val.val.u16 > 0) {
                seconds = val.val.u16;
            }
        }
        ESP_LOGI(TAG, "Identify start on endpoint %u for %u s", endpoint_id, seconds);
        StartIdentifyIndication(seconds);
        break;
    }
    case identification::STOP:
        ESP_LOGI(TAG, "Identify stop on endpoint %u", endpoint_id);
        StopIdentifyIndication();
        break;
    case identification::EFFECT:
        ESP_LOGI(TAG, "Identify effect 0x%02X on endpoint %u", effect_id, endpoint_id);
        switch ((Identify::EffectIdentifierEnum)effect_id) {
        case Identify::EffectIdentifierEnum::kBreathe:
            StartIdentifyIndication(15);
            break;
        case Identify::EffectIdentifierEnum::kChannelChange:
            StartIdentifyIndication(8);
            break;
        case Identify::EffectIdentifierEnum::kFinishEffect:
            if (s_identifyEndSec > 0) {
                StartIdentifyIndication(1); // wrap up the running effect
            }
            break;
        case Identify::EffectIdentifierEnum::kStopEffect:
            StopIdentifyIndication();
            break;
        default: // kBlink, kOkay and anything unknown: a short blink burst
            StartIdentifyIndication(2);
            break;
        }
        break;
    }
    return ESP_OK;
}

static void OpenSettings()
{
    s_editSettings = s_settings;
    s_editSettings.autoRotate = s_autoRotate; // reflect a live pause; saving persists it
    s_settingsField = 0;
    s_settingsOpen = true;
    s_messageEndSec = 0;
    s_pairingPageDrawn = false; // force a full redraw when the menu closes
    RenderDisplay();
}

static void ApplyAndCloseSettings()
{
    s_settingsOpen = false;
    s_pairingPageDrawn = false;

    bool changed = s_editSettings.refreshSeconds != s_settings.refreshSeconds ||
                   s_editSettings.altitudeMeters != s_settings.altitudeMeters ||
                   s_editSettings.rotateSeconds != s_settings.rotateSeconds ||
                   s_editSettings.autoRotate != s_settings.autoRotate;

    if (s_editSettings.refreshSeconds != s_settings.refreshSeconds && sensor_timer_handle) {
        esp_timer_stop(sensor_timer_handle);
        esp_timer_start_periodic(sensor_timer_handle, (uint64_t)s_editSettings.refreshSeconds * 1000000ULL);
        ESP_LOGI(TAG, "Sensor refresh period set to %u s", (unsigned)s_editSettings.refreshSeconds);
    }

    if (s_editSettings.altitudeMeters != s_settings.altitudeMeters && airQualitySensor) {
        int status = airQualitySensor->UpdateAltitude(s_editSettings.altitudeMeters);
        if (status == 0) {
            ESP_LOGI(TAG, "Sensor altitude set to %u m", s_editSettings.altitudeMeters);
        } else {
            ESP_LOGE(TAG, "Failed to set sensor altitude: %d", status);
        }
    }

    s_autoRotate = s_editSettings.autoRotate;
    s_lastRotateSec = NowSec();

    if (changed) {
        s_settings = s_editSettings;
        s_settings.Save();
        ShowMessage("   Settings saved", 2);
    } else {
        RenderDisplay();
    }
}

static void StepSettingsField(int direction)
{
    switch (s_settingsField) {
    case kFieldRefresh: {
        constexpr int count = sizeof(AppSettings::kRefreshChoices) / sizeof(AppSettings::kRefreshChoices[0]);
        int index = 0;
        for (int i = 0; i < count; i++) {
            if (AppSettings::kRefreshChoices[i] == s_editSettings.refreshSeconds) {
                index = i;
                break;
            }
        }
        index = (index + direction + count) % count;
        s_editSettings.refreshSeconds = AppSettings::kRefreshChoices[index];
        break;
    }
    case kFieldAltitude: {
        int32_t altitude = (int32_t)s_editSettings.altitudeMeters + direction * AppSettings::kAltitudeStepMeters;
        if (altitude < 0) {
            altitude = AppSettings::kAltitudeMaxMeters;
        } else if (altitude > AppSettings::kAltitudeMaxMeters) {
            altitude = 0;
        }
        s_editSettings.altitudeMeters = (uint16_t)altitude;
        break;
    }
    case kFieldRotatePeriod: {
        int32_t rotate = (int32_t)s_editSettings.rotateSeconds + direction;
        if (rotate < AppSettings::kRotateMinSec) {
            rotate = AppSettings::kRotateMaxSec;
        } else if (rotate > AppSettings::kRotateMaxSec) {
            rotate = AppSettings::kRotateMinSec;
        }
        s_editSettings.rotateSeconds = (uint8_t)rotate;
        break;
    }
    case kFieldAutoRotate:
        s_editSettings.autoRotate = !s_editSettings.autoRotate;
        break;
    default:
        break;
    }
    RenderDisplay();
}

static void UpdateDisplay()
{
    std::vector<Sensor::Measurement> measurements = airQualitySensor->ReadAllMeasurements();
    if (measurements.empty()) {
        return;
    }

    DisplayReadings readings;
    for (const auto& measurement : measurements) {
        switch (measurement.type) {
        case Sensor::MeasurementType::Temperature:      readings.temperature = measurement.value; break;
        case Sensor::MeasurementType::RelativeHumidity: readings.humidity = measurement.value; break;
        case Sensor::MeasurementType::CO2:              readings.co2 = measurement.value; break;
        case Sensor::MeasurementType::VOC:              readings.voc = measurement.value; break;
        case Sensor::MeasurementType::NOx:              readings.nox = measurement.value; break;
        case Sensor::MeasurementType::PM1p0:            readings.pm1 = measurement.value; break;
        case Sensor::MeasurementType::PM2p5:            readings.pm25 = measurement.value; break;
        case Sensor::MeasurementType::PM4p0:            readings.pm4 = measurement.value; break;
        case Sensor::MeasurementType::PM10p0:           readings.pm10 = measurement.value; break;
        default: break;
        }
    }
    readings.valid = true;
    s_prevReadings = s_readings;
    s_readings = readings;

    if (!std::isnan(readings.co2)) {
        if (s_co2HistoryCount == LCD2004::kColumns) {
            memmove(&s_co2History[0], &s_co2History[1], sizeof(float) * (LCD2004::kColumns - 1));
            s_co2HistoryCount--;
        }
        s_co2History[s_co2HistoryCount++] = readings.co2;
    }

    TrackMinMax(readings.temperature, s_minReadings.temperature, s_maxReadings.temperature);
    TrackMinMax(readings.humidity, s_minReadings.humidity, s_maxReadings.humidity);
    TrackMinMax(readings.co2, s_minReadings.co2, s_maxReadings.co2);
    TrackMinMax(readings.pm25, s_minReadings.pm25, s_maxReadings.pm25);

    RenderDisplay();
}

// The VOC gas-index algorithm state is persisted to NVS at this cadence so the
// algorithm resumes after a reboot instead of relearning from scratch. Kept
// coarse to limit flash wear; losing at most this much learning on a sudden
// power loss is fine.
static constexpr int32_t kVocStateSaveIntervalSec = 1800; // 30 min
static int32_t s_lastVocSaveSec = 0;

// Timer callback to measure air quality
void UpdateSensorsTimerCallback(void *arg)
{
    matterAirQualitySensor->UpdateMeasurements();
    matterTemperatureSensor->UpdateMeasurements();
    matterHumiditySensor->UpdateMeasurements();

    if (lcd) {
        UpdateDisplay();
    }

    if (airQualitySensor && NowSec() - s_lastVocSaveSec >= kVocStateSaveIntervalSec) {
        airQualitySensor->PersistState();
        s_lastVocSaveSec = NowSec();
    }
}

/*
 * UI buttons. The iot_button callbacks run in the esp_timer task -- the same
 * task that runs UpdateSensorsTimerCallback -- so LCD and sensor access needs
 * no extra locking. Matter interactions are scheduled onto the Matter thread.
 */

static constexpr int kDisplayButtonGpio = 23; // button 1
static constexpr int kControlButtonGpio = 22; // button 2

// Returns true when the press only woke the display; the action is swallowed
static bool WakeDisplayOnly()
{
    s_lastActivitySec = NowSec();
    if (lcd && !lcd->IsBacklightOn()) {
        lcd->SetBacklight(true);
        RenderDisplay();               // redraw fresh content while still blanked
        lcd->SetDisplayVisible(true);  // then reveal it
        return true;
    }
    return false;
}

static void OnDisplayPageButton(void *arg, void *data)
{
    if (WakeDisplayOnly()) {
        return;
    }
    int32_t now = NowSec();
    s_messageEndSec = 0; // dismiss any transient message

    if (s_settingsOpen) {
        s_settingsField = (s_settingsField + 1) % kSettingsFieldCount;
        RenderDisplay();
        return;
    }

    if (now < s_pairingCloseAtSec) {
        // Dismiss the pairing overlay (the commissioning window stays open)
        s_pairingCloseAtSec = 0;
        RenderDisplay();
        return;
    }

    // Manual navigation takes over from the auto rotation
    if (s_autoRotate) {
        s_autoRotate = false;
        ESP_LOGI(TAG, "Auto-rotate paused");
    }

    s_displayPage = (s_displayPage + 1) % kDisplayPageCount;
    ESP_LOGI(TAG, "Display page %d", s_displayPage);
    RenderDisplay();
}

static void OnSettingsMenuButton(void *arg, void *data)
{
    if (WakeDisplayOnly()) {
        return;
    }
    if (lcd == nullptr) {
        return;
    }
    if (s_settingsOpen) {
        ApplyAndCloseSettings();
    } else {
        OpenSettings();
    }
}

static void OnForceRefreshButton(void *arg, void *data)
{
    if (WakeDisplayOnly()) {
        return;
    }
    if (s_settingsOpen) {
        return;
    }
    ESP_LOGI(TAG, "Manual sensor refresh");
    UpdateSensorsTimerCallback(nullptr);
}

static void OnLightToggleButton(void *arg, void *data)
{
    if (WakeDisplayOnly()) {
        return;
    }
    if (s_settingsOpen) {
        StepSettingsField(1);
        return;
    }
    chip::DeviceLayer::SystemLayer().ScheduleLambda([]() {
        if (matterExtendedColorLight) {
            bool on = matterExtendedColorLight->GetOnOff();
            matterExtendedColorLight->SetLightOnOff(!on);
        }
    });
}

static void OnCommissioningWindowButton(void *arg, void *data)
{
    if (WakeDisplayOnly()) {
        return;
    }
    if (s_settingsOpen) {
        return; // long-pressing button 2 in the menu is the fast value scroll
    }
    chip::DeviceLayer::SystemLayer().ScheduleLambda([]() {
        chip::CommissioningWindowManager& commissionMgr = chip::Server::GetInstance().GetCommissioningWindowManager();
        if (commissionMgr.IsCommissioningWindowOpen()) {
            ESP_LOGI(TAG, "Commissioning window already open");
            return;
        }
        CHIP_ERROR err = commissionMgr.OpenBasicCommissioningWindow(chip::System::Clock::Seconds16(300),
                                                                    chip::CommissioningWindowAdvertisement::kAllSupported);
        if (err != CHIP_NO_ERROR) {
            ESP_LOGE(TAG, "Failed to open commissioning window: %" CHIP_ERROR_FORMAT, err.Format());
        } else {
            ESP_LOGI(TAG, "Commissioning window open for 300 s");
            s_pairingCloseAtSec = NowSec() + 300; // the display ticker shows the pairing page
        }
    });
}

static void OnFanCleaningButton(void *arg, void *data)
{
    if (WakeDisplayOnly()) {
        return;
    }
    if (s_settingsOpen) {
        StepSettingsField(-1);
        return;
    }
    if (!airQualitySensor) {
        return;
    }
    int status = airQualitySensor->StartFanCleaning();
    if (status == 0) {
        ESP_LOGI(TAG, "SEN66 fan cleaning started (takes ~10 s)");
        ShowMessage("   Fan cleaning...", 12);
    } else {
        ESP_LOGE(TAG, "Failed to start fan cleaning: %d", status);
    }
}

// Serial LONG_PRESS_HOLD events on button 2, throttled to a controlled
// fast-scroll through the selected settings value
static void OnValueFastScroll(void *arg, void *data)
{
    if (!s_settingsOpen) {
        return;
    }
    static int64_t s_lastStepUs = 0;
    int64_t now = esp_timer_get_time();
    if (now - s_lastStepUs < 150000) {
        return;
    }
    s_lastStepUs = now;
    s_lastActivitySec = NowSec(); // holding counts as activity for the menu timeout
    StepSettingsField(1);
}

static button_handle_t CreateUiButton(int gpio)
{
    button_config_t btn_cfg = {};
    // Kconfig's 5 s default is for the BOOT factory-reset hold; UI gestures
    // want something snappier
    btn_cfg.long_press_time = 1500;
    button_gpio_config_t gpio_cfg = {};
    gpio_cfg.gpio_num = gpio;
    gpio_cfg.active_level = 0;

    button_handle_t handle = nullptr;
    esp_err_t err = iot_button_new_gpio_device(&btn_cfg, &gpio_cfg, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create button on GPIO %d: %s", gpio, esp_err_to_name(err));
        return nullptr;
    }
    ESP_LOGI(TAG, "UI button ready on GPIO %d", gpio);
    return handle;
}

static void RegisterUiButtons()
{
    button_handle_t displayButton = CreateUiButton(kDisplayButtonGpio);
    if (displayButton) {
        iot_button_register_cb(displayButton, BUTTON_SINGLE_CLICK, nullptr, OnDisplayPageButton, nullptr);
        iot_button_register_cb(displayButton, BUTTON_DOUBLE_CLICK, nullptr, OnForceRefreshButton, nullptr);
        iot_button_register_cb(displayButton, BUTTON_LONG_PRESS_START, nullptr, OnSettingsMenuButton, nullptr);
    }

    button_handle_t controlButton = CreateUiButton(kControlButtonGpio);
    if (controlButton) {
        iot_button_register_cb(controlButton, BUTTON_SINGLE_CLICK, nullptr, OnLightToggleButton, nullptr);
        iot_button_register_cb(controlButton, BUTTON_DOUBLE_CLICK, nullptr, OnFanCleaningButton, nullptr);
        iot_button_register_cb(controlButton, BUTTON_LONG_PRESS_START, nullptr, OnCommissioningWindowButton, nullptr);
        iot_button_register_cb(controlButton, BUTTON_LONG_PRESS_HOLD, nullptr, OnValueFastScroll, nullptr);
    }
}

/*
 * 1 s display ticker: backlight timeout, auto page rotation and refreshing
 * the pairing/message overlay pages.
 */
static void DisplayTickCallback(void *arg)
{
    if (lcd == nullptr) {
        return;
    }

    int32_t now = NowSec();

    if (now < s_identifyEndSec) {
        s_lastActivitySec = now; // keep the display on while identifying
        if (!lcd->IsBacklightOn()) {
            lcd->SetBacklight(true);
            RenderDisplay();               // draw the banner while still blanked
            lcd->SetDisplayVisible(true);  // then reveal it
        }
    }

    if (s_settingsOpen) {
        if (now - s_lastActivitySec >= kSettingsTimeoutSec) {
            ApplyAndCloseSettings(); // idle timeout saves and leaves the menu
        }
        return;
    }

    bool overlayActive = now < s_messageEndSec || now < s_pairingCloseAtSec || now < s_identifyEndSec;
    static bool s_overlayWasActive = false;

    if (lcd->IsBacklightOn() && !overlayActive && now - s_lastActivitySec >= kBacklightTimeoutSec) {
        lcd->SetBacklight(false);
        lcd->SetDisplayVisible(false); // blank the (reflective) panel too
    }

    if (!lcd->IsBacklightOn()) {
        return;
    }

    if (overlayActive) {
        RenderDisplay(); // keeps the pairing countdown fresh
        s_overlayWasActive = true;
        return;
    }

    if (s_overlayWasActive) {
        // Overlay just expired; put the normal page back
        s_overlayWasActive = false;
        RenderDisplay();
        return;
    }

    if (s_autoRotate && now - s_lastRotateSec >= s_settings.rotateSeconds) {
        s_lastRotateSec = now;
        s_displayPage = (s_displayPage + 1) % kDisplayPageCount;
        RenderDisplay();
    }
}

static void StartDisplayTimer()
{
    if (lcd == nullptr) {
        return;
    }

    s_lastActivitySec = NowSec();

    esp_timer_create_args_t timer_args = {
        .callback = &DisplayTickCallback,
        .arg = nullptr,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "display_tick",
        .skip_unhandled_events = true,
    };

    esp_timer_handle_t handle = nullptr;
    if (esp_timer_create(&timer_args, &handle) == ESP_OK) {
        esp_timer_start_periodic(handle, 1000000);
    }
}

static void CreateIdentifyBlinkTimer()
{
    esp_timer_create_args_t timer_args = {
        .callback = &IdentifyBlinkTimerCallback,
        .arg = nullptr,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "identify_blink",
        .skip_unhandled_events = true,
    };

    esp_err_t err = esp_timer_create(&timer_args, &s_identifyBlinkTimer);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create identify blink timer: %s", esp_err_to_name(err));
    }
}

void StartUpdateSensorsTimer()
{
    // Setup periodic timer to update sensor measurements

    esp_timer_create_args_t timer_args = {
        .callback = &UpdateSensorsTimerCallback,
        .arg = nullptr,
        .dispatch_method = ESP_TIMER_TASK, // Run callback in a task (safer for I2C)
        .name = "update_sensors_timer",
        .skip_unhandled_events = true, // Skip if previous callback is still running
    };
    
    esp_err_t err = esp_timer_create(&timer_args, &sensor_timer_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create timer: %s", esp_err_to_name(err));
        return;
    }
    
    err = esp_timer_start_periodic(sensor_timer_handle, (uint64_t)s_settings.refreshSeconds * 1000000ULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start timer: %s", esp_err_to_name(err));
    }
    ESP_LOGI(TAG, "Update sensors timer started (every %u s).", (unsigned)s_settings.refreshSeconds);
}

extern "C" void app_main()
{
    esp_err_t err = ESP_OK;

    /* Initialize the ESP NVS layer */
    nvs_flash_init();

    s_settings.Load();
    s_autoRotate = s_settings.autoRotate;

    /* Initialize driver */
    app_driver_handle_t button_handle = app_driver_button_init();
    app_reset_button_register(button_handle);

    /* Create a Matter node and add the mandatory Root Node device type on endpoint 0 */
    matterNode = MatterNode::Create();
    ABORT_APP_ON_FAILURE(matterNode != nullptr, ESP_LOGE(TAG, "Failed to create Matter node"));

    matterExtendedColorLight = matterExtendedColorLight->CreateEndpoint(matterNode);
    ABORT_APP_ON_FAILURE(matterExtendedColorLight != nullptr, ESP_LOGE(TAG, "Failed to create extended color light endpoint"));

    // Note this is a global variable used in other source files.
    light_endpoint_id = matterExtendedColorLight->GetId();

    ESP_LOGI(TAG, "Light created with endpoint_id %d", light_endpoint_id);

    airQualitySensor = std::make_shared<SensirionSEN66>((float)s_settings.altitudeMeters);
    airQualitySensor->Init();

    /* The SEN66 HAL created the I2C master bus; the LCD shares it */
    i2c_master_bus_handle_t i2c_bus = nullptr;
    if (i2c_master_get_bus_handle(I2C_NUM_0, &i2c_bus) == ESP_OK) {
        lcd = LCD2004::Create(i2c_bus);
    }

    RegisterUiButtons();
    StartDisplayTimer();
    CreateIdentifyBlinkTimer();

    // Create Matter Air Quality Sensor Endpoint
    matterAirQualitySensor = MatterAirQualitySensor::CreateEndpoint(matterNode, airQualitySensor, matterExtendedColorLight);

    // Create Matter Temperature Sensor Endpoint
    matterTemperatureSensor = MatterTemperatureSensor::CreateEndpoint(matterNode, airQualitySensor);

    // Create Humidity Sensor Endpoint
    matterHumiditySensor = MatterHumiditySensor::CreateEndpoint(matterNode, airQualitySensor);

#if CHIP_DEVICE_CONFIG_ENABLE_THREAD && CHIP_DEVICE_CONFIG_ENABLE_WIFI_STATION
    // Enable secondary network interface
    secondary_network_interface::config_t secondary_network_interface_config;
    endpoint = endpoint::secondary_network_interface::create(node, &secondary_network_interface_config, ENDPOINT_FLAG_NONE, nullptr);
    ABORT_APP_ON_FAILURE(endpoint != nullptr, ESP_LOGE(TAG, "Failed to create secondary network interface endpoint"));
#endif

#if CHIP_DEVICE_CONFIG_ENABLE_THREAD
    /* Set OpenThread platform config */
    esp_openthread_platform_config_t config = {
        .radio_config = ESP_OPENTHREAD_DEFAULT_RADIO_CONFIG(),
        .host_config = ESP_OPENTHREAD_DEFAULT_HOST_CONFIG(),
        .port_config = ESP_OPENTHREAD_DEFAULT_PORT_CONFIG(),
    };
    set_openthread_platform_config(&config);
#endif

#ifdef CONFIG_ENABLE_SET_CERT_DECLARATION_API
    auto * dac_provider = get_dac_provider();
#ifdef CONFIG_SEC_CERT_DAC_PROVIDER
    static_cast<ESP32SecureCertDACProvider *>(dac_provider)->SetCertificationDeclaration(cdSpan);
#elif defined(CONFIG_FACTORY_PARTITION_DAC_PROVIDER)
    static_cast<ESP32FactoryDataProvider *>(dac_provider)->SetCertificationDeclaration(cdSpan);
#endif
#endif // CONFIG_ENABLE_SET_CERT_DECLARATION_API

    /* Matter start */
    err = matterNode->StartMatter();
    ABORT_APP_ON_FAILURE(err == ESP_OK, ESP_LOGE(TAG, "Failed to start Matter, err:%d", err));

    StartUpdateSensorsTimer();

#if CONFIG_ENABLE_ENCRYPTED_OTA
    err = esp_matter_ota_requestor_encrypted_init(s_decryption_key, s_decryption_key_len);
    ABORT_APP_ON_FAILURE(err == ESP_OK, ESP_LOGE(TAG, "Failed to initialized the encrypted OTA, err: %d", err));
#endif // CONFIG_ENABLE_ENCRYPTED_OTA

#if CONFIG_ENABLE_CHIP_SHELL
    esp_matter::console::diagnostics_register_commands();
    esp_matter::console::wifi_register_commands();
    esp_matter::console::factoryreset_register_commands();
#if CONFIG_OPENTHREAD_CLI
    esp_matter::console::otcli_register_commands();
#endif
    esp_matter::console::init();
#endif
}
