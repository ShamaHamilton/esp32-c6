"""
Модуль моделей данных (Data Models).

Описывает физические параметры компонентов:
- Литиевого аккумулятора (емкость, отсечка по напряжению, саморазряд).
- Паразитных утечек платы ESP32-C6 SuperMini (делитель АЦП, стабилизатор LDO, зарядка LTH7R).
- Задачи измерения АЦП и чтения GPIO.
- Профилей сна МК и сценариев беспроводной передачи данных.
"""

from __future__ import annotations

from dataclasses import dataclass, field


@dataclass
class BatteryConfig:
    """
    Параметры литиевого аккумулятора (по умолчанию: Li-Ion 18350, 1000 мАч, 3.7 В).
    """

    # Паспортная (номинальная) емкость аккумулятора при полном разряде от 4.2 В до 2.75 В (мАч)
    nominal_capacity_mah: float = 1000.0

    # Среднее номинальное напряжение ячейки на плато разряда (В).
    # Используется для вычисления среднего тока утечки через резистивный делитель.
    nominal_voltage_v: float = 3.7

    # Напряжение полностью заряженного аккумулятора (В)
    max_voltage_v: float = 4.2

    # Минимальное рабочее напряжение на АКБ до срабатывания сброса (Brownout) или отсечки LDO (В).
    # Складывается из минимального напряжения ESP32-C6 с радио (~3.0 В) + падение на LDO (~0.25 В).
    cutoff_voltage_v: float = 3.25

    # Доля реально извлекаемой емкости выше порога cutoff_voltage_v (от 0.1 до 1.0).
    # Учитывает неизвлекаемый остаток заряда на дне разрядной кривой (~15%).
    usable_capacity_ratio: float = 0.85

    # Внутренний химический саморазряд аккумулятора (% от номинальной емкости в месяц)
    self_discharge_percent_per_month: float = 2.0

    # Количество часов в расчетном месяце (используется для перевода %/мес в непрерывный ток мкА)
    hours_per_month: float = 720.72

    @property
    def effective_capacity_mah(self) -> float:
        """Реально доступная емкость аккумулятора до отключения схемы (мАч)."""
        return self.nominal_capacity_mah * self.usable_capacity_ratio

    @property
    def self_discharge_current_ua(self) -> float:
        """
        Эквивалентный непрерывный ток внутреннего саморазряда АКБ (мкА).
        Формула: (C_nom * (Процент / 100) / Часов_в_месяце) * 1000.
        """
        lost_mah_per_month = self.nominal_capacity_mah * (self.self_discharge_percent_per_month / 100.0)
        return (lost_mah_per_month / self.hours_per_month) * 1000.0


@dataclass
class BoardParasiticConfig:
    """
    Параметры постоянных фоновых утечек платы ESP32-C6 SuperMini и внешней обвязки.
    Эти токи расходуют заряд АКБ непрерывно (24/7), даже когда сам МК находится в Deep Sleep.
    """

    # Верхнее плечо резистивного делителя напряжения АКБ (между BAT+ и GPIO0), кОм
    divider_r1_kohm: float = 200.0

    # Нижнее плечо резистивного делителя напряжения АКБ (между GPIO0 и GND), кОм
    divider_r2_kohm: float = 200.0

    # Холостой ток покоя (Quiescent Current, Iq) LDO-стабилизатора 3.3 В на плате, мкА.
    # Штатный LDO на SuperMini (ME6211C33 / LN1134) потребляет ~45 мкА.
    # Для ультраэкономичных LDO (HT7833 / MCP1700) составляет ~2.0 мкА; для TPS7A02 — 0.025 мкА.
    ldo_quiescent_current_ua: float = 45.0

    # Обратный ток утечки контроллера заряда LTH7R (LTC4054) через пин BAT при отключенном USB, мкА
    charger_leakage_ua: float = 2.0

    # Дополнительные паразитные утечки (внешние датчики, подтяжки, ток утечки конденсаторов), мкА
    extra_leakage_ua: float = 0.0

    @property
    def total_divider_resistance_kohm(self) -> float:
        """Суммарное сопротивление резистивного делителя АКБ (кОм)."""
        return self.divider_r1_kohm + self.divider_r2_kohm

    def get_divider_leakage_ua(self, battery_voltage_v: float) -> float:
        """
        Рассчитывает непрерывный ток утечки через делитель напряжения АКБ по закону Ома (мкА).
        I (мкА) = (U_bat (В) / (R1 + R2) (кОм)) * 1000.
        """
        r_sum = self.total_divider_resistance_kohm
        if r_sum <= 0:
            return 0.0
        return (battery_voltage_v / r_sum) * 1000.0

    def get_total_hardware_leakage_ua(self, battery_voltage_v: float) -> float:
        """Суммарный ток утечки аппаратной обвязки платы без учета саморазряда АКБ и МК (мкА)."""
        return (
            self.get_divider_leakage_ua(battery_voltage_v)
            + self.ldo_quiescent_current_ua
            + self.charger_leakage_ua
            + self.extra_leakage_ua
        )


@dataclass
class MeasurementTaskConfig:
    """
    Параметры активной фазы опроса датчиков: чтение АЦП (ADC1_CH0) с фильтрацией и пина USB (GPIO1).
    """

    # Количество последовательных выборок АЦП для усреднения шума
    adc_samples: int = 64

    # Аппаратная пауза между выборками АЦП (esp_rom_delay_us), мкс
    inter_sample_delay_us: float = 500.0

    # Накладные расходы времени на само чтение регистра АЦП и суммирование одной выборки, мкс
    adc_read_overhead_us: float = 15.0

    # Время инициализации драйвера adc_oneshot, калибровки curve_fitting и GPIO, мс
    driver_init_time_ms: float = 2.04

    # Средний ток потребления процессора ESP32-C6 (RISC-V @ 160 МГц) во время опроса АЦП без радио, мА
    active_mcu_current_ma: float = 22.0

    @property
    def total_duration_ms(self) -> float:
        """Полная длительность фазы измерения в миллисекундах."""
        samples_time_ms = (self.adc_samples * (self.inter_sample_delay_us + self.adc_read_overhead_us)) / 1000.0
        return self.driver_init_time_ms + samples_time_ms

    @property
    def charge_mas(self) -> float:
        """Заряд (энергия), затрачиваемый на один цикл измерения АЦП и GPIO (мА·с)."""
        return self.active_mcu_current_ma * (self.total_duration_ms / 1000.0)


@dataclass
class SleepModeProfile:
    """
    Характеристики режима сна самого микроконтроллера ESP32-C6.
    """

    # Системный код режима ('LIGHT', 'DEEP_STD', 'DEEP_OPT')
    code: str

    # Человекочитаемое название режима сна
    name: str

    # Собственный ток потребления кристалла ESP32-C6 во сне, мкА
    mcu_sleep_current_ua: float

    # Время выхода из сна до входа в функцию app_main / продолжения цикла, мс
    boot_time_ms: float

    # Средний ток потребления чипа на этапе пробуждения / работы загрузчика Bootloader, мА
    boot_current_ma: float

    @property
    def boot_charge_mas(self) -> float:
        """Энергия («штраф»), сжигаемая при каждом пробуждении чипа (мА·с)."""
        return self.boot_current_ma * (self.boot_time_ms / 1000.0)


@dataclass
class ScenarioConfig:
    """
    Описание прикладного сценария: Протокол связи + Режим сна МК.
    """

    # Идентификатор группы протокола ('Offline', 'BLE BTHome', 'Zigbee 3.0', 'Wi-Fi MQTT')
    protocol_group: str

    # Название сценария для отображения в таблицах
    title: str

    # Краткое пояснение механики работы сценария
    subtitle: str

    # Ссылка на профиль режима сна МК
    sleep_profile: SleepModeProfile

    # Длительность работы радиомодуля и сетевого стека за 1 цикл пробуждения, мс
    tx_duration_ms: float

    # Средний ток потребления чипа во время активной фазы радиопередачи, мА
    tx_current_ma: float

    # Дополнительный ток во сне на поддержание ассоциации с сетью (мкА).
    # Актуально для Wi-Fi Light Sleep (DTIM3), где радио просыпается по маячкам роутера.
    extra_sleep_current_ua: float = 0.0

    @property
    def tx_charge_mas(self) -> float:
        """Заряд, расходуемый на подготовку и отправку данных по радиоканалу (мА·с)."""
        return self.tx_current_ma * (self.tx_duration_ms / 1000.0)


@dataclass
class IntervalResult:
    """
    Расчетные показатели автономности для одного конкретного интервала пробуждения.
    """

    interval_sec: int  # Интервал между пробуждениями (сек)
    avg_current_ma: float  # Средний полный ток потребления системы (мА)
    autonomy_hours: float  # Время работы (часов)
    autonomy_days: float  # Время работы (суток)
    autonomy_months: float  # Время работы (месяцев)
    sleep_share_pct: float  # Доля энергии, уходящая на сон и фоновые утечки (%)
    active_share_pct: float  # Доля энергии, уходящая на пробуждения, АЦП и передачу (%)


@dataclass
class ScenarioEvaluation:
    """
    Итоговый результат расчета одного сценария по всем интервалам времени.
    """

    scenario: ScenarioConfig
    total_sleep_current_ua: float  # Полный ток системы во сне с учетом утечек (мкА)
    q_boot_mas: float  # Заряд на пробуждение/загрузку (мА·с)
    q_meas_mas: float  # Заряд на опрос АЦП и GPIO (мА·с)
    q_tx_mas: float  # Заряд на радиопередачу (мА·с)
    q_cycle_total_mas: float  # Суммарная энергия одного цикла пробуждения (мА·с)
    breakeven_vs_light_sec: float | None  # Точка безубыточности против Light Sleep (сек)
    intervals: list[IntervalResult] = field(default_factory=list)
