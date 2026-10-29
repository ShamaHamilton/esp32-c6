#include "app_config.h"
#include "ha_telemetry.h"
#include "esp_log.h"

#if (ACTIVE_HA_PROTOCOL == HA_PROTO_NONE_CONSOLE)

// Если выбран режим "Только консоль", радиомодули вообще не инициализируются
esp_err_t ha_telemetry_init(void) { return ESP_OK; }
esp_err_t ha_telemetry_send(const battery_telemetry_t *data, const char *formatted_log) { return ESP_OK; }
void ha_telemetry_prepare_sleep(void) {}

#elif (ACTIVE_HA_PROTOCOL == HA_PROTO_ZIGBEE)

#include "nvs_flash.h"
#include "esp_zigbee_core.h"

static const char *TAG = "HA_ZIGBEE";
#define HA_ZB_ENDPOINT 1

esp_err_t ha_telemetry_init(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Инициализируем радио 802.15.4 в режиме спящего конечного устройства (Sleepy End Device)
    esp_zb_platform_config_t platform_cfg = {
        .radio_config = {.radio_mode = ZB_RADIO_MODE_NATIVE},
        .host_config = {.host_connection_mode = ZB_HOST_CONNECTION_MODE_NONE},
    };
    ESP_ERROR_CHECK(esp_zb_platform_config(&platform_cfg));

    esp_zb_cfg_t zb_nwk_cfg = {
        .esp_zb_role = ESP_ZB_DEVICE_TYPE_ED, // End Device (не роутер, может спать)
        .install_code_policy = false,
        .nwk_cfg.zed_cfg = {
            .ed_timeout = ESP_ZB_ED_AGING_TIMEOUT_64MIN,
            .keep_alive = 3000,
        },
    };
    esp_zb_init(&zb_nwk_cfg);

    // Разрешаем стеку Zigbee уходить в сон при простое радио
    esp_zb_sleep_enable(true);

    ESP_LOGI(TAG, "Zigbee End Device initialized (Endpoint %d)", HA_ZB_ENDPOINT);
    return ESP_OK;
}

esp_err_t ha_telemetry_send(const battery_telemetry_t *data, const char *formatted_log)
{
    // По стандарту ZCL Power Configuration (0x0001):
    // - Напряжение (0x0020) хранится в единицах по 100 мВ (например, 3.88В -> 39)
    // - Процент заряда (0x0021) хранится с шагом 0.5% (100% -> 200)
    uint8_t zcl_voltage_100mv = (uint8_t)((data->battery_v * 10.0f) + 0.5f);
    uint8_t zcl_bat_half_pct = (uint8_t)(data->battery_pct * 2);

    esp_zb_lock_acquire(portMAX_DELAY);

    esp_zb_zcl_set_attribute_val(
        HA_ZB_ENDPOINT,
        ESP_ZB_ZCL_CLUSTER_ID_POWER_CONFIG,
        ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
        ESP_ZB_ZCL_ATTR_POWER_CONFIG_BATTERY_VOLTAGE_ID,
        &zcl_voltage_100mv,
        false);

    esp_zb_zcl_set_attribute_val(
        HA_ZB_ENDPOINT,
        ESP_ZB_ZCL_CLUSTER_ID_POWER_CONFIG,
        ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
        ESP_ZB_ZCL_ATTR_POWER_CONFIG_BATTERY_PERCENTAGE_REMAINING_ID,
        &zcl_bat_half_pct,
        false);

    esp_zb_lock_release();

    ESP_LOGI(TAG, "Zigbee ZCL attributes updated: %u (x0.1V), %u (x0.5%%)", zcl_voltage_100mv, zcl_bat_half_pct);
    return ESP_OK;
}

void ha_telemetry_prepare_sleep(void) {}

#endif // HA_PROTO_ZIGBEE