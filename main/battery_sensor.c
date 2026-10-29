#include "battery_sensor.h"
#include "app_config.h"
#include "driver/gpio.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_sleep.h"
#include "esp_rom_sys.h"

static adc_oneshot_unit_handle_t s_adc_handle = NULL;
static adc_cali_handle_t s_cali_handle = NULL;

/**
 * Вспомогательная функция перевода напряжения Li-Ion в проценты (0..100%).
 */
static uint8_t calc_battery_percentage(float voltage)
{
    if (voltage <= CFG_BAT_MIN_VOLTAGE)
        return 0;
    if (voltage >= CFG_BAT_MAX_VOLTAGE)
        return 100;
    float pct = ((voltage - CFG_BAT_MIN_VOLTAGE) / (CFG_BAT_MAX_VOLTAGE - CFG_BAT_MIN_VOLTAGE)) * 100.0f;
    return (uint8_t)(pct + 0.5f);
}

esp_err_t battery_sensor_init(void)
{
    // 1. Настройка GPIO детектора USB
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_INPUT,
        .pin_bit_mask = (1ULL << CFG_USB_PIN),
        .pull_down_en = GPIO_PULLDOWN_DISABLE, // Нижний резистор делителя уже притягивает к GND
        .pull_up_en = GPIO_PULLUP_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&io_conf));

    // 2. Инициализация блока ADC1
    adc_oneshot_unit_init_cfg_t init_cfg = {
        .unit_id = ADC_UNIT_1,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_cfg, &s_adc_handle));

    // 3. Конфигурация канала АЦП
    adc_oneshot_chan_cfg_t chan_cfg = {
        .bitwidth = ADC_BITWIDTH_DEFAULT,
        .atten = ADC_ATTEN_DB_12,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(s_adc_handle, CFG_BAT_ADC_CHAN, &chan_cfg));

    // 4. Аппаратная калибровка Curve Fitting
    adc_cali_curve_fitting_config_t cali_cfg = {
        .unit_id = ADC_UNIT_1,
        .chan = CFG_BAT_ADC_CHAN,
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    ESP_ERROR_CHECK(adc_cali_create_scheme_curve_fitting(&cali_cfg, &s_cali_handle));

    return ESP_OK;
}

esp_err_t battery_sensor_read(battery_telemetry_t *out_data)
{
    if (!out_data || !s_adc_handle)
        return ESP_ERR_INVALID_STATE;

    // 1. Определяем причину пробуждения (актуальный API ESP-IDF v6.1)
    uint32_t causes = esp_sleep_get_wakeup_causes();
    if (causes & (1 << ESP_SLEEP_WAKEUP_TIMER))
    {
        out_data->wakeup_cause = "TIMER";
    }
    else if (causes & (1 << ESP_SLEEP_WAKEUP_GPIO))
    {
        out_data->wakeup_cause = "USB_PIN";
    }
    else
    {
        out_data->wakeup_cause = "BOOT";
    }

    // 2. Читаем уровень на пине USB
    out_data->usb_plugged = (gpio_get_level(CFG_USB_PIN) == 1);

    // 3. Выполняем серию замеров АЦП с аппаратной задержкой 500 мкс
    long adc_sum = 0;
    for (int i = 0; i < CFG_ADC_SAMPLES; i++)
    {
        int raw = 0;
        adc_oneshot_read(s_adc_handle, CFG_BAT_ADC_CHAN, &raw);
        adc_sum += raw;
        esp_rom_delay_us(500);
    }

    out_data->adc_raw_avg = (int)(adc_sum / CFG_ADC_SAMPLES);
    adc_cali_raw_to_voltage(s_cali_handle, out_data->adc_raw_avg, &out_data->adc_pin_mv);

    // 4. Пересчет в напряжение АКБ (делитель 200к+200к = * 2.0)
    out_data->battery_v = (out_data->adc_pin_mv * 2.0f / 1000.0f) * CFG_SOFTWARE_CALI_FACTOR;
    out_data->battery_pct = calc_battery_percentage(out_data->battery_v);

    return ESP_OK;
}

void battery_sensor_deinit(void)
{
    if (s_cali_handle)
    {
        adc_cali_delete_scheme_curve_fitting(s_cali_handle);
        s_cali_handle = NULL;
    }
    if (s_adc_handle)
    {
        adc_oneshot_del_unit(s_adc_handle);
        s_adc_handle = NULL;
    }
}