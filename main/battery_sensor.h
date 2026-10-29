#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

/**
 * Структура с результатами одного полного замера состояния питания.
 */
typedef struct
{
    int adc_raw_avg;          // Усредненное сырое значение АЦП (0..4095)
    int adc_pin_mv;           // Напряжение непосредственно на пине GPIO0 (мВ)
    float battery_v;          // Реальное напряжение на клеммах АКБ (В)
    uint8_t battery_pct;      // Оценка уровня заряда АКБ (0..100%)
    bool usb_plugged;         // Статус подключения кабеля USB (true = подключен)
    const char *wakeup_cause; // Текстовое описание причины пробуждения ("TIMER", "USB_PIN", "BOOT")
} battery_telemetry_t;

// Инициализация GPIO1 и АЦП ADC1_CH0 с заводской калибровкой Curve Fitting
esp_err_t battery_sensor_init(void);

// Выполнение серии из 64 замеров АЦП, чтение пина USB и заполнение структуры
esp_err_t battery_sensor_read(battery_telemetry_t *out_data);

// Освобождение ресурсов АЦП перед уходом в глубокий сон
void battery_sensor_deinit(void);
