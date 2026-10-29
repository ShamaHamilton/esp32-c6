"""
Модуль интерфейса командной строки (CLI).

Отвечает за парсинг аргументов терминала, инициализацию конфигурационных моделей
и передачу управления вычислительному ядру и рендереру отчетов.
"""

from __future__ import annotations

import argparse
import sys

from .engine import PowerCalculatorEngine
from .formatter import export_markdown_table, print_report
from .models import BatteryConfig, BoardParasiticConfig, MeasurementTaskConfig
from .presets import get_default_scenarios, get_default_sleep_profiles


def build_arg_parser() -> argparse.ArgumentParser:
    """Создает и настраивает парсер аргументов командной строки."""
    parser = argparse.ArgumentParser(
        description="Инженерный калькулятор энергопотребления и автономности ESP32-C6 SuperMini",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter,
    )
    # Параметры АКБ
    parser.add_argument("--capacity", type=float, default=1000.0, help="Номинальная емкость АКБ (мАч)")
    parser.add_argument("--voltage", type=float, default=3.7, help="Среднее номинальное напряжение АКБ (В)")
    parser.add_argument(
        "--usable-ratio", type=float, default=0.85, help="Доля реально доступной емкости АКБ (0.1 - 1.0)"
    )
    parser.add_argument("--self-discharge", type=float, default=2.0, help="Саморазряд АКБ в месяц (%%)")

    # Параметры обвязки платы
    parser.add_argument("--r1", type=float, default=200.0, help="Верхний резистор делителя АКБ R1 (кОм)")
    parser.add_argument("--r2", type=float, default=200.0, help="Нижний резистор делителя АКБ R2 (кОм)")
    parser.add_argument("--ldo-ua", type=float, default=45.0, help="Ток покоя LDO стабилизатора платы (мкА)")
    parser.add_argument("--charger-ua", type=float, default=2.0, help="Обратный ток утечки чипа зарядки LTH7R (мкА)")
    parser.add_argument(
        "--extra-ua", type=float, default=0.0, help="Дополнительный паразитный ток утечки датчиков (мкА)"
    )

    # Параметры замера АЦП
    parser.add_argument("--samples", type=int, default=64, help="Число выборок АЦП для усреднения за 1 цикл")

    # Интервалы и вывод
    parser.add_argument(
        "--intervals",
        type=int,
        nargs="+",
        default=[60, 600, 1800, 3600],
        help="Список интервалов пробуждения в секундах для расчета столбцов таблицы",
    )
    parser.add_argument(
        "--focus", type=int, default=600, help="Целевой интервал (в секундах) для детального анализа в Блоке 3"
    )
    parser.add_argument(
        "--export-md", type=str, default=None, help="Путь к файлу для сохранения Markdown-таблицы (.md)"
    )
    parser.add_argument("--no-color", action="store_true", help="Отключить ANSI-цвета в выводе")
    return parser


def run_cli(argv: list[str] | None = None) -> None:
    """Точка запуска CLI-приложения."""
    parser = build_arg_parser()
    args = parser.parse_args(argv)

    battery = BatteryConfig(
        nominal_capacity_mah=args.capacity,
        nominal_voltage_v=args.voltage,
        usable_capacity_ratio=args.usable_ratio,
        self_discharge_percent_per_month=args.self_discharge,
    )

    board = BoardParasiticConfig(
        divider_r1_kohm=args.r1,
        divider_r2_kohm=args.r2,
        ldo_quiescent_current_ua=args.ldo_ua,
        charger_leakage_ua=args.charger_ua,
        extra_leakage_ua=args.extra_ua,
    )

    meas_task = MeasurementTaskConfig(adc_samples=args.samples)
    intervals = sorted(set(args.intervals + [args.focus]))

    profiles = get_default_sleep_profiles()
    scenarios = get_default_scenarios(profiles)

    engine = PowerCalculatorEngine(
        battery=battery,
        board=board,
        meas_task=meas_task,
        intervals_sec=intervals,
    )
    evaluations = engine.evaluate_all(scenarios)

    use_colors = (not args.no_color) and sys.stdout.isatty()
    print_report(engine, evaluations, focus_interval_sec=args.focus, use_colors=use_colors)

    if args.export_md:
        md_content = export_markdown_table(engine, evaluations)
        with open(args.export_md, "w", encoding="utf-8") as f:
            f.write(md_content + "\n")
        print(f"Markdown-таблица сохранена в файл: {args.export_md}\n")
