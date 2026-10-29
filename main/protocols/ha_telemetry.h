/**
 * Заголовок для работы с телеметрией (Home Assistant)
 * Связывает main.c с любым выбранным радиопротоколом.
 * Используется для интеграции с Home Assistant через выбранный радиопротокол.
 */

#pragma once

#include "esp_err.h"
#include "battery_sensor.h"

// Инициализация выбранного протокола связи (вызывается при старте)
esp_err_t ha_telemetry_init(void);

// Отправка собранных данных и сформированной строки лога в Home Assistant
esp_err_t ha_telemetry_send(const battery_telemetry_t *data, const char *formatted_log);

// Подготовка радиомодуля ко сну (остановка рекламы / закрытие сокетов)
void ha_telemetry_prepare_sleep(void);
