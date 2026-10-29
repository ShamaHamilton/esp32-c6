/**
 * Программа мониторинга АКБ и статуса USB в режиме LIGHT SLEEP (ESP-IDF v6.1).
 *
 * Особенность Light Sleep: оперативная память (RAM) и настройки периферии не сбрасываются.
 * Инициализация АЦП и GPIO выполняется один раз при старте, после чего микроконтроллер
 * в бесконечном цикле засыпает и просыпается ровно на том же месте кода.
 */

#include <stdio.h>                   // Для стандартного вывода и fflush(stdout)
#include "freertos/FreeRTOS.h"       // Базовые типы и макросы ОС FreeRTOS
#include "freertos/task.h"           // Для функции задержки vTaskDelay
#include "driver/gpio.h"             // Драйвер GPIO (чтение пина и настройка gpio_wakeup_enable)
#include "esp_log.h"                 // Вывод логов ESP_LOGI
#include "esp_adc/adc_oneshot.h"     // Драйвер одиночных замеров АЦП
#include "esp_adc/adc_cali.h"        // Интерфейс аппаратной калибровки АЦП
#include "esp_adc/adc_cali_scheme.h" // Схема калибровки Curve Fitting для ESP32-C6
#include "esp_sleep.h"               // Управление режимами сна (Light / Deep Sleep)
#include "esp_rom_sys.h"             // Для точной микросекундной задержки esp_rom_delay_us

static const char *TAG = "BAT_LIGHT_SLEEP";

// Конфигурация пинов
#define USB_PIN GPIO_NUM_1         // Пин детектора 5В от USB (через резистивный делитель)
#define BAT_ADC_CHAN ADC_CHANNEL_0 // GPIO0 = ADC1_CHANNEL_0 на ESP32-C6

// Конфигурация измерений и сна
#define SAMPLES_COUNT 64            // Число выборок АЦП для фильтрации шума
#define SOFTWARE_CALI_FACTOR 1.008f // Индивидуальный поправочный коэффициент по мультиметру
#define SLEEP_TIME_SEC 10           // Период автоматического пробуждения по таймеру (сек)

void app_main(void)
{
    // =========================================================================
    // 1. ОДНОКРАТНАЯ ИНИЦИАЛИЗАЦИЯ ПЕРИФЕРИИ (выполняется 1 раз при подаче питания)
    // =========================================================================

    // Настраиваем GPIO1 на вход.
    // Если на плате распаян внешний делитель (например, 10к + 15к на GND),
    // внутреннюю подтяжку лучше отключить (GPIO_PULLDOWN_DISABLE), чтобы не искажать делитель.
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_INPUT,
        .pin_bit_mask = (1ULL << USB_PIN),
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en = GPIO_PULLUP_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&io_conf));

    // Инициализируем модуль ADC1
    adc_oneshot_unit_handle_t adc1_handle;
    adc_oneshot_unit_init_cfg_t init_config1 = {
        .unit_id = ADC_UNIT_1,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config1, &adc1_handle));

    // Настраиваем канал 0 (GPIO0) с ослаблением 12 дБ (измерение до ~3.1 В)
    adc_oneshot_chan_cfg_t config = {
        .bitwidth = ADC_BITWIDTH_DEFAULT,
        .atten = ADC_ATTEN_DB_12,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc1_handle, BAT_ADC_CHAN, &config));

    // Инициализируем заводскую калибровку Curve Fitting
    adc_cali_handle_t cali_handle = NULL;
    adc_cali_curve_fitting_config_t cali_config = {
        .unit_id = ADC_UNIT_1,
        .chan = BAT_ADC_CHAN,
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    ESP_ERROR_CHECK(adc_cali_create_scheme_curve_fitting(&cali_config, &cali_handle));

    // Включаем таймер как источник пробуждения (настройка сохраняется между циклами)
    ESP_ERROR_CHECK(esp_sleep_enable_timer_wakeup(SLEEP_TIME_SEC * 1000000ULL));

    // Разрешаем блоку GPIO будить процессор из Light Sleep
    ESP_ERROR_CHECK(esp_sleep_enable_gpio_wakeup());

    uint32_t cycle_counter = 0; // Счетчик циклов (в Light Sleep переменные в ОЗУ не сбрасываются!)

    // =========================================================================
    // 2. РАБОЧИЙ ЦИКЛ С ПРОБУЖДЕНИЕМ И ЗАСЫПАНИЕМ
    // =========================================================================
    while (1)
    {
        cycle_counter++;

        // --- А. Определяем причину последнего пробуждения (API ESP-IDF v6.1) ---
        uint32_t wakeup_causes = esp_sleep_get_wakeup_causes();
        const char *wakeup_reason_str = "INITIAL_BOOT";

        if (wakeup_causes & (1 << ESP_SLEEP_WAKEUP_TIMER))
        {
            wakeup_reason_str = "TIMER";
        }
        else if (wakeup_causes & (1 << ESP_SLEEP_WAKEUP_GPIO))
        {
            wakeup_reason_str = "USB_GPIO_EVENT";
        }

        // --- Б. Считываем текущий статус USB и напряжение АКБ ---
        int usb_connected = gpio_get_level(USB_PIN);

        long adc_sum = 0;
        for (int i = 0; i < SAMPLES_COUNT; i++)
        {
            int raw_val = 0;
            adc_oneshot_read(adc1_handle, BAT_ADC_CHAN, &raw_val);
            adc_sum += raw_val;
            esp_rom_delay_us(500); // Точная пауза 500 мкс для фильтрации ВЧ-помех
        }

        int adc_raw_avg = adc_sum / SAMPLES_COUNT;
        int voltage_mv = 0;
        adc_cali_raw_to_voltage(cali_handle, adc_raw_avg, &voltage_mv);

        // Умножаем на 2.0 (делитель 200к/200к) и на поправочный коэффициент
        float bat_voltage = (voltage_mv * 2.0f / 1000.0f) * SOFTWARE_CALI_FACTOR;

        // --- В. Выводим телеметрию в консоль ---
        ESP_LOGI(TAG, "[Cycle #%lu | Wake: %s] USB: %s | ADC Raw: %d | Bat: %.3f V",
                 (unsigned long)cycle_counter,
                 wakeup_reason_str,
                 usb_connected ? "PLUGGED" : "UNPLUGGED",
                 adc_raw_avg,
                 bat_voltage);

        // --- Г. Перенастраиваем полярность пробуждения по пину USB ---
        // В Light Sleep используется пробуждение по УРОВНЮ (HIGH_LEVEL или LOW_LEVEL).
        // Если USB сейчас подключен (1), ждем падения в 0 (отключение). И наоборот.
        gpio_int_type_t wake_level = usb_connected ? GPIO_INTR_LOW_LEVEL : GPIO_INTR_HIGH_LEVEL;
        ESP_ERROR_CHECK(gpio_wakeup_enable(USB_PIN, wake_level));

        // --- Д. Сбрасываем буфер вывода USB Serial/JTAG и засыпаем ---
        fflush(stdout);
        vTaskDelay(pdMS_TO_TICKS(20)); // Даем 20 мс USB-контроллеру отправить пакет в ПК

        // Входим в Light Sleep. Процессор останавливается здесь.
        esp_light_sleep_start();

        // <--- ПРИ ПРОБУЖДЕНИИ ВЫПОЛНЕНИЕ ПРОДОЛЖАЕТСЯ РОВНО С ЭТОЙ ТОЧКИ!
    }
}
