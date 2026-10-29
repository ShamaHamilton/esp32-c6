/**
 * Главный модуль управления мониторингом АКБ и отправкой телеметрии в Home Assistant.
 * Включает динамическую компенсацию времени активной работы (АЦП + лог + радио + сброс USB),
 * обеспечивая строго фиксированный шаг между циклами независимо от числа выборок АЦП.
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_timer.h" // Высокоточный 64-битный аппаратный таймер (шаг 1 мкс)

#include "app_config.h"
#include "battery_sensor.h"
#include "ha_telemetry.h"

static const char *TAG = "MAIN_APP";

// Целевой период цикла в микросекундах (для Light / Deep Sleep) и миллисекундах (для RTOS)
#define TARGET_PERIOD_MS ((uint32_t)(CFG_SLEEP_TIME_SEC) * 1000UL)
#define TARGET_PERIOD_US ((int64_t)(CFG_SLEEP_TIME_SEC) * 1000000LL)

// Минимальное безопасное время сна (1 мс), если активная фаза длилась дольше периода
#define MIN_SLEEP_TIME_US 1000LL

/**
 * Форматирует время работы со старта МК (в микросекундах) в читаемую строку:
 * - до 24 часов:  "02:52:45.258"
 * - после 24 ч:   "3d 02:52:45.258"
 */
static void format_uptime(int64_t time_us, char *out_buf, size_t buf_size)
{
    int64_t total_ms = time_us / 1000LL;
    uint32_t ms = (uint32_t)(total_ms % 1000LL);

    int64_t total_sec = total_ms / 1000LL;
    uint32_t sec = (uint32_t)(total_sec % 60LL);
    uint32_t min = (uint32_t)((total_sec / 60LL) % 60LL);
    uint32_t hours = (uint32_t)((total_sec / 3600LL) % 24LL);
    uint32_t days = (uint32_t)(total_sec / 86400LL);

    if (days > 0)
    {
        snprintf(out_buf, buf_size, "%lud %02lu:%02lu:%02lu.%03lu",
                 (unsigned long)days,
                 (unsigned long)hours,
                 (unsigned long)min,
                 (unsigned long)sec,
                 (unsigned long)ms);
    }
    else
    {
        snprintf(out_buf, buf_size, "%02lu:%02lu:%02lu.%03lu",
                 (unsigned long)hours,
                 (unsigned long)min,
                 (unsigned long)sec,
                 (unsigned long)ms);
    }
}

void app_main(void)
{
    // Фиксируем самую раннюю метку времени при входе в app_main (актуально для Deep Sleep)
    int64_t boot_entry_us = esp_timer_get_time();

    // 1. Инициализируем аппаратный модуль АЦП и пина USB
    ESP_ERROR_CHECK(battery_sensor_init());

    // 2. Инициализируем выбранный протокол связи с Home Assistant
    if (ha_telemetry_init() != ESP_OK)
    {
        ESP_LOGW(TAG, "Telemetry protocol init failed or timed out, continuing locally...");
    }

#if (ACTIVE_WORK_MODE == WORK_MODE_CONTINUOUS_LOOP)
    // Для режима цикла: фиксируем опорную точку времени в тиках FreeRTOS
    // Прямо перед началом первой итерации while(1)
    TickType_t last_wake_time = xTaskGetTickCount();
    const TickType_t period_ticks = pdMS_TO_TICKS(TARGET_PERIOD_MS);
#elif (ACTIVE_WORK_MODE == WORK_MODE_LIGHT_SLEEP)
    // Разрешаем блоку GPIO будить чип из Light Sleep
    ESP_ERROR_CHECK(esp_sleep_enable_gpio_wakeup());
    // Опорная метка времени в мкс для компенсации как работы, так и выхода из Light Sleep
    int64_t next_wakeup_target_us = esp_timer_get_time() + TARGET_PERIOD_US;
#endif

    // 3. Основной рабочий цикл
    while (1)
    {
        // Засекаем точное время начала замера
        int64_t active_start_us = esp_timer_get_time();

        // Переводим микросекунды со старта в строку "ЧЧ:ММ:СС.ммм" (или "Дд ЧЧ:ММ:СС.ммм")
        char uptime_str[32];
        format_uptime(active_start_us, uptime_str, sizeof(uptime_str));

        // --- А. Считываем показания датчиков (АЦП + статус USB) ---
        battery_telemetry_t telemetry = {0};
        ESP_ERROR_CHECK(battery_sensor_read(&telemetry));

        uint32_t meas_duration_ms = (uint32_t)((esp_timer_get_time() - active_start_us) / 1000LL);

        // --- Б. Формируем единую строку лога (теперь с точным Uptime для консоли и HA) ---
        char log_buffer[200];
        snprintf(log_buffer, sizeof(log_buffer),
                 "[Uptime: %s | Wake: %s] USB: %s | ADC: %d | Bat: %.3f V (%u%%) | Meas: %lu ms",
                 uptime_str,
                 telemetry.wakeup_cause,
                 telemetry.usb_plugged ? "PLUGGED" : "UNPLUGGED",
                 telemetry.adc_raw_avg,
                 telemetry.battery_v,
                 telemetry.battery_pct,
                 (unsigned long)meas_duration_ms);

        ESP_LOGI(TAG, "%s", log_buffer);
        // --- В. Отправляем данные и строку лога в Home Assistant ---
        ha_telemetry_send(&telemetry, log_buffer);

        // --- Г. Синхронизированная пауза / сон с вычитанием времени активной работы ---
#if (ACTIVE_WORK_MODE == WORK_MODE_CONTINUOUS_LOOP)

        /**
         * РЕЖИМ 1: Непрерывный цикл (без сна процессора).
         * vTaskDelayUntil автоматически вычитает все время, потраченное на battery_sensor_read(),
         * ESP_LOGI() и ha_telemetry_send(), и будит задачу строго по сетке TARGET_PERIOD_MS.
         */
        vTaskDelayUntil(&last_wake_time, period_ticks);

#elif (ACTIVE_WORK_MODE == WORK_MODE_LIGHT_SLEEP)

        /**
         * РЕЖИМ 2: Легкий сон (Light Sleep).
         */
        ha_telemetry_prepare_sleep();

        // Настраиваем пробуждение на противоположный уровень пина USB
        gpio_int_type_t wake_level = telemetry.usb_plugged ? GPIO_INTR_LOW_LEVEL : GPIO_INTR_HIGH_LEVEL;
        ESP_ERROR_CHECK(gpio_wakeup_enable(CFG_USB_PIN, wake_level));

        // Сначала сбрасываем буфер USB-консоли и ждем его физической отправки (20 мс),
        // чтобы эти 20 мс тоже вошли в измеряемое время активной работы и были вычтены из сна!
        fflush(stdout);
        vTaskDelay(pdMS_TO_TICKS(20));

        int64_t now_us = esp_timer_get_time();

        // Если предыдущее пробуждение произошло досрочно (по дерганью кабеля USB),
        // или если радиопередача затянулась дольше периода — пересчитываем сетку от текущего момента
        uint32_t wakeup_causes = esp_sleep_get_wakeup_causes();
        if ((wakeup_causes & (1 << ESP_SLEEP_WAKEUP_GPIO)) || (now_us >= next_wakeup_target_us))
        {
            next_wakeup_target_us = now_us + TARGET_PERIOD_US;
        }

        // Вычисляем точный остаток сна в микросекундах до следующей плановой точки сетки
        int64_t sleep_duration_us = next_wakeup_target_us - now_us;
        if (sleep_duration_us < MIN_SLEEP_TIME_US)
        {
            sleep_duration_us = MIN_SLEEP_TIME_US;
        }

        ESP_ERROR_CHECK(esp_sleep_enable_timer_wakeup((uint64_t)sleep_duration_us));

        // Сдвигаем плановую точку для следующей итерации ровно на TARGET_PERIOD_US
        next_wakeup_target_us += TARGET_PERIOD_US;

        esp_light_sleep_start();

#elif (ACTIVE_WORK_MODE == WORK_MODE_DEEP_SLEEP)

        /**
         * РЕЖИМ 3: Глубокий сон (Deep Sleep).
         * Вычитаем все время работы с момента старта app_main (включая инициализацию,
         * замер АЦП, отправку по радио и 50 мс ожидания сброса буфера консоли).
         */
        ha_telemetry_prepare_sleep();
        battery_sensor_deinit();

        if (telemetry.usb_plugged)
        {
            esp_sleep_enable_gpio_wakeup_on_hp_periph_powerdown((1ULL << CFG_USB_PIN), ESP_GPIO_WAKEUP_GPIO_LOW);
        }
        else
        {
            esp_sleep_enable_gpio_wakeup_on_hp_periph_powerdown((1ULL << CFG_USB_PIN), ESP_GPIO_WAKEUP_GPIO_HIGH);
        }

        ESP_LOGI(TAG, "Entering Deep Sleep (compensated period: %d sec)...", CFG_SLEEP_TIME_SEC);
        fflush(stdout);
        vTaskDelay(pdMS_TO_TICKS(50)); // Эти 50 мс тоже учитываются и вычитаются ниже!

        // Общее время бодрствования в микросекундах со старта чипа
        int64_t total_awake_us = esp_timer_get_time() - boot_entry_us;
        int64_t sleep_duration_us = TARGET_PERIOD_US - total_awake_us;
        if (sleep_duration_us < MIN_SLEEP_TIME_US)
        {
            sleep_duration_us = MIN_SLEEP_TIME_US;
        }

        ESP_ERROR_CHECK(esp_sleep_enable_timer_wakeup((uint64_t)sleep_duration_us));
        esp_deep_sleep_start();

#endif
    }
}