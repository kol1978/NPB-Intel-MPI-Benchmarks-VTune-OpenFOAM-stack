# iso3dfd — 3D изотропный конечноразностный решатель (4 режима × 3 блока)

Решение трёхмерного акустического волнового уравнения методом конечных разностей
(16-й порядок по пространству, 2-й по времени) с поддержкой четырёх режимов
параллелизма: последовательный, чистый MPI, гибридный MPI+OpenMP и чистый OpenMP.

Проект разработан на базе Intel oneAPI-samples (iso3dfd_omp_offload), переработан
под CPU-only сервер **Intel Xeon X5675 (Westmere-EP)** без GPU, с добавлением
нативной MPI-декомпозиции, 4 режимов выполнения и двухуровневого cache-blocking.

---

## Содержание

1. [Архитектура](#архитектура)
2. [Структура проекта](#структура-проекта)
3. [Сборка](#сборка)
4. [Запуск](#запуск)
5. [Режимы выполнения](#режимы-выполнения)
6. [Декомпозиция и halo-обмен](#декомпозиция-и-halo-обмен)
7. [Cache-blocking](#cache-blocking)
8. [NUMA-оптимизация](#numa-оптимизация)
9. [Профилирование](#профилирование)
10. [Результаты экспериментов](#результаты-экспериментов)
11. [Требования](#требования)
12. [Файлы проекта](#файлы-проекта)

---

## Архитектура

### Три блока кода

| Блок | Файлы | Ответственность |
|---|---|---|
| **GRID** | `iso3dfd_grid.hpp / .cpp` | Аллокация 3D-массивов (`prev`, `next`, `vel`), инициализация (источник в центре, коэффициенты 16-го порядка), доступ по линейному индексу |
| **SOLVER** | `iso3dfd_solver.hpp / .cpp` | Одна итерация 3D-стенсиля (49 точек, 97 FLOP/точка), переключение `compute_serial` / `compute_omp`, чтение `prev` → запись `next` |
| **DRIVER** | `iso3dfd_driver.hpp / .cpp` | Декомпозиция домена (split по Z), halo-обмен через `MPI_Sendrecv`, временной цикл, тайминг, управление 4 режимами |

### Матрица 4 режима × 3 блока

| Режим | GRID | SOLVER | DRIVER |
|---|---|---|---|
| **sequential** (1 proc, 1 thread) | Полная сетка n1×n2×n3 | `compute_serial` (no OMP) | Нет MPI, 1 ранг, 1 поток |
| **pure_mpi** (N procs, 1 thread) | Кусок n1×n2×(n3/N) + halo | `compute_serial` (no OMP) | `MPI_Sendrecv`, N рангов, 1 поток |
| **hybrid** (M procs, T threads) | Кусок n1×n2×(n3/M) + halo | `compute_omp` (parallel for, collapse) | `MPI_Sendrecv`, M рангов, T потоков |
| **pure_omp** (1 proc, N threads) | Полная сетка n1×n2×n3 | `compute_omp` (parallel for, collapse) | Нет MPI, 1 ранг, N потоков |

### Уровни параллелизма

| Уровень | Технология | Режимы |
|---|---|---|
| 4. Межузловой | MPI | pure_mpi, hybrid |
| 3. Внутриузловой | MPI между рангами | pure_mpi, hybrid |
| 2. Многопоточный | OpenMP | hybrid, pure_omp |
| 1. SIMD/векторный | Авто-векторизация (SSE4.2) | все режимы |

### Поток данных в одной итерации

```
1. Halo exchange (MPI_Sendrecv)   — только pure_mpi, hybrid
2. Call Solver (compute_serial / compute_omp) — read prev, write next
3. Swap prev ↔ next (pointer swap)
4. Timing / Output
```

В режиме `sequential` и `pure_omp` шаг 1 пропускается (нет MPI).

---

## Структура проекта

```text
iso3dfd4MODES3BLOCKS/
├── CMakeLists.txt                     # Единый CMake (icpx + Intel MPI + OpenMP)
├── include/
│   ├── iso3dfd.h                      # Константы (kHalfLength=8, dxyz, dt)
│   ├── iso3dfd_grid.hpp               # GRID: Allocate, Free, Initialize, Print*
│   ├── iso3dfd_solver.hpp             # SOLVER: compute_iteration
│   └── iso3dfd_driver.hpp             # DRIVER: run_driver (MPI + halo)
├── src/
│   ├── iso3dfd.cpp                    # MAIN: парсинг 8 аргументов, MPI_Init, режимы
│   ├── iso3dfd_driver.cpp             # DRIVER: exchange_halo + временной цикл
│   ├── iso3dfd_solver.cpp             # SOLVER: stencil_point + serial + omp + blocking
│   ├── iso3dfd_grid.cpp               # GRID: аллокация, коэффициенты 16-го порядка
│   └── iso3dfd_verify.cpp             # VERIFY: проверка NaN/Inf (опционально)
└── build/
    └── iso3dfd                        # Бинарник (после сборки)
```

---

## Сборка

### Зависимости

| Компонент | Версия | Путь |
|---|---|---|
| Intel oneAPI Compiler | icpx 2026.1.1 | `/opt/intel/oneapi/compiler/2026.1/bin` |
| Intel MPI | 2021.18 (MPI 4.1) | `/opt/intel/oneapi/mpi/2021.18` |
| OpenMP | Intel OpenMP 5.1 (`-fiopenmp`) | в составе icpx |
| CMake | ≥ 3.20 | системный |

### Команды

```bash
# 1. Активация oneAPI
source /opt/intel/oneapi/setvars.sh

# 2. Конфигурация
cmake -S . -B build -DVERIFY_RESULTS=0

# 3. Сборка
cmake --build build -j$(nproc)
```

### Опции CMake

| Опция | По умолчанию | Описание |
|---|---|---|
| `VERIFY_RESULTS` | `OFF` | Включает `iso3dfd_verify.cpp` (проверка NaN/Inf) |
| `CMAKE_BUILD_TYPE` | `Release` | `Release` (-O3), `Debug` (-g -O0) |

### Флаги компиляции

| Build type | Флаги |
|---|---|
| Release | `-O3 -fiopenmp` |
| Debug | `-g -O0 -fiopenmp` |

> **Примечание:** `-fno-alias` убран — icpx LLVM игнорирует его (warning unused).

---

## Запуск

### Синтаксис

```bash
./build/iso3dfd n1 n2 n3 b1 b2 b3 niter <режим>
```

| Позиция | Параметр | По умолчанию | Описание |
|---|---|---|---|
| 1 | `n1` | 256 | Размер сетки по X |
| 2 | `n2` | 256 | Размер сетки по Y |
| 3 | `n3` | 256 | Размер сетки по Z (должен делиться на число рангов в MPI-режимах) |
| 4 | `b1` | 16 | Размер блока по X |
| 5 | `b2` | 8 | Размер блока по Y |
| 6 | `b3` | 64 | Размер блока по Z |
| 7 | `niter` | 100 | Число итераций по времени |
| 8 | `режим` | sequential | `sequential` \| `pure_mpi` \| `hybrid` \| `pure_omp` |

### Примеры

```bash
# Sequential — базовая проверка
./build/iso3dfd 256 256 256 16 8 64 100 sequential

# Pure MPI — 12 рангов (n3 кратно 12)
mpirun -n 12 ./build/iso3dfd 256 256 480 16 8 40 100 pure_mpi

# Hybrid — 2 ранга × 6 потоков (по NUMA-узлу)
OMP_NUM_THREADS=6 mpirun -n 2 ./build/iso3dfd 256 256 256 16 8 64 100 hybrid

# Pure OpenMP — 12 потоков
OMP_NUM_THREADS=12 ./build/iso3dfd 256 256 256 16 8 64 100 pure_omp
```

### Вывод программы

```
iso3dfd — 3D isotropic finite-difference solver
Grid:        256 x 256 x 256
Block:       16 x 8 x 64
Iterations:  100
Half-length: 8 (16th-order stencil)
------------------------------------------------
------------------------------------------------
Time:         4.438 s
Performance:  19.62 GFLOPS
Lattice:      13824000 points/iteration
```

| Метрика | Описание |
|---|---|
| `Time` | Время kernel-цикла (все итерации) |
| `Performance` | GFLOPS — вычислительная производительность (от rank 0) |
| `Lattice` | Число interior-точек на итерацию |

---

## Режимы выполнения

### sequential

Однопроцессорная, однопоточная реализация. Базовый эталон для сравнения.

```bash
./build/iso3dfd 256 256 256 16 8 64 100 sequential
```

- Нет MPI, нет OpenMP
- Вызывается `compute_serial`
- Полная сетка в одном процессе

### pure_mpi

N независимых процессов, по 1 потоку каждый. Декомпозиция по оси Z.

```bash
mpirun -n 12 ./build/iso3dfd 256 256 480 16 8 40 100 pure_mpi
```

- Каждый ранг работает со своим куском `n1 × n2 × (n3/N)` + 16 слоёв halo
- `MPI_Sendrecv` обменивает halo между соседями каждую итерацию
- `n3` **должен делиться** на число рангов
- Каждый ранг вызывает `compute_serial` (без OpenMP)

### hybrid

M процессов × T потоков OpenMP. Целевой режим для NUMA-серверов.

```bash
OMP_NUM_THREADS=6 mpirun -n 2 ./build/iso3dfd 256 256 256 16 8 64 100 hybrid
```

- 2 MPI-ранга (по одному на NUMA-узел), 6 OpenMP-потоков на ранг
- `MPI_Sendrecv` для halo между рангами
- `compute_omp` с `#pragma omp parallel for collapse(2)` по Z и Y
- Cache-blocking по X (b1) и Z (b3)
- `n3` должен делиться на M (число рангов)

### pure_omp

Один процесс, N потоков OpenMP. Без MPI.

```bash
OMP_NUM_THREADS=12 ./build/iso3dfd 256 256 256 16 8 64 100 pure_omp
```

- Полная сетка в одном процессе
- `compute_omp` с `collapse(2)` и cache-blocking
- Нет накладных расходов на MPI

---

## Декомпозиция и halo-обмен

### Схема декомпозиции

```
       n3 (полный домен по Z)
┌──────────────────────────────────┐
│         Rank 0 (Z: 0..127)       │  ← n3/2 = 128 слоёв
│  ┌─────────────────────────┐    │
│  │  interior (8..119)       │    │  ← 112 слоёв полезной работы
│  │  halo_top (0..7)         │    │  ← 8 слоёв от Rank 1
│  │  halo_bot (120..127)     │    │  ← 8 слоёв от Rank 1
│  └─────────────────────────┘    │
├──────────────────────────────────┤
│         Rank 1 (Z: 128..255)     │  ← n3/2 = 128 слоёв
│  ┌─────────────────────────┐    │
│  │  interior (136..247)     │   │  ← 112 слоёв
│  │  halo_top (128..135)     │    │  ← 8 слоёв от Rank 0
│  │  halo_bot (248..255)     │    │  ← 8 слоёв от Rank 0
│  └─────────────────────────┘    │
└──────────────────────────────────┘
```

### Правила делимости

| Режим | Условие |
|---|---|
| pure_mpi (N рангов) | `n3 % N == 0` |
| hybrid (M рангов) | `n3 % M == 0` |
| sequential, pure_omp | Нет ограничений |

### Подбор n3 для 12 рангов

| n3 | На ранг | Interior (−16) | Доля полезного |
|---|---|---|---|
| 240 | 20 | 4 | 20% |
| 480 | 40 | 24 | 60% |
| 960 | 80 | 64 | 80% |

> **Правило:** минимум 4×kHalfLength = 32 слоя на ранг, чтобы halo занимал не больше 50%.

---

## Cache-blocking

### Формула рабочего набора

```
input(halo) = (b1 + 16) × (b2 + 16) × (b3 + 16) × 4    ← prev с halo
output      = b1 × b2 × b3 × 4                          ← next
vel         = b1 × b2 × b3 × 4                          ← vel
total       = input(halo) + output + vel
```

### Характеристики кэша Xeon X5675

| Уровень | Размер | На поток (6 потоков/сокет) |
|---|---|---|
| L1 Data | 32 КБ | 32 КБ (на ядро) |
| L2 | 256 КБ | 256 КБ (на ядро) |
| L3 | 12 МБ | **2 МБ** (shared / 6) |

### Размеры блоков и заполнение L3

| b3 | На поток | Сумма (6 потоков) | % L3 (2 МБ) | Вердикт |
|---|---|---|---|---|
| 32 | 176 КБ | 1056 КБ | 51% | ✓ оптимально |
| **64** | **304 КБ** | **1824 КБ** | **87%** | **✓ рекомендуемый** |
| 128 | 560 КБ | 3360 КБ | 160% | ✗ переполнение |

> **Вывод:** для hybrid (2×6) оптимальный `b3 = 64`. `b3 = 128` переполняет L3 на 60%,
> что объясняет деградацию при увеличении домена до 512³.

---

## NUMA-оптимизация

### Архитектура X5675

```
┌─────────────── Socket 0 ───────────────┐  ┌─────────────── Socket 1 ───────────────┐
│  6 ядер (0–5)                          │  │  6 ядер (6–11)                         │
│  L3: 12 МБ (shared)                   │  │  L3: 12 МБ (shared)                    │
│  RAM: DDR3-1333 × 3 канала (~32 ГБ/с)  │  │  RAM: DDR3-1333 × 3 канала (~32 ГБ/с)   │
│  NUMA-узел 0                           │  │  NUMA-узел 1                            │
└────────────────────────────────────────┘  └────────────────────────────────────────┘
                    │                                           │
                    └───────────── QPI (~25.6 ГБ/с) ──────────┘
```

### Рекомендуемые конфигурации

| Режим | Команда | Привязка |
|---|---|---|
| Hybrid (оптимальный) | `OMP_NUM_THREADS=6 mpirun -n 2 ...` | Каждый ранг на свой NUMA-узел |
| Pure MPI (12 рангов) | `mpirun -n 12 ...` | По 6 рангов на сокет |
| Pure OMP (12 потоков) | `OMP_NUM_THREADS=12 numactl --interleave=0,1 ...` | Равномерно по двум узлам |

### Intel MPI pinning

```bash
# Привязка рангов к сокетам
I_MPI_PIN_DOMAIN=[0xffffffff00,0x00ffffffff] \
OMP_NUM_THREADS=6 mpirun -n 2 ./build/iso3dfd 256 256 256 16 8 64 100 hybrid
```

### numactl для pure_omp

```bash
# Один NUMA-узел (6 ядер, локальная память)
OMP_NUM_THREADS=6 numactl --cpunodebind=0 --membind=0 ./build/iso3dfd 256 256 256 16 8 64 100 pure_omp

# Interleave по двум узлам (12 потоков)
OMP_NUM_THREADS=12 numactl --interleave=0,1 ./build/iso3dfd 256 256 256 16 8 64 100 pure_omp
```

---

## Профилирование

### Intel APS (Application Performance Snapshot)

Лёгкий профилировщик, overhead 1–3%, не требует root.

```bash
# Сбор (все метрики)
aps --collection-mode=all --result-dir=aps_result -- \
    ./build/iso3dfd 256 256 256 16 8 64 100 pure_omp

# Отчёт
aps --report aps_result
```

| Метрика | Норма для stencil | Что означает |
|---|---|---|
| CPI | 0.5–1.0 | Тактов на инструкцию |
| Memory Stalls | < 20% | Доля ожидания памяти |
| NUMA ratio | < 5% | Доля удалённых доступов |
| Vectorization | > 80% (128-bit SSE) | Процент векторизованных FP |
| OpenMP Imbalance | < 10% | Дисбаланс потоков |

### Intel VTune Profiler

Глубокий анализ, overhead ~5% (HW) или ~2× (SW).

```bash
# Hotspots (HW sampling, нужен SEP-драйвер)
vtune -collect hotspots -knob sampling-mode=hw \
    -r vtune_result -- ./build/iso3dfd 256 256 256 16 8 64 100 pure_omp

# Hotspots с MPI
mpirun -n 2 -gtool "vtune -collect hotspots -r vtune_result" :all \
    ./build/iso3dfd 256 256 256 16 8 64 100 hybrid
```

### Сборка с профилировочными флагами

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DVERIFY_RESULTS=0
# Добавить флаги: -g -fdebug-info-for-profiling
```

---

## Результаты экспериментов

### Условия

- **CPU:** 2 × Intel Xeon X5675 (Westmere-EP, 3.07 GHz, 6 ядер/сокет)
- **Кэш:** L1 32 КБ, L2 256 КБ, L3 12 МБ/сокет
- **Память:** DDR3-1333 × 6 каналов (~64 ГБ/с пик)
- **SIMD:** SSE4.2 (128-bit, 4 float/операция)
- **Компилятор:** icpx 2026.1.1, `-O3 -fiopenmp`
- **MPI:** Intel MPI 2021.18

### Сводная таблица

| Режим | Рангов | Потоков | Домен | Время | GFLOPS всего | Speedup |
|---|---|---|---|---|---|---|
| sequential | 1 | 1 | 256³ | 44.9 с | 1.94 | 1.0× |
| pure_mpi 2 | 2 | 2 | 256×256×256 | 22.4 с | 3.64 | 1.9× |
| pure_mpi 12 | 12 | 12 | 256×256×480 | 11.2 с | 9.36 | 4.8× |
| pure_mpi 12 | 12 | 12 | 256×256×960 | 25.4 с | 10.92 | 5.6× |
| **hybrid 2×6** | 2 | 12 | **256³** | **4.0 с** | **20.32** | **10.5×** |
| hybrid 2×6 | 2 | 12 | 512×512×256 | 22.7 с | 15.28 | — |
| pure_omp 12 | 1 | 12 | 256³ | 4.4 с | 19.62 | 10.1× |

### Ключевые выводы

1. **Hybrid 2×6 — оптимальный режим:** 20.32 GFLOPS, ускорение 10.5× — насыщение пропускной способности памяти DDR3 (~64 ГБ/с → ~21 GFLOPS потолок)
2. **Pure OMP 12 — близкий к hybrid:** 19.62 GFLOPS (−3.5%), без накладных расходов MPI
3. **Pure MPI 12 — в 2× медленнее hybrid:** 10.92 GFLOPS — коммуникационные накладные расходы на тонких доменах
4. **Увеличение домена 256³ → 512³ убило производительность hybrid:** 20.3 → 15.3 GFLOPS — насыщение памяти, данные вытесняются в DRAM
5. **b3 = 128 переполняет L3 на 60%** в hybrid-режиме (6 потоков × 560 КБ = 3360 КБ > 2048 КБ L3/сокет)

### Потолок производительности

```
Performance (GFLOPS):
                                                       
sequential  ██  1.94
pure_mpi 2  ████  3.64
pure_mpi 12 ██████████  10.92
pure_omp    ███████████████████  19.62
hybrid 2×6  ████████████████████  20.32
            ─────────────────────────────────────────
            Потолок памяти DDR3 ≈ 21 GFLOPS
```

---

## Требования

### Аппаратное обеспечение

| Компонент | Требование |
|---|---|
| CPU | Intel x86-64 (SSE4.2 минимум) |
| Ядра | ≥ 1 (тестировалось на 12 физических) |
| RAM | ~230 МБ на сетку 256³ (3 массива × 4 байта × 256³) |
| GPU | **Не требуется** (CPU-only) |

### Программное обеспечение

| Компонент | Версия |
|---|---|
| ОС | Ubuntu 22.04+ (Linux 6.x) |
| Intel oneAPI Compiler | icpx 2026.1+ |
| Intel MPI | 2021.18+ (MPI 4.1) |
| CMake | ≥ 3.20 |
| OpenMP | Intel OpenMP 5.1 (в составе icpx) |

---

## Файлы проекта

| Файл | Описание |
|---|---|
| `CMakeLists.txt` | Единый CMake: icpx + Intel MPI + OpenMP, выбор компилятора до `project()` |
| `include/iso3dfd.h` | Константы: `kHalfLength=8`, `DEFAULT_DXYZ`, `DEFAULT_DT` |
| `include/iso3dfd_grid.hpp` | GRID API: `Allocate`, `Free`, `Initialize`, `PrintStats`, `PrintSummary` |
| `include/iso3dfd_solver.hpp` | SOLVER API: `compute_iteration()` |
| `include/iso3dfd_driver.hpp` | DRIVER API: `run_driver()` |
| `src/iso3dfd.cpp` | MAIN: парсинг 8 аргументов, `MPI_Init`, выбор режима, вызов `run_driver` |
| `src/iso3dfd_driver.cpp` | DRIVER: `exchange_halo()` через `MPI_Sendrecv`, временной цикл |
| `src/iso3dfd_solver.cpp` | SOLVER: `stencil_point()`, `compute_serial()`, `compute_omp()` с cache-blocking |
| `src/iso3dfd_grid.cpp` | GRID: `posix_memalign(64)`, коэффициенты 16-го порядка, импульс в центре |
| `src/iso3dfd_verify.cpp` | VERIFY: проверка NaN/Inf (компилируется при `-DVERIFY_RESULTS=1`) |

---

## Лицензия

На основе Intel oneAPI-samples (iso3dfd_omp_offload), лицензия MIT.
Переработано: добавлена нативная MPI-декомпозиция, 4 режима, CPU-only сборка.
