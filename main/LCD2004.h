#pragma once

#include <driver/i2c_master.h>
#include <string>

// 2004A 20x4 character LCD (HD44780) behind a PCF8574 I2C backpack (HW-61)
class LCD2004
{
public:
    static constexpr int kColumns = 20;
    static constexpr int kRows = 4;

    // Probes the bus for the backpack (0x27, then 0x3F) and initializes the
    // display in 4-bit mode. Returns nullptr if no display is found.
    static LCD2004* Create(i2c_master_bus_handle_t bus);

    // Writes text on the given row (0-3), padded/truncated to 20 columns
    void WriteLine(int row, const std::string& text);

    void Clear();

    void SetBacklight(bool on);

    bool IsBacklightOn() const { return m_backlightOn; }

    // Blanks or shows the display content (DDRAM is retained either way)
    void SetDisplayVisible(bool visible);

    // Programs one of the 8 CGRAM slots with a 5x8 glyph (one byte per pixel
    // row). The glyph is printable as character 0x08 + slot.
    void DefineChar(uint8_t slot, const uint8_t pattern[8]);

private:
    explicit LCD2004(i2c_master_dev_handle_t device);

    void Initialize();
    void PulseNibble(uint8_t value);
    void WriteByte(uint8_t value, bool isData);
    void Command(uint8_t command);
    void ExpanderWrite(uint8_t value);

    i2c_master_dev_handle_t m_device;
    bool m_backlightOn = true;
};
