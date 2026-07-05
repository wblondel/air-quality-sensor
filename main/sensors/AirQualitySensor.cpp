#include "AirQualitySensor.h"
#include <map>

bool AirQualitySensor::Init()
{
    SetAltitude(m_sensorAltitude);

    return true;
}

int AirQualitySensor::UpdateAltitude(float altitudeMeters)
{
    m_sensorAltitude = altitudeMeters;

    return SetAltitude(altitudeMeters);
}
