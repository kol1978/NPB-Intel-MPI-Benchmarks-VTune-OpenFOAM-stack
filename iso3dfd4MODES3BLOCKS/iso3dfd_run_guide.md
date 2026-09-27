# Инструкция по запуску iso3dfd (4 режима × 3 блока)

## Содержание

1. [Быстрый старт](#быстрый-старт)
2. [Четыре режима выполнения](#четыре-режима-выполнения)
3. [Параметры командной строки](#параметры-командной-строки)
4. [Декомпозиция домена и ограничения](#декомпозиция-домена-и-ограничения)
5. [Cache-blocking: расчёт для Xeon X5675](#cache-blocking-расчёт-для-xeon-x5675)
6. [NUMA-оптимизация](#numa-оптимизация)
7. [Управление потоками OpenMP](#управление-потоками-openmp)
8. [Сборка для профилирования (VTune/APS)](#сборка-для-профилирования-vtuneaps)
9. [Профилирование: ptrace_scope, VTune, APS](#профилирование-ptrace_scope-vtune-aps)
10. [Сценарии использования](#сценарии-использования)
11. [Результаты экспериментов](#результаты-экспериментов)
12. [Устранение проблем](#устранение-проблем)

---

## Быстрый старт

### Требования

| Компонент | Версия | Путь |
|-----------|--------|------|
| Компилятор | Intel LLVM (icpx) 2026.1.1 | `/opt/intel/oneapi/compiler/2026.1/bin` |
| MPI | Intel MPI | `/opt/intel/oneapi/mpi/latest` |
| OpenMP | Intel OpenMP (`-fiopenmp`) | в составе icpx |
| VTune | Intel VTune Profiler | `/opt/intel/oneapi/vtune/latest` |
| CPU | Intel Xeon X5675 × 2 (Westmere-EP) | 12 ядер / 24 потока, SSE4.2 |
| ОС | Ubuntu (Linux 6.x) | `ptrace_scope` по умолчанию = 1 |

### Сборка

```bash
source /opt/intel/oneapi/setvars.sh

cmake -S . -B build -DVERIFY_RESULTS=0
cmake --build build -j$(nproc)
```

### Запуск (4 режима)

```bash
# 1. Sequential — 1 процесс, 1 поток (baseline)
./build/iso3dfd 256 256 256 16 8 64 100 sequential

# 2. Pure MPI — 12 процессов, 1 поток на каждый
mpirun -n 12 ./build/iso3dfd 256 256 256 16 8 20 100 pure_mpi

# 3. Hybrid — 2 процесса × 6 потоков OpenMP (рекомендуется)
OMP_NUM_THREADS=6 mpirun -n 2 ./build/iso3dfd 256 256 256 16 8 128 100 hybrid

# 4. Pure OpenMP — 1 процесс, 12 потоков
OMP_NUM_THREADS=12 ./build/iso3dfd 256 256 256 16 8 64 100 pure_omp
```

### Ожидаемый вывод

```
Grid Sizes: 256 256 256
Block sizes: 16 8 64
Iterations: 100
Mode: hybrid
--------------------------------------
time         : 2.96 secs
throughput   : 566.798 Mpts/s
flops        : 34.5747 GFlops
bytes        : 6.80157 GBytes/s
--------------------------------------
```

---

## Четыре режима выполнения

### 1. Sequential (1 процесс, 1 поток)

Полная сетка обрабатывается одним процессом без распараллеливания. Базовый
режим для измерения однопоточной производительности и проверки корректности.

**Команда:**
```bash
./build/iso3dfd 256 256 256 16 8 64 100 sequential
```

- MPI не используется (1 ранг, `MPI_COMM_WORLD` размера 1)
- OpenMP не используется (1 поток)
- Включена авто-векторизация (SSE4.2, уровень 1 параллелизма)
- Полная сетка n1 × n2 × n3, без декомпозиции

**Когда использовать:**
- Baseline-замер для сравнения с параллельными режимами
- Проверка корректности (верификация результата)
- Замер однопоточной пропускной способности памяти

---

### 2. Pure MPI (N процессов, 1 поток на каждый)

Сетка режется по оси Z на N блоков, каждый блок обрабатывается отдельным
MPI-рангом. Halo-обмен между соседями через `MPI_Sendrecv`.

**Команда:**
```bash
mpirun -n 12 ./build/iso3dfd 256 256 256 16 8 20 100 pure_mpi
```

- N рангов MPI, 1 поток OpenMP на ранг
- Декомпозиция по Z: `n3 / nprocs` точек на ранг + halo (16 слоёв)
- `MPI_Sendrecv` для halo-обмена между соседями
- Внутри ранга — последовательный цикл (авто-векторизация только)

**Ограничения:**
- `n3` должно делиться на `nprocs` без остатка
- `n3 / nprocs ≥ kHalfLength × 2 + 1` (минимум 17 точек на ранг)
- При 12 рангах на 2 сокетах: 6 рангов на сокет, каждый занимает ядро

**Когда использовать:**
- Сравнение MPI-масштабируемости
- Замер накладных расходов на halo-обмен
- Baseline для hybrid-режима (pure_mpi vs hybrid при том же числе ядер)

---

### 3. Hybrid (M процессов × T потоков OpenMP)

Сетка режется по Z на M блоков. Каждый блок обрабатывается MPI-рангом,
внутри ранга — OpenMP `parallel for` по потокам. Двухуровневый параллелизм.

**Команда:**
```bash
OMP_NUM_THREADS=6 mpirun -n 2 ./build/iso3dfd 256 256 256 16 8 128 100 hybrid
```

- M рангов MPI, T потоков OpenMP на ранг
- Декомпозиция по Z: `n3 / M` точек на ранг + halo
- `MPI_Sendrecv` для halo-обмена
- Внутри ранга — OpenMP `parallel for collapse(2)` по плоскостям блока
- Рекомендуемая конфигурация: 2 ранга × 6 потоков = 12 ядер (по сокетам)

**Ограничения:**
- `n3` должно делиться на M без остатка
- `n3 / M ≥ kHalfLength × 2 + 1`
- `b3` (n3_block) должно быть ≤ `n3 / M`
- Для 2 сокетов: M = 2 (по рангу на сокет), T = 6 (по потоку на ядро)

**Когда использовать:**
- Целевой режим для максимальной производительности
- Сравнение с pure_mpi и pure_omp
- Проверка NUMA-локальности (2 ранга = 2 NUMA-узла)

---

### 4. Pure OpenMP (1 процесс, N потоков)

Полная сетка обрабатывается одним процессом, распараллеливание — через
OpenMP `parallel for` по потокам внутри одного ранга.

**Команда:**
```bash
OMP_NUM_THREADS=12 ./build/iso3dfd 256 256 256 16 8 64 100 pure_omp
```

- 1 ранг MPI, N потоков OpenMP
- Нет декомпозиции, нет halo-обмена
- OpenMP `parallel for collapse(2)` по плоскостям блока
- Все потоки разделяют один адресный поток

**Ограничения:**
- При N > 6: потоки выходят за пределы одного NUMA-узла → NUMA-эффект
- Рекомендуется `numactl --membind` для привязки

**Когда использовать:**
- Сравнение с hybrid (pure_omp vs hybrid при том же числе потоков)
- Замер overhead MPI (pure_omp без MPI vs hybrid с MPI)
- Проверка масштабируемости OpenMP (1 → 6 → 12 потоков)

---

## Параметры командной строки

| Позиция | Имя | По умолчанию | Описание |
|---------|-----|-------------|----------|
| 1 | `n1` | 256 | Размер сетки по оси X |
| 2 | `n2` | 256 | Размер сетки по оси Y |
| 3 | `n3` | 256 | Размер сетки по оси Z (режется MPI) |
| 4 | `n1_block` | 16 | Размер блока (тайла) по X |
| 5 | `n2_block` | 8 | Размер блока (тайла) по Y |
| 6 | `n3_block` | 64 | Размер блока (тайла) по Z |
| 7 | `Iterations` | 100 | Число шагов по времени |
| 8 | `mode` | sequential | Режим: `sequential` / `pure_mpi` / `hybrid` / `pure_omp` |

### Правила делимости

| Параметр | Правило | Пример |
|----------|---------|--------|
| `n3` ÷ nprocs (pure_mpi) | без остатка | 256 / 12 — НЕ делится → использовать 240 или 12 × 20 |
| `n3` ÷ M (hybrid) | без остатка | 256 / 2 = 128 ✓ |
| `n3_block` ≤ `n3 / nprocs` | блок помещается в кусок ранга | 64 ≤ 128 (hybrid 2×6) ✓ |
| `n3 / nprocs` ≥ 17 | минимум kHalfLength × 2 + 1 | 128 ≥ 17 ✓ |

### Влияние параметров на производительность

| Параметр | Эффект |
|----------|--------|
| Сетка 256³ → 512³ | Время × 8, память × 8, throughput примерно тот же |
| Block 16×8×64 → 32×8×64 | Меньше блоков → меньше overhead OpenMP, больше давление на кэш |
| Block b3: 64 → 128 | Больше переиспользование кэша, но при 6 потоках — переполнение L3 |
| Iterations 100 → 1000 | Время × 10, throughput стабилизируется |
| Threads 6 → 12 (pure_omp) | Рост, но упор в NUMA после 6 |

---

## Декомпозиция домена и ограничения

### Схема декомпозиции по Z

```
         ┌─────────────────────────────────────┐
         │         Полная сетка n1×n2×n3        │
         │                                     │
         │  ┌──────────────┐  ┌──────────────┐ │
         │  │  Ранг 0      │  │  Ранг 1      │ │
         │  │  n1×n2×(n3/M)│  │  n1×n2×(n3/M)│ │
         │  │  + halo      │←→│  + halo      │ │
         │  └──────────────┘  └──────────────┘ │
         └─────────────────────────────────────┘
              Сокет 0              Сокет 1
              (NUMA 0)             (NUMA 1)
```

### Halo-обмен

Для стенсиля 16-го порядка (`kHalfLength = 8`) каждый ранг нуждается в
8 слоях от соседа с каждой стороны. Halo-обмен выполняется через
`MPI_Sendrecv` на каждой итерации:

- **Левый сосед** (`rank - 1`): отправка левого края, приём правого halo
- **Правый сосед** (`rank + 1`): отправка правого края, приём левого halo
- **Граничные ранги** (первый и последний): обмениваются только с одним соседом

Размер halo-буфера: `n1 × n2 × kHalfLength × sizeof(float)` =
`256 × 256 × 8 × 4` = 2 МБ на сторону.

### Таблица подбора параметров

| Режим | nprocs | Потоков | n3 на ранг | n3_block (b3) | Память на ранг |
|-------|--------|---------|------------|---------------|----------------|
| sequential | 1 | 1 | 256 | 64 | 192 МБ |
| pure_mpi | 12 | 1 | 20 | 20 | 16 МБ |
| pure_mpi | 2 | 1 | 128 | 128 | 96 МБ |
| hybrid | 2 | 6 | 128 | 128 | 96 МБ |
| pure_omp | 1 | 12 | 256 | 64 | 192 МБ |

> Память на ранг = 3 массива × n1 × n2 × (n3/nprocs + 16) × 4 байта.
> 16 — halo с обеих сторон (kHalfLength × 2).

---

## Cache-blocking: расчёт для Xeon X5675

### Параметры железа

| Параметр | Значение |
|----------|----------|
| L1 Data | 32 КБ на ядро |
| L2 | 256 КБ на ядро |
| L3 | 12 МБ на сокет (shared, 6 ядер) |
| Cache line | 64 байт |
| SIMD | SSE4.2 (128-bit, 4 float) |
| Память | DDR3-1333 × 6 каналов ~64 ГБ/с пик |

### L3 на поток (ключевой параметр)

| Конфигурация | L3 на поток | Расчёт |
|--------------|-------------|--------|
| 1 поток на сокет | 12 МБ | весь L3 сокета |
| 6 потоков (no HT) | 2 МБ | 12 МБ / 6 |
| 12 потоков (HT) | 1 МБ | 12 МБ / 12 |

### Формула рабочего набора

Для блока b1 × b2 × b3 с halo (kHalfLength = 8, halo = 16 слоёв):

```
input(halo) = (b1 + 16) × (b2 + 16) × (b3 + 16) × 4   ← prev с halo
output      = b1 × b2 × b3 × 4                           ← next
vel         = b1 × b2 × b3 × 4                           ← vel (поле скоростей)
total       = input(halo) + output + vel
```

> Важно: 3 массива (prev + next + vel), а не 2. Vel читается на каждой точке.

### Расчёт для L3 / 6 потоков = 2 МБ = 2 097 152 байт

| b3 | input(halo) | output | vel | total | % от L3/поток | Влезает? |
|----|-------------|--------|-----|-------|---------------|----------|
| 32 | 144 КБ | 16 КБ | 16 КБ | 176 КБ | 8.6% | да |
| 64 | 240 КБ | 32 КБ | 32 КБ | 304 КБ | 14.8% | да |
| 128 | 432 КБ | 64 КБ | 64 КБ | 560 КБ | 27.3% | да |
| 256 | 816 КБ | 128 КБ | 128 КБ | 1072 КБ | 52.3% | да |
| 512 | 1584 КБ | 256 КБ | 256 КБ | 2096 КБ | 100% | на грани |

### Кумулятивный эффект при 6 потоках на сокете

При 6 потоках, разделяющих L3, суммарный рабочий набор:

| b3 | На поток | × 6 потоков | Сумма | L3 (2 МБ) | Статус |
|----|---------|-------------|-------|-----------|--------|
| 32 | 176 КБ | × 6 | 1056 КБ | 51% | оптимально |
| 64 | 304 КБ | × 6 | 1824 КБ | 87% | на грани |
| 128 | 560 КБ | × 6 | 3360 КБ | 160% | переполнение |

### Рекомендации по b3

| Режим | Потоков/сокет | L3/поток | b3 оптимальный | Сумма (6 потоков) | % L3 |
|-------|-------------|----------|---------------|-------------------|------|
| sequential | 1 | 12 МБ | 64–256 | — | 2.5–8.5% |
| pure_omp (12) | 12 (HT) | 1 МБ | 32 | 6 × 176 = 1056 КБ | 51% |
| hybrid (2×6) | 6 | 2 МБ | 32–64 | 1056–1824 КБ | 51–87% |
| pure_mpi (12) | 1 на ранг | 12 МБ | 64–128 | — | 2.5–4.6% |
| pure_mpi (2) | 1 на ранг | 12 МБ | 64–128 | — | 2.5–4.6% |

> b3 = 128 в hybrid переполняет L3 на 60% — данные вытесняются в DRAM.

---

## NUMA-оптимизация

### Архитектура X5675

```
┌─────────────────────────┐  ┌─────────────────────────┐
│      Сокет 0 (NUMA 0)    │  │      Сокет 1 (NUMA 1)    │
│  6 ядер / 12 потоков     │  │  6 ядер / 12 потоков     │
│  L3: 12 МБ              │  │  L3: 12 МБ              │
│  DDR3: 3 канала (~32 ГБ/с)│ │  DDR3: 3 канала (~32 ГБ/с)│
│  Память: локальная       │  │  Память: локальная       │
└───────────┬─────────────┘  └─────────────┬───────────┘
            │   QPI Link (~25.6 ГБ/с)       │
            └───────────────────────────────┘
```

### Рекомендуемые конфигурации

| Режим | Команда | NUMA-эффект |
|-------|---------|-------------|
| Hybrid (2×6) | `OMP_NUM_THREADS=6 mpirun -n 2 ...` | Ранг 0 → сокет 0, ранг 1 → сокет 1 (через MPI pinning) |
| Pure OMP (6) | `OMP_NUM_THREADS=6 numactl --membind=0 --cpunodebind=0 ...` | Один сокет, нет QPI |
| Pure OMP (12) | `OMP_NUM_THREADS=12 numactl --interleave=0,1 ...` | Оба сокета, round-robin |
| Pure MPI (12) | `mpirun -n 12 -genv I_MPI_PIN_DOMAIN=[0-5,6-11] ...` | 6 рангов на сокет |

### Intel MPI pinning

```bash
# Автоматический пиннинг (рекомендуется)
mpirun -n 2 -genv I_MPI_PIN=on -genv I_MPI_PIN_DOMAIN=[0-5,6-11] \
    OMP_NUM_THREADS=6 ./build/iso3dfd 256 256 256 16 8 128 100 hybrid

# Ручной пиннинг через numactl
mpirun -n 2 \
    -genv OMP_NUM_THREADS=6 \
    numactl --membind=0 --cpunodebind=0 ./build/iso3dfd 256 256 256 16 8 128 100 hybrid : \
    numactl --membind=1 --cpunodebind=1 ./build/iso3dfd 256 256 256 16 8 128 100 hybrid
```

### Проверка NUMA-локальности через APS

```bash
aps --collection-mode=hwc --result-dir=aps_numa_check \
    -- mpirun -n 2 OMP_NUM_THREADS=6 ./build/iso3dfd 256 256 256 16 8 128 100 hybrid
```

В отчёте APS смотреть `NUMA ratio`:
- < 5% — отлично, данные локальны
- 5–15% — приемлемо
- > 15% — данные на удалённом узле, нужен `numactl`

---

## Управление потоками OpenMP

### Стандартные варианты

```bash
# Все ядра (12 физических, без HT)
OMP_NUM_THREADS=12 ./build/iso3dfd 256 256 256 16 8 64 100 pure_omp

# Один NUMA-узел (рекомендуется для pure_omp)
OMP_NUM_THREADS=6 numactl --membind=0 --cpunodebind=0 \
    ./build/iso3dfd 256 256 256 16 8 64 100 pure_omp

# Hybrid: 6 потоков на ранг, 2 ранга
OMP_NUM_THREADS=6 mpirun -n 2 ./build/iso3dfd 256 256 256 16 8 128 100 hybrid
```

### Schedule (распределение итераций)

```bash
# Static — по умолчанию, лучший для равномерной нагрузки
OMP_SCHEDULE=static OMP_NUM_THREADS=6 \
    mpirun -n 2 ./build/iso3dfd 256 256 256 16 8 128 100 hybrid

# Dynamic — для неравномерной нагрузки (если OpenMP Imbalance > 15%)
OMP_SCHEDULE=dynamic,4 OMP_NUM_THREADS=6 \
    mpirun -n 2 ./build/iso3dfd 256 256 256 16 8 128 100 hybrid
```

---

## Сборка для профилирования (VTune/APS)

### Три типа сборки

Для профилирования с привязкой к строкам исходного кода нужна сборка с
отладочными символами. CMake поддерживает out-of-source сборку — отдельная
директория не затирает Release.

| Сборка | Директория | Тип | Флаги (поверх `-fiopenmp`) | Назначение |
|--------|-----------|-----|--------------------------|------------|
| **Release** | `build/` | Release (по умолчанию) | `-O3` | Бенчмарки, замеры производительности |
| **Profiling** | `build-vtune/` | RelWithDebInfo | `-O2 -g -gline-tables-only -fdebug-info-for-profiling` | VTune, APS |
| **Debug** | `build-debug/` | Debug | `-g -O0` | gdb, пошаговая отладка |

> Флаг `-gline-tables-only` рекомендуется Intel VTune User Guide 2025.4 —
> даёт номера строк без полной отладочной информации, снижая overhead.
> Флаг `-fdebug-info-for-profiling` добавляет метаданные для точной
> корреляции профиля с исходным кодом (Intel System Requirements 2025.1).
> `-O2` (не `-O0`) — оптимизация сохраняется, иначе профиль нерепрезентативен.

### Команды сборки

```bash
source /opt/intel/oneapi/setvars.sh

# 1. Release — бенчмарки, замеры (как раньше)
cmake -S . -B build -DVERIFY_RESULTS=0
cmake --build build -j$(nproc)

# 2. Профилирование — VTune, APS (отдельная папка, не трогает Release)
cmake -S . -B build-vtune -DCMAKE_BUILD_TYPE=RelWithDebInfo -DVERIFY_RESULTS=0
cmake --build build-vtune -j$(nproc)

# 3. Debug — gdb, пошаговая отладка
cmake -S . -B build-debug -DCMAKE_BUILD_TYPE=Debug -DVERIFY_RESULTS=0
cmake --build build-debug -j$(nproc)
```

### Проверка отладочной информации

```bash
# Проверить наличие секций .debug_line и .debug_info
readelf -S build-vtune/iso3dfd | grep debug

# Ожидаемый вывод:
#  [Nr] Name            Type   Address          Off    Size
#  [27] .debug_line     PROGBITS 0000000000000000 001234 0005678
#  [28] .debug_info     PROGBITS 0000000000000000 006789 0001234
```

---

## Профилирование: ptrace_scope, VTune, APS

### 9.1. kernel.yama.ptrace_scope — настройка ядра Linux

#### Проблема

Linux-ядро (с модулем YAMA) по умолчанию **ограничивает** использование
`ptrace` — системного вызова, через который одни процессы могут
трассировать (наблюдать, отлаживать) другие.

VTune Profiler в режиме **SW sampling** и при **MPI-трассировке**
использует `ptrace` (сигнал `SIGPROF` для периодического сбора образцов).
Без правильной настройки YAMA VTune молча не собирает данные или падает
с ошибкой.

#### Что такое YAMA

YAMA (Yama Linux Security Module) — модуль безопасности Linux-ядра,
контролирующий доступ к `ptrace`. Внедрён в Ubuntu начиная с версии 10.10.
Управляется через `/proc/sys/kernel/yama/ptrace_scope`.

#### Значения ptrace_scope

| Значение | Режим | Поведение | Кто рекомендует |
|----------|-------|-----------|-----------------|
| **0** | classic ptrace | Любой процесс может трассировать потомков. **Требуется для VTune SW sampling.** | HPC-серверы, профилирование |
| **1** | restricted (по умолчанию) | Только родитель может трассировать своих потомков. | Canonical, Ubuntu по умолчанию |
| **2** | admin-only | Только root с `CAP_SYS_PTRACE` может трассировать. | CIS Benchmark Level 2, KSPP, харденинг Ubuntu 24.04 |
| **3** | no ptrace | Полностью запрещено. Ничего не может трассировать. | KSPP (максимальная защита) |

#### Источники рекомендаций по харденингу

| Источник | Рекомендация | Уровень |
|----------|-------------|---------|
| CIS Ubuntu 24.04 LTS Benchmark v1.0.0 | `ptrace_scope = 1` (Level 1), `= 2` (Level 2) | Базовый / строгий |
| KSPP (Kernel Self-Protection Project) | `ptrace_scope = 3` (2 как компромисс) | Максимальный |
| Linux Secure Ops (sysctl hardening 2026) | `ptrace_scope = 2` | Production |
| Canonical (Ubuntu Security Docs) | `ptrace_scope = 1` (по умолчанию) | Базовый |
| Ubuntu 24.04 (фактически из коробки) | `ptrace_scope = 1` | Базовый |

> На HPC-сервере с профилированием — **ставим 0** (нужно для VTune SW sampling).
> На продакшн-сервере, где профилирование не используется, — **1 или 2** по CIS.
> Значение 2 ломает `gdb attach` и VTune SW, но HW sampling и APS продолжают
> работать (они используют `perf_event_open`, а не `ptrace`).

#### Проверка текущего значения

```bash
cat /proc/sys/kernel/yama/ptrace_scope
```

#### Когда ptrace_scope нужен, а когда нет

| Сценарий | ptrace_scope | Нужен root? | Альтернатива |
|----------|-------------|-------------|--------------|
| VTune **HW sampling** (`-knob sampling-mode=hw`) | не нужен | нужен (SEP-драйвер) | — |
| VTune **SW sampling** (`-knob sampling-mode=sw`) | **нужен = 0** | нет | HW sampling (нужен SEP) |
| VTune **MPI tracing** (`-trace-mpi`) | **нужен = 0** | нет | APS |
| APS (через `perf` API) | не нужен | нет (обычно) | — |
| `gdb attach` | **нужен = 0** | нет (при scope=0) | `gdb` из той же сессии |
| `strace -p <pid>` | **нужен = 0** | нет (при scope=0) | — |

#### Вариант 1: включение одной командой (временно, до перезагрузки)

```bash
# Включить ptrace для текущей сессии (требует sudo)
echo 0 | sudo tee /proc/sys/kernel/yama/ptrace_scope

# Проверить
cat /proc/sys/kernel/yama/ptrace_scope
# Вывод: 0

# ... запуск VTune / профилирование ...

# Вернуть обратно (после профилирования)
echo 1 | sudo tee /proc/sys/kernel/yama/ptrace_scope
```

#### Вариант 2: постоянное включение (через sysctl)

```bash
# Создать конфигурационный файл
echo "kernel.yama.ptrace_scope = 0" | sudo tee /etc/sysctl.d/10-ptrace.conf

# Применить без перезагрузки
sudo sysctl -p /etc/sysctl.d/10-ptrace.conf

# Проверить
cat /proc/sys/kernel/yama/ptrace_scope
# Вывод: 0
```

> Для продакшн-сервера (без профилирования) — поставить `1` или `2`:
> ```bash
> echo "kernel.yama.ptrace_scope = 2" | sudo tee /etc/sysctl.d/10-ptrace.conf
> sudo sysctl -p /etc/sysctl.d/10-ptrace.conf
> ```

#### Вариант 3: скрипт с автоматическим сохранением/восстановлением

Скрипт сохраняет текущее значение `ptrace_scope`, временно устанавливает `0`,
запускает VTune, затем восстанавливает исходное значение. Подходит для
серверов, где нельзя оставлять `ptrace_scope = 0` постоянно.

```bash
#!/bin/bash
# run_vtune_ptrace.sh — запуск VTune с автоматическим управлением ptrace_scope
# Использование: ./run_vtune_ptrace.sh <режим_vtune> <команда_запуска>
# Пример: ./run_vtune_ptrace.sh hotspots "mpirun -n 2 ./build-vtune/iso3dfd 256 256 256 16 8 128 100 hybrid"

set -euo pipefail

VTUNE_MODE="${1:-hotspots}"
RUN_CMD="${2:-./build-vtune/iso3dfd 256 256 256 16 8 64 100 sequential}"
RESULT_DIR="vtune_$(date +%Y%m%d_%H%M%S)"

# Проверка root
if [ "$(id -u)" -ne 0 ]; then
    echo "Запустите через sudo: sudo $0 $@"
    exit 1
fi

# Сохраняем текущее значение ptrace_scope
SAVED_SCOPE=$(cat /proc/sys/kernel/yama/ptrace_scope)
echo "Текущий ptrace_scope: $SAVED_SCOPE"

# Включаем ptrace
echo 0 > /proc/sys/kernel/yama/ptrace_scope
echo "ptrace_scope установлен в 0 (classic ptrace)"

# Функция восстановления (срабатывает даже при ошибке)
restore_ptrace() {
    echo "$SAVED_SCOPE" > /proc/sys/kernel/yama/ptrace_scope
    echo "ptrace_scope восстановлен в $SAVED_SCOPE"
}
trap restore_ptrace EXIT

# Активация окружения Intel oneAPI
source /opt/intel/oneapi/setvars.sh --force 2>/dev/null

# Запуск VTune
echo "Запуск VTune ($VTUNE_MODE)..."
echo "Команда: $RUN_CMD"
echo "Результат: $RESULT_DIR"

vtune -collect "$VTUNE_MODE" \
    -knob sampling-mode=sw \
    -knob enable-characterization-insights=false \
    -finalization-mode=deferred \
    -r "$RESULT_DIR" \
    -- $RUN_CMD

# Финализация и отчёт
echo "Финализация результатов..."
vtune -finalize -r "$RESULT_DIR" 2>/dev/null

echo ""
echo "=== VTune завершён ==="
echo "Результат: $RESULT_DIR"
echo ""
echo "Просмотр отчёта:"
echo "  vtune -report summary -r $RESULT_DIR"
echo "  vtune -report hotspots -r $RESULT_DIR -limit 10"
echo "  vtune-gui $RESULT_DIR"
```

Установка и использование:

```bash
# Сохранить скрипт
chmod +x run_vtune_ptrace.sh

# Запуск VTune hotspots для hybrid-режима
sudo ./run_vtune_ptrace.sh hotspots \
    "mpirun -n 2 OMP_NUM_THREADS=6 ./build-vtune/iso3dfd 256 256 256 16 8 128 100 hybrid"

# Запуск VTune для pure_mpi
sudo ./run_vtune_ptrace.sh hotspots \
    "mpirun -n 12 ./build-vtune/iso3dfd 256 256 256 16 8 20 100 pure_mpi"

# Запуск VTune для sequential
sudo ./run_vtune_ptrace.sh hotspots \
    "./build-vtune/iso3dfd 256 256 256 16 8 64 100 sequential"
```

#### Дополнительно: perf_event_paranoid (для APS на Ubuntu 24.04)

Ubuntu 24.04 по умолчанию устанавливает `perf_event_paranoid = 2`, что
ограничивает доступ к HW-счётчикам через `perf` API. APS использует этот
интерфейс, поэтому для полной функциональности нужно `≤ 1`.

```bash
# Проверка
cat /proc/sys/kernel/perf_event_paranoid
# По умолчанию: 2

# Временное включение (до перезагрузки)
echo 1 | sudo tee /proc/sys/kernel/perf_event_paranoid

# Постоянное включение
echo "kernel.perf_event_paranoid = 1" | sudo tee /etc/sysctl.d/10-perf.conf
sudo sysctl -p /etc/sysctl.d/10-perf.conf
```

| Значение | Доступ | Что работает |
|----------|--------|-------------|
| 2 | restricted (по умолчанию) | Только root — HW счётчики |
| 1 | user-level | APS HW-счётчики без root |
| 0 | all | Всё, включая kernel-level профилирование |

---

### 9.2. VTune Profiler

#### VTune HW sampling (нужен SEP-драйвер, root)

```bash
# Сборка для профилирования
cmake -S . -B build-vtune -DCMAKE_BUILD_TYPE=RelWithDebInfo -DVERIFY_RESULTS=0
cmake --build build-vtune -j$(nproc)

# Запуск HW sampling (нужен SEP-драйвер)
sudo vtune -collect hotspots -knob sampling-mode=hw \
    -knob enable-characterization-insights=false \
    -finalization-mode=deferred \
    -r vtune_hw_$(date +%Y%m%d_%H%M%S) \
    -- mpirun -n 2 OMP_NUM_THREADS=6 ./build-vtune/iso3dfd 256 256 256 16 8 128 100 hybrid
```

> HW sampling не требует `ptrace_scope = 0`, но требует SEP-драйвер (установка
> с root). Точность времени максимальная (~5% overhead).
> Флаги проверены по Intel VTune User Guide 2025.4.

#### VTune SW sampling (не нужен SEP, нужен ptrace_scope=0)

```bash
# Через скрипт (рекомендуется — автоматически управляет ptrace_scope)
sudo ./run_vtune_ptrace.sh hotspots \
    "mpirun -n 2 OMP_NUM_THREADS=6 ./build-vtune/iso3dfd 256 256 256 16 8 128 100 hybrid"

# Или вручную (с предварительной установкой ptrace_scope)
echo 0 | sudo tee /proc/sys/kernel/yama/ptrace_scope
vtune -collect hotspots -knob sampling-mode=sw \
    -knob enable-characterization-insights=false \
    -finalization-mode=deferred \
    -r vtune_sw_$(date +%Y%m%d_%H%M%S) \
    -- mpirun -n 2 OMP_NUM_THREADS=6 ./build-vtune/iso3dfd 256 256 256 16 8 128 100 hybrid
```

> SW sampling: overhead ~2× (сигнал SIGPROF каждые 10 мс), но не требует
> SEP-драйвера. Для 3-секундного запуска — ~300 образцов.

#### VTune с MPI-трассировкой

```bash
# MPI-трассировка требует ptrace_scope=0
sudo ./run_vtune_ptrace.sh hotspots \
    "mpirun -n 12 ./build-vtune/iso3dfd 256 256 256 16 8 20 100 pure_mpi"

# Или с явным -trace-mpi
echo 0 | sudo tee /proc/sys/kernel/yama/ptrace_scope
vtune -collect hotspots -knob sampling-mode=sw \
    -trace-mpi \
    -r vtune_mpi_$(date +%Y%m%d_%H%M%S) \
    -- mpirun -n 12 ./build-vtune/iso3dfd 256 256 256 16 8 20 100 pure_mpi
```

#### Просмотр результатов VTune

```bash
# Консольный — summary
vtune -report summary -r vtune_hw_*

# Консольный — top-10 горячих функций
vtune -report hotspots -r vtune_hw_* -limit 10

# GUI (если есть X-сервер)
vtune-gui vtune_hw_*
```

#### Что показывает VTune

| Метрика | Описание | Норма для stencil |
|---------|----------|-------------------|
| **Top-10 функций** | Какие функции занимают больше всего CPU time | `compute_iteration` ~90% |
| **CPU Time vs Elapsed** | CPU Time ~ Elapsed × NP → хорошая утилизация | ~99% |
| **Per-thread timeline** | Загрузка каждого потока OpenMP | Ровные полосы |
| **Call stack** | Дерево вызовов для горячих точек | stencil → driver → main |
| **Source-level attribution** | Привязка времени к строкам кода | Нужна RelWithDebInfo |

---

### 9.3. Intel APS (Application Performance Snapshot)

APS — лёгкий профилировщик из состава VTune. Собирает HW-счётчики CPU
через `perf` API (без SEP-драйвера). Не требует `ptrace_scope = 0`.

#### Сбор данных

```bash
# Вариант A: через aps --collection-mode
aps --collection-mode=all --result-dir=aps_$(date +%Y%m%d_%H%M%S) \
    -- mpirun -n 2 OMP_NUM_THREADS=6 ./build/iso3dfd 256 256 256 16 8 128 100 hybrid

# Вариант B: через переменную окружения (для MPI)
export APS_ENABLE=1
mpirun -n 2 OMP_NUM_THREADS=6 ./build/iso3dfd 256 256 256 16 8 128 100 hybrid
aps --report aps_result_*
```

#### Режимы сбора APS

| Режим | Команда | Overhead | Что собирает |
|-------|---------|----------|--------------|
| `all` | `--collection-mode=all` | 1–3% | HW + OpenMP |
| `hwc` | `--collection-mode=hwc` | < 1% | Только HW-счётчики |
| `omp` | `--collection-mode=omp` | < 1% | Только OpenMP |

#### Просмотр отчёта

```bash
# Консольный
aps --report aps_result_*

# HTML-отчёт
aps-report -g aps_result_*
firefox aps_report_*.html
```

#### Метрики APS и интерпретация

##### Аппаратные метрики

| Метрика | Описание | Норма для X5675 |
|---------|----------|-----------------|
| **CPI** | Cycles Per Instruction | 0.5–1.0 — отлично; > 2.0 — проблема |
| **Memory Stalls** | Доля тактов, потерянных на ожидание памяти | < 20% — хорошо; > 50% — memory-bound |
| **Cache Stalls** | Разбивка: L1/L2/L3 cache misses | Зависит от блока |
| **DRAM Stalls** | Ожидание DRAM (дальних доступов) | Чем меньше, тем лучше |
| **NUMA ratio** | Доля удалённых доступов к памяти | < 5% — хорошо; > 15% — NUMA-проблема |
| **Vectorization** | Процент векторизованных FP-операций | SSE4.2: > 80% — хорошо |
| **SP GFLOPS** | Single-precision GFLOPS | Сравнение с метрикой iso3dfd |
| **CPU Utilization** | Загрузка всех логических ядер | 90–100% — хорошо |

> Для X5675 (Westmere): векторизация — **SSE4.2 (128-bit, 4 float)**.
> AVX2/AVX-512 не поддерживаются. Ожидается 80–95% векторизации в 128-bit.

##### OpenMP-метрики

| Метрика | Описание | Норма |
|---------|----------|-------|
| **OpenMP Imbalance** | Доля времени, потерянного на барьерах | < 10% — хорошо; > 30% — дисбаланс |
| **Serial Time** | Время вне OpenMP-регионов | < 5% — хорошо; > 20% — много serial-кода |

##### Метрики памяти

| Метрика | Описание |
|---------|----------|
| **Memory Footprint (max)** | Максимальное потребление resident memory |
| **Memory Footprint (avg)** | Среднее потребление resident memory |

#### Пример вывода APS

```
==================================================================
                      APS Report
================================================================--

Elapsed Time:                    2.96 sec
CPU Utilization:                 12.0 cores (100%)
SP GFLOPS:                      34.57
CPI:                            0.87

--- Memory Bandwidth & Stalls ---
Memory Stalls:                  18.3%
  -- L2 Stalls:                  8.1%
  -- L3 Stalls:                  6.2%
  -- DRAM Stalls:                4.0%

--- NUMA ---
NUMA ratio:                      0.02 (2.0%)

--- Vectorization ---
Vectorization:                  91.2%
  -- 128-bit:                   91.2%
  -- 256-bit:                    0.0%
  -- 512-bit:                    0.0%

--- OpenMP ---
OpenMP Imbalance:                3.1%
Serial Time:                     1.2%

--- Memory Footprint ---
Max RSS:                        235.4 MB
Avg RSS:                        232.1 MB
```

#### Интерпретация для X5675

| Метрика | Значение в примере | Оценка | Действие |
|---------|---------------------|--------|----------|
| CPI | 0.87 | Хорошо | Норма для stencil |
| Memory Stalls | 18.3% | Хорошо | Stencil переиспользует кэш |
| NUMA ratio | 2.0% | Отлично | Данные локальны |
| Vectorization 128-bit | 91.2% | Отлично | SSE4.2 работает |
| Vectorization 256/512-bit | 0% | Ожидаемо | X5675 не поддерживает AVX |
| OpenMP Imbalance | 3.1% | Отлично | Потоки сбалансированы |
| Serial Time | 1.2% | Отлично | Почти всё в OpenMP |

---

### 9.4. Сравнение инструментов профилирования

| Характеристика | VTune HW | VTune SW | APS |
|----------------|---------|---------|-----|
| Overhead | ~5% | ~2× | 1–3% |
| SEP-драйвер | нужен | не нужен | не нужен |
| ptrace_scope | не нужен | **нужен = 0** | не нужен |
| root | нужен | нет | нет (обычно) |
| Точность времени | максимальная | искажена | почти точная |
| Детализация | per-function, call stack | per-function | сводные метрики |
| MPI-трассировка | да (`-trace-mpi`) | да (`-trace-mpi`) | нет |
| OpenMP-анализ | да (GUI timeline) | да (GUI timeline) | да (imbalance, serial) |

---

## Сценарии использования

### Сценарий 1: Baseline-замер

**Цель:** получить эталонные метрики для сравнения.

```bash
# Release-сборка, чистый замер
./build/iso3dfd 256 256 256 16 8 64 100 sequential
./build/iso3dfd 256 256 256 16 8 64 100 pure_omp
OMP_NUM_THREADS=6 mpirun -n 2 ./build/iso3dfd 256 256 256 16 8 128 100 hybrid
mpirun -n 12 ./build/iso3dfd 256 256 256 16 8 20 100 pure_mpi
```

Сравнить `time`, `throughput`, `flops`, `bytes` между режимами.

### Сценарий 2: Поиск оптимального размера блока

**Цель:** найти b3, дающий максимальный throughput в hybrid-режиме.

```bash
# b3 = 32 (L3 заполнен на 51%)
OMP_NUM_THREADS=6 mpirun -n 2 ./build/iso3dfd 256 256 256 16 8 32 100 hybrid

# b3 = 64 (L3 заполнен на 87%)
OMP_NUM_THREADS=6 mpirun -n 2 ./build/iso3dfd 256 256 256 16 8 64 100 hybrid

# b3 = 128 (L3 переполнен на 160%)
OMP_NUM_THREADS=6 mpirun -n 2 ./build/iso3dfd 256 256 256 16 8 128 100 hybrid
```

Ожидание: b3 = 64 — лучший, b3 = 128 — худший из-за вытеснения в DRAM.

### Сценарий 3: Проверка NUMA-локальности

**Цель:** понять, не теряет ли производительность из-за межсокетных пересылок.

```bash
# Без numactl (по умолчанию)
aps --collection-mode=all --result-dir=aps_no_numa \
    -- mpirun -n 2 OMP_NUM_THREADS=6 ./build/iso3dfd 256 256 256 16 8 128 100 hybrid

# С numactl (привязка к NUMA-узлам)
aps --collection-mode=all --result-dir=aps_with_numa \
    -- mpirun -n 2 -genv I_MPI_PIN_DOMAIN=[0-5,6-11] \
    OMP_NUM_THREADS=6 ./build/iso3dfd 256 256 256 16 8 128 100 hybrid
```

Сравнить `NUMA ratio` — должен быть < 5% с пиннингом.

### Сценарий 4: Проверка векторизации

**Цель:** убедиться, что компилятор векторизовал код под SSE4.2.

```bash
aps --collection-mode=hwc --result-dir=aps_vec \
    -- ./build/iso3dfd 256 256 256 16 8 64 100 sequential
```

Смотреть `Vectorization`:
- > 80% в 128-bit — отлично, SSE4.2 работает
- < 50% — код скалярный, проверить `-O3` в CMake

### Сценарий 5: Scaling-тест по числу потоков

**Цель:** понять, как масштабируется pure_omp.

```bash
OMP_NUM_THREADS=1  ./build/iso3dfd 256 256 256 16 8 64 100 pure_omp
OMP_NUM_THREADS=2  ./build/iso3dfd 256 256 256 16 8 64 100 pure_omp
OMP_NUM_THREADS=4  ./build/iso3dfd 256 256 256 16 8 64 100 pure_omp
OMP_NUM_THREADS=6  ./build/iso3dfd 256 256 256 16 8 64 100 pure_omp
OMP_NUM_THREADS=12 ./build/iso3dfd 256 256 256 16 8 64 100 pure_omp
```

Ожидание: линейный рост до 6 потоков, насыщение после 6 (NUMA-эффект).

### Сценарий 6: Hybrid vs Pure MPI

**Цель:** сравнить две стратегии распараллеливания при том же числе ядер.

```bash
# Hybrid: 2 ранга × 6 потоков = 12 ядер
OMP_NUM_THREADS=6 mpirun -n 2 ./build/iso3dfd 256 256 256 16 8 128 100 hybrid

# Pure MPI: 12 рангов × 1 поток = 12 ядер
mpirun -n 12 ./build/iso3dfd 256 256 256 16 8 20 100 pure_mpi
```

Сравнить `flops` — hybrid должен быть выше (меньше halo-обмена).

### Сценарий 7: Полный цикл профилирования

**Цель:** комплексный анализ hybrid-режима.

```bash
# Шаг 1: Сборка для профилирования
cmake -S . -B build-vtune -DCMAKE_BUILD_TYPE=RelWithDebInfo -DVERIFY_RESULTS=0
cmake --build build-vtune -j$(nproc)

# Шаг 2: Быстрый снимок через APS
aps --collection-mode=all --result-dir=aps_hybrid \
    -- mpirun -n 2 OMP_NUM_THREADS=6 ./build-vtune/iso3dfd 256 256 256 16 8 128 100 hybrid
aps --report aps_hybrid

# Шаг 3: Глубокий анализ через VTune (нужен ptrace_scope=0)
sudo ./run_vtune_ptrace.sh hotspots \
    "mpirun -n 2 OMP_NUM_THREADS=6 ./build-vtune/iso3dfd 256 256 256 16 8 128 100 hybrid"

# Шаг 4: Просмотр результатов
vtune -report summary -r vtune_*
vtune -report hotspots -r vtune_* -limit 10
```

---

## Результаты экспериментов

### Конфигурация эксперимента

| Параметр | Значение |
|----------|----------|
| CPU | Intel Xeon X5675 × 2 (Westmere-EP) |
| Ядра | 12 физических (6 на сокет) |
| Потоки | 24 (Hyper-Threading, не используется) |
| L3 | 12 МБ на сокет |
| Память | DDR3-1333, 6 каналов, ~64 ГБ/с пик |
| Компилятор | Intel LLVM (icpx) 2026.1.1 |
| Флаги | `-fiopenmp -O3` |
| SIMD | SSE4.2 (128-bit) |

### Результаты замеров

| # | Режим | nprocs | Потоки | Сетка | Блок | Время (с) | GFLOPS | GBytes/s |
|---|-------|--------|--------|-------|------|-----------|--------|----------|
| 0 | sequential | 1 | 1 | 256³ | 16×8×64 | 49.96 | 2.05 | 0.41 |
| 1 | pure_mpi | 12 | 1 | 256³ | 16×8×20 | 9.84 | 10.41 | 2.05 |
| 2 | pure_mpi | 2 | 1 | 256³ | 16×8×128 | 5.59 | 18.33 | 3.61 |
| 3 | hybrid | 2 | 6 | 256³ | 16×8×128 | 5.04 | 20.32 | 4.00 |
| 4 | pure_omp | 1 | 12 | 256³ | 16×8×64 | 5.33 | 19.21 | 3.78 |
| 5 | hybrid | 2 | 6 | 512³ | 16×8×128 | 39.84 | 20.12 | 3.96 |
| 6 | pure_mpi | 12 | 1 | 256×256×960 | 16×8×80 | 9.03 | 14.25 | 2.80 |

### Анализ

**Лучший результат:** hybrid 2×6 = 20.32 GFLOPS (запуск #3).

**Потолок производительности:** ~20 GFLOPS — насыщение пропускной
способности памяти DDR3. При 3 массивах × 256³ × 4 байта × 100 итераций
теоретический минимум времени ограничен пропускной способностью DRAM.

**Масштабирование:**
- Sequential → pure_mpi (12): 5.1× ускорение (10.41 / 2.05)
- Sequential → hybrid (2×6): 9.9× ускорение (20.32 / 2.05)
- Pure_mpi (12) → hybrid (2×6): 1.95× ускорение (20.32 / 10.41)
- 256³ → 512³ (hybrid): время × 7.9, GFLOPS стабильный (20.32 → 20.12)

**Bottleneck:**
- Memory-bound (stencil 16-го порядка: 49 точек, 97 FLOP на точку)
- L3 переполнение при b3=128 и 6 потоках (160% от L3)
- NUMA-эффект при неправильном пиннинге

---

## Устранение проблем

### Программа падает с "n3 must be divisible by nprocs"

**Причина:** `n3` не делится на число MPI-рангов без остатка.

**Решение:** подобрать `n3` или `nprocs` так, чтобы деление было без остатка.

| n3 | nprocs=2 | nprocs=12 |
|----|---------|-----------|
| 256 | 128 ✓ | 21.3 ✗ |
| 240 | 120 ✓ | 20 ✓ |
| 960 | 480 ✓ | 80 ✓ |

### Программа падает с "n3_block must be <= n3/nprocs"

**Причина:** размер блока b3 больше, чем кусок сетки на ранг.

**Решение:** уменьшить b3 или увеличить n3.

### Низкая производительность (< 10 GFLOPS в hybrid)

**Проверить:**
1. `OMP_NUM_THREADS` — установлено ли? (`echo $OMP_NUM_THREADS`)
2. MPI pinning — привязаны ли ранги к сокетам? (APS: NUMA ratio > 15%)
3. Cache-blocking — переполняет ли b3 кэш L3? (b3=128 при 6 потоках → 160%)
4. `OMP_SCHEDULE` — дисбаланс? (APS: OpenMP Imbalance > 15%)

### VTune: пустой отчёт (0 samples)

**Причина:** `ptrace_scope = 1` (или 2/3), VTune SW sampling не работает.

**Решение:**
```bash
echo 0 | sudo tee /proc/sys/kernel/yama/ptrace_scope
```

Или использовать скрипт `run_vtune_ptrace.sh` (раздел 9.1).

### VTune: "SEP driver not found"

**Причина:** HW sampling требует SEP-драйвер (Sampling Enabling Product).

**Решение:** либо установить SEP-драйвер (нужен root), либо использовать
SW sampling (`-knob sampling-mode=sw`) с `ptrace_scope=0`.

### APS: "perf_event_open: Permission denied"

**Причина:** `perf_event_paranoid = 2` (Ubuntu 24.04 по умолчанию).

**Решение:**
```bash
echo 1 | sudo tee /proc/sys/kernel/perf_event_paranoid
```

### APS: векторизация < 50%

**Причина:** компилятор не векторизовал код.

**Решение:** проверить флаги компиляции — должен быть `-O3` (не `-O0`).
Проверить через `readelf` наличие SSE-инструкций:
```bash
objdump -d build/iso3dfd | grep -c "mulps\|addps\|movaps"
```

### NUMA ratio > 15% в APS

**Причина:** данные размазаны по двум сокетам, обращения через QPI.

**Решение:**
```bash
# Для hybrid: пиннинг рангов к сокетам
mpirun -n 2 -genv I_MPI_PIN_DOMAIN=[0-5,6-11] \
    OMP_NUM_THREADS=6 ./build/iso3dfd 256 256 256 16 8 128 100 hybrid

# Для pure_omp: привязка к одному сокету
OMP_NUM_THREADS=6 numactl --membind=0 --cpunodebind=0 \
    ./build/iso3dfd 256 256 256 16 8 64 100 pure_omp
```
