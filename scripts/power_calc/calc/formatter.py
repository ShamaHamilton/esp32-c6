"""
Модуль форматирования и визуализации (Console & Markdown Formatter).

Реализует ANSI-совместимый табличный движок, который вычисляет видимую длину строк
без учета цветовых escape-последовательностей, гарантируя идеальное совпадение
всех колонок и разделителей в терминале.
"""

from __future__ import annotations

import re
from collections.abc import Sequence

from .engine import PowerCalculatorEngine
from .models import ScenarioEvaluation

# Регулярное выражение для удаления невидимых управляющих ANSI-кодов при подсчете ширины ячейки
ANSI_ESCAPE_RE = re.compile(r"\x1b\[[0-9;]*m")


def visible_len(text: str) -> int:
    """
    Возвращает реальную видимую длину строки в знакоместах терминала,
    игнорируя невидимые ANSI-коды цветовой разметки.
    """
    return len(ANSI_ESCAPE_RE.sub("", text))


def pad_ansi(text: str, width: int, align: str = "left") -> str:
    """
    Дополняет строку пробелами до нужной видимой ширины `width` с учетом ANSI-цветов.
    Поддерживаемые значения `align`: 'left', 'right', 'center'.
    """
    vis = visible_len(text)
    pad = max(0, width - vis)
    if align == "right":
        return (" " * pad) + text
    if align == "center":
        left_pad = pad // 2
        right_pad = pad - left_pad
        return (" " * left_pad) + text + (" " * right_pad)
    return text + (" " * pad)


def format_interval_label(seconds: int) -> str:
    """Форматирует интервал в секундах в компактную подпись ('1.0 мин', '10.0 мин', '1.0 час')."""
    if seconds < 60:
        return f"{seconds} сек"
    if seconds < 3600:
        return f"{seconds / 60.0:.1f} мин"
    return f"{seconds / 3600.0:.1f} час"


class ConsoleFormatter:
    """
    Рендерер консольного отчета с поддержкой ANSI-цветов и автоматическим выравниванием таблиц.
    """

    def __init__(self, use_colors: bool = True) -> None:
        self.use_colors = use_colors
        self.RESET = "\033[0m" if use_colors else ""
        self.BOLD = "\033[1m" if use_colors else ""
        self.DIM = "\033[2m" if use_colors else ""
        self.CYAN = "\033[36m" if use_colors else ""
        self.GREEN = "\033[32m" if use_colors else ""
        self.YELLOW = "\033[33m" if use_colors else ""
        self.RED = "\033[31m" if use_colors else ""

    def colorize_days(self, days: float, months: float) -> str:
        """
        Форматирует дни и месяцы в строку строго фиксированной видимой длины (20 символов)
        и окрашивает ее по порогам автономности.
        """
        text = f"{days:6.1f} дн ({months:4.1f} мес)"
        if not self.use_colors:
            return text
        if days >= 300:
            return f"{self.GREEN}{text}{self.RESET}"
        if days >= 120:
            return f"{self.YELLOW}{text}{self.RESET}"
        return f"{self.RED}{text}{self.RESET}"

    def make_bar(self, value: float, max_value: float, width: int = 20) -> str:
        """Строит горизонтальный текстовый индикатор (прогресс-бар) заданной ширины."""
        if max_value <= 0:
            return "░" * width
        ratio = max(0.0, min(1.0, value / max_value))
        filled = round(ratio * width)
        empty = width - filled
        bar_str = ("█" * filled) + ("░" * empty)
        if not self.use_colors:
            return bar_str
        color = self.GREEN if ratio >= 0.75 else (self.YELLOW if ratio >= 0.35 else self.RED)
        return f"{color}{bar_str}{self.RESET}"

    def render_table(
        self,
        headers: Sequence[str],
        rows: Sequence[Sequence[str]],
        alignments: Sequence[str],
        group_breaks: Sequence[int] = (),
    ) -> tuple[str, int]:
        """
        Универсальный генератор идеально выровненных ASCII-таблиц.
        Вычисляет максимальную видимую ширину каждого столбца и выравнивает все ячейки.
        Возвращает готовый текст таблицы и ее полную видимую ширину в символах.
        """
        num_cols = len(headers)
        col_widths = [visible_len(h) for h in headers]
        for row in rows:
            for i in range(num_cols):
                col_widths[i] = max(col_widths[i], visible_len(row[i]))

        # Строим строку заголовка
        header_cells = [
            f"{self.BOLD}{pad_ansi(headers[i], col_widths[i], alignments[i])}{self.RESET}" for i in range(num_cols)
        ]
        header_line = " | ".join(header_cells)
        total_width = sum(col_widths) + (3 * (num_cols - 1))
        separator = "-" * total_width

        lines = [separator, header_line, separator]
        break_set = set(group_breaks)

        for row_idx, row in enumerate(rows):
            if row_idx in break_set:
                lines.append(separator)
            padded_cells = [pad_ansi(row[i], col_widths[i], alignments[i]) for i in range(num_cols)]
            lines.append(" | ".join(padded_cells))

        lines.append(separator)
        return "\n".join(lines), total_width


def print_report(
    engine: PowerCalculatorEngine,
    evaluations: list[ScenarioEvaluation],
    focus_interval_sec: int,
    use_colors: bool = True,
) -> None:
    """
    Формирует и выводит в консоль полный 3-секционный инженерный отчет с идеальным выравниванием.
    """
    fmt = ConsoleFormatter(use_colors=use_colors)
    bat = engine.battery
    brd = engine.board
    meas = engine.meas_task

    div_ua = brd.get_divider_leakage_ua(bat.nominal_voltage_v)
    sd_ua = bat.self_discharge_current_ua
    bg_ua = engine.background_leakage_ua
    eternal_deep_sleep_days = (bat.effective_capacity_mah / ((bg_ua + 7.0) / 1000.0)) / 24.0

    # --- ПОДГОТОВКА ТАБЛИЦЫ БЛОКА 2 (Сравнительная матрица), чтобы узнать общую ширину отчета ---
    comp_headers = [
        "Сценарий (Протокол и Режим сна)",
        "Ток сна",
        "Q цикла",
        "Безубыточность",
    ] + [f"Цикл {format_interval_label(sec)}" for sec in engine.intervals_sec]

    comp_alignments = ["left", "right", "right", "right"] + (["right"] * len(engine.intervals_sec))
    comp_rows: list[list[str]] = []
    group_breaks: list[int] = []

    last_group = None
    for idx, ev in enumerate(evaluations):
        if last_group is not None and ev.scenario.protocol_group != last_group:
            group_breaks.append(idx)
        last_group = ev.scenario.protocol_group

        if ev.breakeven_vs_light_sec is None:
            be_str = f"{fmt.DIM}Базовый (Light){fmt.RESET}"
        else:
            be_min = ev.breakeven_vs_light_sec / 60.0
            be_str = f"> {be_min:4.1f} мин ({ev.breakeven_vs_light_sec:3.0f}с)"

        row = [
            f"{fmt.BOLD}{ev.scenario.title}{fmt.RESET}",
            f"{ev.total_sleep_current_ua:6.0f} мкА",
            f"{ev.q_cycle_total_mas:6.2f} мАс",
            be_str,
        ] + [fmt.colorize_days(ir.autonomy_days, ir.autonomy_months) for ir in ev.intervals]
        comp_rows.append(row)

    comp_table_str, report_width = fmt.render_table(
        headers=comp_headers,
        rows=comp_rows,
        alignments=comp_alignments,
        group_breaks=group_breaks,
    )

    # --- ПОДГОТОВКА ТАБЛИЦЫ БЛОКА 1 (Аппаратный профиль и постоянные утечки) ---
    hw_headers = ["Узел / Параметр схемы", "Физический режим / Описание", "Расчетная величина"]
    hw_alignments = ["left", "left", "right"]
    hw_rows = [
        [
            "Аккумулятор (Li-Ion 18350)",
            f"Номинал {bat.nominal_capacity_mah:.0f} мАч ({bat.nominal_voltage_v:.1f} В), отсечка LDO {bat.cutoff_voltage_v:.2f} В ({bat.usable_capacity_ratio * 100:.0f}%)",
            f"{fmt.BOLD}{fmt.GREEN}{bat.effective_capacity_mah:6.2f} мАч{fmt.RESET}",
        ],
        [
            "Делитель АЦП АКБ (GPIO0)",
            f"R1 = {brd.divider_r1_kohm:.0f} кОм + R2 = {brd.divider_r2_kohm:.0f} кОм (R_sum = {brd.total_divider_resistance_kohm:.0f} кОм)",
            f"{fmt.YELLOW}{div_ua:6.2f} мкА{fmt.RESET}",
        ],
        [
            "Стабилизатор питания LDO",
            "Собственный холостой ток покоя стабилизатора (Quiescent, Iq)",
            f"{fmt.YELLOW}{brd.ldo_quiescent_current_ua:6.2f} мкА{fmt.RESET}",
        ],
        [
            "Контроллер заряда LTH7R",
            "Обратная утечка линии BAT при отключенном кабеле USB",
            f"{fmt.YELLOW}{brd.charger_leakage_ua + brd.extra_leakage_ua:6.2f} мкА{fmt.RESET}",
        ],
        [
            "Химический саморазряд АКБ",
            f"Потеря {bat.self_discharge_percent_per_month:.1f}% емкости в месяц ({bat.hours_per_month:.1f} ч/мес)",
            f"{fmt.YELLOW}{sd_ua:6.2f} мкА{fmt.RESET}",
        ],
        [
            f"{fmt.BOLD}ИТОГО фоновый ток (I_bg){fmt.RESET}",
            f"Непрерывный разряд 24/7 (потолок в вечном Deep Sleep: {eternal_deep_sleep_days:.0f} дн / {eternal_deep_sleep_days / 30.4375:.1f} мес)",
            f"{fmt.BOLD}{fmt.RED}{bg_ua:6.2f} мкА{fmt.RESET}",
        ],
        [
            "Активный замер (АЦП + GPIO1)",
            f"{meas.adc_samples} выборок с паузой {meas.inter_sample_delay_us:.0f} мкс ({meas.total_duration_ms:.1f} мс @ {meas.active_mcu_current_ma:.1f} мА)",
            f"{fmt.BOLD}{fmt.CYAN}{meas.charge_mas:6.2f} мАс{fmt.RESET}",
        ],
    ]
    hw_table_str, _ = fmt.render_table(
        headers=hw_headers,
        rows=hw_rows,
        alignments=hw_alignments,
        group_breaks=[5, 6],
    )

    # --- ВЫВОД ШАПКИ И БЛОКОВ 1 И 2 ---
    banner_line = "=" * report_width
    title_text = "ИНЖЕНЕРНЫЙ КАЛЬКУЛЯТОР АВТОНОМНОСТИ ESP32-C6 SUPERMINI (Home Assistant IoT)"
    print(f"\n{fmt.BOLD}{fmt.CYAN}{banner_line}{fmt.RESET}")
    print(f"{fmt.BOLD}{pad_ansi(title_text, report_width, 'center')}{fmt.RESET}")
    print(f"{fmt.BOLD}{fmt.CYAN}{banner_line}{fmt.RESET}")

    print(f"\n{fmt.BOLD}1. АППАРАТНЫЙ ПРОФИЛЬ И ПОСТОЯННЫЕ ФОНОВЫЕ УТЕЧКИ (РАБОТАЮТ 24/7):{fmt.RESET}")
    print(hw_table_str)

    print(f"\n{fmt.BOLD}2. СРАВНИТЕЛЬНАЯ ТАБЛИЦА АВТОНОМНОСТИ ПО ПРОТОКОЛАМ И РЕЖИМАМ СНА:{fmt.RESET}")
    print(comp_table_str)

    # --- ПОДГОТОВКА И ВЫВОД БЛОКА 3 (Детальный анализ и визуализация) ---
    focus_label = format_interval_label(focus_interval_sec)
    print(
        f"\n{fmt.BOLD}3. ДЕТАЛЬНЫЙ АНАЛИЗ И ВИЗУАЛИЗАЦИЯ ДЛЯ ЦЕЛЕВОГО ИНТЕРВАЛА: {fmt.CYAN}{focus_label.upper()} ({focus_interval_sec} с){fmt.RESET}"
    )
    print("   Распределение бюджета энергии АКБ между фоновым сном/утечками и активными пробуждениями:")

    max_days_in_focus = max(
        next(ir.autonomy_days for ir in ev.intervals if ir.interval_sec == focus_interval_sec) for ev in evaluations
    )

    focus_headers = [
        "Сценарий (Протокол и Режим сна)",
        "Шкала автономности",
        "Время работы",
        "Средний ток",
        "Доля: Сон+Утечки",
        "Доля: Активность",
    ]
    focus_alignments = ["left", "center", "right", "right", "right", "right"]
    focus_rows: list[list[str]] = []

    for ev in evaluations:
        target_ir = next(ir for ir in ev.intervals if ir.interval_sec == focus_interval_sec)
        bar = fmt.make_bar(target_ir.autonomy_days, max_days_in_focus, width=22)
        avg_ua = target_ir.avg_current_ma * 1000.0
        focus_rows.append(
            [
                f"{fmt.BOLD}{ev.scenario.title}{fmt.RESET}",
                bar,
                fmt.colorize_days(target_ir.autonomy_days, target_ir.autonomy_months),
                f"{avg_ua:6.1f} мкА",
                f"{target_ir.sleep_share_pct:5.1f} %",
                f"{target_ir.active_share_pct:5.1f} %",
            ]
        )

    focus_table_str, _ = fmt.render_table(
        headers=focus_headers,
        rows=focus_rows,
        alignments=focus_alignments,
        group_breaks=group_breaks,
    )
    print(focus_table_str)
    print()


def export_markdown_table(engine: PowerCalculatorEngine, evaluations: list[ScenarioEvaluation]) -> str:
    """
    Формирует чистую Markdown-таблицу для сохранения результатов в файл документации (.md).
    """
    int_headers = [f"Цикл {format_interval_label(sec)}" for sec in engine.intervals_sec]
    lines = [
        "# Расчет автономности ESP32-C6 SuperMini",
        "",
        f"* **Доступная емкость АКБ:** `{engine.battery.effective_capacity_mah:.1f} мАч` "
        f"(номинал `{engine.battery.nominal_capacity_mah:.0f} мАч`, коэфф. `{engine.battery.usable_capacity_ratio:.2f}`)",
        f"* **Фоновый паразитный ток ($I_{{bg}}$):** `{engine.background_leakage_ua:.2f} мкА` "
        f"(делитель: `{engine.board.get_divider_leakage_ua(engine.battery.nominal_voltage_v):.2f} мкА`, "
        f"LDO: `{engine.board.ldo_quiescent_current_ua:.1f} мкА`, "
        f"LTH7R: `{engine.board.charger_leakage_ua:.1f} мкА`, "
        f"саморазряд: `{engine.battery.self_discharge_current_ua:.2f} мкА`)",
        "",
        "| Сценарий и Режим | Реальный ток во сне | Энергия цикла ($Q_{cycle}$) | Безубыточность (vs Light) | "
        + " | ".join(int_headers)
        + " |",
        "| :--- | ---: | ---: | ---: | " + " | ".join(["---:"] * len(int_headers)) + " |",
    ]

    for ev in evaluations:
        be_str = (
            "Базовый (Light)"
            if ev.breakeven_vs_light_sec is None
            else f"> {ev.breakeven_vs_light_sec / 60.0:.1f} мин ({ev.breakeven_vs_light_sec:.0f}с)"
        )
        cols = [f"**{ir.autonomy_days:.1f} дн** ({ir.autonomy_months:.1f} мес)" for ir in ev.intervals]
        lines.append(
            f"| **{ev.scenario.title}**<br>*({ev.scenario.subtitle})* | "
            f"{ev.total_sleep_current_ua:.0f} мкА | "
            f"**{ev.q_cycle_total_mas:.2f} мА·с** | "
            f"{be_str} | " + " | ".join(cols) + " |"
        )

    return "\n".join(lines)
