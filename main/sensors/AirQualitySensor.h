#pragma once

#include "Sensor.h"
#include "TemperatureSensor.h"
#include "RelativeHumiditySensor.h"
#include <string>


class AirQualitySensor : public TemperatureSensor, public RelativeHumiditySensor
{

public:

    // Constructor
    explicit AirQualitySensor(float sensorAltitude = 0.0f)
        : m_sensorAltitude(sensorAltitude)
    {
    }

    // Virtual destructor for proper cleanup
    virtual ~AirQualitySensor() = default;

    // Initialize the sensor
    bool Init() override;
    
    // Methods from Sensor
    std::vector<Measurement> ReadAllMeasurements() override = 0;

    // Methods from TemperatureSensor
    std::optional<float> MeasureTemperature() override = 0;

    // Methods from RelativeHumiditySensor
    std::optional<float> MeasureRelativeHumidity() override = 0;

    virtual std::string GetProductName() = 0;

    virtual std::string GetVendorName() = 0;

    virtual int GetFirmwareVersion(int* firmwareMajorVersion, int* firmwareMinorVersion) = 0;

    // Get the set of measurement types supported by this sensor
    virtual std::set<MeasurementType> GetSupportedMeasurements() const = 0;

    virtual int ActivateAutomaticSelfCalibration() = 0;

    virtual int SetAmbientPressure(float ambientPressureKiloPascal) = 0;

    // Starts the fan cleaning cycle, if the sensor supports it
    virtual int StartFanCleaning() { return -1; }

    // Persists any slowly-learned calibration/algorithm state to non-volatile
    // storage so it survives a reboot. Called periodically by the app. Default
    // no-op for sensors that have no such state.
    virtual void PersistState() {}

    // Applies a new pressure-compensation altitude (meters) at runtime and
    // remembers it. May briefly interrupt measurement, depending on the sensor.
    virtual int UpdateAltitude(float altitudeMeters);

protected:

    virtual int SetAltitude(float altitude) = 0;

    virtual int StartContinuousMeasurement() = 0;

    float m_sensorAltitude;
};
