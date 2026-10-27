# Гайд для начинающего. ESP32-C6 SuperMini + ESP-IDF v6.1 + C

## 1. Активация окружения
```bash
get_idf
cd ~/Projects/esp32/C6_SuperMini
```
Пути: `~/.esp/esp-idf/`, `~/.espressif/`, порт `/dev/ttyACM0`

## 2. Основные команды
```bash
idf.py set-target esp32c6
idf.py menuconfig
idf.py build
idf.py -p /dev/ttyACM0 flash monitor  # Ctrl+] для выхода
idf.py fullclean
idf.py size-components
```

## 3. Ключевые заголовки
| Заголовок | Назначение |
|---|---|
| `driver/gpio.h` | GPIO, прерывания |
| `driver/uart.h` | UART |
| `driver/i2c_master.h` | I2C |
| `driver/spi_master.h` | SPI |
| `esp_log.h` | Логирование |
| `freertos/FreeRTOS.h`, `freertos/task.h` | FreeRTOS задачи |
| `esp_timer.h` | Таймеры |
| `nvs_flash.h` | NVS хранилище |
| `esp_wifi.h`, `esp_netif.h` | Wi-Fi/сеть |

## 4. Примеры

### Hello + лог
```c
#include <stdio.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "DEMO";

void app_main(void)
{
    ESP_LOGI(TAG, "Старт ESP32-C6");
    int cnt = 0;
    while (1) {
        ESP_LOGI(TAG, "Счётчик: %d", cnt++);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
```

### Мигалка
```c
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define LED_GPIO GPIO_NUM_8

void app_main(void)
{
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << LED_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&io);

    while (1) {
        gpio_set_level(LED_GPIO, 1);
        vTaskDelay(pdMS_TO_TICKS(500));
        gpio_set_level(LED_GPIO, 0);
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}
```

### Кнопка с прерыванием
```c
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"

#define BTN_GPIO GPIO_NUM_9
static const char *TAG = "BTN";
static QueueHandle_t gpio_evt_queue;

static void IRAM_ATTR gpio_isr_handler(void *arg)
{
    uint32_t gpio_num = (uint32_t)arg;
    xQueueSendFromISR(gpio_evt_queue, &gpio_num, NULL);
}

void button_task(void *arg)
{
    uint32_t gpio_num;
    while (1) {
        if (xQueueReceive(gpio_evt_queue, &gpio_num, portMAX_DELAY)) {
            ESP_LOGI(TAG, "Нажата GPIO %d", gpio_num);
        }
    }
}

void app_main(void)
{
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << BTN_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_NEGEDGE
    };
    gpio_config(&io);

    gpio_evt_queue = xQueueCreate(10, sizeof(uint32_t));
    xTaskCreate(button_task, "button_task", 2048, NULL, 10, NULL);
    gpio_install_isr_service(0);
    gpio_isr_handler_add(BTN_GPIO, gpio_isr_handler, (void *)BTN_GPIO);
}
```

## 5. VSCode
Расширения: `espressif.esp-idf-extension`, `ms-vscode.cpptools`.
Настройки уже в `.vscode/`.

Если `#include` подчёркнуты:
1. `idf.py build`
2. `Ctrl+Shift+P` -> `C/C++: Reset IntelliSense Database`
3. `Ctrl+Shift+P` -> `Developer: Reload Window`

## 6. Отладка
- Логи: `esp_log.h` (ESP_LOGI, ESP_LOGE, ESP_LOGW, ESP_LOGD, ESP_LOGV)
- Монитор: `idf.py -p /dev/ttyACM0 monitor`
- JTAG: OpenOCD `/tmp/opencode/openocd/openocd-esp32/bin/openocd` с `interface/esp_usb_jtag.cfg` + `target/esp32c6.cfg`

## 7. Добавление библиотек

### Собственный компонент
```bash
mkdir -p components/my_lib/{include,src}
cat > components/my_lib/CMakeLists.txt << 'EOF'
idf_component_register(SRCS "src/my_lib.c" INCLUDE_DIRS "include" REQUIRES esp_log)
EOF
cat > components/my_lib/include/my_lib.h << 'EOF'
#ifndef MY_LIB_H
#define MY_LIB_H
void my_lib_init(void);
#endif
EOF
cat > components/my_lib/src/my_lib.c << 'EOF'
#include "my_lib.h"
#include "esp_log.h"
static const char *TAG = "MY_LIB";
void my_lib_init(void) { ESP_LOGI(TAG, "init"); }
EOF
```
Использование: `#include "my_lib.h"`

### IDF Component Registry
```bash
idf.py add-dependency "espressif/led_strip^2.0.0"
```

## 8. Полезные ссылки
- [ESP-IDF v6.1 Programming Guide](https://docs.espressif.com/projects/esp-idf/en/v6.1/esp32c6/)
- [API Reference](https://docs.espressif.com/projects/esp-idf/en/v6.1/esp32c6/api-reference/)
- [ESP-IDF Examples](https://github.com/espressif/esp-idf/tree/master/examples)
- [IDF Component Registry](https://components.espressif.com/)
- [Espressif Forums](https://esp32.com/)
- [Espressif Technical Documents](https://www.espressif.com/en/support/documents/technical-documents)
- [JTAG Debugging Guide](https://docs.espressif.com/projects/esp-idf/en/v6.1/esp32c6/api-guides/jtag-debugging/index.html)
- [Build System](https://docs.espressif.com/projects/esp-idf/en/v6.1/esp32c6/api-guides/build-system.html)

## 11. Структура проекта и память
См. [Архитектура, память, тесты](АРХИТЕКТУРА_ПАМЯТЬ_ТЕСТЫ.md) - карта разделов, как прошивать, как оценить размер, про несколько проектов на плате.

## 12. Тестирование кода (pytest + ESP-IDF)
В проекте настроено локальное venv `.venv/` с `pytest-embedded-idf`.

```bash
cd ~/Projects/esp32/C6_SuperMini
source .venv/bin/activate
get_idf
pytest tests/pytest_hello_world.py --target esp32c6 -p esp-idf
deactivate
```

Для своих примеров можно добавлять тесты в `tests/` по аналогии с `pytest_hello_world.py`. См. также [Архитектура, память, тесты](АРХИТЕКТУРА_ПАМЯТЬ_ТЕСТЫ.md).
