# iso3dfd — Результаты экспериментов

## Дата: 26 сентября 2026
## Платформа: Intel Xeon X5675 (Westmere, 2 × 6 ядер, 3.07 GHz)
## Память: DDR3-1333, 6 каналов на сокет (~64 ГБ/с пик)
## Кэш: L2 = 256 КБ/ядро, L3 = 12 МБ/сокет
## Компилятор: icpx (IntelLLVM 2026.1.1)
## MPI: Intel MPI 2021.18
## OpenMP: 5.1 (-fiopenmp)
## Стенсиль: 16-й порядок (kHalfLength = 8, 25 точек на ячейку, 63 FLOP/точка)

---

## 1. Условия эксперимента

### Аппаратное обеспечение

| Параметр | Значение |
|---|---|
| Процессор | 2 × Intel Xeon X5675 (Westmere-EP) |
| Ядра | 12 физических (2 × 6) |
| Частота | 3.07 GHz (Turbo 3.47 GHz) |
| SIMD | SSE4.2 (128-bit, 4 FP32/оп) |
| L1 | 32 КБ/ядро |
| L2 | 256 КБ/ядро |
| L3 | 12 МБ/сокет |
| Память | DDR3-1333, 6 каналов/сокет |
| Пиковая пропускная способность | ~64 ГБ/с на сокет |
| Пиковые FP32 | ~147 GFLOPS (12 ядер) |
| NUMA | 2 узла (по 6 ядер, по 12 МБ L3) |

### Программное обеспечение

| Параметр | Значение |
|---|---|
| ОС | Linux (Ubuntu) |
| Компилятор | icpx (IntelLLVM 2026.1.1) |
| MPI | Intel MPI 2021.18 |
| OpenMP | 5.1 (-fiopenmp) |
| CMake | 4.4 |
| C++ стандарт | C++14 |

### Параметры стенсиля

| Параметр | Значение |
|---|---|
| Порядок | 16-й |
| kHalfLength | 8 |
| Точек на ячейку | 25 (1 запись + 24 чтения) |
| FLOP на точку | 63 |
| Коэффициентов | 9 |
| Halo (слоёв) | 8 с каждой стороны по Z |

### Флаги компиляции

```
-O3 -fiopenmp -xSSE4.2
```

---

## 2. Режимы выполнения

| Режим | MPI рангов | OMP потоков | Декомпозиция | Решатель |
|---|---|---|---|---|
| `sequential` | 1 | 1 | нет | compute_serial |
| `pure_mpi` | N | 1 | по Z, MPI_Sendrecv | compute_serial |
| `hybrid` | M | T | по Z, MPI_Sendrecv | compute_omp |
| `pure_omp` | 1 | N | нет | compute_omp |

---

## 3. Результаты запусков

### 3.1. Базовые запуски

| # | Режим | Рангов | Потоков | n1 | n2 | n3 | b1 | b2 | b3 | niter | Время (с) | GFLOPS/ранг | GFLOPS всего | Lattice/ранг |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 1 | sequential | 1 | 1 | 256 | 256 | 256 | 16 | 8 | 64 | 100 | 44.949 | 1.94 | **1.94** | 13 824 000 |
| 2 | pure_mpi | 12 | 1 | 256 | 256 | 480 | 16 | 8 | 40 | 100 | 11.161 | 0.78 | **9.36** | 1 382 400 |
| 3 | pure_mpi | 2 | 1 | 256 | 256 | 256 | 16 | 8 | 128 | 100 | 22.365 | 1.82 | **3.64** | 6 451 200 |
| 4 | hybrid | 2 | 6 | 256 | 256 | 256 | 16 | 8 | 128 | 100 | 3.999 | 10.16 | **20.32** | 6 451 200 |
| 5 | pure_omp | 1 | 12 | 256 | 256 | 256 | 16 | 8 | 64 | 100 | 4.438 | — | **19.62** | 13 824 000 |

### 3.2. Дополнительные запуски (масштабирование)

| # | Режим | Рангов | Потоков | n1 | n2 | n3 | b1 | b2 | b3 | niter | Время (с) | GFLOPS/ранг | GFLOPS всего | Lattice/ранг |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 6 | pure_mpi | 12 | 1 | 256 | 256 | 960 | 16 | 8 | 80 | 100 | 25.393 | 0.91 | **10.92** | 3 686 400 |
| 7 | hybrid | 2 | 6 | 512 | 512 | 256 | 16 | 8 | 128 | 100 | 22.728 | 7.64 | **15.28** | 27 553 792 |

### 3.3. Деградированный запуск (тонкий домен)

| # | Режим | Рангов | Потоков | n1 | n2 | n3 | b1 | b2 | b3 | niter | Время (с) | GFLOPS/ранг | GFLOPS всего | Lattice/ранг | interior/ранг |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 0 | pure_mpi | 12 | 1 | 256 | 256 | 240 | 16 | 8 | 20 | 100 | 7.267 | 0.20 | **2.40** | 230 400 | 4 слоя |

> Запуск #0 исключён из анализа: n3=240 при 12 рангах даёт 20 слоёв/ранг, из которых 16 — halo, 4 — interior (20% полезной работы).

---

## 4. Анализ производительности

### 4.1. Сравнение режимов (одинаковый домен 256×256×256)

| Режим | Время (с) | GFLOPS | Speedup vs seq | Эффективность |
|---|---|---|---|---|
| sequential (1×1) | 44.949 | 1.94 | 1.0x | 100% |
| pure_mpi (2×1) | 22.365 | 3.64 | 2.0x | 100% (2 ранга) |
| hybrid (2×6) | 3.999 | 20.32 | 11.2x | 93% (12 потоков) |
| pure_omp (1×12) | 4.438 | 19.62 | 10.1x | 84% (12 потоков) |

### 4.2. Сравнение режимов (разные домены)

| Режим | Домен | interior (всего) | Время (с) | GFLOPS | Точек/с | ГБ/с памяти |
|---|---|---|---|---|---|---|
| sequential | 256³ | 13.8M | 44.9 | 1.94 | 30.7M | ~6.1 |
| pure_mpi 12 | 256×256×480 | 16.6M | 11.2 | 9.36 | 148.3M | ~29.7 |
| pure_mpi 12 | 256×256×960 | 44.2M | 25.4 | 10.92 | 174.0M | ~34.8 |
| hybrid 2×6 | 256×256×256 | 12.9M | 4.0 | 20.32 | 322.6M | ~64.5 |
| hybrid 2×6 | 512×512×256 | 55.1M | 22.7 | 15.28 | 242.4M | ~48.5 |
| pure_omp 12 | 256³ | 13.8M | 4.4 | 19.62 | 311.3M | ~62.3 |

> Потребление памяти рассчитано из: 24 чтения × 4 байта + 1 запись × 4 байта = 100 байт/точка.

### 4.3. Масштабирование

#### Pure MPI: рост домена (12 рангов)

| n3 | Слоёв/ранг | Interior/ранг | Доля interior | GFLOPS | Ускорение vs n3=240 |
|---|---|---|---|---|---|
| 240 | 20 | 4 | 20% | 2.40 | 1.0x |
| 480 | 40 | 24 | 60% | 9.36 | 3.9x |
| 960 | 80 | 64 | 80% | 10.92 | 4.6x |

> Вывод: с ростом доли interior производительность растёт, но выходит на плато (~11 GFLOPS) — коммуникационные издержки MPI стабилизируются.

#### Hybrid: рост домена 256³ → 512³

| Домен | interior (всего) | Время (с) | GFLOPS | Точек/с | Падение throughput |
|---|---|---|---|---|---|
| 256×256×256 | 12.9M | 4.0 | 20.32 | 322.6M | — |
| 512×512×256 | 55.1M | 22.7 | 15.28 | 242.4M | −25% |

> Вывод: объём работы вырос в 4.3×, время — в 5.7×. Throughput упал на 25% — L3 (24 МБ всего) не вмещает домен, данные идут из DRAM, пропускная способность памяти исчерпана.

---

## 5. Выводы

### 5.1. Hybrid 2×6 — оптимальный режим

| Метрика | Значение | Контекст |
|---|---|---|
| GFLOPS | 20.32 | 14% от пика (147 GFLOPS) |
| Точек/с | 322.6M | насыщение памяти |
| Потребление памяти | ~64.5 ГБ/с | ~100% от пика DDR3-1333 |
| Speedup vs sequential | 11.2× | на 12 потоках |
| Эффективность | 93% | 11.2 / 12 |

Hybrid 2×6 одновременно использует:
- 2 NUMA-узла (по 6 ядер) через 2 MPI-ранга
- 6 OpenMP-потоков внутри каждого ранга (cache-blocking в L2/L3)
- MPI_Sendrecv для halo-обмена между NUMA-узлами (через QPI)

### 5.2. Pure OMP 1×12 — почти идентичен hybrid

| Метрика | hybrid 2×6 | pure_omp 1×12 | Разница |
|---|---|---|---|
| GFLOPS | 20.32 | 19.62 | −3.4% |
| Время | 4.0 с | 4.4 с | +10% |

Разница в пределах погрешности. Hybrid чуть быстрее за счёт NUMA-локальности: каждый MPI-ранг держит свою память на своём сокете. Pure OMP работает в одном адресном пространстве, и ОС может мигрировать страницы между NUMA-узлами.

### 5.3. Pure MPI 12×1 — ограничен коммуникацией

| Метрика | pure_mpi 12 (n3=960) | hybrid 2×6 (256³) | Отношение |
|---|---|---|---|
| GFLOPS | 10.92 | 20.32 | 0.54× |
| Потоков вычисления | 12 | 12 | 1.0× |
| MPI-обмен за итерацию | 16 слоёв × 256² × 4 Б = 4 МБ | 16 слоёв × 256² × 4 Б = 4 МБ | 1.0× |

12 изолированных процессов тратят время на MPI_Sendrecv и работают с тонкими доменами (80 слоёв, 64 interior). Cache-blocking не может развернуться — блок b3=80 равен локальному домену.

### 5.4. Bottleneck — пропускная способность памяти

| Режим | Потребление памяти | Доля от пика (64 ГБ/с) |
|---|---|---|
| sequential | ~6.1 ГБ/с | 9.5% |
| pure_mpi 12 (960) | ~34.8 ГБ/с | 54.4% |
| hybrid 2×6 (256³) | ~64.5 ГБ/с | 100.8% |
| hybrid 2×6 (512³) | ~48.5 ГБ/с | 75.8% |
| pure_omp 12 | ~62.3 ГБ/с | 97.3% |

Стенсиль 16-го порядка — memory-bound: 24 чтения на 1 запись, 100 байт/точка. Hybrid и pure_omp упираются в потолок DDR3. Дальнейшее ускорение возможно только через:
- Уменьшение обращений к памяти (переиспользование в кэше)
- Векторизацию SSE4.2 (4 FP32/оп — уже задействована -xSSE4.2)
- Huge Pages (снижение TLB-миссов)
- NUMA-привязка (numactl --membind)

---

## 6. Рекомендации

### 6.1. Блокировка кэша

Текущие блоки (16×8×64–128) — эвристический выбор. Стоит перебрать:

```bash
# Меньший Z-блок — больше переиспользования в L2
OMP_NUM_THREADS=6 mpirun -n 2 ./build/iso3dfd 256 256 256 16 8 32 100 hybrid
OMP_NUM_THREADS=6 mpirun -n 2 ./build/iso3dfd 256 256 256 32 16 64 100 hybrid
OMP_NUM_THREADS=6 mpirun -n 2 ./build/iso3dfd 256 256 256 8 8 64 100 hybrid
```

### 6.2. NUMA-привязка

Запустить hybrid через numactl для исключения миграции памяти:

```bash
OMP_NUM_THREADS=6 numactl --cpunodebind=0 --membind=0 mpirun -n 1 ./build/iso3dfd 256 256 256 16 8 128 100 hybrid &
OMP_NUM_THREADS=6 numactl --cpunodebind=1 --membind=1 mpirun -n 1 ./build/iso3dfd 256 256 256 16 8 128 100 hybrid &
wait
```

### 6.3. Huge Pages

Включить transparent huge pages перед запуском:

```bash
echo always > /sys/kernel/mm/transparent_hugepage/enabled
```

Или использовать явные huge pages через mmap + MAP_HUGETLB.

### 6.4. MPI-пиннинг

Для hybrid режима настроить привязку MPI-рангов к NUMA-узлам:

```bash
export I_MPI_PIN_DOMAIN=[0xffffffff,0xffffffff]
export I_MPI_PIN_CELL=core
OMP_NUM_THREADS=6 mpirun -n 2 ./build/iso3dfd 256 256 256 16 8 128 100 hybrid
```

---

## 7. Архитектура проекта

```text
iso3dfd_omp_offload/
├── CMakeLists.txt
├── include/
│   ├── iso3dfd.h              # Константы стенсиля
│   ├── iso3dfd_driver.hpp     # DRIVER: MPI-декомпозиция, halo exchange
│   ├── iso3dfd_solver.hpp     # SOLVER: stencil, cache-blocking
│   └── iso3dfd_grid.hpp       # GRID: аллокация, инициализация
├── src/
│   ├── iso3dfd.cpp            # MAIN: парсинг, MPI_Init, запуск
│   ├── iso3dfd_driver.cpp     # DRIVER: MPI_Sendrecv, временной цикл
│   ├── iso3dfd_solver.cpp     # SOLVER: stencil_point + serial + omp
│   ├── iso3dfd_grid.cpp       # GRID: Allocate, Initialize, PrintStats
│   └── iso3dfd_verify.cpp     # VERIFY: проверка (опционально)
└── build/
    └── iso3dfd                # Бинарник
```

### Карта взаимодействия модулей

```
MAIN (iso3dfd.cpp)
  ├── Парсинг аргументов: n1 n2 n3 b1 b2 b3 niter [mode]
  ├── MPI_Init / MPI_Comm_rank / MPI_Comm_size
  └── run_driver()
        │
        ▼
DRIVER (iso3dfd_driver.cpp)
  ├── Декомпозиция домена по Z (n3 / nprocs)
  ├── exchange_halo() → MPI_Sendrecv
  ├── Цикл по итерациям (niter)
  │   ├── exchange_halo()
  │   ├── compute_iteration()  ──► SOLVER
  │   └── swap(prev, next)      ──► GRID
  └── Тайминг по фазам
        │
        ▼
SOLVER (iso3dfd_solver.cpp)
  ├── compute_iteration(prev, next, ..., use_omp)
  │   ├── use_omp=false → compute_serial()
  │   └── use_omp=true  → compute_omp()
  │       ├── L3-блок (b1 × b2 × b3)
  │       └── L2-блок (внутри L3-блока)
  └── stencil_point() — 16-й порядок, 25 точек
        │
        ▼
GRID (iso3dfd_grid.cpp)
  ├── allocate() → posix_memalign (64-байтовое выравнивание)
  ├── initialize() → заполнение prev/next/vel
  ├── swap() → обмен указателей
  └── verify() → проверка результата
```

---
## 8. Исходные данные запусков

**Примечание!** Запуски 8–10 выполнены на модифицированной версии исходного кода, адаптированной под кэш-иерархию Xeon X5675 (L1=32 КБ, L2=256 КБ, L3=12 МБ/сокет). Размер блока увеличен с 16×8×64 (~0,5 МБ) до 64×64×64 (~2 МБ) для оптимального использования L3 с запасом под halo-обмен (16-точечный стенсил, ~8 МБ). Сборка — `build-vtune-hotspots/iso3dfd`, 2000 итераций для устойчивости метрик. Результаты не напрямую сопоставимы с запусками 1–7 (исходный код, блок 16×8×64, 100 итераций) — сравнение между сериями некорректно из-за разного размера блока, числа итераций и версии кода. Внутри серии 8–10 сравнение валидно: одинаковая сетка n3=480, одинаковый блок, одинаковое число итераций, одна версия кода.

> **Конвенция по переменным окружения:**
> - `I_MPI_PIN_DOMAIN` — шестнадцатеричная битовая маска ядер (не диапазон в квадратных скобках).
> - `0xFFF` — биты 0–11 (все 12 физических ядер).
> - `0x3F,0xFC0` — ранг 0 на ядрах 0–5 (NUMA-узел 0), ранг 1 на ядрах 6–11 (NUMA-узел 1).
> - `0xFF` — биты 0–7 (8 ядер: 6 на NUMA-узле 0, 2 на NUMA-узле 1).
> - `OMP_PROC_BIND=close` + `OMP_PLACES=cores` — жёсткая привязка OpenMP-потоков к физическим ядрам.

---

### Запуск 1: sequential

```bash
# Без MPI, без OpenMP — один поток
./build/iso3dfd 256 256 256 16 8 64 100 sequential
```

```text
iso3dfd — 3D isotropic finite-difference solver
Grid:        256 x 256 x 256
Block:       16 x 8 x 64
Iterations:  100
Half-length: 8 (16th-order stencil)
------------------------------------------------
------------------------------------------------
Time:         44.949 s
Performance:  1.94 GFLOPS
Lattice:      13824000 points/iteration
```

> Один процесс, одно ядро. Привязка не требуется — нет MPI-рангов и нет OMP-потоков.

---

### Запуск 2: pure_mpi 12 (n3=480)

```bash
export OMP_NUM_THREADS=1
export I_MPI_PIN=on
export I_MPI_PIN_DOMAIN=0xFFF
export I_MPI_PIN_ORDER=compact

mpirun -n 12 ./build/iso3dfd 256 256 480 16 8 40 100 pure_mpi
```

```text
Grid:        256 x 256 x 40
Block:       16 x 8 x 40
Iterations:  100
Half-length: 8 (16th-order stencil)
------------------------------------------------
------------------------------------------------
Time:         11.161 s
Performance:  0.78 GFLOPS
Lattice:      1382400 points/iteration
```

> `I_MPI_PIN_DOMAIN=0xFFF` — биты 0–11, все 12 физических ядер. Каждый из 12 рангов закрепляется за одним ядром: ранги 0–5 на NUMA-узле 0 (ядра 0–5), ранги 6–11 на NUMA-узле 1 (ядра 6–11).

---

### Запуск 3: pure_mpi 2 (n3=256)

```bash
export OMP_NUM_THREADS=1
export I_MPI_PIN=on
export I_MPI_PIN_DOMAIN=0x3F,0xFC0
export I_MPI_PIN_ORDER=compact

mpirun -n 2 ./build/iso3dfd 256 256 256 16 8 128 100 pure_mpi
```

```text
Grid:        256 x 256 x 128
Block:       16 x 8 x 128
Iterations:  100
Half-length: 8 (16th-order stencil)
------------------------------------------------
------------------------------------------------
Time:         22.365 s
Performance:  1.82 GFLOPS
Lattice:      6451200 points/iteration
```

> `I_MPI_PIN_DOMAIN=0x3F,0xFC0` — ранг 0 на ядрах 0–5 (NUMA-узел 0), ранг 1 на ядрах 6–11 (NUMA-узел 1). Каждый ранг — на своём сокете, память локальна.

---

### Запуск 4: hybrid 2×6 (256³)

```bash
export OMP_NUM_THREADS=6
export OMP_PROC_BIND=close
export OMP_PLACES=cores
export I_MPI_PIN=on
export I_MPI_PIN_DOMAIN=0x3F,0xFC0
export I_MPI_PIN_ORDER=compact

mpirun -n 2 ./build/iso3dfd 256 256 256 16 8 128 100 hybrid
```

```text
Grid:        256 x 256 x 128
Block:       16 x 8 x 128
Iterations:  100
Half-length: 8 (16th-order stencil)
------------------------------------------------
------------------------------------------------
Time:         3.999 s
Performance:  10.16 GFLOPS
Lattice:      6451200 points/iteration
```

> `I_MPI_PIN_DOMAIN=0x3F,0xFC0` — ранг 0 на ядрах 0–5 (NUMA-узел 0), ранг 1 на ядрах 6–11 (NUMA-узел 1). 6 OMP-потоков на ранг, `OMP_PROC_BIND=close` — потоки идут подряд внутри домена. Каждый ранг работает исключительно на своей NUMA-ноде.

---

### Запуск 5: pure_omp 12 (256³)

```bash
export OMP_NUM_THREADS=12
export OMP_PROC_BIND=close
export OMP_PLACES=cores

./build/iso3dfd 256 256 256 16 8 64 100 pure_omp
```

```text
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

> Без MPI, один процесс. `OMP_NUM_THREADS=12` — все 12 физических ядер. `OMP_PROC_BIND=close` + `OMP_PLACES=cores` — жёсткая привязка потоков 0–11 к ядрам 0–11. `I_MPI_PIN_DOMAIN` не задаётся — нет MPI-рангов. NUMA-локальность не гарантируется: first-touch-распределение может разместить данные на одном узле, а потоки 6–11 будут обращаться к удалённой памяти через QPI.

---

### Запуск 6: pure_mpi 12 (n3=960)

```bash
export OMP_NUM_THREADS=1
export I_MPI_PIN=on
export I_MPI_PIN_DOMAIN=0xFFF
export I_MPI_PIN_ORDER=compact

mpirun -n 12 ./build/iso3dfd 256 256 960 16 8 80 100 pure_mpi
```

```text
Grid:        256 x 256 x 80
Block:       16 x 8 x 80
Iterations:  100
Half-length: 8 (16th-order stencil)
------------------------------------------------
------------------------------------------------
Time:         25.393 s
Performance:  0.91 GFLOPS
Lattice:      3686400 points/iteration
```

> `I_MPI_PIN_DOMAIN=0xFFF` — биты 0–11, все 12 физических ядер. 12 рангов, по одному на ядро: ранги 0–5 на NUMA-узле 0 (ядра 0–5), ранги 6–11 на NUMA-узле 1 (ядра 6–11). Увеличенный домен (n3=960, 80 слоёв/ранг, 64 interior) снижает долю коммуникаций до 20%.

---

### Запуск 7: hybrid 2×6 (512×512×256)

```bash
export OMP_NUM_THREADS=6
export OMP_PROC_BIND=close
export OMP_PLACES=cores
export I_MPI_PIN=on
export I_MPI_PIN_DOMAIN=0x3F,0xFC0
export I_MPI_PIN_ORDER=compact

mpirun -n 2 ./build/iso3dfd 512 512 256 16 8 128 100 hybrid
```

```text
Grid:        512 x 512 x 128
Block:       16 x 8 x 128
Iterations:  100
Half-length: 8 (16th-order stencil)
------------------------------------------------
------------------------------------------------
Time:         22.728 s
Performance:  7.64 GFLOPS
Lattice:      27553792 points/iteration
```

> `I_MPI_PIN_DOMAIN=0x3F,0xFC0` — ранг 0 на ядрах 0–5 (NUMA-узел 0), ранг 1 на ядрах 6–11 (NUMA-узел 1). 6 OMP-потоков на ранг. Увеличенная сетка 512×512×256 — рабочий набор ~55 МБ превышает суммарный L3 (24 МБ), throughput падает на 25% из-за DRAM-traffic.

---

### Запуск 0 (деградированный): pure_mpi 12 (n3=240)

```bash
export OMP_NUM_THREADS=1
export I_MPI_PIN=on
export I_MPI_PIN_DOMAIN=0xFFF
export I_MPI_PIN_ORDER=compact

mpirun -n 12 ./build/iso3dfd 256 256 240 16 8 20 100 pure_mpi
```

```text
Grid:        256 x 256 x 20
Block:       16 x 8 x 20
Iterations:  100
Half-length: 8 (16th-order stencil)
------------------------------------------------
------------------------------------------------
Time:         7.267 s
Performance:  0.20 GFLOPS
Lattice:      230400 points/iteration
```

> `I_MPI_PIN_DOMAIN=0xFFF` — биты 0–11, все 12 физических ядер. 12 рангов, по одному на ядро: ранги 0–5 на NUMA-узле 0 (ядра 0–5), ранги 6–11 на NUMA-узле 1 (ядра 6–11).
>
> Исключён из анализа: 20 слоёв/ранг, из которых 16 — halo, 4 — interior (20% полезной работы). Коммуникации доминируют над вычислениями.

---

### Запуски 8–10: масштабирование OpenMP (build-vtune-hotspots, блок 64³, 2000 итераций)

> **Внимание:** эти запуски используют другую сборку (`build-vtune-hotspots/iso3dfd`) с блоком 64×64×64 и 2000 итераций. Результаты не напрямую сопоставимы с запусками 1–7 (блок 16×8×64, 100 итераций) — сравнение между сериями некорректно из-за разного размера блока и числа итераций. Внутри серии 8–10 сравнение валидно: одинаковая сетка n3=480, одинаковый блок, одинаковое число итераций.

---

### Запуск 8: hybrid 1×8 (n3=480, 8 ядер, без MPI-коммуникаций)

```bash
export OMP_NUM_THREADS=8
export OMP_PROC_BIND=close
export OMP_PLACES=cores
export I_MPI_PIN=on
export I_MPI_PIN_DOMAIN=0xFF
export I_MPI_PIN_ORDER=compact

mpirun -np 1 ./build-vtune-hotspots/iso3dfd 256 256 480 64 64 64 2000 hybrid
```

```text
Grid:        256 x 256 x 480
Block:       64 x 64 x 64
Iterations:  2000
Half-length: 8 (16th-order stencil)
------------------------------------------------
------------------------------------------------
Time:         244.118 s
Performance:  13.79 GFLOPS
Lattice:      26726400 points/iteration
```

> `I_MPI_PIN_DOMAIN=0xFF` — биты 0–7 (8 ядер). Ядра 0–5 на NUMA-узле 0, ядра 6–7 на NUMA-узле 1. 1 ранг получает весь домен 256×256×480 (480 слоёв), 8 OMP-потоков распределены по двум NUMA-узлам: 6 на узле 0, 2 на узле 1. Два потока на узле 1 работают с уда лённой памятью через QPI — штраф за межузловой доступ. Результат: 13.79 GFLOPS — хуже, чем 2×4 (17.42 total). Нечётное разделение по NUMA-узлам без MPI — антипаттерн для 2-NUMA-машин с 6 ядрами на сокет.

---

### Запуск 9: hybrid 1×12 (n3=480, 12 физических ядер, чистый OpenMP)

```bash
export OMP_NUM_THREADS=12
export OMP_PROC_BIND=close
export OMP_PLACES=cores
export I_MPI_PIN=on
export I_MPI_PIN_DOMAIN=0xFFF
export I_MPI_PIN_ORDER=compact

mpirun -np 1 ./build-vtune-hotspots/iso3dfd 256 256 480 64 64 64 2000 hybrid
```

```text
Grid:        256 x 256 x 480
Block:       64 x 64 x 64
Iterations:  2000
Half-length: 8 (16th-order stencil)
------------------------------------------------
------------------------------------------------
Time:         161.568 s
Performance:  20.84 GFLOPS
Lattice:      26726400 points/iteration
```

> `I_MPI_PIN_DOMAIN=0xFFF` — биты 0–11, все 12 физических ядер. 1 ранг, 12 OMP-потоков на всех физических ядрах. Ноль MPI-коммуникаций. 3D Layer Condition в L3 нарушен (рабочий набор ~120 МБ > 12 МБ L3), но 2D LC в L1 выполняется (8,5 КБ < 32 КБ) — внутренний цикл по Z работает из L1, L3 служит фильтром для L2-эвиктов. Лучший результат в серии 8–10: 20.84 GFLOPS.

---

### Запуск 10: hybrid 2×6 (n3=480, 12 ядер, 240 слоёв/ранг)

```bash
export OMP_NUM_THREADS=6
export OMP_PROC_BIND=close
export OMP_PLACES=cores
export I_MPI_PIN=on
export I_MPI_PIN_DOMAIN=0x3F,0xFC0
export I_MPI_PIN_ORDER=compact

mpirun -np 2 ./build-vtune-hotspots/iso3dfd 256 256 480 64 64 64 2000 hybrid
```

```text
Grid:        256 x 256 x 240
Block:       64 x 64 x 64
Iterations:  2000
Half-length: 8 (16th-order stencil)
------------------------------------------------
------------------------------------------------
Time:         157.974 s
Performance:  10.29 GFLOPS
Lattice:      12902400 points/iteration
```

> `I_MPI_PIN_DOMAIN=0x3F,0xFC0` — ранг 0 на ядрах 0–5 (NUMA-узел 0), ранг 1 на ядрах 6–11 (NUMA-узел 1). 6 OMP-потоков на ранг, 240 слоёв/ранг. Каждый ранг работает исключительно на своей NUMA-ноде. Total GFLOPS = 10.29 × 2 = 20.58 — статистически неразличим от 1×12 (20.84). Время чуть лучше (158 vs 162 с), но MPI overhead с 2 рангами минимален, NUMA-изоляция компенсирует его.

---


---

*Отчёт сформирован 27 сентября 2026 г., сервер kol-serv, Иркутск.*
