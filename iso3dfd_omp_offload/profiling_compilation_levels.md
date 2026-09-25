# Уровни модификации компиляции для профилирования iso3dfd_omp_offload

> Руководство по флагам компиляции для анализа в Intel VTune Profiler.
> Применимо к CPU-only режиму (`NO_OFFLOAD=ON`) и к исходному GPU-offload режиму.

---

## Содержание

1. [Базовый уровень: debug-символы](#1-базовый-уровень-debug-символы)
2. [Профилировочный уровень: точное соотнесение строк](#2-профилировочный-уровень-точное-соотнесение-строк)
3. [OpenMP-аналитический уровень](#3-openmp-аналитический-уровень)
4. [Режимы сборки для разных задач профилирования](#4-режимы-сборки-для-разных-задач-профилирования)
5. [Модификация CMakeLists.txt](#5-модификация-cmakeliststxt)
6. [Соответствие типов анализа VTune и флагов](#6-соответствие-типов-анализа-vtune-и-флагов)
7. [Переменные окружения OpenMP runtime](#7-переменные-окружения-openmp-runtime)
8. [Краткая шпаргалка](#8-краткая-шпаргалка)

---

## 1. Базовый уровень: debug-символы

Оригинальный `CMakeLists.txt` собирает с `-O3` без debug-информации. VTune видит имена функций, но не может сопоставить метрики со строками исходного кода.

**Добавить флаг:**

```cmake
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g")
```

- `-g` — добавляет DWARF-отладочную информацию: имена функций, номера строк, типы.
- `-O3` можно оставить — VTune умеет работать с оптимизированным кодом, хотя маппинг строк может быть неточным для агрессивно оптимизированных функций.

**Эффект:** VTune Hotspots показывает имена функций с привязкой к строкам кода.

---

## 2. Профилировочный уровень: точное соотнесение строк

Для максимально точного соотнесения метрик VTune со строками исходного кода:

```cmake
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -gline-tables-only -fdebug-info-for-profiling")
```

| Флаг | Назначение |
| --- | --- |
| `-gline-tables-only` | Генерирует только таблицы строк (line tables), без информации о переменных. Меньше размер бинарника, достаточно для VTune hotspots. |
| `-fdebug-info-for-profiling` | Добавляет дополнительные отладочные данные для более точного профиля. |

Альтернативный рецепт из официальной документации Intel для iso3dfd:

```cmake
cmake -DVERIFY_RESULTS=0 -DCMAKE_CXX_FLAGS="-g -mllvm -parallel-source-info=2" ..
```

| Флаг | Назначение |
| --- | --- |
| `-mllvm -parallel-source-info=2` | Передаёт LLVM внутренний флаг для генерации информации о параллельных регионах. VTune использует это для отображения OpenMP frame domains с привязкой к исходному коду. |

---

## 3. OpenMP-аналитический уровень

Для профилировки OpenMP-регионов (fork/join, барьеры, дисбаланс) критически важны два флага:

```cmake
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fiopenmp -g -mllvm -parallel-source-info=2")
```

Без `-parallel-source-info=2` VTune **не сможет** показать параллельные регионы как frame domains — не будет видно, какой `#pragma omp` сколько времени выполняется.

---

## 4. Режимы сборки для разных задач профилирования

### Режим A: Базовый hotspots

```cmake
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fiopenmp -std=c++17 -O3 -D__STRICT_ANSI__ -g -mllvm -parallel-source-info=2")
```

Сборка:

```bash
cmake -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
      -DNO_OFFLOAD=ON \
      -DVERIFY_RESULTS=OFF \
      -DCMAKE_CXX_FLAGS="-g -mllvm -parallel-source-info=2" \
      ..
make -j$(nproc)
```

**Результат:** VTune показывает горячие функции с привязкой к строкам кода и OpenMP-регионы как отдельные frame domains.

---

### Режим B: Memory-bound анализ

```cmake
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fiopenmp -std=c++17 -O3 -g -mllvm -parallel-source-info=2 -fdebug-info-for-profiling")
```

Сборка:

```bash
cmake -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
      -DNO_OFFLOAD=ON \
      -DVERIFY_RESULTS=OFF \
      -DCMAKE_CXX_FLAGS="-g -mllvm -parallel-source-info=2 -fdebug-info-for-profiling" \
      ..
make -j$(nproc)
```

Запуск VTune:

```bash
sudo vtune -collect memory-consumption \
  -r vtune_mem \
  -- ./iso3dfd 256 256 256 16 8 64 100
```

---

### Режим C: Без оптимизации (точный маппинг строк)

```cmake
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fiopenmp -std=c++17 -O0 -g -mllvm -parallel-source-info=2")
```

`-O0` отключает все оптимизации — маппинг строк идеальный, но производительность нереалистична. Подходит только для отладки логики, не для оценки производительности.

---

### Режим D: Microarchitecture analysis (TMA)

```cmake
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fiopenmp -std=c++17 -O3 -g -mllvm -parallel-source-info=2 -fdebug-info-for-profiling")
```

Запуск VTune:

```bash
sudo vtune -collect uarch-analysis \
  -knob collect-memory-bandwidth=true \
  -r vtune_uarch \
  -- ./iso3dfd 256 256 256 16 8 64 100
```

**Покажет:** CPI rate, cache misses, memory bandwidth, bottleneck по таксонам (Frontend Bound, Backend Bound, Bad Speculation, Retiring).

---

## 5. Модификация CMakeLists.txt

### Текущий патч (NO_OFFLOAD)

```cmake
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fiopenmp -std=c++17 -O3 -D__STRICT_ANSI__ ")
if(NOT NO_OFFLOAD)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fopenmp-targets=spir64")
endif()
```

### Рекомендуемое расширение — профилировочные флаги через CMake-опцию

```cmake
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fiopenmp -std=c++17 -O3 -D__STRICT_ANSI__ ")

if(NOT NO_OFFLOAD)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fopenmp-targets=spir64")
endif()

# Профилировочные флаги (включаются через -DENABLE_PROFILING=ON)
option(ENABLE_PROFILING "Add debug/profiling flags for VTune" OFF)
if(ENABLE_PROFILING)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -mllvm -parallel-source-info=2 -fdebug-info-for-profiling")
    message(STATUS "Profiling flags enabled: -g -parallel-source-info=2 -fdebug-info-for-profiling")
endif()
```

### Сборка с профилированием

```bash
cmake -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
      -DNO_OFFLOAD=ON \
      -DVERIFY_RESULTS=OFF \
      -DENABLE_PROFILING=ON \
      ..
make -j$(nproc)
```

### Сборка без профилирования (release)

```bash
cmake -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
      -DNO_OFFLOAD=ON \
      -DVERIFY_RESULTS=OFF \
      ..
make -j$(nproc)
```

---

## 6. Соответствие типов анализа VTune и флагов

| Тип анализа VTune | Флаги компиляции | Что даёт |
| --- | --- | --- |
| Hotspots (basic) | `-g` | Имена функций, номера строк |
| Hotspots + OpenMP regions | `-g -mllvm -parallel-source-info=2` | + параллельные регионы как frame domains |
| HPC Performance Characterization | `-g -mllvm -parallel-source-info=2` | + OpenMP metrics, SIMD, memory bandwidth |
| Microarchitecture (TMA) | `-g -fdebug-info-for-profiling` | + точный маппинг source/assembly |
| Memory Consumption | `-g` | Достаточно для функции-уровня |
| Thread Analysis | `-g -mllvm -parallel-source-info=2` | + OpenMP synchronization timeline |

---

## 7. Переменные окружения OpenMP runtime

Дополнительно к флагам компиляции, переменные окружения Intel OpenMP runtime дают трассировку offload-операций (только для GPU-offload режима, не для `NO_OFFLOAD`):

```bash
# Профилирование OpenMP offload runtime
export LIBOMPTARGET_PLUGIN_PROFILE=T

# Детальный debug offload runtime
export LIBOMPTARGET_DEBUG=1
```

Для CPU-only режима (`NO_OFFLOAD`) эти переменные не применяются.

Полезные переменные для CPU-режима:

```bash
# Число потоков OpenMP
export OMP_NUM_THREADS=24

# Привязка потоков к ядрам
export OMP_PLACES=cores
export OMP_PROC_BIND=close

# Отладка OpenMP runtime
export KMP_DEBUG_AFFINITY=1
```

---

## 8. Краткая шпаргалка

```bash
# ── Сборка с профилированием ──
source /opt/intel/oneapi/setvars.sh --force
cd build
cmake -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
      -DNO_OFFLOAD=ON \
      -DVERIFY_RESULTS=OFF \
      -DENABLE_PROFILING=ON \
      ..
make -j$(nproc)

# ── Запуск VTune hotspots ──
sudo ./run_vtune_hotspots.sh

# ── Анализ результатов ──
./analyze_vtune.sh vtune_hotspots_mpi

# ── Запуск APS ──
export APS_ENABLE=1
mpirun -n 12 ./iso3dfd 256 256 256 16 8 64 100
aps --report aps_result_YYYYMMDD_HHMMSS
firefox aps_report_YYYYMMDD_HHMMSS.html
```

---

## Сводка изменений

| Элемент | Зачем | Где менять |
| --- | --- | --- |
| `-g` | Debug-символы для VTune | `CMakeLists.txt` или `-DCMAKE_CXX_FLAGS` |
| `-mllvm -parallel-source-info=2` | OpenMP-регионы в VTune | `CMakeLists.txt` или `-DCMAKE_CXX_FLAGS` |
| `-fdebug-info-for-profiling` | Точный source/assembly маппинг | `CMakeLists.txt` или `-DCMAKE_CXX_FLAGS` |
| `-gline-tables-only` | Лёгкий профиль (только строки) | `CMakeLists.txt` или `-DCMAKE_CXX_FLAGS` |
| `ENABLE_PROFILING` option | Включение/выключение флагов | `CMakeLists.txt` |
| `NO_OFFLOAD` option | CPU-only режим без GPU | `CMakeLists.txt` + `iso3dfd.cpp` |

Эти флаги не меняют логику программы и не требуют модификации исходного кода — только `CMakeLists.txt`.
