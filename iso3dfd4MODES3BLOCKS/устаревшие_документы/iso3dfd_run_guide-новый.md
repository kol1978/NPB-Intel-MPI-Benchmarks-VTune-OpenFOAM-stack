# Инструкция по запуску iso3dfd — 4 режима × 3 блока

## Содержание

1. [Быстрый старт](#быстрый-старт)
2. [Четыре режима выполнения](#четыре-режима-выполнения)
3. [Параметры командной строки](#параметры-командной-строки)
4. [Декомпозиция и ограничения](#декомпозиция-и-ограничения)
5. [Cache-blocking: расчёт для Xeon X5675](#cache-blocking-расчёт-для-xeon-x5675)
6. [NUMA-оптимизация](#numa-оптимизация)
7. [Управление потоками OpenMP](#управление-потоками-openmp)
8. [Сборка для профилирования (VTune/APS)](#сборка-для-профилирования-vtuneaps)
9. [Профилирование: APS и VTune](#профилирование-aps-и-vtune)
10. [Сценарии использования](#сценарии-использования)
11. [Результаты экспериментов](#результаты-экспериментов)
12. [Устранение проблем](#устранение-проблем)

---

## Быстрый старт

### Требования

| Компонент | Версия | Путь |
|---|---|---|
| Компилятор | Intel LLVM (icpx) 2026.1.1 | `/opt/intel/oneapi/compiler/2026.1/bin` |
| MPI | Intel MPI 2021.18 | `/opt/intel/oneapi/mpi/2021.18` |
| OpenMP | Intel OpenMP 5.1 (`-fiopenmp`) | в составе icpx |
| VTune | 2025.x (опционально) | `/opt/intel/oneapi/vtune/latest` |
| GPU | не требуется | CPU-only |

### Сборка

```bash
# 1. Активация oneAPI
source /opt/intel/oneapi/setvars.sh

# 2. Конфигурация и сборка (Release)
rm -rf build
cmake -S . -B build -DVERIFY_RESULTS=0
cmake --build build -j$(nproc)
```

Бинарник: `build/iso3dfd`

### Запуск (4 режима)

```bash
# Sequential — 1 процесс, 1 поток
./build/iso3dfd 256 256 256 16 8 64 100 sequential

# Pure MPI — 12 процессов, 1 поток каждый
mpirun -n 12 ./build/iso3dfd 256 256 480 16 8 40 100 pure_mpi

# Hybrid — 2 процесса × 6 потоков OpenMP
OMP_NUM_THREADS=6 mpirun -n 2 ./build/iso3dfd 256 256 256 16 8 128 100 hybrid

# Pure OpenMP — 1 процесс, 12 потоков
OMP_NUM_THREADS=12 ./build/iso3dfd 256 256 256 16 8 64 100 pure_omp
```

---

## Четыре режима выполнения

Программа принимает 8-й аргумент командной строки — имя режима. Если аргумент
отсутствует, по умолчанию используется `sequential`.

### 1. Sequential

| Параметр | Значение |
|---|---|
| Процессов MPI | 1 |
| Потоков OpenMP | 1 |
| Декомпозиция | нет |
| Halo exchange | нет |
| Вычисление | `compute_serial` |

```bash
./build/iso3dfd 256 256 256 16 8 64 100 sequential
```

Применение: базовый замер, проверка корректности, reference для сравнения.

### 2. Pure MPI

| Параметр | Значение |
|---|---|
| Процессов MPI | N (задаётся через `mpirun -n N`) |
| Потоков OpenMP | 1 на ранг |
| Декомпозиция | по оси Z: n3 / N слоёв на ранг |
| Halo exchange | `MPI_Sendrecv`, 8 слоёв в каждую сторону |
| Вычисление | `compute_serial` в каждом ранге |

```bash
mpirun -n 12 ./build/iso3dfd 256 256 480 16 8 40 100 pure_mpi
```

**Ограничение:** n3 должно делиться на N без остатка.

Применение: проверка MPI-декомпозиции и halo-обмена, масштабирование по рангам.

### 3. Hybrid (MPI + OpenMP)

| Параметр | Значение |
|---|---|
| Процессов MPI | M (задаётся через `mpirun -n M`) |
| Потоков OpenMP | T (задаётся через `OMP_NUM_THREADS=T`) |
| Декомпозиция | по оси Z: n3 / M слоёв на ранг |
| Halo exchange | `MPI_Sendrecv`, 8 слоёв в каждую сторону |
| Вычисление | `compute_omp` — OpenMP `parallel for` внутри ранга |

```bash
OMP_NUM_THREADS=6 mpirun -n 2 ./build/iso3dfd 256 256 256 16 8 128 100 hybrid
```

**Ограничение:** n3 должно делиться на M без остатка.

Применение: **целевой режим для 2-сокетного NUMA-сервера** — каждый ранг
привязывается к своему NUMA-узлу, потоки OpenMP заполняют ядра сокета.

### 4. Pure OpenMP

| Параметр | Значение |
|---|---|
| Процессов MPI | 1 |
| Потоков OpenMP | N (задаётся через `OMP_NUM_THREADS=N`) |
| Декомпозиция | нет |
| Halo exchange | нет |
| Вычисление | `compute_omp` — OpenMP `parallel for` |

```bash
OMP_NUM_THREADS=12 ./build/iso3dfd 256 256 256 16 8 64 100 pure_omp
```

Применение: проверка масштабирования OpenMP без MPI-накладных расходов,
сравнение с hybrid для оценки стоимости MPI-обмена.

---

## Параметры командной строки

```
./iso3dfd n1 n2 n3 b1 b2 b3 niter [mode]
```

| Позиция | Имя | По умолчанию | Описание |
|---|---|---|---|
| 1 | `n1` | 256 | Размер сетки по X |
| 2 | `n2` | 256 | Размер сетки по Y |
| 3 | `n3` | 256 | Размер сетки по Z (должна делиться на число рангов) |
| 4 | `b1` | 16 | Размер блока по X (тайл для L1/L2) |
| 5 | `b2` | 8 | Размер блока по Y (тайл для L2) |
| 6 | `b3` | 64 | Размер блока по Z (тайл для L3) |
| 7 | `niter` | 100 | Число шагов по времени |
| 8 | `mode` | sequential | Режим: `sequential` / `pure_mpi` / `hybrid` / `pure_omp` |

### Влияние параметров на производительность

| Параметр | Эффект |
|---|---|
| n3 → кратно N | Обязательно для pure_mpi и hybrid; иначе ошибка |
| b1: 16 → 32 | Больше векторизации, но выше давление на L1 |
| b2: 8 → 16 | Больше переиспользования L2, но больше блоков OMP |
| b3: 32 → 64 → 128 | Ключевой параметр L3; 64 — оптимальный для 6 потоков/сокет |
| niter: 100 → 1000 | Время × 10, throughput стабилизируется |

---

## Декомпозиция и ограничения

### Правило делимости

Для `pure_mpi` и `hybrid` ось Z (n3) делится поровну между рангами:

```
n3_local = n3 / nprocs
```

Если n3 не делится на nprocs — программа выдаёт ошибку:
```
Error: n3 (256) must be divisible by nprocs (12)
```

### Таблица подбора n3

| nprocs | n3 | n3/ранг | interior (−16 halo) | Доля полезного |
|---|---|---|---|---|
| 12 | 240 | 20 | 4 | 20% — слишком мало |
| 12 | 480 | 40 | 24 | 60% — приемлемо |
| 12 | 960 | 80 | 64 | 80% — хорошо |
| 2 | 256 | 128 | 112 | 88% — отлично |
| 3 | 252 | 84 | 68 | 81% — хорошо |

> **Правило:** на каждый ранг должно приходиться минимум 4 × kHalfLength = 32
> слоя по Z, чтобы halo (16 слоёв) занимал не больше 50%.

### Halo-обмен

Стенсиль 16-го порядка требует 8 слоёв halo с каждой стороны (16 всего).
Обмен выполняется через `MPI_Sendrecv` между соседними рангами по оси Z.

Размер halo-буфера на один ранг: 2 × n1 × n2 × 8 × 4 байт.
Для 256×256: 2 × 256 × 256 × 8 × 4 = 4 МБ за итерацию (2 МБ в каждую сторону).

---

## Cache-blocking: расчёт для Xeon X5675

### Параметры железа

| Параметр | Значение |
|---|---|
| L1 Data | 32 КБ на ядро |
| L2 | 256 КБ на ядро (unified) |
| L3 | 12 МБ на сокет (shared, 6 ядер) |
| Cache line | 64 байт |
| Память | DDR3-1333 × 6 каналов ~ 64 ГБ/с пик |

### Параметры стенсиля

| Параметр | Значение |
|---|---|
| Порядок | 16-й (kHalfLength = 8) |
| Точек в стенсиле | 49 (1 + 3 × 2 × 8) |
| FLOPs на точку | 97 (48 multiply-add + 1) |
| sizeof(float) | 4 байта |
| Halo (обе стороны) | 16 слоёв |

### Массивы в рабочем наборе

| Массив | Роль | Доступ |
|---|---|---|
| `prev` | Входное поле (текущий шаг) | read |
| `next` | Выходное поле (следующий шаг) | write |
| `vel` | Поле скоростей (постоянное) | read |

**Всего: 3 массива** (prev + next + vel).

### Расчёт footprint блока (с учётом halo)

```
input(halo) = (b1+16) × (b2+16) × (b3+16) × 4   ← prev с halo
output      = b1 × b2 × b3 × 4                   ← next
vel         = b1 × b2 × b3 × 4                   ← vel
total       = input(halo) + output + vel
```

### L1 (32 КБ) — не ограничивает

При b1=16: 16 × 4 × 3 = 192 байт = 0.6% от L1.
Реальный лимит b1 — качество векторизации (SSE4.2, 4 float на регистр).
Рекомендация: **b1 = 16** (кратно 4).

### L2 (256 КБ) — не ограничивает

При b1=16, b2=8: 16 × 8 × 4 × 3 = 1536 байт = 0.6% от L2.
Рекомендация: **b2 = 8**.

### L3 (12 МБ) — ключевой расчёт

При 6 потоках на сокет: L3/поток = 12 МБ / 6 = **2 МБ**.

| b3 | total/поток | × 6 потоков | Сумма | % от L3 (2 МБ) | Влезает? |
|---|---|---|---|---|---|
| 32 | 176 КБ | 6 × 176 | 1056 КБ | 51% | ✓ |
| **64** | **304 КБ** | **6 × 304** | **1824 КБ** | **87%** | **✓ оптимально** |
| 128 | 560 КБ | 6 × 560 | 3360 КБ | 160% | ✗ переполнение |
| 256 | 1072 КБ | 6 × 1072 | 6432 КБ | 307% | ✗ катастрофа |

> **Вывод:** для hybrid (2×6) оптимальный b3 = 64 (87% L3).
> b3 = 128 переполняет L3 на 60% — данные вытесняются в DRAM.

### Неверный vs верный расчёт

| Параметр | Неверный | Верный |
|---|---|---|
| L3 | 12 МБ (весь сокет) | 2 МБ (на поток при 6) |
| Массивов | 2 (prev + next) | 3 (prev + next + vel) |
| b3 рекомендация | 256 | 64 |
| Результат | переполнение L3 в 3 раза | 87% L3 — оптимально |

---

## NUMA-оптимизация

### Архитектура X5675

| Параметр | Значение |
|---|---|
| Сокетов | 2 |
| Ядер на сокет | 6 |
| NUMA-узлов | 2 (по одному на сокет) |
| L3 на сокет | 12 МБ (shared) |
| Память на узел | локальная DDR3 |

### Рекомендуемая конфигурация: hybrid 2×6

Каждый MPI-ранг закрепляется на своём NUMA-узле, 6 потоков OpenMP
заполняют ядра сокета. Память аллоцируется локально.

```bash
# Оптимальный запуск для 2-сокетного сервера
OMP_NUM_THREADS=6 mpirun -n 2 ./build/iso3dfd 256 256 256 16 8 64 100 hybrid
```

### Привязка через numactl

```bash
# Ранг 0 — NUMA-узел 0
OMP_NUM_THREADS=6 numactl --cpunodebind=0 --membind=0 \
    mpirun -n 1 ./build/iso3dfd 256 256 256 16 8 64 100 hybrid &

# Ранг 1 — NUMA-узел 1
OMP_NUM_THREADS=6 numactl --cpunodebind=1 --membind=1 \
    mpirun -n 1 ./build/iso3dfd 256 256 256 16 8 64 100 hybrid &
wait
```

### Intel MPI pinning

```bash
# Привязка рангов к сокетам
I_MPI_PIN_DOMAIN=[0-5,6-11] OMP_NUM_THREADS=6 \
    mpirun -n 2 ./build/iso3dfd 256 256 256 16 8 64 100 hybrid

# Альтернатива — автоматический пиннинг
I_MPI_PIN=on I_MPI_PIN_DOMAIN=omp OMP_NUM_THREADS=6 \
    mpirun -n 2 ./build/iso3dfd 256 256 256 16 8 64 100 hybrid
```

---

## Управление потоками OpenMP

### Базовые варианты

```bash
# Все ядра (12 физических)
OMP_NUM_THREADS=12 ./build/iso3dfd 256 256 256 16 8 64 100 pure_omp

# Один NUMA-узел (6 ядер)
OMP_NUM_THREADS=6 ./build/iso3dfd 256 256 256 16 8 64 100 pure_omp

# Hybrid: 2 ранга × 6 потоков
OMP_NUM_THREADS=6 mpirun -n 2 ./build/iso3dfd 256 256 256 16 8 64 100 hybrid
```

### Schedule (распределение итераций)

```bash
# Static — по умолчанию, лучший для равномерной нагрузки
OMP_SCHEDULE=static OMP_NUM_THREADS=6 \
    mpirun -n 2 ./build/iso3dfd 256 256 256 16 8 64 100 hybrid

# Dynamic — для неравномерной нагрузки
OMP_SCHEDULE=dynamic,4 OMP_NUM_THREADS=6 \
    mpirun -n 2 ./build/iso3dfd 256 256 256 16 8 64 100 hybrid
```

### numactl + OpenMP

```bash
# Привязка к одному NUMA-узлу
OMP_NUM_THREADS=6 numactl --membind=0 --cpunodebind=0 \
    ./build/iso3dfd 256 256 256 16 8 64 100 pure_omp

# Interleave по двум узлам (для pure_omp на 12 потоках)
OMP_NUM_THREADS=12 numactl --interleave=0,1 \
    ./build/iso3dfd 256 256 256 16 8 64 100 pure_omp
```

---

## Сборка для профилирования (VTune/APS)

### Зачем отдельная сборка

Для source-level анализа в VTune нужны отладочные символы (debug info).
Release-сборка с `-O3` не сохраняет отладочную информацию, поэтому VTune
не сможет привязать метрики к строкам исходного кода.

**Решение:** отдельная директория сборки `build-vtune`, которая не затирает
Release-сборку в `build`.

### Флаги компиляции для профилирования

Intel рекомендует для профилировочных сборок использовать комбинацию
`-gline-tables-only` и `-fdebug-info-for-profiling` с сохранением
оптимизации `-O2` (не `-O0`):

| Флаг | Назначение | Источник |
|---|---|---|
| `-gline-tables-only` | Только таблицы строк отладки — даёт backtrace с inlining-инфо, без переменных/типов. Меньший бинарник, чем при полном `-g` | Intel VTune User Guide 2025.4 |
| `-fdebug-info-for-profiling` | Дополнительная отладочная информация для более точной корреляции профиля с исходным кодом | Intel VTune System Requirements 2025.1 |
| `-O2` | Оптимизация сохраняется — VTune корректно мапит метрики на оптимизированный код | Intel Community, VTune docs |

> **Важно:** полный Debug (`-O0 -g`) не рекомендуется для профилирования —
> без оптимизации код работает иначе, чем в Release, и профиль будет
> нерепрезентативен. Intel рекомендует `-O2` с минималистичными debug-флагами.

### Команда сборки

```bash
# 1. Отдельная директория (не затирает Release в build/)
cmake -S . -B build-vtune \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DVERIFY_RESULTS=1 \
    -DCMAKE_CXX_FLAGS_RELWITHDEBINFO="-O2 -gline-tables-only -fdebug-info-for-profiling -fiopenmp"

# 2. Сборка
cmake --build build-vtune -j$(nproc)
```

> `RelWithDebInfo` — стандартный CMake-тип, который включает оптимизацию
> и debug-информацию одновременно. Мы переопределяем его флаги под
> рекомендации Intel.

Бинарник: `build-vtune/iso3dfd`

### Проверка отладочной информации

```bash
# Проверить, что debug-информация присутствует
readelf -S build-vtune/iso3dfd | grep debug

# Должны быть секции:
# .debug_line  (от -gline-tables-only)
# .debug_info  (от -fdebug-info-for-profiling)
```

### Сравнение сборок

| Сборка | Директория | Тип | Флаги | Назначение |
|---|---|---|---|---|
| Release | `build/` | Release | `-O3 -fiopenmp -fno-alias` | Замеры производительности |
| Profiling | `build-vtune/` | RelWithDebInfo | `-O2 -gline-tables-only -fdebug-info-for-profiling -fiopenmp` | VTune, APS |
| Debug | `build-debug/` | Debug | `-O0 -g -fiopenmp` | Отладка в gdb (не для профилирования) |

### Полный цикл: сборка → запуск → отчёт

```bash
# ── 1. Активация окружения ──
source /opt/intel/oneapi/setvars.sh --force

# ── 2. Сборка для профилирования ──
cmake -S . -B build-vtune \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DVERIFY_RESULTS=1 \
    -DCMAKE_CXX_FLAGS_RELWITHDEBINFO="-O2 -gline-tables-only -fdebug-info-for-profiling -fiopenmp"
cmake --build build-vtune -j$(nproc)

# ── 3. APS — быстрый снимок ──
aps --collection-mode=all --result-dir=aps_result \
    -- ./build-vtune/iso3dfd 256 256 256 16 8 64 100 hybrid
aps --report aps_result

# ── 4. VTune hotspots — детальный анализ ──
vtune -collect hotspots -knob sampling-mode=hw \
    -result-dir vtune_hotspots \
    -finalization-mode=deferred \
    -- ./build-vtune/iso3dfd 256 256 256 16 8 64 100 pure_omp

# Финализация и отчёт
vtune -finalize vtune_hotspots
vtune -report hotspots -r vtune_hotspots -limit 10
```

---

## Профилирование: APS и VTune

### APS — Application Performance Snapshot

APS — лёгкий профилировщик из состава VTune. Работает через `perf` API
без SEP-драйвера. Overhead: 1–3% (all), <1% (hwc/omp).

**Документация:** Intel VTune Profiler User Guide 2025.4, раздел APS.

#### Режимы сбора

| Режим | Что собирает | Overhead |
|---|---|---|
| `all` | HW-счётчики + OpenMP + MPI | 1–3% |
| `hwc` | Только HW: CPI, Memory, NUMA, Vectorization | <1% |
| `omp` | Только OpenMP: Imbalance, Serial Time | <1% |
| `mpi` | Только MPI: время в вызовах, дисбаланс рангов | <1% |

#### Команды

```bash
# Для single-process (sequential, pure_omp)
aps --collection-mode=all --result-dir=aps_result \
    -- ./build-vtune/iso3dfd 256 256 256 16 8 64 100 pure_omp

# Для MPI (pure_mpi, hybrid) — aps оборачивает mpirun
aps --collection-mode=all --result-dir=aps_result_hybrid \
    -- mpirun -n 2 ./build-vtune/iso3dfd 256 256 256 16 8 64 100 hybrid

# Альтернатива через переменную окружения (Intel MPI)
export APS_ENABLE=1
mpirun -n 2 ./build-vtune/iso3dfd 256 256 256 16 8 64 100 hybrid
# APS автоматически создаст директорию aps_result_<дата>_<время>/
```

#### Генерация отчёта

```bash
# Консольный отчёт
aps --report aps_result

# HTML-отчёт (открывается в браузере)
aps-report -g aps_result
# Firefox: firefox aps_result_<postfix>.html
```

#### Ключевые метрики APS

| Метрика | Норма для HPC | Плохо | Действие |
|---|---|---|---|
| **CPI** (Cycles Per Instruction) | 0.5–1.0 | > 2.0 | Проверить Memory Stalls |
| **Memory Stalls** | < 20% | > 50% | Memory-bound — уменьшить b3 |
| **L2 Stalls** | < 10% | > 20% | Рабочий набор не влезает в L1 |
| **L3 Stalls** | < 15% | > 30% | Рабочий набор не влезает в L2 |
| **DRAM Stalls** | < 5% | > 15% | Данные уходят в DRAM — уменьшить b3 |
| **NUMA ratio** | < 5% | > 15% | Использовать `numactl --membind` |
| **Vectorization** | > 80% | < 50% | Пересобрать с `-O3 -xSSE4.2` |
| **OpenMP Imbalance** | < 5% | > 15% | Изменить `OMP_SCHEDULE` или b3 |
| **Serial Time** | < 5% | > 15% | Вынести инициализацию из замера |
| **SP GFLOPS** | — | — | Сравнить с метрикой iso3dfd |

> **Примечание:** для Westmere (X5675) векторизация — SSE4.2 (128-bit).
> AVX2/AVX-512 не поддерживаются. `Vectorization: 128-bit = 100%` — норма.

### VTune — детальный анализ горячих точек

**Документация:** Intel VTune Profiler User Guide 2025.4, раздел
"hotspots Command Line Analysis".

#### Режимы сэмплирования

| Режим | Кnob | Требует root | Overhead | Когда использовать |
|---|---|---|---|---|
| User-Mode Sampling (SW) | `sampling-mode=sw` | нет | ~2× (тяжёлый) | Длинные запуски (> 5 сек) |
| Hardware Event-Based (HW) | `sampling-mode=hw` | да (SEP-драйвер) | ~5% (лёгкий) | Короткие запуски, точный профиль |

> SW sampling использует `SIGPROF` (интервал 10 мс) — для 4-секундного
> запуска даёт ~400 образцов. HW sampling точнее, но требует
> установки SEP-драйвера (нужны root-права).

#### Дополнительные knobs (из Intel VTune User Guide 2025.4)

| Knob | Описание |
|---|---|
| `enable-stack-collection` | Собирать call stacks для каждой горячей точки |
| `sampling-interval` | Интервал сэмплирования (для HW, в мс) |
| `enable-characterization-insights` | Включить дополнительные инсайты микроархитектуры |

#### Команды VTune

```bash
# HW hotspots (нужен SEP-драйвер, root)
sudo vtune -collect hotspots \
    -knob sampling-mode=hw \
    -knob enable-stack-collection=true \
    -result-dir vtune_hotspots \
    -finalization-mode=deferred \
    -- ./build-vtune/iso3dfd 256 256 256 16 8 64 100 pure_omp

# SW hotspots (без root, но ~2× overhead)
vtune -collect hotspots \
    -knob sampling-mode=sw \
    -result-dir vtune_hotspots_sw \
    -finalization-mode=deferred \
    -- ./build-vtune/iso3dfd 256 256 256 16 8 64 100 pure_omp

# Финализация и отчёт
vtune -finalize vtune_hotspots
vtune -report hotspots -r vtune_hotspots -limit 10
vtune -report summary -r vtune_hotspots
```

#### VTune для MPI

```bash
# Трассировка MPI-рангов
sudo vtune -collect hotspots \
    -knob sampling-mode=hw \
    -trace-mpi \
    -result-dir vtune_mpi \
    -finalization-mode=deferred \
    -- mpirun -n 2 ./build-vtune/iso3dfd 256 256 256 16 8 64 100 hybrid

vtune -finalize vtune_mpi
vtune -report hotspots -r vtune_mpi -limit 10
```

> **Важно:** для VTune с MPI нужен `ptrace_scope = 0`:
> ```bash
> echo 0 | sudo tee /proc/sys/kernel/yama/ptrace_scope
> ```
> После завершения — восстановить:
> ```bash
> echo 1 | sudo tee /proc/sys/kernel/yama/ptrace_scope
> ```

#### Что искать в отчёте VTune

| Метрика | Что означает | Норма для iso3dfd |
|---|---|---|
| **Top-10 функций** | Какие функции занимают больше всего CPU time | `compute_omp` или `compute_serial` ~90% |
| **CPU Time vs Elapsed × NP** | Утилизация ядер | CPU Time ≈ Elapsed × NP — хорошая |
| **Wait Time** | Время простоя (синхронизация, I/O) | < 10% — хорошо |
| **Spin Time** | Время в барьерах OpenMP | < 5% — хорошо |

---

## Сценарии использования

### Сценарий 1: Baseline-замер

```bash
./build/iso3dfd 256 256 256 16 8 64 100 sequential
./build/iso3dfd 256 256 256 16 8 64 100 pure_omp
OMP_NUM_THREADS=6 mpirun -n 2 ./build/iso3dfd 256 256 256 16 8 64 100 hybrid
```

### Сценарий 2: Поиск оптимального размера блока

```bash
# b3 = 32 (L3 заполнен на 51%)
OMP_NUM_THREADS=6 mpirun -n 2 ./build/iso3dfd 256 256 256 16 8 32 100 hybrid

# b3 = 64 (L3 заполнен на 87%) — ожидается лучший результат
OMP_NUM_THREADS=6 mpirun -n 2 ./build/iso3dfd 256 256 256 16 8 64 100 hybrid

# b3 = 128 (L3 переполнен на 160%) — ожидается деградация
OMP_NUM_THREADS=6 mpirun -n 2 ./build/iso3dfd 256 256 256 16 8 128 100 hybrid
```

### Сценарий 3: Проверка NUMA-локальности

```bash
# Без numactl
aps --collection-mode=all --result-dir=aps_numa_default \
    -- mpirun -n 2 ./build-vtune/iso3dfd 256 256 256 16 8 64 100 hybrid
aps --report aps_numa_default

# С numactl (ручная привязка)
OMP_NUM_THREADS=6 numactl --cpunodebind=0 --membind=0 \
    mpirun -n 1 ./build-vtune/iso3dfd 256 256 256 16 8 64 100 hybrid &
OMP_NUM_THREADS=6 numactl --cpunodebind=1 --membind=1 \
    mpirun -n 1 ./build-vtune/iso3dfd 256 256 256 16 8 64 100 hybrid &
wait
```

Сравнить `NUMA ratio` между запусками. Если без numactl > 15%, а с numactl < 5% —
память мигрировала между сокетами.

### Сценарий 4: Scaling-тест (по числу потоков)

```bash
for t in 1 2 4 6 8 12; do
    echo "=== OMP_NUM_THREADS=$t ==="
    OMP_NUM_THREADS=$t ./build/iso3dfd 256 256 256 16 8 64 100 pure_omp
done
```

### Сценарий 5: Полный цикл профилирования

```bash
# 1. Сборка для профилирования
source /opt/intel/oneapi/setvars.sh --force
cmake -S . -B build-vtune \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DVERIFY_RESULTS=1 \
    -DCMAKE_CXX_FLAGS_RELWITHDEBINFO="-O2 -gline-tables-only -fdebug-info-for-profiling -fiopenmp"
cmake --build build-vtune -j$(nproc)

# 2. APS — быстрый снимок
aps --collection-mode=all --result-dir=aps_result \
    -- mpirun -n 2 ./build-vtune/iso3dfd 256 256 256 16 8 64 100 hybrid
aps --report aps_result

# 3. VTune — детальный анализ (если APS показал проблему)
echo 0 | sudo tee /proc/sys/kernel/yama/ptrace_scope
sudo vtune -collect hotspots -knob sampling-mode=hw \
    -trace-mpi \
    -result-dir vtune_hybrid \
    -finalization-mode=deferred \
    -- mpirun -n 2 ./build-vtune/iso3dfd 256 256 256 16 8 64 100 hybrid
vtune -finalize vtune_hybrid
vtune -report hotspots -r vtune_hybrid -limit 10
echo 1 | sudo tee /proc/sys/kernel/yama/ptrace_scope
```

### Сценарий 6: Сравнение hybrid vs pure_omp

```bash
# Hybrid 2×6
OMP_NUM_THREADS=6 mpirun -n 2 ./build/iso3dfd 256 256 256 16 8 64 100 hybrid

# Pure OMP 12
OMP_NUM_THREADS=12 ./build/iso3dfd 256 256 256 16 8 64 100 pure_omp

# Pure OMP 6 (один сокет)
OMP_NUM_THREADS=6 ./build/iso3dfd 256 256 256 16 8 64 100 pure_omp
```

### Сценарий 7: Диагностика низкой производительности

**Шаг 1 — APS all:**
```bash
aps --collection-mode=all --result-dir=aps_diag \
    -- mpirun -n 2 ./build-vtune/iso3dfd 256 256 256 16 8 64 100 hybrid
aps --report aps_diag
```

Анализ:
- CPI > 1.5 → проблема в инструкциях
- Memory Stalls > 40% → memory-bound, уменьшить b3
- NUMA ratio > 15% → использовать `numactl`
- Vectorization < 50% → пересобрать с `-O3 -xSSE4.2`
- OpenMP Imbalance > 15% → изменить `OMP_SCHEDULE` или b3

**Шаг 2 — VTune hotspots (если APS показал проблему):**
```bash
vtune -report hotspots -r vtune_diag -limit 10
```
- Какая функция в топе? Должна быть `compute_omp`
- Есть ли unexpected функции (initialize, memcpy)?
- Per-thread timeline: есть ли простаивающие потоки?

---

## Результаты экспериментов

### Условия

- **CPU:** 2 × Intel Xeon X5675 (Westmere, 6 ядер/сокет, 12 ядер всего)
- **Память:** DDR3-1333 × 6 каналов на сокет (~64 ГБ/с пик)
- **Компилятор:** icpx 2026.1.1
- **Флаги:** `-O3 -fiopenmp`
- **Стенсиль:** 16-й порядок, kHalfLength = 8

### Сводная таблица

| Режим | Рангов | Потоков | Домен | Время | GFLOPS всего | Speedup |
|---|---|---|---|---|---|---|
| sequential | 1 | 1 | 256³ | 44.9 с | 1.94 | 1.0x |
| pure_mpi 12 | 12 | 1 | 256×256×480 | 11.2 с | 9.36 | 4.8x |
| pure_mpi 12 | 12 | 1 | 256×256×960 | 25.4 с | 10.92 | 5.6x |
| pure_mpi 2 | 2 | 1 | 256×256×256 | 22.4 с | 3.64 | 1.9x |
| **hybrid 2×6** | 2 | 12 | 256×256×256 | **4.0 с** | **20.32** | **10.5x** |
| hybrid 2×6 | 2 | 12 | 512×512×256 | 22.7 с | 15.28 | — |
| pure_omp 12 | 1 | 12 | 256³ | 4.4 с | 19.62 | 10.2x |

> GFLOPS всего = значение одного ранга × количество рангов
> (код печатает метрики от rank 0)

### Главные выводы

1. **Hybrid 2×6 — абсолютный победитель** (20.32 GFLOPS, 10.5x vs sequential)
2. **Hybrid ≈ Pure OMP** (разница 3.5% — в пределах погрешности)
3. **Pure MPI на 12 рангах в 2 раза медленнее** (10.92 vs 20.32) — накладные
   расходы на MPI_Sendrecv
4. **Увеличение домена 256³ → 512³ убило throughput** (20.3 → 15.3 GFLOPS) —
   насыщение пропускной способности памяти
5. **Код упирается в память** (~64 ГБ/с DDR3), а не в вычисления:
   20.32 GFLOPS = 14% от пика (147 GFLOPS), но ~100% от bandwidth

### Потолок производительности

```
 20.32 GFLOPS (hybrid)
 ├─ 14% от вычислительного пика (147 GFLOPS)
 └─ ~100% от пика памяти (64 ГБ/с DDR3-1333 × 6 каналов)

 Дальнейшее ускорение возможно только через:
 1. Уменьшение b3 (чтобы рабочий набор влезал в L3)
 2. NUMA-привязку (numactl --membind)
 3. Huge Pages (echo 512 > /proc/sys/vm/nr_hugepages)
 4. MPI pinning (I_MPI_PIN_DOMAIN)
```

---

## Устранение проблем

### Ошибка: n3 must be divisible by nprocs

```
Error: n3 (256) must be divisible by nprocs (12)
```

**Причина:** n3 не делится на число рангов.
**Решение:** выбрать n3, кратный nprocs (240, 252, 264, 288, 480, 960 для 12).

### Предупреждение: -fno-alias unused

```
icpx: warning: argument unused during compilation: '-fno-alias'
```

**Причина:** icpx (LLVM) не использует этот флаг — он сам управляет алиасингом.
**Решение:** убрать `-fno-alias` из `CMAKE_CXX_FLAGS` в `CMakeLists.txt`.

### Предупреждение: setvars.sh already run

```
WARNING: setvars.sh has already been run. Skipping re-execution.
```

**Причина:** oneAPI окружение уже активировано в текущей сессии.
**Решение:** игнорировать. Для принудительного перезапуска:
```bash
source /opt/intel/oneapi/setvars.sh --force
```

### Низкая производительность (< 10 GFLOPS)

| Симптом | Причина | Решение |
|---|---|---|
| pure_mpi < 5 GFLOPS | n3/ранг слишком мал | Увеличить n3 (минимум 32 на ранг) |
| hybrid < 15 GFLOPS | b3 переполняет L3 | Уменьшить b3 с 128 до 64 |
| pure_omp < 15 GFLOPS | Память мигрирует между NUMA | Использовать `numactl --membind` |
| Все режимы < 5 GFLOPS | Код не векторизован | Проверить флаги: `-O3 -xSSE4.2` |

### VTune: Cannot locate debugging information

```
Warning: Cannot locate debugging information for the Linux kernel.
```

**Причина:** VTune не находит debug-информацию в бинарнике.
**Решение:** пересобрать с `-gline-tables-only -fdebug-info-for-profiling`
(см. раздел "Сборка для профилирования").

### VTune: ptrace_scope error

```
Error: Scope of ptrace System Call Is Limited
```

**Решение:**
```bash
echo 0 | sudo tee /proc/sys/kernel/yama/ptrace_scope
# После завершения:
echo 1 | sudo tee /proc/sys/kernel/yama/ptrace_scope
```

### APS: no metrics collected

**Причина:** `perf_event_paranoid` слишком ограничен.
**Решение:**
```bash
sudo sysctl -w kernel.perf_event_paranoid=0
# Для постоянного изменения:
echo kernel.perf_event_paranoid=0 | sudo tee /etc/sysctl.d/99-perf.conf
```
