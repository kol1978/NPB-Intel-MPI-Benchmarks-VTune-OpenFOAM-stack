# MPI-рефакторинг iso3dfd: декомпозиция области и гало-обмен

> Документ описывает рефакторинг примера `iso3dfd_omp_offload` для добавления
> гибридного параллелизма (MPI + OpenMP) по аналогии с OpenFOAM.
>
> Рефакторинг не меняет вычислительное ядро (стенсил 16-го порядка) —
> добавляется новый уровень параллелизма поверх существующего OpenMP.

---

## Содержание

1. [Терминология](#1-терминология)
2. [Назначение рефакторинга](#2-назначение-рефакторинга)
3. [Структура исходного кода до рефакторинга](#3-структура-исходного-кода-до-рефакторинга)
4. [Конкретные изменения (рефакторинг)](#4-конкретные-изменения-рефакторинг)
   - 4.1. [Заголовок — подключение MPI](#41-заголовок--подключение-mpi)
   - 4.2. [Инициализация MPI в main()](#42-инициализация-mpi-в-main)
   - 4.3. [Декомпозиция по оси Z](#43-декомпозиция-по-оси-z)
   - 4.4. [Выделение памяти — локальные подобласти](#44-выделение-памяти--локальные-подобласти)
   - 4.5. [Модификация вызова Iso3dfdIteration](#45-модификация-вызова-iso3dfditeration)
   - 4.6. [Гало-обмен между итерациями](#46-гало-обмен-между-итерациями)
   - 4.7. [Сбор результатов для вывода](#47-сбор-результатов-для-вывода)
   - 4.8. [Финализация MPI](#48-финализация-mpi)
5. [Изменения в CMakeLists.txt](#5-изменения-в-cmakeliststxt)
6. [Сборка и запуск](#6-сборка-и-запуск)
7. [Что даёт рефакторинг для профилирования](#7-что-даёт-рефакторинг-для-профилирования)
8. [Сложность и риски](#8-сложность-и-риски)
9. [Совместимость с NO_OFFLOAD](#9-совместимость-с-no_offload)
10. [Шпаргалка](#10-шпаргалка)

---

## 1. Терминология

| Термин | Значение |
|---|---|
| **Рефакторинг** | Переработка кода без изменения внешнего поведения, с улучшением внутренней структуры |
| **Декомпозиция области** (Domain Decomposition) | Разделение 3D-сетки на подобласти по оси Z между MPI-рангами |
| **Гало-обмен** (Halo / Ghost Cell Exchange) | Обмен граничными слоями (kHalfLength плоскостей) между соседними рангами |
| **Гибридный параллелизм** (Hybrid Parallelism) | MPI между процессами + OpenMP внутри процесса |
| **Ранг** (Rank) | MPI-процесс, работающий над своей подобластью сетки |
| **Гало-область** (Halo / Ghost Cells) | kHalfLength слоёв ячеек, принимаемых от соседей — нужны для стенсила у границ |

---

## 2. Назначение рефакторинга

### Проблема

Оригинальный `iso3dfd_omp_offload` — чистое OpenMP-приложение. В коде нет
`#include <mpi.h>`, нет `MPI_Init`, нет обмена между рангами. Команда
`mpirun -n 12 ./iso3dfd ...` запускает 12 независимых копий, каждый процесс
считает полную сетку 256x256x256.

Последствия:

- VTune `-trace-mpi` бесполезен — нет MPI-вызовов для трассировки.
- APS (Intel Application Performance Snapshot) не показывает ничего — нет коммуникаций.
- Дисбаланс рангов = 0 (все делают одно и то же).
- Невозможно профилировать реальный HPC-сценарий: коммуникация vs вычисление.

### Решение

Добавить MPI-декомпозицию по оси Z (как в OpenFOAM `decomposePar`):

- Каждый ранг хранит свою подобласть + kHalfLength слоёв гало.
- Каждую итерацию ранги обмениваются гало-данными через `MPI_Isend / MPI_Irecv`.
- Синхронизация через `MPI_Waitall`.
- Сбор метрик через `MPI_Reduce`.

Объём изменений: ~80–120 строк в `iso3dfd.cpp`, ~5 строк в `CMakeLists.txt`.

---

## 3. Структура исходного кода до рефакторинга

| Элемент | Описание |
|---|---|
| Сетка | `n1 x n2 x n3`, хранится как 1D-массив: индекс `iz * n1*n2 + iy * n1 + ix` |
| Гало | `2 * kHalfLength` (= 16) ячеек по каждой размерности для краевых точек стенсила |
| Стенсил | 16-й порядок: для каждой точки нужны 8 соседей в каждом направлении X, Y, Z |
| Ядро | `Iso3dfdIteration(ptr_next, ptr_prev, ptr_vel, coeff, n1, n2, n3, ...)` |
| Параллелизм | Только `#pragma omp parallel for` внутри одного процесса |
| MPI | Отсутствует |

---
```text
iso3dfd_omp_offload/
│
├── README.md                              # Скорректированный README (оригинал + расширения)
├── LICENSE.txt                             # MIT (из оригинала)
├── third-party-programs.txt                # Из оригинала
│
├── docs/                                   # Документация рефакторинга
│   ├── NO_OFFLOAD_PATCH.md                 # Патч CPU-only режима (без GPU)
│   ├── profiling_compilation_levels.md     # Уровни компиляции для профилирования
│   ├── vtune_profiling_guide.md            # Руководство VTune + APS
│   └── MPI_DOMAIN_DECOMPOSITION_PATCH.md  # MPI-рефакторинг (этот документ)
│
├── src/                                    # Исходный код
│   ├── iso3dfd.cpp                         # Оригинал → патч NO_OFFLOAD → MPI-рефакторинг
│   ├── iso3dfd_kernels.cpp                 # Вычислительное ядро (не меняется)
│   ├── iso3dfd_kernels.h                   # Заголовки ядра
│   ├── iso3dfd.h                           # Общие определения
│   ├── CMakeLists.txt                      # Патч: NO_OFFLOAD + ENABLE_MPI + ENABLE_PROFILING
│   │
│   ├── iso3dfd.cpp.orig                    # Резервная копия оригинала
│   ├── CMakeLists.txt.orig                 # Резервная копия оригинала
│   │
│   └── patches/                            # Готовые патчи (diff-формат)
│       ├── 01_noffload.patch               # Патч CPU-only (Patch 1 + Patch 2)
│       ├── 02_mpi.patch                    # Патч MPI-декомпозиции
│       └── 03_profiling.patch              # Патч профилировочных флагов
│
├── build/                                  # Сборка (gitignore)
│   └── src/
│       ├── iso3dfd                         # Бинарник
│       ├── vtune                           # Скрипт запуска VTune hotspots
│       ├── analyze                         # Скрипт парсинга CSV-отчётов VTune
│       ├── vtune_hotspots_mpi/             # Результат VTune (gitignore)
│       ├── vtune_hotspots.csv              # CSV-отчёты (gitignore)
│       ├── vtune_hotspots_func.csv
│       └── vtune_hotspots_ranks.csv
│
├── scripts/                                # Вспомогательные скрипты
│   ├── apply_patches.sh                    # Автоматическое применение патчей
│   ├── build_all.sh                        # Сборка всех конфигураций
│   └── run_benchmark.sh                    # Запуск бенчмарка (все конфигурации)
│
└── results/                                # Результаты экспериментов (gitignore)
    ├── vtune/                              # Дампы VTune
    ├── aps/                                # Отчёты APS
    └── csv/                                # CSV-отчёты
```


## 4. Конкретные изменения (рефакторинг)

### 4.1. Заголовок — подключение MPI

**Было:**

```cpp
// src/iso3dfd.cpp
#include <omp.h>
```

**Стало:**

```cpp
// src/iso3dfd.cpp
#include <omp.h>
#include <mpi.h>
```

---

### 4.2. Инициализация MPI в main()

**Было:**

```cpp
int main(int argc, char *argv[]) {
    // ... разбор аргументов, выделение памяти ...
}
```

**Стало:**

```cpp
int main(int argc, char *argv[]) {
    // ── Инициализация MPI ──
    int rank, nprocs;
    MPI_Init(&argc, &argv);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &nprocs);

    // ... разбор аргументов (все ранги парсят, чтобы знать размеры) ...
}
```

---

### 4.3. Декомпозиция по оси Z

**Было:** все ранги используют полную сетку `n1 x n2 x n3`.

**Стало:** каждый ранг получает свою полосу по Z.

```cpp
// ── Декомпозиция по оси Z ──
// Делим n3 на nprocs частей
int n3_local = n3 / nprocs;        // базовый размер
int remainder = n3 % nprocs;
if (rank < remainder) n3_local++;  // первые ранги получают лишнюю плоскость

// Границы Z для данного ранга
int z_start = rank * (n3 / nprocs);
if (rank < remainder) z_start += rank;
else z_start += remainder;

int z_end = z_start + n3_local;

// Локальная сетка: n1 x n2 x (n3_local + 2 * kHalfLength)
// kHalfLength слоёв гало сверху и снизу для MPI-обмена
size_t n3_alloc = n3_local + 2 * kHalfLength;
size_t nsize_local = n1 * n2 * n3_alloc;
```

---

### 4.4. Выделение памяти — локальные подобласти

**Было:**

```cpp
float *ptr_next = (float *)malloc(nsize * sizeof(float));
float *ptr_prev = (float *)malloc(nsize * sizeof(float));
float *ptr_vel  = (float *)malloc(nsize * sizeof(float));
```

**Стало:**

```cpp
// Каждый ранг выделяет память только под свою подобласть
float *ptr_next = (float *)malloc(nsize_local * sizeof(float));
float *ptr_prev = (float *)malloc(nsize_local * sizeof(float));
float *ptr_vel  = (float *)malloc(nsize_local * sizeof(float));
```

---

### 4.5. Модификация вызова Iso3dfdIteration

**Было:**

```cpp
Iso3dfdIteration(ptr_next, ptr_prev, ptr_vel, coeff, n1, n2, n3, ...);
```

**Стало:**

```cpp
// Передаём локальные размеры и границы вычислений
// n3_alloc — полный локальный размер (с гало)
// kHalfLength — начало вычислений (пропуск нижнего гало)
// kHalfLength + n3_local — конец вычислений (до верхнего гало)
Iso3dfdIteration(ptr_next, ptr_prev, ptr_vel, coeff,
                  n1, n2, n3_alloc,
                  kHalfLength,
                  kHalfLength + n3_local, ...);
```

> Если функция `Iso3dfdIteration` работает с полной сеткой `(n1, n2, n3_alloc)`
> и сама управляет границами через `kHalfLength`, изменения минимальны —
> достаточно передать `n3_alloc` вместо `n3` и скорректировать диапазон Z.

---

### 4.6. Гало-обмен между итерациями

Это ключевой блок рефакторинга — аналог обмена граничными полями в OpenFOAM.

```cpp
for (int iter = 0; iter < num_iterations; iter++) {
    // ── Обмен гало по Z ──
    MPI_Request reqs[4];
    int nreqs = 0;

    // Отправка вверх, приём снизу
    if (rank < nprocs - 1) {
        // Отправляем последние kHalfLength слоёв (перед верхним гало)
        float *send_up = &ptr_prev[(n3_alloc - 2 * kHalfLength) * n1 * n2];
        MPI_Isend(send_up, kHalfLength * n1 * n2, MPI_FLOAT,
                  rank + 1, 0, MPI_COMM_WORLD, &reqs[nreqs++]);
        // Принимаем в верхнее гало
        float *recv_up = &ptr_prev[(n3_alloc - kHalfLength) * n1 * n2];
        MPI_Irecv(recv_up, kHalfLength * n1 * n2, MPI_FLOAT,
                  rank + 1, 1, MPI_COMM_WORLD, &reqs[nreqs++]);
    }

    // Отправка вниз, приём сверху
    if (rank > 0) {
        // Отправляем первые kHalfLength слоёв (после нижнего гало)
        float *send_down = &ptr_prev[kHalfLength * n1 * n2];
        MPI_Isend(send_down, kHalfLength * n1 * n2, MPI_FLOAT,
                  rank - 1, 1, MPI_COMM_WORLD, &reqs[nreqs++]);
        // Принимаем в нижнее гало
        float *recv_down = &ptr_prev[0];
        MPI_Irecv(recv_down, kHalfLength * n1 * n2, MPI_FLOAT,
                  rank - 1, 0, MPI_COMM_WORLD, &reqs[nreqs++]);
    }

    // Ждём завершения обмена
    MPI_Waitall(nreqs, reqs, MPI_STATUSES_IGNORE);

    // ── Вычисление итерации ──
    if (iter % 2 == 0)
        Iso3dfdIteration(ptr_next, ptr_prev, ptr_vel, coeff,
                         n1, n2, n3_alloc, kHalfLength,
                         kHalfLength + n3_local, ...);
    else
        Iso3dfdIteration(ptr_prev, ptr_next, ptr_vel, coeff,
                         n1, n2, n3_alloc, kHalfLength,
                         kHalfLength + n3_local, ...);
}
```

#### Логика обмена

```
Ранг 0          Ранг 1          Ранг 2
┌─────────┐    ┌─────────┐    ┌─────────┐
│ гало    │    │ гало    │    │ гало    │
│─────────│    │─────────│    │─────────│
│ данные  │──> │ данные  │──> │ данные  │
│         │    │         │    │         │
│─────────│    │─────────│    │─────────│
│ гало    │    │ гало    │    │ гало    │
└─────────┘    └─────────┘    └─────────┘
     <──          <──          <──
   снизу         снизу         снизу
```

- `send_up` — последние kHalfLength вычисляемых слоёв (перед верхним гало).
- `recv_up` — верхнее гало, заполняется данными от ранга `rank + 1`.
- `send_down` — первые kHalfLength вычисляемых слоёв (после нижнего гало).
- `recv_down` — нижнее гало, заполняется данными от ранга `rank - 1`.

---

### 4.7. Сбор результатов для вывода

**Было:**

```cpp
// Один процесс считает и выводит метрики
printf("time         : %f secs\n", time);
printf("throughput   : %f Mpts/s\n", throughput);
printf("flops        : %f GFlops\n", flops);
printf("bytes        : %f GBytes/s\n", bytes);
```

**Стало:**

```cpp
// Каждый ранг считает локальные метрики
double local_time, local_flops;

// ... вычисление локальных метрик ...

// Редукция по рангам для суммарных метрик
double total_flops;
MPI_Reduce(&local_flops, &total_flops, 1, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);

// Только ранг 0 выводит результаты
if (rank == 0) {
    printf("Grid Sizes: %d %d %d\n", n1, n2, n3);
    printf("MPI ranks:  %d\n", nprocs);
    printf("OpenMP threads per rank: %d\n", omp_get_max_threads());
    printf("time         : %f secs\n", time);
    printf("throughput   : %f Mpts/s\n", throughput);
    printf("flops        : %f GFlops\n", total_flops / 1e9);
    printf("bytes        : %f GBytes/s\n", bytes);
}
```

---

### 4.8. Финализация MPI

**Было:**

```cpp
// конец main()
return 0;
```

**Стало:**

```cpp
// конец main()
MPI_Finalize();
return 0;
```

---

## 5. Изменения в CMakeLists.txt

**Было:**

```cmake
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fiopenmp -std=c++17 -O3 -D__STRICT_ANSI__ ")

if(NOT NO_OFFLOAD)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fopenmp-targets=spir64")
endif()

# Сборка без MPI
add_executable(iso3dfd src/iso3dfd.cpp)
target_link_libraries(iso3dfd PRIVATE m)
```

**Стало:**

```cmake
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fiopenmp -std=c++17 -O3 -D__STRICT_ANSI__ ")

if(NOT NO_OFFLOAD)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fopenmp-targets=spir64")
endif()

# ── Опция MPI-рефакторинга ──
option(ENABLE_MPI "Enable MPI domain decomposition" OFF)
if(ENABLE_MPI)
    find_package(MPI REQUIRED)
    add_definitions(-DUSE_MPI)
    message(STATUS "MPI domain decomposition enabled")
endif()

# Сборка
add_executable(iso3dfd src/iso3dfd.cpp)
target_link_libraries(iso3dfd PRIVATE m)

if(ENABLE_MPI)
    target_link_libraries(iso3dfd PRIVATE MPI::MPI_CXX)
endif()

# Профилировочные флаги (опционально)
option(ENABLE_PROFILING "Add debug/profiling flags for VTune" OFF)
if(ENABLE_PROFILING)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -mllvm -parallel-source-info=2 -fdebug-info-for-profiling")
    message(STATUS "Profiling flags enabled")
endif()
```

---

## 6. Сборка и запуск

### Сборка с MPI и профилированием

```bash
source /opt/intel/oneapi/setvars.sh --force

cd iso3dfd_omp_offload
rm -rf build && mkdir build && cd build

cmake -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
      -DNO_OFFLOAD=ON \
      -DVERIFY_RESULTS=OFF \
      -DENABLE_MPI=ON \
      -DENABLE_PROFILING=ON \
      ..

make -j$(nproc)
```

### Сборка без MPI (оригинальный режим)

```bash
cmake -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
      -DNO_OFFLOAD=ON \
      -DVERIFY_RESULTS=OFF \
      ..
make -j$(nproc)
```

### Запуск с MPI

```bash
# Intel MPI
mpirun -n 12 ./src/iso3dfd 256 256 256 16 8 64 100

# OpenMPI (если используется вместо Intel MPI)
mpirun -np 12 ./src/iso3dfd 256 256 256 16 8 64 100
```

### Запуск с гибридным параллелизмом (MPI + OpenMP)

```bash
# 12 рангов, по 2 OpenMP-потока на ранг = 24 потока
OMP_NUM_THREADS=2 mpirun -n 12 ./src/iso3dfd 256 256 256 16 8 64 100

# 6 рангов, по 4 потока = 24 потока
OMP_NUM_THREADS=4 mpirun -n 6 ./src/iso3dfd 256 256 256 16 8 64 100

# Привязка к NUMA-узлам (Intel MPI)
I_MPI_PIN_DOMAIN=omp OMP_NUM_THREADS=2 mpirun -n 12 ./src/iso3dfd 256 256 256 16 8 64 100
```

### Запуск с VTune (Intel MPI)

```bash
sudo mpirun -n 12 \
    vtune -quiet -collect hotspots \
    -knob sampling-mode=sw \
    -trace-mpi \
    -result-dir vtune_hotspots_mpi \
    -- ./src/iso3dfd 256 256 256 16 8 64 100
```

### Запуск с APS

```bash
export APS_ENABLE=1
mpirun -n 12 ./src/iso3dfd 256 256 256 16 8 64 100
aps --report aps_result_YYYYMMDD_HHMMSS
```

---

## 7. Что даёт рефакторинг для профилирования

| Аспект | До рефакторинга (без MPI) | После рефакторинга (с MPI) |
|---|---|---|
| Ранги | 12 независимых копий, каждая считает полную сетку | 12 рангов, каждый считает свою подобласть + гало-обмен |
| MPI-вызовы | Нет | `MPI_Init`, `MPI_Isend`, `MPI_Irecv`, `MPI_Waitall`, `MPI_Reduce`, `MPI_Finalize` |
| VTune `-trace-mpi` | Бесполезен — нет MPI-вызовов | Показывает MPI timeline, время в коммуникациях, ожидание на барьерах |
| APS | Ничего не показывает | Показывает время в MPI, дисбаланс рангов, топ MPI-вызовов |
| Дисбаланс рангов | 0 (все делают одно и то же) | Возникает: краевые ранги (rank 0 и rank nprocs-1) обмениваются с одной стороной, внутренние — с двумя |
| Сценарий | Тривиальный — один процесс | Реальный HPC: коммуникация vs вычисление, гало-обмен, синхронизация |
| Память | Каждый ранг: n1 x n2 x n3 | Каждый ранг: n1 x n2 x (n3/nprocs + 2*kHalfLength) |
| Масштабируемость | Нет (дублирование работы) | Да: больше рангов — меньше подобласть, но больше коммуникаций |

---

## 8. Сложность и риски

| Аспект | Сложность | Комментарий |
|---|---|---|
| Декомпозиция по Z | Низкая | Линейное разбиение, один `MPI_Comm_rank` |
| Гало-обмен | Средняя | 8 слоёв в каждую сторону, `Isend/Irecv/Waitall` |
| Индексация | Средняя | Сдвиг по Z на `kHalfLength` для гало, аккуратность с `n1*n2` |
| Верификация | Средняя | Сравнение с однопроцессорной версией через `MPI_Gatherv` |
| OpenMP + MPI | Низкая | Стандартный HPC-паттерн: MPI между узлами, OpenMP внутри узла |
| Совместимость с NO_OFFLOAD | Полная | MPI работает и в CPU-only, и в GPU-offload режиме |
| Совместимость с OpenMPI | Полная | Код использует стандартные MPI-вызовы, не Intel-специфичные |

---

## 9. Совместимость с NO_OFFLOAD

MPI-рефакторинг **полностью совместим** с патчем `NO_OFFLOAD`:

| Режим | NO_OFFLOAD | ENABLE_MPI | Поведение |
|---|---|---|---|
| Оригинал (GPU) | OFF | OFF | OpenMP offload на GPU, один процесс |
| CPU-only | ON | OFF | OpenMP на CPU, один процесс |
| CPU + MPI | ON | ON | OpenMP на CPU + MPI-декомпозиция (гибридный) |
| GPU + MPI | OFF | ON | OpenMP offload на GPU + MPI-декомпозиция (требует GPU на каждом узле) |

Все три опции (`NO_OFFLOAD`, `ENABLE_MPI`, `ENABLE_PROFILING`) независимы
и могут комбинироваться в любой конфигурации.

---

## 10. Шпаргалка

```bash
# ── Сборка с MPI + профилирование ──
source /opt/intel/oneapi/setvars.sh --force
cd iso3dfd_omp_offload/build
cmake -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
      -DNO_OFFLOAD=ON \
      -DVERIFY_RESULTS=OFF \
      -DENABLE_MPI=ON \
      -DENABLE_PROFILING=ON \
      ..
make -j$(nproc)

# ── Запуск (Intel MPI, 12 рангов x 2 потока) ──
OMP_NUM_THREADS=2 mpirun -n 12 ./src/iso3dfd 256 256 256 16 8 64 100

# ── VTune с MPI-трассировкой ──
sudo mpirun -n 12 \
    vtune -quiet -collect hotspots \
    -knob sampling-mode=sw -trace-mpi \
    -result-dir vtune_hotspots_mpi \
    -- ./src/iso3dfd 256 256 256 16 8 64 100

# ── Анализ результатов VTune ──
cd src && ./analyze vtune_hotspots_mpi

# ── APS ──
export APS_ENABLE=1
mpirun -n 12 ./src/iso3dfd 256 256 256 16 8 64 100
aps --report aps_result_YYYYMMDD_HHMMSS
firefox aps_report_YYYYMMDD_HHMMSS.html
```

---

## Файлы, затрагиваемые рефакторингом

| Файл | Изменение | Объём |
|---|---|---|
| `src/iso3dfd.cpp` | Добавление `#include <mpi.h>`, `MPI_Init/Finize`, декомпозиция Z, гало-обмен, редукция метрик | ~80–120 строк |
| `src/CMakeLists.txt` | `find_package(MPI)`, опция `ENABLE_MPI`, линковка `MPI::MPI_CXX` | ~5 строк |

Вычислительное ядро (`Iso3dfdIteration`) не меняется — только параметры вызова
(локальные размеры вместо глобальных).
