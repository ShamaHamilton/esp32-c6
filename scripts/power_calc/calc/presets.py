"""
Модуль предустановок (Presets).

Содержит калиброванные параметры режимов сна ESP32-C6 (Light Sleep, Deep Sleep Std, Deep Sleep Opt)
и профили энергопотребления для протоколов Оффлайн, BLE BTHome v2, Zigbee 3.0 и Wi-Fi MQTT.
"""

from __future__ import annotations

from .models import ScenarioConfig, SleepModeProfile


def get_default_sleep_profiles() -> dict[str, SleepModeProfile]:
    """
    Возвращает словарь базовых профилей сна микроконтроллера ESP32-C6.
    """
    return {
        "LIGHT": SleepModeProfile(
            code="LIGHT",
            name="Light Sleep",
            mcu_sleep_current_ua=15.0,  # ОЗУ запитано, состояние программы сохраняется
            boot_time_ms=2.0,  # Выход из паузы за ~2 мс без перезагрузки
            boot_current_ma=20.0,  # 0.04 мА·с
        ),
        "DEEP_STD": SleepModeProfile(
            code="DEEP_STD",
            name="Deep Sleep Std",
            mcu_sleep_current_ua=7.0,  # Питается только RTC-домен и логика пробуждения
            boot_time_ms=140.0,  # Полный старт загрузчика, проверка SHA256 и вывод логов UART
            boot_current_ma=25.0,  # 3.50 мА·с
        ),
        "DEEP_OPT": SleepModeProfile(
            code="DEEP_OPT",
            name="Deep Sleep Opt",
            mcu_sleep_current_ua=7.0,  # Ток сна 7 мкА
            boot_time_ms=35.2,  # Fast Boot: пропуск валидации образа и отключение UART-логов
            boot_current_ma=25.0,  # 0.88 мА·с
        ),
    }


def get_default_scenarios(profiles: dict[str, SleepModeProfile]) -> list[ScenarioConfig]:
    """
    Возвращает список из 12 сценариев работы датчика (4 протокола x 3 режима сна).
    """
    return [
        # 1. ОФФЛАЙН (Только чтение АЦП и GPIO без включения радиомодуля)
        ScenarioConfig(
            protocol_group="Offline",
            title="Оффлайн (Light Sleep)",
            subtitle="Замер АЦП в ОЗУ, без радио",
            sleep_profile=profiles["LIGHT"],
            tx_duration_ms=0.0,
            tx_current_ma=0.0,
        ),
        ScenarioConfig(
            protocol_group="Offline",
            title="Оффлайн (Deep Sleep Std)",
            subtitle="Полная загрузка + замер в RTC RAM",
            sleep_profile=profiles["DEEP_STD"],
            tx_duration_ms=0.0,
            tx_current_ma=0.0,
        ),
        ScenarioConfig(
            protocol_group="Offline",
            title="Оффлайн (Deep Sleep Opt)",
            subtitle="Fast Boot + замер в RTC RAM",
            sleep_profile=profiles["DEEP_OPT"],
            tx_duration_ms=0.0,
            tx_current_ma=0.0,
        ),
        # 2. BLUETOOTH LE 5.3 (Отправка рекламных пакетов BTHome v2 в Home Assistant)
        ScenarioConfig(
            protocol_group="BLE BTHome",
            title="BLE BTHome (Light Sleep)",
            subtitle="3 пакета Adv, стек BLE в ОЗУ",
            sleep_profile=profiles["LIGHT"],
            tx_duration_ms=40.0,
            tx_current_ma=20.0,  # 0.80 мА·с
        ),
        ScenarioConfig(
            protocol_group="BLE BTHome",
            title="BLE BTHome (Deep Sleep Std)",
            subtitle="Полный старт + иниц. BLE + 3 Adv",
            sleep_profile=profiles["DEEP_STD"],
            tx_duration_ms=80.0,
            tx_current_ma=25.0,  # 2.00 мА·с
        ),
        ScenarioConfig(
            protocol_group="BLE BTHome",
            title="BLE BTHome (Deep Sleep Opt)",
            subtitle="Fast Boot + иниц. BLE + 3 Adv",
            sleep_profile=profiles["DEEP_OPT"],
            tx_duration_ms=80.0,
            tx_current_ma=25.0,  # 2.00 мА·с
        ),
        # 3. ZIGBEE 3.0 (Sleepy End Device -> Координатор Zigbee2MQTT / ZHA)
        ScenarioConfig(
            protocol_group="Zigbee 3.0",
            title="Zigbee (Light Sleep)",
            subtitle="ZCL Report в Z2M, стек в ОЗУ",
            sleep_profile=profiles["LIGHT"],
            tx_duration_ms=25.0,
            tx_current_ma=36.0,  # 0.90 мА·с
        ),
        ScenarioConfig(
            protocol_group="Zigbee 3.0",
            title="Zigbee (Deep Sleep Std)",
            subtitle="Полный старт + NVS + ZCL Report",
            sleep_profile=profiles["DEEP_STD"],
            tx_duration_ms=70.0,
            tx_current_ma=35.0,  # 2.45 мА·с
        ),
        ScenarioConfig(
            protocol_group="Zigbee 3.0",
            title="Zigbee (Deep Sleep Opt)",
            subtitle="Fast Boot + NVS + ZCL Report",
            sleep_profile=profiles["DEEP_OPT"],
            tx_duration_ms=70.0,
            tx_current_ma=35.0,  # 2.45 мА·с
        ),
        # 4. WI-FI (Публикация JSON в MQTT-брокер Home Assistant)
        ScenarioConfig(
            protocol_group="Wi-Fi MQTT",
            title="Wi-Fi MQTT (Light Sleep DTIM3)",
            subtitle="Удержание связи с роутером и TCP",
            sleep_profile=profiles["LIGHT"],
            tx_duration_ms=35.0,
            tx_current_ma=80.0,  # 2.80 мА·с
            extra_sleep_current_ua=1085.0,  # Поддержание Wi-Fi DTIM3 во сне (~1100 мкА суммарно на чип)
        ),
        ScenarioConfig(
            protocol_group="Wi-Fi MQTT",
            title="Wi-Fi MQTT (Deep Sleep Std)",
            subtitle="Полный скан каналов + DHCP + MQTT",
            sleep_profile=profiles["DEEP_STD"],
            tx_duration_ms=2200.0,
            tx_current_ma=110.0,  # 242.00 мА·с
        ),
        ScenarioConfig(
            protocol_group="Wi-Fi MQTT",
            title="Wi-Fi MQTT (Deep Sleep Opt)",
            subtitle="Fast Boot + Static IP + BSSID в RTC",
            sleep_profile=profiles["DEEP_OPT"],
            tx_duration_ms=350.0,
            tx_current_ma=110.0,  # 38.50 мА·с
        ),
    ]
