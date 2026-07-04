#include "LCD2004.h"

#include <esp_log.h>
#include <esp_rom_sys.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

static const char* TAG = "LCD2004";

// PCF8574 to HD44780 pin mapping used by the common I2C backpacks:
// P0=RS, P1=RW, P2=E, P3=backlight, P4-P7=D4-D7
static constexpr uint8_t kRs = 0x01;
static constexpr uint8_t kEnable = 0x04;
static constexpr uint8_t kBacklight = 0x08;

LCD2004* LCD2004::Create(i2c_master_bus_handle_t bus)
{
    static constexpr uint8_t kAddresses[] = {0x27, 0x3F};

    uint8_t address = 0;
    for (uint8_t candidate : kAddresses) {
        if (i2c_master_probe(bus, candidate, 100) == ESP_OK) {
            address = candidate;
            break;
        }
    }
    if (address == 0) {
        ESP_LOGE(TAG, "No PCF8574 backpack found at 0x27 or 0x3F");
        return nullptr;
    }

    i2c_device_config_t dev_cfg = {};
    dev_cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dev_cfg.device_address = address;
    dev_cfg.scl_speed_hz = 100000;

    i2c_master_dev_handle_t device = nullptr;
    esp_err_t err = i2c_master_bus_add_device(bus, &dev_cfg, &device);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add LCD device: %s", esp_err_to_name(err));
        return nullptr;
    }

    ESP_LOGI(TAG, "Found display at address 0x%02X", address);
    auto* lcd = new LCD2004(device);
    lcd->Initialize();
    return lcd;
}

LCD2004::LCD2004(i2c_master_dev_handle_t device) : m_device(device)
{
}

void LCD2004::ExpanderWrite(uint8_t value)
{
    uint8_t byte = value | (m_backlightOn ? kBacklight : 0);
    esp_err_t err = i2c_master_transmit(m_device, &byte, 1, 100);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "I2C write failed: %s", esp_err_to_name(err));
    }
}

void LCD2004::SetBacklight(bool on)
{
    m_backlightOn = on;
    ExpanderWrite(0);
}

void LCD2004::SetDisplayVisible(bool visible)
{
    Command(visible ? 0x0C : 0x08); // display on / off, cursor off
}

void LCD2004::DefineChar(uint8_t slot, const uint8_t pattern[8])
{
    Command(0x40 | ((slot & 0x07) << 3)); // set CGRAM address
    for (int i = 0; i < 8; i++) {
        WriteByte(pattern[i], true);
    }
    // WriteLine() sets a DDRAM address before writing, so no need to restore it here
}

void LCD2004::PulseNibble(uint8_t value)
{
    ExpanderWrite(value | kEnable);
    esp_rom_delay_us(1); // enable pulse width >= 450 ns
    ExpanderWrite(value);
    esp_rom_delay_us(50); // instruction execution time >= 37 us
}

void LCD2004::WriteByte(uint8_t value, bool isData)
{
    uint8_t control = isData ? kRs : 0;
    PulseNibble((value & 0xF0) | control);
    PulseNibble(static_cast<uint8_t>(value << 4) | control);
}

void LCD2004::Command(uint8_t command)
{
    WriteByte(command, false);
}

void LCD2004::Initialize()
{
    vTaskDelay(pdMS_TO_TICKS(50)); // power-on ramp-up

    // HD44780 "initialization by instruction" into 4-bit mode
    PulseNibble(0x30);
    vTaskDelay(pdMS_TO_TICKS(5));
    PulseNibble(0x30);
    esp_rom_delay_us(150);
    PulseNibble(0x30);
    esp_rom_delay_us(150);
    PulseNibble(0x20);

    Command(0x28); // function set: 4-bit, 2 lines, 5x8 font
    Command(0x08); // display off
    Clear();
    Command(0x06); // entry mode: increment, no shift
    Command(0x0C); // display on, cursor off
}

void LCD2004::Clear()
{
    Command(0x01);
    vTaskDelay(pdMS_TO_TICKS(2)); // clear needs ~1.5 ms
}

void LCD2004::WriteLine(int row, const std::string& text)
{
    static constexpr uint8_t kRowOffsets[kRows] = {0x00, 0x40, 0x14, 0x54};

    if (row < 0 || row >= kRows) {
        return;
    }

    Command(0x80 | kRowOffsets[row]);
    for (int i = 0; i < kColumns; i++) {
        char c = i < static_cast<int>(text.size()) ? text[i] : ' ';
        WriteByte(static_cast<uint8_t>(c), true);
    }
}
