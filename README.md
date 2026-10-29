# C6_SuperMini (ESP32-C6)

Проект для платы ESP32-C6 SuperMini на ESP-IDF v6.1.

## Документация
- [Инструкция по использованию](docs/ИНСТРУКЦИЯ_ПО_ИСПОЛЬЗОВАНИЮ.md) - как работать в настроенной среде
- [Гайд для начинающего](docs/ГАЙД_ДЛЯ_НАЧИНАЮЩЕГО_ESP32_C6_ESP-IDF_C.md) - основы ESP-IDF, примеры, отладка
- [Настройка новой машины (WSL + VSCode)](docs/НАСТРОЙКА_НОВОЙ_МАШИНЫ_WSL_VSCODE.md) - разворачивание среды с нуля
- [Архитектура, память, тесты](docs/АРХИТЕКТУРА_ПАМЯТЬ_ТЕСТЫ.md) - структура проекта, карта памяти ESP32-C6, pytest

## Быстрый старт
```bash
get_idf
cd ~/Projects/esp32/C6_SuperMini
idf.py build
idf.py -p /dev/ttyACM0 flash monitor  # Ctrl+] выход
```

## Состояние
- Чип: ESP32-C6FH4 v0.2, 4MB Flash
- Загрузчик + partition table + `c6sm` (IDF v6.1) прошиты корректно
- Порт: `/dev/ttyACM0` (usbipd из Windows)
- Резервные копии: `backups/`
- [Тестирование](docs/ТЕСТИРОВАНИЕ.md) - pytest, venv, как писать и запускать тесты

```Plaintext
your_project_dir/
├── CMakeLists.txt                   # Корневой файл сборки проекта
├── idf_component.yml                # Менеджер зависимостей (для автозагрузки Zigbee SDK)
└── main/
    ├── CMakeLists.txt               # Регистрация всех исходников и папок инклудов
    ├── app_config.h                 # Главный пульт управления: выбор Режима сна и Протокола
    ├── main.c                       # Единая логика жизненного цикла (Cycle / Light / Deep)
    ├── battery_sensor.h             # Заголовок драйвера измерения АКБ и пина USB
    ├── battery_sensor.c             # Реализация чтения АЦП, фильтрации и калибровки
    └── protocols/                   # Директория модулей связи с Home Assistant
        ├── ha_telemetry.h           # Единый интерфейс отправки данных и логов
        ├── proto_ble_bthome.c       # Протокол 1: BLE BTHome v2 (Автообнаружение в HA без паролей)
        ├── proto_wifi_mqtt.c        # Протокол 2: Wi-Fi + MQTT (JSON-телеметрия + текстовый лог)
        └── proto_zigbee.c           # Протокол 3: Zigbee 3.0 Sleepy End Device (для Z2M / ZHA)
```
