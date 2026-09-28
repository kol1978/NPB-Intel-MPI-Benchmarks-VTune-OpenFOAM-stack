# ============================================================================
#                  АРХИТЕКТУРА iso3dfd: 4 РЕЖИМА × 3 БЛОКА
# ============================================================================
#
# Проект: iso3dfd4MODES3BLOCKS
# Железо: 2× Intel Xeon X5675 (Westmere, 6 ядер/сокет, 12 физических всего)
#         2 NUMA-узла, DDR3-1333, 12 МБ L3/сокет
# Компилятор: icpx (Intel oneAPI 2026.1)
# Флаги: -O3 -fiopenmp -xSSE4.2
# MPI: Intel MPI 2021.18
# OpenMP: 5.1
# Стандарт: C++14
#
# ============================================================================

## 1. Дерево проекта (фактическое)

```text
iso3dfd4MODES3BLOCKS/
├── CMakeLists.txt                        # Единственный CMake (MPI + OpenMP + icpx)
│
├── include/                              # Заголовки (публичный API)
│   ├── iso3dfd.h                         # Константы: kHalfLength=8, dxyz, dt
│   ├── iso3dfd_grid.hpp                  # GRID: Allocate, Free, Initialize, PrintStats, PrintSummary
│   ├── iso3dfd_solver.hpp                # SOLVER: compute_iteration, compute_serial, compute_omp
│   └── iso3dfd_driver.hpp                # DRIVER: run_driver (MPI + halo exchange)
│
├── src/                                  # Реализации (только .cpp)
│   ├── iso3dfd.cpp                       # MAIN: парсинг аргументов, MPI_Init, аллокация, запуск
│   ├── iso3dfd_driver.cpp                # DRIVER: MPI_Sendrecv, декомпозиция по Z, временной цикл
│   ├── iso3dfd_solver.cpp                # SOLVER: stencil_point 16-го порядка + cache-blocking
│   ├── iso3dfd_grid.cpp                  # GRID: posix_memalign, коэффициенты, импульс в центре
│   └── iso3dfd_verify.cpp                # VERIFY: проверка NaN/Inf (опционально, -DVERIFY_RESULTS=1)
│
└── build/                                # Артефакты сборки (не в git)
    ├── CMakeCache.txt
    ├── Makefile
    └── iso3dfd                           # Готовый бинарник
```

### Примечания к структуре

- **Единый CMakeLists.txt** — без `src/CMakeLists.txt` (избыточно для одного бинарника).
- **Заголовки в `include/`** — соответствие конвенции Intel oneAPI-samples: `.hpp` в `include/`, `.cpp` в `src/`.
- **Бинарник в `build/iso3dfd`** — CMake кладёт исполняемый файл в корень build, а не в `build/src/`, потому что используется единый CMake без `add_subdirectory(src)`.
- **Нет файлов Intel-оригинала** — `License.txt`, `sample.json`, `runIso3dfd`, `apsIso3dfd` и т.д. могут быть добавлены позже при подготовке к публикации в репозитории.


## 2. Сборка и запуск

### Сборка

```bash
source /opt/intel/oneapi/setvars.sh
rm -rf build
cmake -S . -B build -DVERIFY_RESULTS=0
cmake --build build -j$(nproc)
```

### Аргументы командной строки

```
./iso3dfd n1 n2 n3 b1 b2 b3 niter [mode]
```

| Параметр | Описание | Пример |
|----------|----------|--------|
| n1, n2, n3 | Размеры сетки (n3 делится по Z между рангами) | 256 256 256 |
| b1, b2, b3 | Размеры блока cache-blocking | 16 8 64 |
| niter | Количество итераций по времени | 100 |
| mode | Режим выполнения (8-й аргумент) | hybrid |

### Режимы

| Режим | MPI рангов | OMP потоков | Команда |
|-------|-----------|-------------|---------|
| `sequential` | 1 | 1 | `./build/iso3dfd 256 256 256 16 8 64 100 sequential` |
| `pure_mpi` | N | 1 | `mpirun -n 12 ./build/iso3dfd 256 256 960 16 8 80 100 pure_mpi` |
| `hybrid` | M | T | `OMP_NUM_THREADS=6 mpirun -n 2 ./build/iso3dfd 256 256 256 16 8 128 100 hybrid` |
| `pure_omp` | 1 | N | `OMP_NUM_THREADS=12 ./build/iso3dfd 256 256 256 16 8 64 100 pure_omp` |

### Важное ограничение

**n3 должно делиться на количество MPI-рангов** (только для `pure_mpi` и `hybrid`). Иначе — ошибка:
```
Error: n3 (256) must be divisible by nprocs (12)
```
Рекомендуемые n3 для 12 рангов: 240, 480, 960. Для 2 рангов: 256, 512.


## 3. Блоки (слои кода)

### БЛОК 1: GRID (Сетка)

**Файлы:** `iso3dfd_grid.hpp` / `iso3dfd_grid.cpp`

**Ответственность:**
- Аллокация 3D-массивов с выравниванием 64 байта (`posix_memalign`)
- First-touch NUMA-политика (инициализация нулями на домене вызывающего потока)
- Инициализация данных: волновой импульс в центре, поле скоростей, коэффициенты стенсиля
- Хранение размеров (n1, n2, n3 + halo kHalfLength с каждой стороны)
- Доступ по линейному индексу: `ptr[k * nx * ny + j * nx + i]`
- Освобождение памяти
- Верификация результата (NaN/Inf — при `VERIFY_RESULTS=1`)

**НЕ знает про:** MPI, OpenMP, режимы, стенсиль. Просто данные.

---

### БЛОК 2: SOLVER (Решатель — вычислительное ядро)

**Файлы:** `iso3dfd_solver.hpp` / `iso3dfd_solver.cpp`

**Ответственность:**
- Вычисление одной точки стенсиля 16-го порядка (25 точек, 9 коэффициентов)
- Одна итерация: чтение `prev` -> запись `next`
- Переключение: `compute_serial` (1 поток) или `compute_omp` (OpenMP)
- Cache-blocking: блоки (b1, b2, b3) переиспользуют данные в L2/L3
- Обработка внутренних границ домена (kHalfLength отступ от краёв)

**НЕ знает про:** MPI, декомпозицию домена, halo exchange. Работает с локальным куском Grid, переданным Driver-ом.

**Два пути вычисления:**
- `compute_serial()` — линейный обход, hardware prefetcher эффективен
- `compute_omp()` — `#pragma omp parallel for collapse(2)` по Z и Y, двухуровневое блокирование

---

### БЛОК 3: DRIVER (Делитель/Драйвер — оркестрация)

**Файлы:** `iso3dfd_driver.hpp` / `iso3dfd_driver.cpp`

**Ответственность:**
- Декомпозиция домена: разрез по Z (n3 / nprocs слоёв на ранг)
- Halo exchange: `MPI_Sendrecv` между соседями по Z
- Временной цикл: niter итераций
- Передача управления в Solver на каждой итерации
- Смена буферов prev <-> next (обмен указателей, без копирования)
- Тайминг: wall time, GFLOPS, lattice points/iteration

**Знаёт про:** MPI (через `MPI_Sendrecv`), OpenMP (через флаг `use_omp`), Grid (через указатели на буферы).

**Поведение по режимам:**
- `sequential` / `pure_omp` — nprocs=1, нет MPI, нет halo exchange
- `pure_mpi` — nprocs=N, `use_omp=false`, `MPI_Sendrecv` на каждой итерации
- `hybrid` — nprocs=M, `use_omp=true`, `MPI_Sendrecv` + OpenMP внутри


## 4. Матрица: 4 режима × 3 блока

| Режим | GRID | SOLVER | DRIVER |
|-------|------|--------|--------|
| **SEQUENTIAL** (1 proc, 1 thread) | Полная сетка n1×n2×n3 | `compute_serial` (no OMP) | Нет MPI, нет декомпозиции, 1 ранг, 1 поток |
| **PURE_MPI** (N procs, 1 thread) | Кусок n1×n2×(n3/N) + halo | `compute_serial` (no OMP) | `MPI_Sendrecv`, декомпозиция по Z, N рангов, 1 поток |
| **HYBRID** (M procs, T threads) | Кусок n1×n2×(n3/M) + halo | `compute_omp` (parallel for) | `MPI_Sendrecv`, декомпозиция по Z, M рангов, T потоков |
| **PURE_OMP** (1 proc, N threads) | Полная сетка n1×n2×n3 | `compute_omp` (parallel for) | Нет MPI, нет декомпозиции, 1 ранг, N потоков |


## 5. Уровни параллелизма (снизу вверх)

| Уровень | Механизм | Режимы |
|---------|----------|--------|
| 1. SIMD/Векторный | Авто-векторизация icpx (-xSSE4.2) | Все режимы |
| 2. Многопоточный | OpenMP (`#pragma omp parallel for`) | HYBRID, PURE_OMP |
| 3. Внутриузловой | MPI между рангами на одном сокете | PURE_MPI, HYBRID |
| 4. Межузловой | MPI между серверами | PURE_MPI, HYBRID |

```
SEQUENTIAL: уровень 1 (авто-векторизация)
PURE_MPI:   уровни 1 + 3/4 (SIMD + MPI)
HYBRID:     уровни 1 + 2 + 3/4 (SIMD + OMP + MPI)
PURE_OMP:   уровни 1 + 2 (SIMD + OMP)
```


## 6. Поток данных в одной итерации

```
  DRIVER                         SOLVER                      GRID
  -------                        ------                      ----

  +------------------+
  | 1. Halo exchange |------------------------------------>  prev (halo updated)
  |  (MPI_Sendrecv)  |
  +--------+---------+
           |
           v
  +------------------+    +----------------------+
  | 2. Call Solver   |--->| compute_iteration()  |-->  read prev[i+-8,j+-8,k+-8]
  |  (mode-aware)    |    |  serial or OMP       |      write next[i,j,k]
  +--------+---------+    +----------+-----------+
           |                         |
           v                         v
  +------------------+                       prev <--> next (swap pointers)
  | 3. Swap prev/next|--------------------------------->  pointer swap
  +--------+---------+
           |
           v
  +------------------+
  | 4. Timing/Output |
  +------------------+
```

### Описание шагов

1. **Halo exchange** — Driver отправляет/принимает kHalfLength=8 слоёв по Z соседним рангам через `MPI_Sendrecv`. В режимах `sequential` и `pure_omp` (nprocs=1) этот шаг пропускается.

2. **Call Solver** — Driver вызывает `compute_iteration(prev, next, nx, ny, nz, coeff, use_omp)`. Параметр `use_omp` определяется режимом: `false` для sequential/pure_mpi, `true` для hybrid/pure_omp.

3. **Swap** — обмен указателей `prev <-> next` без копирования данных. Выполняется в Driver, но фактически затрагивает только Grid-буферы.

4. **Timing** — на каждой итерации (или после последней) Driver фиксирует время и считает GFLOPS.


## 7. Карта взаимодействия модулей

```
MAIN (iso3dfd.cpp)
  +-- Парсинг аргументов: n1 n2 n3 b1 b2 b3 niter [mode]
  +-- MPI_Init / MPI_Comm_rank / MPI_Comm_size
  +-- Allocate (GRID)
  +-- Initialize (GRID)
  +-- run_driver() (DRIVER)
        |
        +-- exchange_halo() -> MPI_Sendrecv
        |
        +-- Цикл по итерациям (niter)
        |   +-- exchange_halo()
        |   +-- compute_iteration()  --> SOLVER
        |   |   +-- use_omp=false -> compute_serial()
        |   |   +-- use_omp=true  -> compute_omp()
        |   |       +-- L3-блок: (b1, b2, b3) - общий для потоков
        |   |       +-- L2-блок: подблок внутри L3 - по одному на поток
        |   +-- swap(prev, next)      --> GRID
        |
        +-- PrintSummary()  --> GRID
```


## 8. Экспериментальные результаты (справочно)

### Сводная таблица (все запуски)

| # | Режим | Рангов | Потоков | Домен | Время | GFLOPS (всего) | Speedup vs seq |
|---|-------|--------|---------|-------|-------|----------------|----------------|
| 0 | sequential | 1 | 1 | 256^3 | 44.9 с | 1.94 | 1.0x |
| 1 | pure_mpi 12 | 12 | 1 | 256x256x480 | 11.2 с | 9.36 | 4.8x |
| 2 | pure_mpi 12 | 12 | 1 | 256x256x960 | 25.4 с | 10.92 | 5.6x |
| 3 | pure_mpi 2 | 2 | 1 | 256x256x256 | 22.4 с | 3.64 | 1.9x |
| 4 | hybrid 2x6 | 2 | 12 | 256x256x256 | 4.0 с | 20.32 | 10.5x |
| 5 | hybrid 2x6 | 2 | 12 | 512x512x256 | 22.7 с | 15.28 | - |
| 6 | pure_omp 12 | 1 | 12 | 256^3 | 4.4 с | 19.62 | 10.1x |

### Главные выводы

1. **Hybrid 2x6 — оптимальный режим**: 20.32 GFLOPS, упирается в потолок пропускной способности DDR3 (~64 ГБ/с -> ~21 GFLOPS для stencil 16-го порядка).
2. **Pure OMP 12 — почти идентичен hybrid**: 19.62 vs 20.32 GFLOPS (разница 3.5%). Hybrid чуть быстрее за счёт NUMA-локальности.
3. **Pure MPI плохо масштабируется на 12 рангов**: 10.92 GFLOPS — накладные расходы на halo exchange и тонкие домены на ранг.
4. **Больший домен (512^3) снижает throughput**: 15.28 vs 20.32 GFLOPS — насыщение памяти, данные не помещаются в L3.


## 9. Рекомендации по дальнейшей оптимизации

| Направление | Что делать | Ожидаемый эффект |
|-------------|-----------|------------------|
| NUMA-привязка | `numactl --cpunodebind=0 --membind=0` для каждого ранга | Устранение удалённого доступа к памяти через QPI |
| MPI-пиннинг | `I_MPI_PIN_DOMAIN=omp` для hybrid | Привязка рангов к сокетам, потоков — к ядрам |
| Huge pages | `echo always > /sys/kernel/mm/transparent_hugepage/enabled` | Снижение TLB-miss для больших доменов |
| Подбор блоков | Варьировать (b1, b2, b3): 16x8x64, 32x16x64, 8x8x64 | Оптимальное заполнение L2/L3 |
| Векторизация | Проверить через `-qopt-report=5` | Убедиться, что внутренний цикл по X векторизован |
| L3-блок | Подобрать b3 так, чтобы блок помещался в L3 (12 МБ/сокет) | Минимизация L3-miss |
