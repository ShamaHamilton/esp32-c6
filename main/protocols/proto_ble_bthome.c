#include "app_config.h"

#if (ACTIVE_HA_PROTOCOL == HA_PROTO_BLE_BTHOME)

#include <string.h>
#include "ha_telemetry.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "services/gap/ble_svc_gap.h"

static const char *TAG = "HA_BLE_BTHOME";
static bool s_ble_synced = false;

static void ble_on_sync(void)
{
    s_ble_synced = true;
}

static void nimble_host_task(void *param)
{
    nimble_port_run();
    nimble_port_freertos_deinit();
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

    ESP_ERROR_CHECK(nimble_port_init());
    ble_hs_cfg.sync_cb = ble_on_sync;
    ble_svc_gap_init();
    nimble_port_freertos_init(nimble_host_task);

    // Ждем готовности стека BLE (обычно < 15 мс)
    for (int i = 0; i < 50 && !s_ble_synced; i++)
    {
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    return s_ble_synced ? ESP_OK : ESP_ERR_TIMEOUT;
}

esp_err_t ha_telemetry_send(const battery_telemetry_t *data, const char *formatted_log)
{
    if (!s_ble_synced)
        return ESP_ERR_INVALID_STATE;

    // Переводим напряжение АКБ в милливольты (uint16_t) для стандарта BTHome (коэф. 0.001 В)
    uint16_t bat_mv = (uint16_t)(data->battery_v * 1000.0f);

    /**
     * Формируем сырой рекламный пакет стандарта BTHome v2 (UUID 0xFCD2):
     * - 0x02, 0x01, 0x06 : Flags (LE General Discoverable, BR/EDR Not Supported)
     * - 0x08, 0x09, 'C','6','-','B','A','T','1' : Имя устройства "C6-BAT1"
     * - 0x0B, 0x16, 0xD2, 0xFC : Service Data 16-bit UUID (0xFCD2 в Little-Endian)
     *   - 0x40 : BTHome v2 Info Byte (без шифрования)
     *   - 0x01, [pct] : Object 0x01 (Battery %, 1 байт)
     *   - 0x0C, [mv_lo], [mv_hi] : Object 0x0C (Voltage, 2 байта, шаг 0.001 В)
     *   - 0x10, [usb] : Object 0x10 (Power / USB Plugged binary sensor, 1 байт: 0 или 1)
     */
    uint8_t adv_payload[] = {
        0x02, 0x01, 0x06,
        0x08, 0x09, 'C', '6', '-', 'B', 'A', 'T', '1',
        0x0B, 0x16, 0xD2, 0xFC,
        0x40,
        0x01, data->battery_pct,
        0x0C, (uint8_t)(bat_mv & 0xFF), (uint8_t)((bat_mv >> 8) & 0xFF),
        0x10, (uint8_t)(data->usb_plugged ? 1 : 0)};

    ble_gap_adv_stop();
    ESP_ERROR_CHECK(ble_gap_adv_set_data(adv_payload, sizeof(adv_payload)));

    struct ble_gap_adv_params adv_params = {0};
    adv_params.conn_mode = BLE_GAP_CONN_MODE_NON; // Неподключаемый маячок (Non-connectable)
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    adv_params.itvl_min = 32; // Интервал рассылки пакетов: 32 * 0.625 мс = 20 мс
    adv_params.itvl_max = 40; // 25 мс

    ESP_ERROR_CHECK(ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER, &adv_params, NULL, NULL));

    // Держим рекламу активной 70 мс (успеет улететь 3 полных пачки по каналам 37, 38, 39)
    vTaskDelay(pdMS_TO_TICKS(70));
    ble_gap_adv_stop();

    ESP_LOGI(TAG, "BTHome packet broadcasted (%u mV, %u%%, USB=%d)", bat_mv, data->battery_pct, data->usb_plugged);
    return ESP_OK;
}

void ha_telemetry_prepare_sleep(void)
{
    if (s_ble_synced)
    {
        ble_gap_adv_stop();
    }
}

#endif // HA_PROTO_BLE_BTHOME