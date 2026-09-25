# Руководство по профилированию MPI-приложений с Intel VTune Profiler

## Обзор

Документ описывает процесс профилирования гибридного MPI+OpenMP приложения `iso3dfd` (3D-стенцил, конечные разности) с помощью Intel VTune Profiler в режиме горячих точек (hotspots). Руководство охватывает полный цикл: от запуска сбора профили до анализа результатов.

---

## 1. Требования к среде

| Компонент | Версия / значение |
|---|---|
| ОС | Ubuntu 24.04.5 LTS |
| MPI | Intel MPI (проверяется скриптом автоматически) |
| VTune | Intel VTune Profiler (oneAPI) |
| Права | root (sudo) — требуется для ptrace_scope |
| CPU | 24 логических ядра (в данной конфигурации) |
| MPI-рангов | 12 |
| OpenMP-потоков на ранг | 2 |

### 1.1. Зачем нужен root

VTune в режиме `sampling-mode=sw` (программный семплинг) использует механизм `ptrace` для присоединения к процессам. По умолчанию ядро Linux ограничивает ptrace через параметр `kernel.yama.ptrace_scope`:

| Значение | Поведение |
|---|---|
| 1 (по умолчанию) | Только родитель может трассировать потомка |
| 0 | Любой процесс с правами может трассировать любой другой |

Для VTune с Intel MPI нужно `ptrace_scope=0`, чтобы VTune мог аттачиться к каждому из 12 MPI-рангов, запущенных через `mpirun`.

### 1.2. Почему именно Intel MPI

VTune поддерживает трассировку MPI через флаг `-trace-mpi`, но только для Intel MPI. Скрипт проверяет это автоматически — если `mpirun` не от Intel, выполнение прерывается с ошибкой.

---

## 2. Скрипт запуска профилирования

### 2.1. Исходный код скрипта

**Файл:** `run_vtune_hotspots.sh`

```bash
#!/bin/bash
# run_vtune_hotspots.sh — запуск VTune hotspots с автоматическим управлением ptrace_scope

set -e

# ── Проверка root ──────────────────────────────────────────────
if [ "$EUID" -ne 0 ]; then
    echo "Ошибка: запустите через sudo: sudo ./run_vtune_hotspots.sh"
    exit 1
fi

# ── Подключение окружения Intel oneAPI ──────────────────────────
SETVARS="/opt/intel/oneapi/setvars.sh"
if [ -f "$SETVARS" ]; then
    source "$SETVARS" --force
else
    echo "Ошибка: $SETVARS не найден."
    exit 1
fi

# ── Проверка, что mpirun — Intel MPI ────────────────────────────
MPI_INFO=$(mpirun --version 2>&1 | head -5)
echo ">>> MPI runtime:"
echo "$MPI_INFO"
echo

if ! echo "$MPI_INFO" | grep -qi "intel"; then
    echo "Ошибка: mpirun не от Intel MPI. Проверьте setvars.sh."
    exit 1
fi

# ── Сохранение исходного ptrace_scope ───────────────────────────
ORIGINAL_SCOPE=$(cat /proc/sys/kernel/yama/ptrace_scope)
echo ">>> Исходное значение ptrace_scope: $ORIGINAL_SCOPE"

# ── Функция восстановления ──────────────────────────────────────
restore_ptrace() {
    echo
    echo ">>> Восстановление ptrace_scope=$ORIGINAL_SCOPE ..."
    sysctl -w kernel.yama.ptrace_scope="$ORIGINAL_SCOPE"
    echo "    Проверка: $(cat /proc/sys/kernel/yama/ptrace_scope)"
}
trap restore_ptrace EXIT

# ── Включение ptrace_scope=0 ────────────────────────────────────
echo ">>> Включение ptrace_scope=0 ..."
sysctl -w kernel.yama.ptrace_scope=0
echo "    Проверка: $(cat /proc/sys/kernel/yama/ptrace_scope)"
echo

# ── Запуск VTune ────────────────────────────────────────────────
echo ">>> Запуск VTune hotspots ..."
CMD="mpirun -n 12 -l vtune -quiet -collect hotspots -knob sampling-mode=sw -knob enable-characterization-insights=false -trace-mpi -result-dir vtune_hotspots_mpi -- ./iso3dfd 256 256 256 16 8 64 100"
echo "    Команда: $CMD"
echo

$CMD
EXIT_CODE=$?
echo
echo ">>> VTune завершился с кодом: $EXIT_CODE"
```

### 2.2. Разбор скрипта по этапам

#### Этап 1. Проверка root

```bash
if [ "$EUID" -ne 0 ]; then
    echo "Ошибка: запустите через sudo: sudo ./run_vtune_hotspots.sh"
    exit 1
fi
```

Скрипт проверяет, запущен ли он от root. Без root VTune не сможет изменить `ptrace_scope` через `sysctl` и не получит доступ к ptrace.

#### Этап 2. Подключение окружения oneAPI

```bash
SETVARS="/opt/intel/oneapi/setvars.sh"
source "$SETVARS" --force
```

`setvars.sh` настраивает переменные окружения: `PATH`, `LD_LIBRARY_PATH`, `VTUNE_*`. Без этого команды `vtune` и Intel MPI недоступны.

Флаг `--force` подавляет интерактивный запрос подтверждения, чтобы скрипт не останавливался.

#### Этап 3. Проверка Intel MPI

```bash
MPI_INFO=$(mpirun --version 2>&1 | head -5)
if ! echo "$MPI_INFO" | grep -qi "intel"; then
    echo "Ошибка: mpirun не от Intel MPI."
    exit 1
fi
```

Если в системе установлено несколько MPI-реализаций (например, OpenMPI и Intel MPI), `setvars.sh` должен выставить Intel MPI первой в `PATH`. Скрипт проверяет это, и если `mpirun` не от Intel — прерывает выполнение.

#### Этап 4. Сохранение и восстановление ptrace_scope

```bash
ORIGINAL_SCOPE=$(cat /proc/sys/kernel/yama/ptrace_scope)
```

Скрипт запоминает текущее значение `ptrace_scope` (обычно `1`).

```bash
restore_ptrace() {
    sysctl -w kernel.yama.ptrace_scope="$ORIGINAL_SCOPE"
}
trap restore_ptrace EXIT
```

`trap` гарантирует, что `ptrace_scope` будет восстановлен **в любом случае** — при нормальном завершении, при ошибке, при Ctrl+C. Это важно для безопасности: оставлять `ptrace_scope=0` на сервере небезопасно.

#### Этап 5. Включение ptrace_scope=0

```bash
sysctl -w kernel.yama.ptrace_scope=0
```

Разрешает VTune аттачиться к MPI-процессам.

#### Этап 6. Запуск профилирования

```bash
CMD="mpirun -n 12 -l vtune -quiet -collect hotspots \
    -knob sampling-mode=sw \
    -knob enable-characterization-insights=false \
    -trace-mpi \
    -result-dir vtune_hotspots_mpi \
    -- ./iso3dfd 256 256 256 16 8 64 100"
```

Разбор команды по аргументам:

**mpirun:**

| Флаг | Назначение |
|---|---|
| `-n 12` | 12 MPI-рангов |
| `-l` | Префикс в выводе: `[rank N]` |

**vtune:**

| Флаг | Назначение |
|---|---|
| `-quiet` | Минимальный вывод в консоль |
| `-collect hotspots` | Режим сбора: горячие точки |
| `-knob sampling-mode=sw` | Программный семплинг (на основе ITT/API) |
| `-knob enable-characterization-insights=false` | Отключить дополнительные инсайты (ускоряет сбор) |
| `-trace-mpi` | Трассировка MPI-вызовов (только Intel MPI) |
| `-result-dir vtune_hotspots_mpi` | Куда сохранять результат |

**Приложение:**

| Параметр | Значение | Описание |
|---|---|---|
| `256` | d1 | Размер сетки по оси X |
| `256` | d2 | Размер сетки по оси Y |
| `256` | d3 | Размер сетки по оси Z |
| `16` | n1 | Tile-размер по X |
| `8` | n2 | Tile-размер по Y |
| `64` | n3 | Tile-размер по Z |
| `100` | iterations | Количество итераций |

---

## 3. Запуск

### 3.1. Предварительные шаги

```bash
# Убедитесь, что исполняемый файл собран и находится в текущей директории
ls -la iso3dfd

# Убедитесь, что скрипт исполняемый
chmod +x run_vtune_hotspots.sh

# Убедитесь, что нет старых результатов VTune (или удалите их)
ls -d vtune_hotspots_mpi* 2>/dev/null
```

### 3.2. Запуск скрипта

```bash
sudo ./run_vtune_hotspots.sh
```

### 3.3. Ожидаемый вывод

```
>>> MPI runtime:
Intel(R) MPI Library for Linux* OS, Version 2021.14 Build 20241024

>>> Исходное значение ptrace_scope: 1
>>> Включение ptrace_scope=0 ...
    Проверка: 0

>>> Запуск VTune hotspots ...
    Команда: mpirun -n 12 -l vtune -quiet -collect hotspots ...

[1] iso3dfd
[1] Iteration 0 completed
[1] Iteration 99 completed
...
>>> VTune завершился с кодом: 0

>>> Восстановление ptrace_scope=1 ...
    Проверка: 1
```

### 3.4. Результат

После завершения создаётся директория:

```
vtune_hotspots_mpi.kol-serv/
```

Имя формируется как `<result-dir>.<hostname>` — VTune автоматически добавляет имя хоста.

---

## 4. Анализ результатов

### 4.1. Экспорт в CSV

Результаты экспортируются в три TSV-файла (VTune использует табуляцию как разделитель):

```bash
# Сводка по производительности
vtune -report summary -r vtune_hotspots_mpi.kol-serv -format=csv -report-output vtune_summary.csv

# Распределение по MPI-рангам
vtune -report hotspots -r vtune_hotspots_mpi.kol-serv -group-by process -format=csv -report-output vtune_ranks.csv

# Горячие функции
vtune -report hotspots -r vtune_hotspots_mpi.kol-serv -format=csv -report-output vtune_hotspots.csv
```

### 4.2. Автоматический анализ

Используйте скрипт `analyze_vtune.sh`, который:

1. Создаёт три CSV-файла
2. Парсит сводку (Elapsed Time, CPU Time, Effective/Spin/Overhead Time)
3. Выводит топ горячих функций
4. Строит таблицу по всем MPI-рангам с мин/макс/средним/stddev/коэффициентом вариации
5. Выдаёт оценки балансировки, эффективности, Spin Time, Overhead, Lock Contention

```bash
chmod +x analyze_vtune.sh
./analyze_vtune.sh
```

### 4.3. Ручной анализ через GUI

Для интерактивного анализа откройте результат в VTune GUI:

```bash
vtune-gui vtune_hotspots_mpi.kol-serv
```

---

## 5. Метрики VTune для MPI+OpenMP

### 5.1. Иерархия метрик

```
Elapsed Time          — реальное (wall clock) время выполнения
├─ CPU Time           — суммарное по всем потокам всех рангов
│  ├─ Effective Time  — полезная работа (вычисления)
│  ├─ Spin Time       — ожидание на барьерах/синхронизации
│  │  ├─ Imbalance    — дисбаланс нагрузки между потоками
│  │  ├─ Lock Contention — конкуренция за блокировки
│  │  └─ Other         — прочее ожидание
│  └─ Overhead Time   — накладные расходы OpenMP runtime
│     ├─ Creation     — создание потоков
│     ├─ Scheduling   — планирование
│     ├─ Reduction    — редукции
│     ├─ Atomics      — атомарные операции
│     └─ Other        — прочие накладные расходы
└─ Total Thread Count — количество потоков
```

### 5.2. Ключевые соотношения

| Метрика | Формула | Интерпретация |
|---|---|---|
| Эффективность | `Effective Time / CPU Time` | >95% — отлично, <90% — потери |
| Доля Spin | `Spin Time / CPU Time` | <2% — отлично, >5% — проблема барьеров |
| Доля Overhead | `Overhead Time / CPU Time` | <1% — отлично, >3% — проверьте runtime |
| Утилизация | `CPU Time / (Elapsed Time × Threads)` | >80% — хорошая утилизация |
| Разброс рангов | `(max CPU - min CPU) / avg CPU` | <5% — отличная балансировка |

### 5.3. Показатели для iso3dfd (пример)

| Метрика | Значение | Оценка |
|---|---|---|
| Elapsed Time | 28.63 с | — |
| CPU Time | 597.58 с | — |
| Effective Time | 592.08 с (99.1%) | Отлично |
| Spin Time | 4.12 с (0.7%) | Барьеры не проблема |
| Overhead Time | 1.38 с (0.2%) | Минимальный |
| Утилизация | 597.58 / (28.63 × 24) = 87% | Хорошая |
| Top hotspot | Iso3dfdIteration — 98.3% | Вычислительное ядро доминирует |
| Разброс рангов | ~2.2% | Отличная балансировка |
| Lock Contention | 0.0 | Блокировок нет |

---

## 6. Возможные проблемы и решения

### 6.1. Ошибка: `vtune не найден в PATH`

**Причина:** `setvars.sh` не подключён или установлен не в `/opt/intel/oneapi/`.

**Решение:** Найдите `setvars.sh`:

```bash
find /opt/intel -name 'setvars.sh' 2>/dev/null
```

И укажите путь в скрипте:

```bash
SETVARS="/your/path/setvars.sh"
```

### 6.2. Ошибка: `mpirun не от Intel MPI`

**Причина:** В `PATH` другой MPI (OpenMPI, MPICH) стоит раньше Intel MPI.

**Решение:** Перезагрузите `setvars.sh` или укажите полный путь:

```bash
source /opt/intel/oneapi/setvars.sh --force
which mpirun
# должно быть: /opt/intel/oneapi/mpi/latest/bin/mpirun
```

### 6.3. Ошибка: `Permission denied` при доступе к результату

**Причина:** Скрипт запускался от root, результат принадлежит root.

**Решение:**

```bash
sudo chown -R $USER:$USER vtune_hotspots_mpi.kol-serv
```

### 6.4. Ошибка: `ptrace: Operation not permitted`

**Причина:** `ptrace_scope` не был сброшен в 0.

**Решение:** Проверьте и сбросьте вручную:

```bash
cat /proc/sys/kernel/yama/ptrace_scope
sudo sysctl -w kernel.yama.ptrace_scope=0
```

### 6.5. Результат содержит данные только для одного ранга

**Причина:** Запуск без флага `-trace-mpi` или без `ptrace_scope=0`.

**Решение:** Убедитесь, что в команде есть `-trace-mpi`, и что `ptrace_scope=0` применён до запуска `mpirun`.

### 6.6. CPU определяется как `Unknown`

**Причина:** VTune не распознаёт старые процессоры (Westmere X5675).

**Решение:** Это не влияет на корректность сбора — VTune работает, но не показывает микроархитектурные метрики (L2/L3 cache, TLB). Для системного анализа используйте `sampling-mode=hw` (требует root и поддержки CPU).

---

## 7. Параметры бенчмарка iso3dfd

Аргументы командной строки:

```
./iso3dfd <d1> <d2> <d3> <n1> <n2> <n3> <num_iterations>
```

| Параметр | Назначение | Значение по умолчанию в скрипте |
|---|---|---|
| d1, d2, d3 | Размер 3D сетки | 256 × 256 × 256 |
| n1, n2, n3 | Tile-размеры (блоки для кэша) | 16 × 8 × 64 |
| num_iterations | Количество итераций | 100 |

**Размер сетки** определяет вычислительную нагрузку: 256³ = 16.7 млн узлов.

**Tile-размеры** влияют на использование кэша: 16 × 8 × 64 × 4 байта (float) = 32 КБ на tile — соответствует L1/L2 кэшу.

**Количество итераций** определяет длительность профилирования: 100 итераций достаточно для накопления статистики, но не слишком долго для семплинга.

---

## 8. Краткая шпаргалка

```bash
# 1. Запуск профилирования (от root)
sudo ./run_vtune_hotspots.sh

# 2. Исправить права на результат (если нужно)
sudo chown -R $USER:$USER vtune_hotspots_mpi.kol-serv

# 3. Автоматический анализ
./analyze_vtune.sh

# 4. Ручной экспорт CSV
vtune -report summary -r vtune_hotspots_mpi.kol-serv -format=csv -report-output summary.csv
vtune -report hotspots -r vtune_hotspots_mpi.kol-serv -group-by process -format=csv -report-output ranks.csv

# 5. Открыть в GUI
vtune-gui vtune_hotspots_mpi.kol-serv

# 6. Удалить старый результат
rm -rf vtune_hotspots_mpi.kol-serv
```
