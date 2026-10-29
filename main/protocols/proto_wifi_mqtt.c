#include "app_config.h"

#if (ACTIVE_HA_PROTOCOL == HA_PROTO_WIFI_MQTT)

#include <stdio.h>
#include <string.h>
#include "ha_telemetry.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "mqtt_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

static const char *TAG = "HA_WIFI_MQTT";

#define WIFI_CONNECTED_BIT BIT0
#define MQTT_CONNECTED_BIT BIT1

static EventGroupHandle_t s_net_event_group = NULL;
static esp_mqtt_client_handle_t s_mqtt_client = NULL;

static void wifi_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START)
    {
        esp_wifi_connect();
    }
    else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED)
    {
        xEventGroupClearBits(s_net_event_group, WIFI_CONNECTED_BIT);
        esp_wifi_connect();
    }
    else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP)
    {
        xEventGroupSetBits(s_net_event_group, WIFI_CONNECTED_BIT);
    }
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    if (event_id == MQTT_EVENT_CONNECTED)
    {
        xEventGroupSetBits(s_net_event_group, MQTT_CONNECTED_BIT);
    }
    else if (event_id == MQTT_EVENT_DISCONNECTED)
    {
        xEventGroupClearBits(s_net_event_group, MQTT_CONNECTED_BIT);
    }
}

esp_err_t ha_telemetry_init(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    s_net_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));

    wifi_config_t wifi_cfg = {
        .sta = {
            .ssid = CFG_WIFI_SSID,
            .password = CFG_WIFI_PASSWORD,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());

    // Включаем экономию энергии модема для работы в связке с Light Sleep (DTIM)
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_MIN_MODEM));

    ESP_LOGI(TAG, "Waiting for Wi-Fi connection...");
    EventBits_t bits = xEventGroupWaitBits(s_net_event_group, WIFI_CONNECTED_BIT, pdFALSE, pdTRUE, pdMS_TO_TICKS(10000));
    if (!(bits & WIFI_CONNECTED_BIT))
    {
        ESP_LOGE(TAG, "Wi-Fi connection timeout!");
        return ESP_ERR_TIMEOUT;
    }

    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = CFG_MQTT_BROKER_URI,
        .credentials.username = CFG_MQTT_USER,
        .credentials.authentication.password = CFG_MQTT_PASS,
    };
    s_mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(s_mqtt_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(s_mqtt_client);

    bits = xEventGroupWaitBits(s_net_event_group, MQTT_CONNECTED_BIT, pdFALSE, pdTRUE, pdMS_TO_TICKS(5000));
    return (bits & MQTT_CONNECTED_BIT) ? ESP_OK : ESP_ERR_TIMEOUT;
}

esp_err_t ha_telemetry_send(const battery_telemetry_t *data, const char *formatted_log)
{
    if (!s_mqtt_client)
        return ESP_ERR_INVALID_STATE;

    // 1. Формируем JSON с показаниями датчиков для сенсоров Home Assistant
    char json_payload[256];
    snprintf(json_payload, sizeof(json_payload),
             "{\"voltage\":%.3f,\"battery_pct\":%u,\"adc_raw\":%d,\"usb_plugged\":%s,\"wakeup\":\"%s\"}",
             data->battery_v,
             data->battery_pct,
             data->adc_raw_avg,
             data->usb_plugged ? "true" : "false",
             data->wakeup_cause);

    esp_mqtt_client_publish(s_mqtt_client, CFG_MQTT_TOPIC_STATE, json_payload, 0, 1, 1);

    // 2. Отправляем саму текстовую строку консольного лога в отдельный топик
    if (formatted_log)
    {
        esp_mqtt_client_publish(s_mqtt_client, CFG_MQTT_TOPIC_LOG, formatted_log, 0, 0, 0);
    }

    // Даем TCP-стеку 50 мс на отправку пакетов в сеть
    vTaskDelay(pdMS_TO_TICKS(50));
    return ESP_OK;
}

void ha_telemetry_prepare_sleep(void)
{
#if (ACTIVE_WORK_MODE == WORK_MODE_DEEP_SLEEP)
    // Перед уходом в Deep Sleep корректно закрываем MQTT и выключаем Wi-Fi
    if (s_mqtt_client)
    {
        esp_mqtt_client_stop(s_mqtt_client);
    }
    esp_wifi_stop();
#endif
}

#endif // HA_PROTO_WIFI_MQTT