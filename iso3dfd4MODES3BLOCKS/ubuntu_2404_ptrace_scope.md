# Ubuntu 24.04 и `kernel.yama.ptrace_scope`: параллель с профилированием VTune/APS

## 1. Что такое YAMA и почему это важно

Модуль безопасности **YAMA** (Yet Another Module for Access control) встроен в ядро Linux
и управляет правами на системный вызов `ptrace()`. `ptrace` позволяет одному процессу
проверять и модифицировать память и регистры другого процесса — именно на этом основан
сбор образцов в VTune при **SW sampling**.

Без корректной настройки YAMA VTune не может «прикрепиться» к процессам для сбора
call stack, и профилирование либо молча возвращает пустые данные, либо падает с ошибкой:

```
Failed to start profiling because the scope of ptrace() system call
application is limited. To enable profiling, please set
/proc/sys/kernel/yama/ptrace_scope to 0.
```

Intel прямо указывает это в [официальной документации VTune](https://www.intel.com/content/www/us/en/docs/vtune-profiler/user-guide/2023-0/linux-targets.html):
> *"On Ubuntu systems, VTune Profiler may fail to collect Hotspots and Threading
> analysis data if the scope of the ptrace() system call application is limited."*

---

## 2. Значения `ptrace_scope` и их смысл

| Значение | Название | Поведение | Профилирование |
|---|---|---|---|
| **0** | classic ptrace | Любой процесс может трассировать любой процесс того же пользователя | **Требуется для VTune SW** |
| **1** | restricted (по умолчанию в Ubuntu) | Только родитель может трассировать потомков | VTune SW **не работает** |
| **2** | admin-only | Только root с `CAP_SYS_PTRACE` | VTune SW **не работает** без sudo |
| **3** | no ptrace | Полностью запрещён | VTune SW **не работает** |

[Ubuntu Security Documentation](https://documentation.ubuntu.com/security/security-features/process-memory/)
подтверждает: значение `1` включено по умолчанию начиная с **Ubuntu 10.10** и сохраняется
во всех последующих версиях, включая 24.04.

---

## 3. Ubuntu 24.04 — что изменилось

### 3.1 Значение по умолчанию

Ubuntu 24.04 LTS использует `kernel.yama.ptrace_scope = 1` — **то же значение, что и все
предыдущие версии Ubuntu** (с 10.10). Конфигурационный файл:

```
/etc/sysctl.d/10-ptrace.conf
```

Содержимое (по умолчанию):
```
kernel.yama.ptrace_scope = 1
```

Проверить текущее значение:
```bash
cat /proc/sys/kernel/yama/ptrace_scope
# или
sysctl kernel.yama.ptrace_scope
```

### 3.2 Новые ограничения в Ubuntu 24.04

Ubuntu 24.04 добавила **дополнительные ограничения доступа к `/proc`**, которые
косвенно связаны с `ptrace_scope`:

- Чтение `/proc/<pid>/maps` для процессов, не являющихся потомками, — **запрещено**
  даже для того же пользователя (раньше было разрешено при `ptrace_scope=1`)
- Доступ к `/proc/<pid>/mem`, `/proc/<pid>/status` — также ужесточён
- Инструменты вроде `strace`, `gdb attach`, `reptyr` — требуют `ptrace_scope=0`

[Источник: Ask Ubuntu](https://askubuntu.com/questions/1533077/procfs-access-control-ubuntu-24)

**Влияние на VTune/APS:**

| Инструмент | Режим | ptrace_scope=1 (Ubuntu 24.04) | ptrace_scope=0 |
|---|---|---|---|
| VTune Hotspots | SW sampling | **Не работает** | Работает |
| VTune Hotspots | HW sampling (SEP) | Работает (нужен SEP-драйвер) | Работает |
| VTune MPI | `-trace-mpi` | **Не работает** | Работает |
| APS | `--collection-mode=all/hwc/omp` | Работает (через `perf` API) | Работает |
| `strace` | attach | **Не работает** | Работает |
| `gdb attach` | attach | **Не работает** | Работает |

> **Важно:** APS использует `perf_event_open()` через `perf` API ядра, а не `ptrace`.
> Поэтому APS **не требует** `ptrace_scope=0` в большинстве случаев.

### 3.3 Рекомендации по харденингу Ubuntu 24.04

Ряд руководств по безопасности Ubuntu 24.04 (например,
[Hardening Guide](https://massivegrid.com/blog/ubuntu-vps-security-hardening-guide/))
рекомендуют **усиливать** `ptrace_scope` до `2`:

```bash
# "Усиление ptrace для защиты процессов от несанкционированного трассирования"
kernel.yama.ptrace_scope = 2
```

Это **противоречит** требованиям VTune. На сервере для профилирования:

| Сценарий | Рекомендуемое значение | Причина |
|---|---|---|
| Продакшн-сервер (без профилирования) | 1 или 2 | Безопасность |
| Девелоперская машина / HPC-узел с VTune | **0** | Профилирование |
| Контейнер с VTune (Docker) | 0 + `--cap-add=SYS_PTRACE` | См. ниже |

---

## 4. Решение для Ubuntu 24.04

### 4.1 Временно (до перезагрузки)

```bash
# Проверить текущее значение
cat /proc/sys/kernel/yama/ptrace_scope
# Ожидаемый вывод: 1

# Установить 0 для текущей сессии
sudo sysctl -w kernel.yama.ptrace_scope=0

# Проверить
cat /proc/sys/kernel/yama/ptrace_scope
# Ожидаемый вывод: 0
```

### 4.2 Постоянно (через sysctl)

```bash
# В Ubuntu 24.04 файл уже существует — нужно изменить значение
sudo nano /etc/sysctl.d/10-ptrace.conf
# Изменить: kernel.yama.ptrace_scope = 1
# На:       kernel.yama.ptrace_scope = 0

# Применить без перезагрузки
sudo sysctl --system

# Проверить
sysctl kernel.yama.ptrace_scope
# Ожидаемый вывод: kernel.yama.ptrace_scope = 0
```

> **Альтернатива** (без изменения существующего файла):
> ```bash
> echo "kernel.yama.ptrace_scope = 0" | sudo tee /etc/sysctl.d/99-vtune-ptrace.conf
> sudo sysctl --system
> ```

### 4.3 Скрипт с сохранением/восстановлением (рекомендуется)

```bash
#!/bin/bash
# enable_ptrace_for_vtune.sh — безопасное включение ptrace для VTune

set -e

SCOPE_FILE="/proc/sys/kernel/yama/ptrace_scope"

# Сохраняем текущее значение
SAVED_SCOPE=$(cat "$SCOPE_FILE")
echo "Текущий ptrace_scope: $SAVED_SCOPE"

# Включаем classic ptrace
echo 0 | sudo tee "$SCOPE_FILE" > /dev/null
echo "ptrace_scope установлен в 0 (classic ptrace)"

# Функция восстановления
restore_scope() {
    echo "Восстановление ptrace_scope = $SAVED_SCOPE"
    echo "$SAVED_SCOPE" | sudo tee "$SCOPE_FILE" > /dev/null
}

# Регистрируем функцию восстановления на выход
trap restore_scope EXIT

# === Здесь запуск VTune ===
# Пример:
# vtune -collect hotspots -knob sampling-mode=sw #     -knob enable-stack-collection=true #     --result-dir=./vtune_result #     -- mpirun -n 12 ./build-vtune/iso3dfd 256 256 256 16 8 64 100 hybrid

echo "VTune завершён, ptrace_scope будет восстановлен"
```

Скрипт гарантирует, что `ptrace_scope` вернётся к исходному значению, даже если
VTune завершится с ошибкой или будет прерван (Ctrl+C).

---

## 5. Профилирование в Docker на Ubuntu 24.04

Ubuntu 24.04 часто используется как базовый образ для контейнеров. Для VTune
внутри Docker требуются **дополнительные флаги**:

```bash
docker run --rm \
    --cap-add=SYS_PTRACE \
    --cap-add=SYS_ADMIN \
    --security-opt seccomp=unconfined \
    --pid=host \
    -v /opt/intel:/opt/intel \
    -v $(pwd):/workspace \
    -w /workspace \
    ubuntu:24.04 \
    /bin/bash -c "source /opt/intel/oneapi/setvars.sh && \
        sysctl -w kernel.yama.ptrace_scope=0 && \
        vtune -collect hotspots \
            -knob sampling-mode=sw \
            --result-dir=./vtune_results \
            -- ./build-vtune/iso3dfd 256 256 256 16 8 64 100 hybrid"
```

[Источник: vtune-docker-image](https://github.com/wambitz/vtune-docker-image)

| Флаг | Назначение |
|---|---|
| `--cap-add=SYS_PTRACE` | Разрешает `ptrace` внутри контейнера |
| `--cap-add=SYS_ADMIN` | Разрешает сбор аппаратных метрик |
| `--security-opt seccomp=unconfined` | Отключает seccomp-фильтрацию системных вызовов |
| `--pid=host` | (Опционально) Доступ к PID хоста |

---

## 6. Сравнение: Ubuntu 24.04 vs предыдущие версии

| Параметр | Ubuntu 22.04 | Ubuntu 24.04 | Изменение |
|---|---|---|---|
| `ptrace_scope` по умолчанию | 1 | 1 | Без изменений |
| Файл конфигурации | `/etc/sysctl.d/10-ptrace.conf` | `/etc/sysctl.d/10-ptrace.conf` | Без изменений |
| Доступ к `/proc/<pid>/maps` | Разрешён при scope=1 | **Запрещён** при scope=1 | Ужесточение |
| `strace`, `gdb attach` | Работают при scope=0 | Работают при scope=0 | Без изменений |
| VTune SW sampling | Требует scope=0 | Требует scope=0 | Без изменений |
| APS | Не требует ptrace | Не требует ptrace | Без изменений |
| Docker `--cap-add=SYS_PTRACE` | Рекомендуется | **Обязательно** | Ужесточение |

---

## 7. Чек-лист перед профилированием на Ubuntu 24.04

```bash
# 1. Проверить ptrace_scope
cat /proc/sys/kernel/yama/ptrace_scope
# Если 1 — нужно установить 0

# 2. Установить ptrace_scope=0 (временно)
sudo sysctl -w kernel.yama.ptrace_scope=0

# 3. Проверить версию ядра (для APS — нужен perf_event_paranoid)
cat /proc/sys/kernel/perf_event_paranoid
# Если > 1 — APS может не собрать все метрики
sudo sysctl -w kernel.perf_event_paranoid=1

# 4. Активировать окружение Intel oneAPI
source /opt/intel/oneapi/setvars.sh

# 5. Проверить, что бинарник собран с debug-инфо
readelf -S ./build-vtune/iso3dfd | grep -E '\.debug_line|\.debug_info'
# Должны быть секции .debug_line и .debug_info

# 6. Запустить профилирование
# APS (не требует ptrace_scope=0, но требует perf_event_paranoid <= 1):
aps --collection-mode=all --result-dir=aps_result \
    -- ./build-vtune/iso3dfd 256 256 256 16 8 64 100 hybrid

# VTune SW sampling (требует ptrace_scope=0):
vtune -collect hotspots -knob sampling-mode=sw \
    -knob enable-stack-collection=true \
    --result-dir=vtune_result \
    -- mpirun -n 12 ./build-vtune/iso3dfd 256 256 256 16 8 64 100 hybrid

# 7. Восстановить ptrace_scope (если меняли временно)
sudo sysctl -w kernel.yama.ptrace_scope=1
```

---

## 8. Дополнительный параметр: `perf_event_paranoid`

Помимо `ptrace_scope`, Ubuntu 24.04 (как и все современные ядра) управляет доступом
к `perf_event_open()` через `kernel.perf_event_paranoid`:

| Значение | Доступ | APS |
|---|---|---|
| **-1** | Все события доступны всем | Полная функциональность |
| **0** | Per-process, без kernel-level | Большинство метрик работает |
| **1** | Только per-process, без raw trace | Базовые метрики работают |
| **2** (по умолчанию) | Ограниченный доступ | **APS может не собрать часть метрик** |
| **3** | Только root | APS не работает без sudo |

Проверка и установка:
```bash
cat /proc/sys/kernel/perf_event_paranoid
# Если 2 или выше:
sudo sysctl -w kernel.perf_event_paranoid=1
```

Для APS на Ubuntu 24.04 рекомендуется `perf_event_paranoid = 1` (не требует root,
даёт базовые HW-счётчики) или `0` (полный доступ без root).

---

## 9. Сводная таблица: что нужно для каждого инструмента на Ubuntu 24.04

| Инструмент / Режим | `ptrace_scope` | `perf_event_paranoid` | root / sudo | Доп. требования |
|---|---|---|---|---|
| **VTune HW sampling** | любой | любой | нужен SEP-драйвер | Установка драйвера |
| **VTune SW sampling** | **0** | любой | нет | — |
| **VTune MPI tracing** | **0** | любой | нет | `-trace-mpi` |
| **APS (all/hwc/omp)** | любой | **≤ 1** | нет | — |
| **APS в контейнере** | 0 | ≤ 1 | `--cap-add=SYS_PTRACE` | `seccomp=unconfined` |
| `strace` | **0** | — | нет | — |
| `gdb attach` | **0** | — | нет | — |
