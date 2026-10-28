#include <stdio.h>                   // Стандартная библиотека ввода-вывода (для printf)
#include "freertos/FreeRTOS.h"       // Операционная система реального времени (база для ESP)
#include "freertos/task.h"           // Библиотека для работы с задачами и задержками (vTaskDelay)
#include "driver/gpio.h"             // Драйвер для работы с цифровыми пинами (GPIO)
#include "esp_log.h"                 // Библиотека для логирования сообщений в консоль
#include "esp_adc/adc_oneshot.h"     // Драйвер АЦП (одиночные измерения)
#include "esp_adc/adc_cali.h"        // Библиотека аппаратной калибровки АЦП
#include "esp_adc/adc_cali_scheme.h" // Схема калибровки АЦП
#include "esp_sleep.h"               // Библиотека для управления режимами сна
#include "esp_rom_sys.h"             // Библиотека для низкоуровневых функций ROM, таких как esp_rom_delay_us

// Тег для логирования сообщений в консоль
static const char *TAG = "BAT_SLEEP_MONITOR";

#define USB_PIN GPIO_NUM_1         // Пин, куда подключен делитель от 5V питания USB
#define BAT_ADC_CHAN ADC_CHANNEL_0 // Канал АЦП для GPIO0 (у ESP32-C6 GPIO0 - это канал 0 для ADC1)
#define SAMPLES_COUNT 64           // Количество замеров (выборок) для усреднения (сглаживания шума)
// ПРОГРАММНАЯ КАЛИБРОВКА: если мультиметр показывает на батарее 4.15В, а ESP выдает 4.10В,
// коэффициент будет 4.15 / 4.10 = 1.012. Мы умножим финальный результат на него.
#define SOFTWARE_CALI_FACTOR 1.000 // Программный коэффициент калибровки для корректировки измеренного напряжения
#define SLEEP_TIME_SEC 30          // Время сна в секундах между проверками батареи

void app_main(void)
{
    // =========================================================
    // 1. АНАЛИЗ ПРИЧИНЫ ПРОБУЖДЕНИЯ (НОВЫЙ API v6.1)
    // =========================================================
    // В v6.1 функция возвращает uint32_t маску со всеми флагами пробуждения
    uint32_t wakeup_causes = esp_sleep_get_wakeup_causes();

    // Проверяем биты через побитовое И (сдвигаем 1 на номер причины)
    if (wakeup_causes & (1 << ESP_SLEEP_WAKEUP_TIMER))
    {
        ESP_LOGI(TAG, "Woke up by TIMER. Time to check battery.");
    }
    else if (wakeup_causes & (1 << ESP_SLEEP_WAKEUP_GPIO))
    {
        ESP_LOGI(TAG, "Woke up by USB PIN change.");
    }
    else
    {
        ESP_LOGI(TAG, "Normal boot / Reset.");
    }

    // =========================================================
    // 2. БЫСТРАЯ НАСТРОЙКА И ЗАМЕРЫ
    // =========================================================
    gpio_config_t io_conf = {};
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pin_bit_mask = (1ULL << USB_PIN);
    io_conf.pull_down_en = GPIO_PULLDOWN_ENABLE;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    gpio_config(&io_conf);

    adc_oneshot_unit_handle_t adc1_handle;
    adc_oneshot_unit_init_cfg_t init_config1 = {.unit_id = ADC_UNIT_1};
    adc_oneshot_new_unit(&init_config1, &adc1_handle);

    adc_oneshot_chan_cfg_t config = {
        .bitwidth = ADC_BITWIDTH_DEFAULT,
        .atten = ADC_ATTEN_DB_12,
    };
    adc_oneshot_config_channel(adc1_handle, BAT_ADC_CHAN, &config);

    adc_cali_handle_t cali_handle = NULL;
    adc_cali_curve_fitting_config_t cali_config = {
        .unit_id = ADC_UNIT_1,
        .chan = BAT_ADC_CHAN,
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    adc_cali_create_scheme_curve_fitting(&cali_config, &cali_handle);

    int usb_connected = gpio_get_level(USB_PIN);

    long adc_sum = 0;
    for (int i = 0; i < SAMPLES_COUNT; i++)
    {
        int raw_val;
        adc_oneshot_read(adc1_handle, BAT_ADC_CHAN, &raw_val);
        adc_sum += raw_val;
        esp_rom_delay_us(500);
    }

    int adc_raw_avg = adc_sum / SAMPLES_COUNT;
    int voltage_mv = 0;
    adc_cali_raw_to_voltage(cali_handle, adc_raw_avg, &voltage_mv);
    float bat_voltage = (voltage_mv * 2.0 / 1000.0) * SOFTWARE_CALI_FACTOR;

    ESP_LOGI(TAG, "--- SYSTEM STATUS ---");
    ESP_LOGI(TAG, "USB Status: %s", usb_connected ? "PLUGGED IN" : "UNPLUGGED");
    ESP_LOGI(TAG, "Battery Voltage: %.2f V", bat_voltage);
    ESP_LOGI(TAG, "---------------------");

    // Освобождаем память АЦП
    adc_oneshot_del_unit(adc1_handle);

    // =========================================================
    // 3. ПОДГОТОВКА КО СНУ (Новые функции v6.1)
    // =========================================================
    esp_sleep_enable_timer_wakeup(SLEEP_TIME_SEC * 1000000ULL);

    // В v6.1 функция esp_deep_sleep_enable_gpio_wakeup заменена на:
    if (usb_connected)
    {
        ESP_LOGI(TAG, "Setting wakeup condition: USB unplug (LOW level).");
        esp_sleep_enable_gpio_wakeup_on_hp_periph_powerdown((1ULL << USB_PIN), ESP_GPIO_WAKEUP_GPIO_LOW);
    }
    else
    {
        ESP_LOGI(TAG, "Setting wakeup condition: USB plug (HIGH level).");
        esp_sleep_enable_gpio_wakeup_on_hp_periph_powerdown((1ULL << USB_PIN), ESP_GPIO_WAKEUP_GPIO_HIGH);
    }

    ESP_LOGI(TAG, "Going to Deep Sleep now... Good night!\n");

    // Даем логам 100 мс, чтобы они успели "улететь" по USB перед тем как чип уснет
    vTaskDelay(pdMS_TO_TICKS(100));

    esp_deep_sleep_start();
}
