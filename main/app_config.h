/**
 * Конфигурационный файл приложения.
 * Здесь сосредоточены все настройки пинов, калибровки, выбора режима сна и выбора протокола.
 */

#pragma once

// =============================================================================
// 1. ВЫБОР РЕЖИМА РАБОТЫ МИКРОКОНТРОЛЛЕРА (выберите один)
// =============================================================================
#define WORK_MODE_CONTINUOUS_LOOP 0 // Бесконечный цикл с задержкой vTaskDelay (без сна)
#define WORK_MODE_LIGHT_SLEEP 1     // Легкий сон (ОЗУ сохраняется, старт за 2 мс)
#define WORK_MODE_DEEP_SLEEP 2      // Глубокий сон (перезагрузка при каждом пробуждении)

// ---> АКТИВНЫЙ РЕЖИМ РАБОТЫ:
#define ACTIVE_WORK_MODE WORK_MODE_CONTINUOUS_LOOP

// =============================================================================
// 2. ВЫБОР ПРОТОКОЛА ПЕРЕДАЧИ В HOME ASSISTANT (выберите один)
// =============================================================================
#define HA_PROTO_NONE_CONSOLE 0 // Только вывод в консоль USB (без радиомодуля)
#define HA_PROTO_BLE_BTHOME 1   // BLE 5.3 BTHome v2 (энергоэффективные рекламные пакеты)
#define HA_PROTO_WIFI_MQTT 2    // Wi-Fi + MQTT брокер (полный JSON + текстовая строка лога)
#define HA_PROTO_ZIGBEE 3       // Zigbee 3.0 End Device (кластер Power Configuration)

// ---> АКТИВНЫЙ ПРОТОКОЛ СВЯЗИ:
#define ACTIVE_HA_PROTOCOL HA_PROTO_NONE_CONSOLE

// =============================================================================
// 3. АППАРАТНЫЕ ПАРАМЕТРЫ ПЛАТЫ И ДАТЧИКОВ
// =============================================================================
#define CFG_BAT_ADC_CHAN 0              // GPIO0 (ADC1_CH0) — делитель напряжения АКБ 200к/200к
#define CFG_USB_PIN 1                   // GPIO1 — детектор наличия 5В от USB
#define CFG_ADC_SAMPLES 64              // Число замеров для сглаживания шума
#define CFG_SOFTWARE_CALI_FACTOR 1.008f // Поправочный коэффициент калибровки по мультиметру
#define CFG_SLEEP_TIME_SEC 10           // Интервал между замерами (в секундах)

// Пороги напряжения Li-Ion АКБ для расчета заряда в процентах (0..100%)
#define CFG_BAT_MIN_VOLTAGE 3.25f // 0% (отсечка LDO стабилизатора)
#define CFG_BAT_MAX_VOLTAGE 4.20f // 100% (полный заряд)

// =============================================================================
// 4. НАСТРОЙКИ ДЛЯ WI-FI + MQTT (используются, если выбран HA_PROTO_WIFI_MQTT)
// =============================================================================
#define CFG_WIFI_SSID "YOUR_WIFI_SSID"
#define CFG_WIFI_PASSWORD "YOUR_WIFI_PASS"
#define CFG_MQTT_BROKER_URI "mqtt://192.168.1.100:1883"
#define CFG_MQTT_USER "mqtt_user"
#define CFG_MQTT_PASS "mqtt_pass"
#define CFG_MQTT_TOPIC_STATE "homeassistant/sensor/esp32c6_bat/state"
#define CFG_MQTT_TOPIC_LOG "homeassistant/sensor/esp32c6_bat/log"
