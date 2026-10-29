"""
Пакет инженерного калькулятора энергоэффективности и автономности ESP32-C6 SuperMini.
"""

from .engine import PowerCalculatorEngine
from .models import (
    BatteryConfig,
    BoardParasiticConfig,
    IntervalResult,
    MeasurementTaskConfig,
    ScenarioConfig,
    ScenarioEvaluation,
    SleepModeProfile,
)
from .presets import get_default_scenarios, get_default_sleep_profiles

__all__ = [
    "BatteryConfig",
    "BoardParasiticConfig",
    "IntervalResult",
    "MeasurementTaskConfig",
    "PowerCalculatorEngine",
    "ScenarioConfig",
    "ScenarioEvaluation",
    "SleepModeProfile",
    "get_default_scenarios",
    "get_default_sleep_profiles",
]
