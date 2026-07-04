#include <cmath>
#include "MatterSensorBase.h"

#include <app/clusters/relative-humidity-measurement-server/RelativeHumidityMeasurementCluster.h>
#include <app/clusters/temperature-measurement-server/TemperatureMeasurementCluster.h>
#include <esp_matter_data_model_provider.h>

// The MeasuredValue attributes of these clusters are owned by CHIP server
// cluster instances, not by the esp-matter attribute store, so they must be
// set through the cluster registered in the data model provider.

void MatterSensorBase::UpdateRelativeHumidityMeasurementAttributes(std::optional<float> relativeHumidity)
{
    if (!relativeHumidity.has_value()) {
        ESP_LOGE(m_tag, "Relative humidity measurement invalid.");
        return;
    }
    ESP_LOGI(m_tag, "Relative Humidity: %f", relativeHumidity.value());
    uint16_t reportedHumidity = static_cast<uint16_t>(std::round(relativeHumidity.value() * 100));

    auto* cluster = static_cast<chip::app::Clusters::RelativeHumidityMeasurementCluster*>(
        esp_matter::data_model::provider::get_instance().registry().Get(
            chip::app::ConcreteClusterPath(GetId(), chip::app::Clusters::RelativeHumidityMeasurement::Id)));
    if (cluster == nullptr) {
        ESP_LOGE(m_tag, "RelativeHumidityMeasurement server cluster not registered on endpoint %u", GetId());
        return;
    }

    CHIP_ERROR err = cluster->SetMeasuredValue(chip::app::DataModel::MakeNullable(reportedHumidity));
    if (err != CHIP_NO_ERROR) {
        ESP_LOGE(m_tag, "Failed to set humidity MeasuredValue: %" CHIP_ERROR_FORMAT, err.Format());
    }
}

void MatterSensorBase::UpdateTemperatureMeasurementAttributes(std::optional<float> temperature)
{
    if (!temperature.has_value()) {
        ESP_LOGE(m_tag, "Temperature measurement invalid.");
        return;
    }
    ESP_LOGI(m_tag, "Temperature: %f", temperature.value());
    int16_t reportedTemperature = static_cast<int16_t>(std::round(temperature.value() * 100));

    auto* cluster = static_cast<chip::app::Clusters::TemperatureMeasurementCluster*>(
        esp_matter::data_model::provider::get_instance().registry().Get(
            chip::app::ConcreteClusterPath(GetId(), chip::app::Clusters::TemperatureMeasurement::Id)));
    if (cluster == nullptr) {
        ESP_LOGE(m_tag, "TemperatureMeasurement server cluster not registered on endpoint %u", GetId());
        return;
    }

    CHIP_ERROR err = cluster->SetMeasuredValue(chip::app::DataModel::MakeNullable(reportedTemperature));
    if (err != CHIP_NO_ERROR) {
        ESP_LOGE(m_tag, "Failed to set temperature MeasuredValue: %" CHIP_ERROR_FORMAT, err.Format());
    }
}
