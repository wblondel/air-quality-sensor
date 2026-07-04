#include "MatterEndpoint.h"

using namespace esp_matter::attribute;

static const char *TAG = "MatterEndpoint";

MatterEndpoint::MatterEndpoint(endpoint_t* endpoint) : m_endpoint(endpoint)
{
}

endpoint_t* MatterEndpoint::GetEndpoint() const
{
    return m_endpoint;
}

uint16_t MatterEndpoint::GetId()
{
    return endpoint::get_id(m_endpoint);
}

esp_err_t MatterEndpoint::Initialize()
{
    // logging the initialization
    uint16_t endpoint_id = GetId();
    ESP_LOGI(TAG, "MatterEndpoint::Initialize: Entering endpoint_id=%d", endpoint_id);

    return ESP_OK; // Default implementation does nothing
}

esp_err_t MatterEndpoint::HandleAttributePreUpdate(
    uint32_t cluster_id,
    uint32_t attribute_id,
    esp_matter_attr_val_t* val,
    void *priv_data)
{
    ESP_LOGI(TAG, "Pre-update for cluster %lu, attribute %lu", cluster_id, attribute_id);
    return ESP_OK;
}

bool MatterEndpoint::GetAttributeValueBool(uint32_t cluster_id, uint32_t attribute_id) const
{
    uint16_t endpoint_id = esp_matter::endpoint::get_id(m_endpoint);

    esp_matter_attr_val_t val = esp_matter_invalid(NULL);
    attribute_t* attribute = attribute::get(endpoint_id, cluster_id, attribute_id);
    attribute::get_val(attribute, &val);

    return val.val.b;
}

uint8_t MatterEndpoint::GetAttributeValueUInt8(uint32_t cluster_id, uint32_t attribute_id) const
{
    uint16_t endpoint_id = esp_matter::endpoint::get_id(m_endpoint);

    esp_matter_attr_val_t val = esp_matter_invalid(NULL);
    attribute_t* attribute = attribute::get(endpoint_id, cluster_id, attribute_id);
    attribute::get_val(attribute, &val);

    return val.val.u8;
}

uint16_t MatterEndpoint::GetAttributeValueUInt16(uint32_t cluster_id, uint32_t attribute_id) const
{
    uint16_t endpoint_id = esp_matter::endpoint::get_id(m_endpoint);

    esp_matter_attr_val_t val = esp_matter_invalid(NULL);
    attribute_t* attribute = attribute::get(endpoint_id, cluster_id, attribute_id);
    attribute::get_val(attribute, &val);

    return val.val.u16;
}

void MatterEndpoint::UpdateAttributeValueBool(uint32_t cluster_id, uint32_t attribute_id, bool value)
{
    UpdateAttributeValueScalar(cluster_id, attribute_id, value ? 1.0 : 0.0);
}

void MatterEndpoint::UpdateAttributeValueUInt8(uint32_t cluster_id, uint32_t attribute_id, uint8_t value)
{
    UpdateAttributeValueScalar(cluster_id, attribute_id, value);
}

void MatterEndpoint::UpdateAttributeValueInt16(uint32_t cluster_id, uint32_t attribute_id, int16_t value)
{
    UpdateAttributeValueScalar(cluster_id, attribute_id, value);
}

void MatterEndpoint::UpdateAttributeValueFloat(uint32_t cluster_id, uint32_t attribute_id, float value)
{
    UpdateAttributeValueScalar(cluster_id, attribute_id, value);
}

void MatterEndpoint::UpdateAttributeValueScalar(uint32_t cluster_id, uint32_t attribute_id, double value)
{
    if (!m_endpoint) {
        ESP_LOGE(TAG, "Endpoint not initialized.");
        return;
    }
    uint16_t endpoint_id = endpoint::get_id(m_endpoint);

    attribute_t* attribute = attribute::get(endpoint_id, cluster_id, attribute_id);
    if (!attribute) {
        ESP_LOGE(TAG, "Attribute 0x%08" PRIX32 " of cluster 0x%08" PRIX32 " not found on endpoint %u",
                 attribute_id, cluster_id, endpoint_id);
        return;
    }

    // set_val() rejects values whose type differs from the attribute's registered
    // type, so read the current value and only replace its payload
    esp_matter_attr_val_t val = esp_matter_invalid(NULL);
    attribute::get_val(attribute, &val);

    switch (val.type) {
    case ESP_MATTER_VAL_TYPE_BOOLEAN:
    case ESP_MATTER_VAL_TYPE_NULLABLE_BOOLEAN:
        val.val.b = (value != 0.0);
        break;
    case ESP_MATTER_VAL_TYPE_INT8:
    case ESP_MATTER_VAL_TYPE_NULLABLE_INT8:
        val.val.i8 = static_cast<int8_t>(value);
        break;
    case ESP_MATTER_VAL_TYPE_UINT8:
    case ESP_MATTER_VAL_TYPE_NULLABLE_UINT8:
    case ESP_MATTER_VAL_TYPE_ENUM8:
    case ESP_MATTER_VAL_TYPE_NULLABLE_ENUM8:
    case ESP_MATTER_VAL_TYPE_BITMAP8:
    case ESP_MATTER_VAL_TYPE_NULLABLE_BITMAP8:
        val.val.u8 = static_cast<uint8_t>(value);
        break;
    case ESP_MATTER_VAL_TYPE_INT16:
    case ESP_MATTER_VAL_TYPE_NULLABLE_INT16:
        val.val.i16 = static_cast<int16_t>(value);
        break;
    case ESP_MATTER_VAL_TYPE_UINT16:
    case ESP_MATTER_VAL_TYPE_NULLABLE_UINT16:
    case ESP_MATTER_VAL_TYPE_ENUM16:
    case ESP_MATTER_VAL_TYPE_NULLABLE_ENUM16:
    case ESP_MATTER_VAL_TYPE_BITMAP16:
    case ESP_MATTER_VAL_TYPE_NULLABLE_BITMAP16:
        val.val.u16 = static_cast<uint16_t>(value);
        break;
    case ESP_MATTER_VAL_TYPE_INT32:
    case ESP_MATTER_VAL_TYPE_NULLABLE_INT32:
        val.val.i32 = static_cast<int32_t>(value);
        break;
    case ESP_MATTER_VAL_TYPE_UINT32:
    case ESP_MATTER_VAL_TYPE_NULLABLE_UINT32:
    case ESP_MATTER_VAL_TYPE_BITMAP32:
    case ESP_MATTER_VAL_TYPE_NULLABLE_BITMAP32:
        val.val.u32 = static_cast<uint32_t>(value);
        break;
    case ESP_MATTER_VAL_TYPE_INT64:
    case ESP_MATTER_VAL_TYPE_NULLABLE_INT64:
        val.val.i64 = static_cast<int64_t>(value);
        break;
    case ESP_MATTER_VAL_TYPE_UINT64:
    case ESP_MATTER_VAL_TYPE_NULLABLE_UINT64:
        val.val.u64 = static_cast<uint64_t>(value);
        break;
    case ESP_MATTER_VAL_TYPE_FLOAT:
    case ESP_MATTER_VAL_TYPE_NULLABLE_FLOAT:
        val.val.f = static_cast<float>(value);
        break;
    default:
        ESP_LOGE(TAG, "Attribute 0x%08" PRIX32 " of cluster 0x%08" PRIX32 " has non-scalar type %d",
                 attribute_id, cluster_id, val.type);
        return;
    }

    attribute::update(endpoint_id, cluster_id, attribute_id, &val);
}