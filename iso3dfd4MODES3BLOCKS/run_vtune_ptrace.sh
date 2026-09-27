#!/bin/bash
# run_vtune_ptrace.sh
# Запуск VTune с авто-включением ptrace_scope=0 и восстановлением
# Использование: sudo ./run_vtune_ptrace.sh <binary> <args...>

set -euo pipefail

# ── Проверка root ──
if [ "$EUID" -ne 0 ]; then
    echo "Ошибка: требуется root. Запустите: sudo $0 $*"
    exit 1
fi

# ── Сохраняем исходное значение ptrace_scope ──
SAVED_SCOPE=$(cat /proc/sys/kernel/yama/ptrace_scope)
echo "Текущий ptrace_scope: $SAVED_SCOPE → устанавливаю 0"

# ── trap: восстановление при любом выходе (нормальном, ошибке, Ctrl+C) ──
restore_ptrace() {
    echo "$SAVED_SCOPE" > /proc/sys/kernel/yama/ptrace_scope
    echo "ptrace_scope восстановлен: $(cat /proc/sys/kernel/yama/ptrace_scope)"
}
trap restore_ptrace EXIT

# ── Устанавливаем 0 ──
echo 0 > /proc/sys/kernel/yama/ptrace_scope

# ── Активируем Intel oneAPI окружение ──
source /opt/intel/oneapi/setvars.sh --force >/dev/null 2>&1 || true

# ── Запускаем VTune ──
RESULT_DIR="vtune_$(date +%Y%m%d_%H%M%S)"
mkdir -p "$RESULT_DIR"

vtune -collect hotspots \
    -knob sampling-mode=sw \
    -knob enable-characterization-insights=false \
    -finalization-mode=deferred \
    -result-dir "$RESULT_DIR" \
    -- "$@"

echo "Результат: $RESULT_DIR"
