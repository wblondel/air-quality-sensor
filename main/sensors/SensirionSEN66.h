#include "AirQualitySensor.h"
#include "TemperatureSensor.h"
#include <set>
#include <vector>
#include <optional>

class SensirionSEN66 : public AirQualitySensor
{
public:
    // Constructor
    explicit SensirionSEN66(float sensorAltitude = 0.0f)
      : AirQualitySensor(sensorAltitude)
    {
    }

    // Initialize the sensor
    bool Init() override;

    virtual std::string GetProductName() override;

    virtual std::string GetVendorName() override;

    int GetFirmwareVersion(int* firmwareMajorVersion, int* firmwareMinorVersion) override;

    // Get the set of measurement types supported by this sensor
    std::set<Sensor::MeasurementType> GetSupportedMeasurements() const override;

    // Read all supported measurements
    std::vector<AirQualitySensor::Measurement> ReadAllMeasurements() override;

    // Methods from TemperatureSensor
    std::optional<float> MeasureTemperature() override;

    // Methods from RelativeHumiditySensor
    std::optional<float> MeasureRelativeHumidity() override;

    int ActivateAutomaticSelfCalibration() override;

    int StartFanCleaning() override;

    // Start continuous measurement mode
    int StartContinuousMeasurement() override;

    int SetAmbientPressure(float ambientPressureKiloPascal) override;

    // The SEN66 only accepts the altitude command in idle mode, so this stops
    // and restarts continuous measurement around it.
    int UpdateAltitude(float altitudeMeters) override;

    // Reads the current VOC gas-index algorithm state and saves it to NVS, so
    // the algorithm resumes instead of restarting its learning phase after a
    // reboot. Skips the write when the state is unchanged.
    void PersistState() override;

protected:
    // Set sensor altitude
    int SetAltitude(float altitude) override;

private:

    struct SensorData {
        uint16_t pm1p0;
        uint16_t pm2p5;
        uint16_t pm4p0;
        uint16_t pm10p0;
        int16_t humidity;
        int16_t temperature;
        int16_t vocIndex;
        int16_t noxIndex;
        uint16_t co2;

        SensorData();
    };

    int16_t ReadSensorData(SensorData& data);

    // Restores the VOC gas-index algorithm state from NVS (if any) into the
    // sensor. Must be called in idle mode, before StartContinuousMeasurement.
    void RestoreVocState();

    // The SEN66 VOC algorithm state is exactly 8 bytes.
    static constexpr size_t kVocStateSize = 8;

    // Last VOC state written to / read from NVS, used to skip redundant writes.
    bool m_hasSavedVocState = false;
    uint8_t m_lastSavedVocState[kVocStateSize] = {0};

    // Resets and reconfigures the sensor after repeated read failures.
    void Recover();

    // Consecutive ReadSensorData failures; once this reaches
    // kMaxConsecutiveReadFailures a recovery is attempted.
    int m_consecutiveReadFailures = 0;
    static constexpr int kMaxConsecutiveReadFailures = 5;

};
