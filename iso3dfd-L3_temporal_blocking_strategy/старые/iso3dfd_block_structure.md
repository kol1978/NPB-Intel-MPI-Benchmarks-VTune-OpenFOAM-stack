# iso3dfd 2.0 — блочная структура проекта

> **Объём исходного кода**: 858 строк (674 `.cpp` + 184 `.h/.hpp`), 10 файлов  
> **Архитектура**: Xeon X5675 Westmere, SSE4.2, 12 МБ L3  
> **Стек**: icpx 2026.1, Intel MPI 2021.18, OpenMP 5.1  
> **Метод**: L3 temporal blocking + L2 cache blocking + Z-streaming

---

## 1. Дерево проекта

```
iso3dfd-L3_temporal_blocking_strategy/
├── CMakeLists.txt              — сборка (icpx, -xSSE4.2, -fiopenmp)
├── Allrun                      — бенчмарк-скрипт (A/B/C, 19 тестов)
├── include/
│   ├── iso3dfd.h               — константы, параметры, TB-макросы       [47 строк]
│   ├── iso3dfd_grid.hpp        — API: Allocate, Initialize, Print*      [27 строк]
│   ├── iso3dfd_driver.hpp      — API: run_driver (MPI + TB)             [24 строк]
│   └── iso3dfd_solver.hpp      — API: compute_iteration, compute_*_block [86 строк]
├── src/
│   ├── iso3dfd.cpp             — main(): парсинг, запуск, env-параметры  [169 строк]
│   ├── iso3dfd_grid.cpp        — память, инициализация, коэффициенты    [ 96 строк]
│   ├── iso3dfd_driver.cpp      — MPI-цикл, halo-exchange, TB-маршрутизация [155 строк]
│   ├── iso3dfd_solver.cpp      — вычислительное ядро: stencil, TB, v2   [218 строк]
│   └── iso3dfd_verify.cpp      — проверка NaN/Inf (#ifdef VERIFY)       [ 36 строк]
└── build/                      — CMake build (исключается из подсчёта)
```

---

## 2. Блочная диаграмма

```
┌─────────────────────────────────────────────────────────────────┐
│                        iso3dfd.cpp (main)                        │
│  ┌───────────┐  ┌──────────┐  ┌───────────┐  ┌────────────────┐ │
│  │ Парсинг   │  │ Env-Vars │  │ Allocate  │  │ Print* / Verify │ │
│  │ CLI args  │→ │ TB_STEPS │→ │ + Init    │→ │ Summary / NaN   │ │
│  └───────────┘  └──────────┘  └────┬─────┘  └────────────────┘ │
│                                    │                             │
│  halo = kHalfLength × (TB+1)       │                             │
│  nz_total = nz_local + 2×halo      ↓                             │
└────────────────────────────────────┼─────────────────────────────┘
                                     │
                                     ↓
┌─────────────────────────────────────────────────────────────────┐
│                    iso3dfd_driver.cpp                            │
│                                                                  │
│  ┌──────────────┐   ┌──────────────────┐   ┌─────────────────┐  │
│  │ MPI-декомпо- │   │ Главный цикл      │   │ Halo Exchange   │  │
│  │ зиция по Z   │→  │ по итерациям      │→  │ (MPI_Isend/Irecv│  │
│  │ rank/nprocs  │   │ niter раз         │   │  по Z-границам) │  │
│  └──────────────┘   └───────┬──────────┘   └─────────────────┘  │
│                              │                                   │
│              ┌───────────────┴───────────────┐                   │
│              ↓                               ↓                   │
│  ┌────────────────────┐         ┌────────────────────────┐      │
│  │ TB > 0 ?           │         │ TB == 0 ?               │      │
│  │ compute_temporal_  │         │ compute_iteration()    │      │
│  │   block(N шагов)   │         │   (1 шаг prev→next)     │      │
│  │ + swap(prev,next)  │         │ + swap(prev,next)       │      │
│  └────────────────────┘         └────────────────────────┘      │
└──────────────────────────────────────────────────────────────────┘
                              │
                              ↓
┌─────────────────────────────────────────────────────────────────┐
│                    iso3dfd_solver.cpp                            │
│                                                                  │
│  ┌──────────────────────────────────────────────────────────┐   │
│  │ stencil_point()  — inline, 16-й порядок, #pragma unroll   │   │
│  │   value = coeff[0]·P(x,y,z)                                │   │
│  │         + Σ coeff[d]·(P(x±d) + P(y±d) + P(z±d))          │   │
│  └──────────────────────────────────────────────────────────┘   │
│                              │                                   │
│         ┌────────────────────┼────────────────────┐              │
│         ↓                    ↓                    ↓              │
│  ┌──────────────┐  ┌──────────────────┐  ┌───────────────────┐  │
│  │ compute_     │  │ compute_omp_v2   │  │ compute_temporal_  │  │
│  │  serial()    │  │                  │  │    block()         │  │
│  │              │  │ X-block (b1)     │  │                    │  │
│  │ #pragma omp  │  │ L2 micro (b2_l2) │  │ Wave-front по Z    │  │
│  │   simd       │  │ Z-stream         │  │ N шагов за проход  │  │
│  │              │  │ #pragma omp simd │  │ Interior сужается  │  │
│  │ baseline     │  │ collapse(2)     │  │ на h каждый шаг    │  │
│  └──────────────┘  └──────────────────┘  └───────────────────┘  │
└──────────────────────────────────────────────────────────────────┘
```

---

## 3. Зоны ответственности

```
┌─────────────────────────────────────────────────────────────┐
│                     УРОВЕНЬ 1: MAIN                          │
│  Файл: iso3dfd.cpp                          [169 строк]      │
│                                                              │
│  ● Парсинг аргументов CLI (n1 n2 n3 b1 b2 b3 niter mode)    │
│  ● Чтение env: ISO3DFD_TB_STEPS, ISO3DFD_B2_L2               │
│  ● Вычисление halo = kHalfLength × (tb_steps + 1)           │
│  ● Вычисление nz_total = nz_local + 2 × halo               │
│  ● Аллокация (Allocate) и инициализация (Initialize)        │
│  ● Вызов run_driver()                                       │
│  ● Вывод результатов (PrintSummary) и верификация            │
└──────────────────────────┬──────────────────────────────────┘
                           │
┌──────────────────────────┴──────────────────────────────────┐
│                   УРОВЕНЬ 2: DRIVER                           │
│  Файл: iso3dfd_driver.cpp                   [155 строк]      │
│                                                              │
│  ● MPI-декомпозиция домена по оси Z (rank / nprocs)         │
│  ● Глобальный цикл по итерациям (niter)                     │
│  ● Halo exchange между MPI-рангами (MPI_Isend/Irecv)        │
│  ● Маршрутизация: TB vs обычный режим                       │
│    ├─ tb_steps > 0 → compute_temporal_block(N, swap)        │
│    └─ tb_steps = 0 → compute_iteration(swap)                │
│  ● Swap(prev, next) после каждой итерации/волны             │
│  ● Замер времени (MPI_Wtime)                                │
└──────────────────────────┬──────────────────────────────────┘
                           │
┌──────────────────────────┴──────────────────────────────────┐
│                   УРОВЕНЬ 3: SOLVER                           │
│  Файл: iso3dfd_solver.cpp                   [218 строк]      │
│                                                              │
│  ● stencil_point() — inline-функция, 16-й порядок           │
│  ● compute_serial() — baseline, #pragma omp simd            │
│  ● compute_omp_v2() — OpenMP + cache blocking:              │
│    ├─ X-block (b1): ограничение по X для L2                  │
│    ├─ L2 micro-block (b2_l2): авто-расчёт или env           │
│    ├─ Z-streaming: последовательный проход по Z              │
│    └─ collapse(2) + schedule(static)                         │
│  ● compute_temporal_block() — L3 temporal blocking:         │
│    ├─ Волновой фронт: N шагов за один проход                 │
│    ├─ Interior сужается на kHalfLength каждый шаг            │
│    ├─ const_cast<float*> для swap указателей                 │
│    └─ Тот же cache-blocking внутри каждого шага             │
└──────────────────────────┬──────────────────────────────────┘
                           │
┌──────────────────────────┴──────────────────────────────────┐
│                   УРОВЕНЬ 4: GRID + VERIFY                    │
│  Файлы: iso3dfd_grid.cpp [96] + iso3dfd_verify.cpp [36]      │
│                                                              │
│  ● Allocate() — posix_memalign, 64-байтовое выравнивание     │
│  ● InitializeCoefficients() — 9 коэффициентов стенсиля    │
│  ● Initialize() — нули + импульс в центре домена             │
│  ● PrintStats() / PrintSummary() — параметры и GFLOPS        │
│  ● verify_result() — проверка NaN/Inf (#ifdef VERIFY_RESULTS)│
└─────────────────────────────────────────────────────────────┘
```

---

## 4. Поток данных

```
     ┌─────────┐
     │ CLI args│  n1 n2 n3 b1 b2 b3 niter mode
     └────┬────┘
          │
     ┌────┴────┐
     │  env    │  ISO3DFD_TB_STEPS → tb_steps
     │         │  ISO3DFD_B2_L2    → b2_l2 (L2 micro-block)
     └────┬────┘
          │
     ┌────┴──────────┐
     │ halo расчёт   │  halo = kHalfLength × (tb_steps + 1)
     │               │  N=0: halo=8   N=4: halo=40   N=8: halo=72
     └────┬──────────┘
          │
     ┌────┴──────────┐
     │ Allocate      │  prev[n1×n2×nz_total], next[...], vel[...]
     │ + Initialize  │  нули + импульс + vel=1.0
     └────┬──────────┘
          │
     ╔════╧══════════════════════════════════════╗
     ║   ЦИКЛ ПО ИТЕРАЦИЯМ (niter раз)            ║
     ║                                            ║
     ║  1. exchange_halo(halo)  ← MPI Isend/Irecv  ║
     ║  2. IF tb_steps > 0:                      ║
     ║       compute_temporal_block(N шагов)       ║
     ║       ├─ шаг 0: cur→dst  [halo, nz-halo)   ║
     ║       ├─ шаг 1: cur→dst  [halo+h, nz-halo-h)║
     ║       ├─ ...                                ║
     ║       └─ шаг N-1: cur→dst [суженный interior]║
     ║       swap(cur, dst)                        ║
     ║  3. ELSE:                                   ║
     ║       compute_iteration(1 шаг prev→next)   ║
     ║  4. swap(prev, next)                        ║
     ║                                            ║
     ║  ┌─────────────────────────────────────┐    ║
     ║  │  ВНУТРИ КАЖДОГО ШАГА:               │    ║
     ║  │                                      │    ║
     ║  │  for bz (Z-блок, b3)                 │    ║
     ║  │   for by (Y-блок, b2)                │    ║
     ║  │    for bx (X-блок, b1)               │    ║
     ║  │     for by2 (L2 micro, b2_l2)        │    ║
     ║  │      for z (stream)                  │    ║
     ║  │       for y                           │    ║
     ║  │        #pragma omp simd               │    ║
     ║  │        for x: stencil_point(16 порядок)║    ║
     ║  │         dst = 2·cur - dst + dt²·v²·s  ║    ║
     ║  └─────────────────────────────────────┘    ║
     ╚════════════════════════════════════════════╝
          │
     ┌────┴──────────┐
     │ PrintSummary   │  GFLOPS = interior × niter × 63 / time / 1e9
     │ + verify       │  (interior = (n1-2h)×(n2-2h)×(n3-2h))
     └───────────────┘
```

---

## 5. Зависимости модулей

```
iso3dfd.h ◄─────── (включается всеми файлами)
    │
    │
iso3dfd_grid.hpp ◄──── iso3dfd_grid.cpp
    │                        │
    │                        ↓
iso3dfd_driver.hpp ◄──── iso3dfd_driver.cpp
    │                        │
    │                        ↓
iso3dfd_solver.hpp ◄──── iso3dfd_solver.cpp
    │                        │
    │                        ↓
    └────────────────── iso3dfd.cpp (main)
                             │
                    iso3dfd_verify.cpp
                    (#ifdef VERIFY_RESULTS)
```

---

## 6. Распределение объёма кода

### По файлам

| Файл | Строк | Доля | Зона |
|---|---:|---:|---|
| `src/iso3dfd_solver.cpp` | 218 | 25.4% | Вычислительное ядро (stencil, TB, v2) |
| `src/iso3dfd.cpp` | 169 | 19.7% | Точка входа (main, CLI, env) |
| `src/iso3dfd_driver.cpp` | 155 | 18.1% | MPI-цикл, halo-exchange, маршрутизация |
| `include/iso3dfd_solver.hpp` | 86 | 10.0% | API + документация solver |
| `src/iso3dfd_grid.cpp` | 96 | 11.2% | Память, инициализация, вывод |
| `include/iso3dfd.h` | 47 | 5.5% | Константы, макросы, TB-параметры |
| `src/iso3dfd_verify.cpp` | 36 | 4.2% | Верификация NaN/Inf |
| `include/iso3dfd_grid.hpp` | 27 | 3.1% | API grid |
| `include/iso3dfd_driver.hpp` | 24 | 2.8% | API driver |
| **Итого** | **858** | **100%** | |

### По уровням

| Уровень | Файлы | Строк | Доля |
|---|---|---:|---:|
| 1 — Main | `iso3dfd.cpp` | 169 | 19.7% |
| 2 — Driver (MPI) | `iso3dfd_driver.cpp` + `.hpp` | 179 | 20.9% |
| 3 — Solver (compute) | `iso3dfd_solver.cpp` + `.hpp` | 304 | 35.4% |
| 4 — Grid + Verify | `iso3dfd_grid.cpp/.hpp` + `verify.cpp` | 159 | 18.5% |
| 0 — Headers/Const | `iso3dfd.h` | 47 | 5.5% |
| **Итого** | **10 файлов** | **858** | **100%** |

### По типам

| Тип | Файлов | Строк | Доля |
|---|---:|---:|---:|
| `.cpp` (реализация) | 5 | 674 | 78.6% |
| `.h` / `.hpp` (интерфейс) | 5 | 184 | 21.4% |
| **Всего** | **10** | **858** | **100%** |

---

## 7. Параметры temporal blocking

| Параметр | N=0 (baseline) | N=4 | N=8 |
|---|---|---|---|
| `tb_steps` | 0 | 4 | 8 |
| `halo` | 8 | 40 | 72 |
| `b3` (минимум) | 17 | 49 | 81 |
| `b3` (используется) | 64 | 64 | 128 |
| L3 рабочий набор / поток | — | 320 КБ | 384 КБ |
| L3 бюджет / поток | — | 2 МБ | 2 МБ |
| Утилизация L3 | — | 15.6% | 18.8% |

---

## 8. Параметры cache blocking (L2)

| Параметр | Значение | Источник |
|---|---|---|
| `b1` (X-block) | 64 | CLI-аргумент |
| `b2_l2` (L2 micro) | авто: ~48 | `L2 × 0.8 / ((2h+1) × b1 × 4)` |
| `b2_l2` (override) | любое | `ISO3DFD_B2_L2=N` (env) |
| 2D LC размер | ~204 КБ | `(2×8+1) × 64 × 48 × 4` |
| L2 бюджет | 256 КБ | Westmere, private per core |
| Утилизация L2 | ~80% | `L2_RESERVE_FACTOR = 0.8` |
| SIMD ширина | 4 float | SSE4.2 (`__m128`) |
| Выравнивание | 64 байт | `posix_memalign` (cache-line) |

---

## 9. Ключевые константы

| Константа | Значение | Где определена | Назначение |
|---|---|---|---|
| `kHalfLength` | 8 | `iso3dfd.h` | Радиус стенсиля (16-й порядок) |
| `DEFAULT_DT` | 0.002 | `iso3dfd.h` | Шаг по времени |
| `DEFAULT_DXYZ` | 50.0 | `iso3dfd.h` | Шаг по пространству |
| `SIMD_WIDTH` | 4 | `iso3dfd.h` | SSE4.2 (128 бит / 4 float) |
| `L2_CACHE_BYTES` | 262144 | `iso3dfd.h` | 256 КБ L2 Westmere |
| `L2_RESERVE_FACTOR` | 0.8 | `iso3dfd.h` | 80% L2 под рабочий набор |
| `TB_STEPS_DEFAULT` | 0 | `iso3dfd.h` | TB отключён по умолчанию |
| FLOPs / точка / итерация | 63 | `PrintSummary` | 9 mul + 48 add + 6 update |
