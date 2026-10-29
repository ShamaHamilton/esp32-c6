"""
Модуль математического ядра калькулятора (Calculation Engine).

Выполняет расчет:
- Суммарных фоновых токов утечки (обвязка + саморазряд АКБ).
- Заряда, расходуемого за один полный цикл работы (Q_cycle).
- Точки безубыточности (Break-even point) режимов Deep Sleep относительно Light Sleep.
- Среднего тока потребления и времени автономной работы для заданных интервалов.
"""

from __future__ import annotations

from .models import (
    BatteryConfig,
    BoardParasiticConfig,
    IntervalResult,
    MeasurementTaskConfig,
    ScenarioConfig,
    ScenarioEvaluation,
)


class PowerCalculatorEngine:
    """
    Вычислительное ядро анализа энергопотребления ESP32-C6 SuperMini.
    """

    def __init__(
        self,
        battery: BatteryConfig,
        board: BoardParasiticConfig,
        meas_task: MeasurementTaskConfig,
        intervals_sec: list[int],
    ) -> None:
        self.battery = battery
        self.board = board
        self.meas_task = meas_task
        self.intervals_sec = intervals_sec

    @property
    def background_leakage_ua(self) -> float:
        """
        Непрерывный фоновый паразитный ток (аппаратная обвязка платы + саморазряд АКБ), мкА.
        """
        hw_leak_ua = self.board.get_total_hardware_leakage_ua(self.battery.nominal_voltage_v)
        return hw_leak_ua + self.battery.self_discharge_current_ua

    def evaluate_all(self, scenarios: list[ScenarioConfig]) -> list[ScenarioEvaluation]:
        """
        Проводит полный цикл расчетов для списка сценариев по всем заданным интервалам пробуждения.
        """
        bg_ua = self.background_leakage_ua
        q_meas = self.meas_task.charge_mas
        c_eff = self.battery.effective_capacity_mah

        # Сохраняем эталонные показатели Light Sleep (ток сна и энергию цикла) для каждого протокола,
        # чтобы вычислить точку безубыточности (Break-even point) для режимов Deep Sleep.
        light_baselines: dict[str, tuple[float, float]] = {}
        for sc in scenarios:
            if sc.sleep_profile.code == "LIGHT":
                i_sleep_ma = (sc.sleep_profile.mcu_sleep_current_ua + sc.extra_sleep_current_ua + bg_ua) / 1000.0
                q_cycle = sc.sleep_profile.boot_charge_mas + q_meas + sc.tx_charge_mas
                light_baselines[sc.protocol_group] = (i_sleep_ma, q_cycle)

        evaluations: list[ScenarioEvaluation] = []
        for sc in scenarios:
            # 1. Полный ток системы во сне (мкА и мА)
            total_sleep_ua = sc.sleep_profile.mcu_sleep_current_ua + sc.extra_sleep_current_ua + bg_ua
            total_sleep_ma = total_sleep_ua / 1000.0

            # 2. Составляющие энергии одного активного цикла (мА·с)
            q_boot = sc.sleep_profile.boot_charge_mas
            q_tx = sc.tx_charge_mas
            q_total = q_boot + q_meas + q_tx

            # 3. Расчет точки безубыточности (Break-even):
            # Q_deep + I_sleep_deep * T = Q_light + I_sleep_light * T
            # T = (Q_deep - Q_light) / (I_sleep_light - I_sleep_deep)
            breakeven_sec: float | None = None
            if sc.sleep_profile.code != "LIGHT" and sc.protocol_group in light_baselines:
                i_light_ma, q_light_mas = light_baselines[sc.protocol_group]
                delta_i_ma = i_light_ma - total_sleep_ma
                delta_q_mas = q_total - q_light_mas
                if delta_i_ma > 0 and delta_q_mas > 0:
                    breakeven_sec = delta_q_mas / delta_i_ma

            # 4. Расчет автономности для каждого интервала пробуждения
            interval_results: list[IntervalResult] = []
            for t_sec in self.intervals_sec:
                # Средний ток, добавляемый периодическими пробуждениями: I_act = Q_cycle / T_sec
                i_active_avg_ma = q_total / float(t_sec)
                i_avg_total_ma = total_sleep_ma + i_active_avg_ma

                hours = c_eff / i_avg_total_ma
                days = hours / 24.0
                months = days / 30.4375

                sleep_share = (total_sleep_ma / i_avg_total_ma) * 100.0
                active_share = (i_active_avg_ma / i_avg_total_ma) * 100.0

                interval_results.append(
                    IntervalResult(
                        interval_sec=t_sec,
                        avg_current_ma=i_avg_total_ma,
                        autonomy_hours=hours,
                        autonomy_days=days,
                        autonomy_months=months,
                        sleep_share_pct=sleep_share,
                        active_share_pct=active_share,
                    )
                )

            evaluations.append(
                ScenarioEvaluation(
                    scenario=sc,
                    total_sleep_current_ua=total_sleep_ua,
                    q_boot_mas=q_boot,
                    q_meas_mas=q_meas,
                    q_tx_mas=q_tx,
                    q_cycle_total_mas=q_total,
                    breakeven_vs_light_sec=breakeven_sec,
                    intervals=interval_results,
                )
            )

        return evaluations
