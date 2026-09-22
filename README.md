# Сравнительный анализ NPB, Intel MPI Benchmarks и VTune применительно к стеку OpenFOAM / гибридный MPI+OpenMP / NUMA / SSE4.2 (Westmere X5675)

> **IMB** измеряет «голую» коммуникационную инфраструктуру — латентность и пропускную способность MPI-операций в чистом виде.
> **NPB** проверяет, как эта инфраструктура работает под реальной вычислительной нагрузкой CFD-класса.
> **VTune** показывает, где именно код теряет время — на уровне функций, строк кода и микроархитектуры.
> Для стека OpenFOAM + гибрид MPI+OpenMP + NUMA нужны все три — но на разных этапах.

---

## Оглавление

1. [Репозитории и установка из исходного кода](#репозитории-и-установка-из-исходного-кода)
2. [Конфигурирование компиляторов: два варианта для Westmere](#конфигурирование-компиляторов-два-варианта-для-westmere)
3. [Многопутевой бинарник: automatic CPU dispatch](#многопутевой-бинарник-automatic-cpu-dispatch)
4. [FP-модель и OpenFOAM: критический анализ](#fp-модель-и-openfoam-критический-анализ)
5. [Переменные окружения Intel MPI](#переменные-окружения-intel-mpi)
6. [Что измеряет каждый набор](#что-измеряет-каждый-набор)
7. [Сравнение по ключевым параметрам](#сравнение-по-ключевым-параметрам)
8. [Как применять к стеку](#как-применять-к-стеку)
9. [Профилирование с VTune](#профилирование-с-vtune)
10. [Практический чек-лист запуска](#практический-чек-лист-запуска)
11. [Сводная таблица: что отвечает на какой вопрос](#сводная-таблица-что-отвечает-на-какой-вопрос)
12. [Ссылки](#ссылки)

---

## Репозитории и установка из исходного кода

### 1. Intel MPI Benchmarks (IMB)

**Репозиторий:** https://github.com/intel/mpi-benchmarks

**Лицензия:** 3-Clause BSD

**Установка из исходников (Linux):**

```bash
git clone https://github.com/intel/mpi-benchmarks.git
cd mpi-benchmarks

# Вариант A: сборка через make (требуется MPI и C++ компилятор)
make -f make_mpistub.txt

# Вариант B: сборка через CMake
mkdir build && cd build
cmake ..
make -j$(nproc)

# Бинарники появятся в build/
# Основные: IMB-MPI1, IMB-NBC, IMB-RMA, IMB-IO, IMB-MT, IMB-EXT
```

> **Зависимости:** установленный MPI (Intel MPI, OpenMPI или MPICH), компилятор C++ (gcc/g++ или icpx).
> Для Ubuntu: `sudo apt install build-essential g++ libopenmpi-dev openmpi-bin`

---

### 2. NASA Parallel Benchmarks (NPB) — выбор репозитория

Доступно четыре репозитория. Сравнение:

| Репозиторий | Версии NPB | Стандартный MPI | NPB-OMP | NPB-SER | NPB-MZ (Multi-Zone) | NPB-HPF | NPB-JAV | Особенность |
|---|---|---|---|---|---|---|---|---|
| **llnl/NPB** | 3.4 + 3.4-MZ | ✅ 3.4 | ❌ | ❌ | ✅ 3.4-MZ | ❌ | ❌ | Свежая версия 3.4 + Multi-Zone 3.4 |
| **matthiasdiener/nas** | 3.3 | ✅ | ✅ | ✅ | ✅ MZ-MPI + MZ-OMP + MZ-SER | ❌ | ❌ | Все реализации MZ, включая чистый OpenMP |
| **tpatki/NPB3.3.1-MZ** | 3.3.1 | ❌ | ❌ | ❌ | ✅ только MZ-MPI | ❌ | ❌ | Минимальный — только гибрид |
| **mbdevpl/nas-parallel-benchmarks** | 3.0, 3.3.1, 3.4 | ✅ все | ✅ все | ✅ все | ❌ | ✅ 3.0 | ✅ 3.0 | Архивный миррор с NASA, без MZ |

**Что выбрать:**

- **matthiasdiener/nas** — лучший для гибридных тестов: содержит и стандартный NPB-MPI (MG, CG, BT), и NPB-MZ-MPI (гибрид MPI+OpenMP), и NPB-MZ-OMP (чистый OpenMP). Всё в одном репозитории.
- **llnl/NPB** — нужен, если нужна версия 3.4 (последняя официальная от NASA) с Multi-Zone. Содержит NPB3.4 (стандартный MPI) и NPB3.4-MZ (Multi-Zone MPI).
- **tpatki/NPB3.3.1-MZ** — только если нужен гибрид MPI+OpenMP и ничего больше. По сути это подмножество matthiasdiener (папка NPB3.3-MZ-MPI у них идентична).
- **mbdevpl/nas-parallel-benchmarks** — архивный миррор: содержит все версии с сайта NASA (3.0, 3.3.1, 3.4), включая HPF и Java. Но **не содержит Multi-Zone**.

> **Рекомендация для Westmere X5675:** клонируйте **matthiasdiener/nas** — там есть всё, что нужно для тестирования схем MPI×OpenMP. Если нужна версия 3.4 — дополнительно **llnl/NPB**.

---

### Установка NPB (вариант A: matthiasdiener/nas — рекомендуется)

```bash
git clone https://github.com/matthiasdiener/nas.git
cd nas/NPB3.3-MPI

# Создание конфигурационного файла
cp config/make.def.template make.def

# См. раздел «Конфигурирование компиляторов» для редактирования make.def

# Сборка конкретного бенчмарка
# Формат: make <benchmark> CLASS=<class> NPROCS=<n>
make mg CLASS=C NPROCS=8
make cg CLASS=C NPROCS=8
make bt CLASS=C NPROCS=9    # BT требует квадратное число процессов
make sp CLASS=C NPROCS=9
make ft CLASS=C NPROCS=8
make ep CLASS=C NPROCS=8
make is CLASS=C NPROCS=8
make lu CLASS=C NPROCS=8

# Бинарники в ./bin/, например: ./bin/mg.C.8
# Запуск:
mpirun -np 8 ./bin/mg.C.8
```

> **Зависимости:** Fortran-компилятор (gfortran или ifort/ifx), MPI, C-компилятор.
> Для Ubuntu: `sudo apt install build-essential gfortran libopenmpi-dev openmpi-bin`

---

### Установка NPB (вариант B: llnl/NPB — версия 3.4)

```bash
git clone https://github.com/llnl/NPB.git
cd NPB/NPB3.4

cp config/make.def.template make.def
# См. раздел «Конфигурирование компиляторов» для редактирования make.def

make mg CLASS=C NPROCS=8
mpirun -np 8 ./bin/mg.C.8
```

---

### Установка NPB-MZ (Multi-Zone — гибрид MPI+OpenMP)

**Вариант A: из matthiasdiener/nas (рекомендуется — есть все три реализации):**

```bash
cd nas/NPB3.3-MZ-MPI    # гибрид MPI+OpenMP
# или
cd nas/NPB3.3-MZ-OMP    # чистый OpenMP (для сравнения)
# или
cd nas/NPB3.3-MZ-SER    # serial (baseline)

cp config/make.def.template make.def
# См. раздел «Конфигурирование компиляторов» для редактирования make.def

make bt-mz CLASS=C NPROCS=4
```

**Вариант B: из tpatki/NPB3.3.1-MZ (только MZ-MPI):**

```bash
git clone https://github.com/tpatki/NPB3.3.1-MZ.git
cd NPB3.3.1-MZ/NPB3.3-MZ-MPI

cp config/make.def.template make.def
make bt-mz CLASS=C NPROCS=4
```

**Запуск в гибридном режиме (сравнение схем):**

```bash
# 2 MPI × 8 OpenMP
OMP_NUM_THREADS=8  mpirun -np 2 ./bin/bt-mz.C.2

# 4 MPI × 4 OpenMP
OMP_NUM_THREADS=4  mpirun -np 4 ./bin/bt-mz.C.4

# 8 MPI × 2 OpenMP
OMP_NUM_THREADS=2  mpirun -np 8 ./bin/bt-mz.C.8

# 16 MPI × 1 OpenMP (чистый MPI)
OMP_NUM_THREADS=1  mpirun -np 16 ./bin/bt-mz.C.16
```

> **Примечание:** NPB-MZ специально создан для тестирования гибридных MPI+OpenMP стратегий.
> Зоны распределены неравномерно, что моделирует нагрузочный дисбаланс — как в реальных CFD-расчётах.

---

## Конфигурирование компиляторов: два варианта для Westmere

### Контекст

Тестируемая платформа: **Westmere X5675** (6 ядер, SSE4.2, triple-channel DDR3-1333, QPI 6.4 GT/s).

Два компилятора: Intel (icx/icpx/ifx) и GCC (gcc/g++/gfortran).

### Вариант 1: Intel + SSE4.2 (Westmere X5675)

**Файл `make.def` для NPB / NPB-MZ:**

```makefile
# Компиляторы
MPICC   = mpiicc
MPIF77  = mpiifort
MPICXX  = mpiicpc
FLINK   = $(MPIF77)
CLINK   = $(MPICC)

# Флаги Fortran
FFLAGS  = -O3 -xSSE4.2 -ipo -no-prec-div -opt-prefetch=4 -qopt-streaming-stores=auto -fopenmp

# Флаги C
CFLAGS  = -O3 -xSSE4.2 -ipo -no-prec-div -opt-prefetch=4 -qopt-streaming-stores=auto -fopenmp

# Отчёт о векторизации (опционально, для диагностики)
# FFLAGS += -qopt-report=4
# CFLAGS += -qopt-report=4

# Библиотеки
FMPI_LIB = -lmpi
CMPI_LIB = -lmpi
```

**Пояснение по флагам Intel (документация Intel Compiler):**

| Флаг | Назначение | Документация |
|---|---|---|
| `-O3` | Максимальный уровень оптимизации: автовекторизация, развёртка циклов, loop blocking | Базовый флаг, поддерживается всеми версиями icc/icx |
| `-xSSE4.2` | Генерация кода для SSE4.2 (максимум для Westmere). Эквивалент `-march=westmere` для GCC | Intel Compiler User Guide: указывает целевой ISA |
| `-xHost` | Альтернатива: автоопределение ISA хоста. **Внимание:** при кросс-компиляции выберет ISA машины сборки, а не целевой | Intel Compiler docs: `-xHost` = detect host CPU |
| `-ipo` | Межпроцедурная оптимизация (Inter-Procedural Optimization). Анализ всего модуля целиком для инлайнинга и устранения мёртвого кода | Intel Compiler: включается в `-fast`, но `-fast` нельзя использовать с MPI (включает `-static`) |
| `-no-prec-div` | Замена `A/B` на `A*(1/B)` — ускоряет деление. **Для NPB безопасно** (нет isnan-проверок). **Для OpenFOAM — см. раздел FP-модель** | Intel Corden_FP_control.pdf: `fast[=1]` включает по умолчанию |
| `-opt-prefetch=4` | Агрессивный software prefetching (уровень 4 из 0–4/5). Для CFD с большими массивами улучшает локальность | Intel Quick Reference Guide: `n=0 (off)` до `n=4/5 (aggressive)` |
| `-qopt-streaming-stores=auto` | Генерация non-temporal stores (обход кэша) для массивов без переиспользования. Полезно для NPB-FT, NPB-SP с большими потоками данных | Intel Compiler docs: `always` — всегда, `never` — запретить, `auto` — решает компилятор |
| `-fopenmp` / `-qopenmp` | Включение OpenMP. Для NPB-MZ обязательно. `-fopenmp` — для ICX (LLVM-based), `-qopenmp` — для classic ICC | Intel Porting Guide: ICX использует `-fiopenmp` или `-fopenmp` |
| `-qopt-report=4` | Отчёт о векторизации (уровень 4 = verbose). Показывает, какие циклы векторизованы, какие нет и почему | Intel Compiler: `-qopt-report[=n]`, n=0..5 |
| `-no-vec` | Отключение автовекторизации (для baseline-сравнения: scalar vs SSE4.2). По умолчанию векторизация включена при `-O2` и выше | LLNL: «SSE2 vectorization is enabled by default at -O2 and above» |
| `-axSSE4.2` | Многопутевой бинарник: SSE4.2-путь + базовый путь (см. раздел «Многопутевой бинарник») | Intel Compiler: automatic CPU dispatch |

> **Важно:** `-xCORE-AVX512` на Westmere вызовет ошибку компиляции — процессор не поддерживает AVX-512. Используйте только `-xSSE4.2` или `-xHost`.

---

### Вариант 2: GCC + SSE4.2 (Westmere X5675)

**Файл `make.def` для NPB / NPB-MZ:**

```makefile
# Компиляторы
MPICC   = mpicc
MPIF77  = mpifort
MPICXX  = mpicxx
FLINK   = $(MPIF77)
CLINK   = $(MPICC)

# Флаги Fortran
FFLAGS  = -O3 -msse4.2 -fno-prec-div -fprefetch-loop-arrays -funroll-loops -ftree-vectorize -fopenmp

# Флаги C
CFLAGS  = -O3 -msse4.2 -fno-prec-div -fprefetch-loop-arrays -funroll-loops -ftree-vectorize -fopenmp

# Диагностика векторизации (опционально)
# FFLAGS += -fopt-info-vec
# CFLAGS += -fopt-info-vec

# Библиотеки
FMPI_LIB = -lmpi
CMPI_LIB = -lmpi
```

**Пояснение по флагам GCC (документация GCC):**

| Флаг GCC | Эквивалент Intel | Назначение |
|---|---|---|
| `-O3` | `-O3` | Автовекторизация, развёртка циклов, loop interchange |
| `-msse4.2` | `-xSSE4.2` | Включение SSE4.2 инструкций. Эквивалент `-march=westmere` (включает все расширения вплоть до SSE4.2) |
| `-march=westmere` | `-xSSE4.2` | Полная альтернатива: включает SSE4.2 + tuning для Westmere (планировщик инструкций, prefetch) |
| `-march=native` | `-xHost` | Автоопределение ISA хоста. **Внимание при кросс-компиляции** |
| `-fno-prec-div` | `-no-prec-div` | Замена `A/B` на `A*(1/B)` |
| `-fprefetch-loop-arrays` | `-opt-prefetch=4` | Software prefetching для циклов с массивами. Менее гранулярный, чем Intel (нет уровней 0–4) |
| `-funroll-loops` | `-funroll-loops` | Развёртка циклов. Intel тоже поддерживает `-funroll-loops` (и `-unroll[n]`) |
| `-ftree-vectorize` | (включено в `-O3`) | Автовекторизация на уровне дерева. У GCC включена по умолчанию в `-O3` |
| `-fopt-info-vec` | `-qopt-report=4` | Отчёт о векторизации: какие циклы векторизованы, какие нет |
| `-fopenmp` | `-fopenmp` / `-qopenmp` | Включение OpenMP. У GCC и ICX используется `-fopenmp` |

> **Примечание:** У GCC нет прямого эквивалента `-ipo` (межпроцедурной оптимизации). Ближайший аналог — `-flto` (Link-Time Optimization), но поведение отличается: `-flto` работает на стадии линковки, а `-ipo` — на стадии компиляции каждого файла. У GCC нет эквивалента `-qopt-streaming-stores` — streaming stores генерируются автоматически при `-O3` там, где компилятор считает нужным.

---

### Сводная таблица конфигураций

| Параметр | Intel + SSE4.2 | GCC + SSE4.2 |
|---|---|---|
| **Компилятор C** | `mpiicc` | `mpicc` |
| **Компилятор Fortran** | `mpiifort` | `mpifort` |
| **ISA-флаг** | `-xSSE4.2` | `-msse4.2` или `-march=westmere` |
| **Оптимизация** | `-O3 -ipo` | `-O3` (можно добавить `-flto`) |
| **Деление** | `-no-prec-div` | `-fno-prec-div` |
| **Prefetch** | `-opt-prefetch=4` | `-fprefetch-loop-arrays` |
| **Streaming stores** | `-qopt-streaming-stores=auto` | (авто в `-O3`) |
| **Развёртка циклов** | (входит в `-O3`) | `-funroll-loops` |
| **OpenMP** | `-fopenmp` | `-fopenmp` |
| **Отчёт векторизации** | `-qopt-report=4` | `-fopt-info-vec` |
| **Отключение векторизации (baseline)** | `-no-vec` | `-fno-tree-vectorize` |
| **Целевая платформа** | Westmere X5675 | Westmere X5675 |

---

## Многопутевой бинарник: automatic CPU dispatch

### Суть механизма

Intel-компилятор умеет упаковывать в **один исполняемый файл** несколько версий кода, оптимизированных под разные наборы инструкций. Это называется **automatic CPU dispatch** (автоматическая диспетчеризация по процессору) и включается флагом `-ax`. В момент запуска программы встроенный диспетчер определяет capabilities процессора и выбирает оптимальный путь. [web_13_0_0_1](https://www.intel.com/content/www/us/en/docs/dpcpp-cpp-compiler/developer-guide-reference/2025-1/ax-qax.html)

Связка `-axSSE4.2 -xSSE2` создаёт **два пути** в одном бинарнике:

| Путь | ISA | Где выполняется | Как выбирается |
|---|---|---|---|
| **Оптимизированный** (`-axSSE4.2`) | SSE4.2 | Intel Westmere и новее | Диспетчер проверяет CPUID → если есть SSE4.2, идёт сюда |
| **Базовый** (`-xSSE2`) | SSE2 | Любой x86-64 (включая AMD) | Если SSE4.2 нет → fallback на SSE2 |

### Разница между `-x` и `-ax`

- **`-xSSE4.2`** — генерирует **одну** версию кода под SSE4.2. На процессоре без SSE4.2 программа упадёт с `SIGILL` (Illegal Instruction).
- **`-axSSE4.2`** — генерирует SSE4.2-путь **плюс** базовый путь. Программа запустится везде, но на SSE4.2-процессоре выберет быстрый путь.
- **`-axSSE4.2 -xSSE2`** — явно задаёт базовый путь как SSE2. На SSE4.2-процессоре идёт по SSE4.2, на всём остальном — по SSE2.

### Как работает рантайм-диспетчер

Компилятор встраивает в бинарник специальную переменную `__intel_cpu_indicator` и код проверки. На практике это выглядит так (из анализа ассемблера):

```asm
; Диспетчер в начале функции
test DWORD PTR __intel_cpu_indicator[rip], -131072   ; проверка флага SSE4.2
jne  foo.R                                            ; если есть → путь SSE4.2 (R = "optimized")
test DWORD PTR __intel_cpu_indicator[rip], -1          ; проверка общего флага
jne  foo.A                                            ; если нет → базовый путь SSE2 (A = "alternative")

; Путь foo.R (SSE4.2 — оптимизированный):
movups xmm1, [rdi+rcx*4]                              ; 128-bit SSE4.2 инструкции
mulps  xmm1, xmm0

; Путь foo.A (SSE2 — базовый):
; Те же вычисления, но без SSE4.2-специфичных оптимизаций
```

Переменная `__intel_cpu_indicator` инициализируется **один раз** при старте программы через инструкцию `CPUID` и кэшируется. Повторных проверок не происходит — накладные расходы минимальны.

### Практические команды

Для `make.def` NPB / NPB-MZ (если бинарник может запускаться не только на Westmere):

```makefile
# Два пути: SSE4.2 (Westmere+) + SSE2 (baseline для любого x86-64)
FFLAGS = -O3 -axSSE4.2 -xSSE2 -ipo -no-prec-div -opt-prefetch=4 -fopenmp
CFLAGS = -O3 -axSSE4.2 -xSSE2 -ipo -no-prec-div -opt-prefetch=4 -fopenmp
```

Для `c++OPT` OpenFOAM:

```bash
c++OPT = -O3 -axSSE4.2 -xSSE2 -funroll-loops
```

Три пути (если есть кластер с разными поколениями CPU):

```makefile
# AVX-512 (Skylake-X) + AVX2 (Haswell) + SSE4.2 (Westmere) + SSE2 (baseline)
FFLAGS = -O3 -axCORE-AVX512,CORE-AVX2,SSE4.2 -xSSE2 -ipo -fopenmp
CFLAGS = -O3 -axCORE-AVX512,CORE-AVX2,SSE4.2 -xSSE2 -ipo -fopenmp
```

Проверка, что многопутевой режим работает:

```bash
icpx -O3 -axSSE4.2 -xSSE2 -qopt-report=4 test.cpp -o test 2>&1 | grep "cpu dispatch"
# Если видите "F has been targeted for automatic cpu dispatch" — работает
```

### Поддержка в ICX (LLVM-based компилятор)

В ранних версиях ICX (2022.0–2022.0.1) флаг `-ax` **не поддерживался** — это был regression из-за перехода на LLVM. Начиная с oneAPI 2022.2+, `-ax` снова работает. Для ICX 2026.1.1 флаг `-axSSE4.2 -xSSE2` должен работать.

### Когда использовать, а когда нет

| Сценарий | Флаги | Почему |
|---|---|---|
| Бинарник только для Westmere | `-xSSE4.2` | Максимальная оптимизация, нет overhead на диспетчер, нет раздувания бинарника |
| Бинарник для Westmere + возможен запуск на старших/AMD | `-axSSE4.2 -xSSE2` | Не упадёт на SSE2-процессоре, но на Westmere выберет SSE4.2-путь |
| Бинарник для кластера с разными поколениями CPU | `-axCORE-AVX512,CORE-AVX2,SSE4.2 -xSSE2` | Оптимально на каждом поколении, не падает нигде |
| Кросс-компиляция с машины сборки ≠ целевой | `-xSSE4.2` (явно, не `-xHost`) | `-xHost` выберет ISA машины сборки, а не целевой |

### Минусы многопутевого подхода

1. **Размер бинарника** — каждый путь дублирует векторизованные функции. Для трёх путей размер может вырасти в 2–4 раза.
2. **Время компиляции** — каждая функция компилируется многократно.
3. **Диспетчер работает только для Intel-процессоров** — на AMD базовый путь выбирается всегда, даже если AMD поддерживает SSE4.2. Это документированное поведение: `-x` генерирует код только для Intel-процессоров. Если нужен SSE4.2 и на AMD, используйте `-march` вместо `-x` для базового пути:

```bash
# Базовый путь через -march (работает на AMD), не через -x (только Intel)
icpx -O3 -axSSE4.2 -msse2 app.cpp -o app
```

> **Для Westmere X5675:** если все запуски только на X5675 — используйте `-xSSE4.2`, проще и эффективнее. Многопутевой бинарник имеет смысл, если бинарник будет запускаться на разных машинах.

---

## FP-модель и OpenFOAM: критический анализ

> **Внимание:** Этот раздел критичен для OpenFOAM, но **не для NPB**. NPB — бенчмарки без isnan-проверок, для них безопасно использовать агрессивные FP-флаги. Обе конфигурации выше рассчитаны на NPB.
> Для OpenFOAM применяются другие правила — см. ниже.

### Проблема: OpenFOAM использует isnan()/isinf()

В исходниках OpenFOAM есть проверки на NaN/Inf:

- `ensightPart.C` — `if (id >= field.size() || isnan(field[id]))`
- `quadraticEqn.C` — `if (mag(a) < VSMALL) { return Roots<2>(linearEqn(b, c).roots(), roots::nan, 0); }`
- Множество `VSMALL`-защит: `r_PW/(r_PW_mag + VSMALL)`

**Ключевая проблема:** ICX (LLVM-based) при `-fp-model fast` включает `-ffinite-math-only`, который сообщает компилятору: «NaN и Inf никогда не возникнут». В результате проверки `isnan()`/`isinf()` оптимизируются away.

### Текущая конфигурация OpenFOAM (wmake/rules/linux64Icx)

Стандартные правила OpenFOAM для Icx содержат:

```
c++ARCH := -pthread -fp-model precise -frounding-math
```

Проверить в вашей системе:
```bash
grep "frounding-math" $WM_PROJECT_DIR/wmake/rules/linux64Icx/c
grep "frounding-math" $WM_PROJECT_DIR/wmake/rules/linux64Icx/c++
```

### Что делает связка `-fp-model precise` + `-frounding-math`

По документации Intel:

| Оптимизация | `-fp-model precise` | `-frounding-math` | Оба вместе |
|---|---|---|---|
| Реассоциация FP (перестановка слагаемых) | заблокирована | заблокирована | заблокирована |
| Деление через обратную величину | заблокировано | заблокировано | заблокировано |
| Constant folding (вычисление констант) | разрешён | заблокирован | заблокирован |
| Reordering across calls | разрешён | заблокирован | заблокирован |
| FMA (fused multiply-add) | разрешён | заблокирован | заблокирован |
| Автовекторизация FP-циклов | ограничена | дополнительно ограничена | сильно ограничена |

По документации Intel: `-fp-model strict` = `-fp-model precise` + запрет FMA + не предполагать default FP environment. Связка `-fp-model precise` + `-frounding-math` **практически эквивалентна** `-fp-model strict` — самому медленному режиму из всех.

### Оценка потерь производительности

| Что заблокировано | Из-за какого флага | Оценка потери |
|---|---|---|
| FMA (fused multiply-add) | `-frounding-math` | 5–10% на FP-интенсивных операциях |
| Constant folding | `-frounding-math` | 1–3% |
| Reordering FP operations | `-fp-model precise` + `-frounding-math` | 5–10% |
| Reciprocal division | `-fp-model precise` | 2–3% |
| Агрессивная автовекторизация | Оба флага вместе | 3–5% |
| **Суммарная оценка потерь** | | **~15–25%** |

### Три варианта конфигурации OpenFOAM

#### Вариант A: Безопасный (стандарт OpenFOAM)

```bash
# wmake/rules/linux64Icx/c++Opt
c++OPT = -O3 -march=westmere -funroll-loops

# wmake/rules/linux64Icx/c++ARCH (оставить как есть)
c++ARCH := -pthread -fp-model precise -frounding-math
```

- `-fp-model precise` остаётся в `c++ARCH`, не рискует точностью
- Все FP-оптимизации запрещены, `isnan()`/`isinf()` работают
- Риск для сходимости — нулевой
- Потери: ~15–25% производительности vs агрессивный

#### Вариант B: Агрессивный (с проверкой isnan)

```bash
# wmake/rules/linux64Icx/c++Opt
c++OPT = -O3 -march=westmere -funroll-loops -fp-model fast=2 -no-prec-div -fno-finite-math-only -freciprocal-math

# wmake/rules/linux64Icx/c++ARCH (убрать -frounding-math)
c++ARCH := -pthread
```

- `fast=2` — реассоциация, reciprocal math, аппроксимация трансцендентных функций
- `-fno-finite-math-only` — восстанавливает `isnan()`/`isinf()`
- `-freciprocal-math` — заменяет `A/B` на `A*(1/B)`

**Проверка isnan/isinf на ICX 2026.1.1 (подтверждено пользователем):**

```bash
# Тестовый код
cat > /tmp/test_nan.cpp << 'EOF'
#include <cmath>
#include <iostream>
int main() {
    double a = 0.0/0.0;  // NaN
    double b = 1.0/0.0;  // Inf
    std::cout << "isnan(NaN): " << std::isnan(a) << std::endl;
    std::cout << "isinf(Inf): " << std::isinf(b) << std::endl;
    return 0;
}
EOF

icpx -O3 -fp-model fast=2 -freciprocal-math -fno-finite-math-only /tmp/test_nan.cpp -o /tmp/test_nan
/tmp/test_nan
# isnan(NaN): 1
# isinf(Inf): 1
```

Обе проверки проходят на ICX 2026.1.1 — `isnan` и `isinf` работают даже в `fast=2` с `-fno-finite-math-only`.

**Риски варианта B:**
- `-freciprocal-math` — меняет сходимость GAMG/PCG (неточное деление)
- `fast=2` — реассоциация и аппроксимация `sqrt`/`exp` — меняет турбулентность и термодинамику
- Результат может сойтись к другому решению или потребовать на пару итераций больше

#### Вариант C: Компромиссный (НЕ рекомендуется)

```bash
# wmake/rules/linux64Icx/c++Opt
c++OPT = -O3 -march=westmere -funroll-loops -fp-model fast=1 -no-prec-div -freciprocal-math

# wmake/rules/linux64Icx/c++ARCH (убрать -frounding-math)
c++ARCH := -pthread
```

**Вариант C — хуже B, а не компромисс.** Без `-fno-finite-math-only` проверки `isnan()`/`isinf()` сломаны гарантированно: `fast=1` в ICX тоже включает `-ffinite-math-only`. Если решатель разойдётся и появятся NaN — OpenFOAM их не обнаружит и продолжит считать с мусором.

### Сравнение вариантов для OpenFOAM

| Критерий | A (безопасный) | B (агрессивный) | C (НЕ рекоменд.) |
|---|---|---|---|
| FP-реассоциация | ❌ запрещена | ✅ разрешена (`fast=2`) | ✅ разрешена (`fast=1`) |
| Reciprocal division | ❌ нет | ✅ `-no-prec-div` | ✅ `-no-prec-div` |
| Аппроксимация трансцендентных | ❌ нет | ✅ `fast=2` | ❌ нет |
| `isnan()`/`isinf()` работают | ✅ да | ✅ да (`-fno-finite-math-only`) | ❌ сломаны |
| Воспроизводимость | ✅ полная | ❌ нет | ❌ нет |
| Линейные решатели (GAMG, PCG) | ✅ | ⚠️ неточная дивизия | ⚠️ неточная дивизия |
| TVD-лимитеры | ✅ | ⚠️ реассоциация | ⚠️ реассоциация |
| Турбулентность (sqrt, pow) | ✅ | ⚠️ аппроксимация | ✅ |
| Термодинамика (exp, log) | ✅ | ⚠️ аппроксимация | ✅ |
| Обнаружение расходимости | ✅ | ✅ | ❌ сломано |
| Для жёстких задач (ударные волны, multiphase) | ✅ | ❌ высокий риск | ❌ высокий риск |
| Для простых задач (ламинарные, низкий Re) | ✅ | ⚠️ вероятно OK | ⚠️ вероятно OK |

### Рекомендация для OpenFOAM

**Вариант A** — единственный безопасный. Это то, что OpenFOAM поставляет по умолчанию. Для CFD-расчётов экономия 15–25% не стоит риска молчаливой расходимости.

**Вариант B** технически возможен (проверки `isnan`/`isinf` не сломаны на ICX 2026.1.1), но `reciprocal math` и реассоциация меняют сходимость. Можно пробовать только с верификацией: запустить тест и сравнить результаты с вариантом A.

**Вариант C** — не компромисс, а худший вариант. Без `-fno-finite-math-only` проверки сломаны гарантированно.

### Что меняется для NPB vs OpenFOAM

| Аспект | NPB | OpenFOAM |
|---|---|---|
| `isnan()`/`isinf()` проверки | Нет | Да (критично) |
| FP-модель | Можно `fast=2` | Только `precise` (вариант A) |
| `-no-prec-div` | Безопасно | Меняет сходимость решателей |
| `-frounding-math` | Не нужен | Включён в wmake по умолчанию |
| Агрессивные флаги | Обе конфигурации выше | Только вариант A |

---

## Переменные окружения Intel MPI

### I_MPI_FABRICS — выбор транспортов

По документации Intel MPI:

| Значение | Внутриузловой транспорт | Межузловой транспорт | Сценарий |
|---|---|---|---|
| `shm` | Shared Memory | **нет** | Только одноузловой запуск (нет других узлов) |
| `shm:ofi` | Shared Memory | OFI (InfiniBand, OmniPath) | Кластер: SHM внутри узла, OFI между узлами |
| `ofi` | OFI (SHM отключён) | OFI | Только OFI везде, даже внутри узла |

> **Важно:** `shm` и `shm:ofi` — это не альтернативы «какой транспорт лучше для внутриузлового обмена». `shm` работает только на одном узле, `shm:ofi` — на кластере. Реальное сравнение для внутриузлового обмена: `shm:ofi` (SHM включён) vs `ofi` (SHM отключён, даже внутриузловой обмен через OFI).
>
> **Документация Intel MPI:** «If all processes start on one node, the Intel MPI Library uses shm intra-node communication regardless of the selected option» — для одноузлового запуска SHM используется автоматически.

```bash
# Только одноузловой запуск (Westmere X5675, 12 ядер)
export I_MPI_FABRICS=shm

# Кластер: SHM внутри узла + OFI между узлами
export I_MPI_FABRICS=shm:ofi

# Только OFI (внутриузловой SHM отключён — для тестирования overhead SHM vs OFI)
export I_MPI_FABRICS=ofi
```

### I_MPI_SHM — выбор SHM-транспорта

При `I_MPI_FABRICS=shm` или `shm:ofi` Intel MPI использует внутренний SHM-драйвер. Выбор зависит от архитектуры:

| Значение | Архитектура | SIMD | Совместимость с Westmere |
|---|---|---|---|
| `auto` | Автоопределение | — | ✅ (выберет SSE-based драйвер) |
| `bdw_sse` | Broadwell и ниже | SSE4.2 | ✅ (подходит для Westmere) |

> Остальные драйверы (`skx_avx512`, `skx_sse`, `clx_*` и т.д.) либо требуют инструкций, отсутствующих на Westmere, либо используют CLFLUSHOPT (появился в Skylake-X).

```bash
# Для Westmere X5675 — автоопределение выберет SSE-based драйвер
export I_MPI_SHM=auto

# Или явно указать SSE-драйвер (если auto выбрал неоптимально)
export I_MPI_SHM=bdw_sse
```

### I_MPI_PIN — привязка процессов

```bash
# Включить привязку MPI-процессов к ядрам
export I_MPI_PIN=on

# Привязка по NUMA-доменам (каждый MPI-процесс — на свой NUMA-узел)
export I_MPI_PIN_DOMAIN=numa

# Порядок размещения: scatter — по разным NUMA-узлам (лучше для bandwidth)
export I_MPI_PIN_ORDER=scatter

# Или compact — рядом (лучше для latency)
# export I_MPI_PIN_ORDER=compact
```

### KMP_AFFINITY / OMP_PLACES — привязка OpenMP-потоков

Для Intel OpenMP runtime:

```bash
# scatter — потоки по разным ядрам (лучше для memory-bound CFD)
export KMP_AFFINITY=granularity=fine,scatter

# compact — потоки рядом (лучше для compute-bound, выгоднее shared cache)
# export KMP_AFFINITY=granularity=fine,compact
```

Для GCC OpenMP runtime:

```bash
export OMP_PLACES=cores
export OMP_PROC_BIND=spread   # аналог scatter
# или
# export OMP_PROC_BIND=close  # аналог compact
```

### Готовый блок для копирования (Westmere X5675, Intel MPI)

```bash
# === Intel MPI configuration for Westmere X5675 ===
export I_MPI_FABRICS=shm              # одноузловой (или shm:ofi для кластера)
export I_MPI_SHM=auto                 # автоопределение SHM-драйвера (выберет SSE-based)
export I_MPI_PIN=on                   # привязка процессов
export I_MPI_PIN_DOMAIN=numa           # по NUMA-доменам
export I_MPI_PIN_ORDER=scatter        # scatter для bandwidth

# === OpenMP (Intel runtime) ===
export OMP_NUM_THREADS=4             # 4 OpenMP × 4 MPI = 16 потоков (2 × 6 ядер + HT)
export KMP_AFFINITY=granularity=fine,scatter

# === OpenMP (GCC runtime) ===
# export OMP_PLACES=cores
# export OMP_PROC_BIND=spread
```

---

## Что измеряет каждый набор

### Intel MPI Benchmarks (IMB)

IMB — набор микро-бенчмарков для изолированного тестирования MPI-примитивов.
Не вычисляет ничего полезного, только гоняет сообщения через MPI-функции и замеряет время.

**Компоненты IMB:**

| Компонент | Что тестирует | Релевантность для OpenFOAM |
|---|---|---|
| **IMB-MPI1** | Все базовые операции MPI-1: PingPong, Sendrecv, Exchange, Allreduce, Alltoall, Bcast, Reduce и т.д. | Прямо — Allreduce и Alltoall используются в решателях |
| **IMB-P2P** | Точечные шаблоны: Stencil2D/3D, UniRandom, Bidir — паттерны ближайших соседей | Соответствует декомпозиции сетки в OpenFOAM |
| **IMB-NBC** | Неблокирующие коллективы (MPI-3): Iallreduce, Ialltoall и т.д. | Перекрытие вычислений и коммуникаций |
| **IMB-RMA** | One-sided: MPI_Put, MPI_Get, Fetch_and_op | Менее релевантно — OpenFOAM редко использует RMA |
| **IMB-IO** | Параллельный I/O через MPI-IO | Запись/чтение результатов расчёта |
| **IMB-EXT** | Расширенные MPI-2 операции | Вспомогательное |
| **IMB-MT** | Многопоточное MPI (MPI_THREAD_MULTIPLE) | Если используется OpenMP + MPI в одном процессе |

**Ключевые бенчмарки из IMB-MPI1:**

- **PingPong** — латентность и пропускная способность между двумя процессами (база для любого сравнения)
- **Sendrecv / Exchange** — двунаправленный обмен соседями (модель halo-swap в OpenFOAM)
- **Allreduce** — глобальная редукция (норма невязки на каждой итерации решателя)
- **Alltoall / Alltoallv** — полный обмен (FFT-подобные операции, перестановки данных)
- **Bcast** — рассылка от root (обновление граничных условий)
- **Uniband / Biband** — односторонняя и двусторонняя пропускная способность с окном отправки

### NASA Parallel Benchmarks (NPB)

NPB — набор из 8 задач (5 ядер + 3 псевдо-приложения), производных от CFD.
Каждый тест — реальный расчёт с реальной вычислительной нагрузкой и реальным паттерном коммуникаций.

| Бенчмарк | Что делает | Что моделирует в OpenFOAM |
|---|---|---|
| **BT** | Блочно-трёхдиагональный решатель, 5×5 блоки | Неявные решатели, блочная структура |
| **SP** | Скалярный пентадиагональный решатель | Линеаризация уравнений Навье-Стокса |
| **LU** | LU-разложение, метод Гаусса-Зейделя | Прямые решатели для плотных систем |
| **MG** | Многосеточный метод | GAMG-предобуславливатель |
| **CG** | Метод сопряжённых градиентов, нерегулярный доступ к памяти | smoothSolver, PCG |
| **FT** | 3D БПФ | Спектральные методы, Alltoall-интенсивный |
| **EP** | Идеально параллельная задача (без обменов) | Пиковая производительность, baseline |
| **IS** | Целочисленная сортировка | Ребалансировка нагрузки, перераспределение |

NPB-MZ (Multi-Zone) добавляет гибридные версии BT, SP, LU с поддержкой MPI+OpenMP — прямо релевантно гибридному подходу.

---

## Сравнение по ключевым параметрам

| Параметр | IMB | NPB | VTune |
|---|---|---|---|
| **Цель** | Производительность MPI-примитивов | Производительность CFD-подобных приложений | Профилирование конкретного кода |
| **Вычислительная нагрузка** | Нет — только коммуникация | Реальная — арифметика + память + коммуникация | Зависит от анализируемого приложения |
| **Метрики** | Латентность (мкс), пропускная способность (MB/s) | Время (s), Mop/s, MFLOPS | CPU time, cache misses, DRAM bandwidth, CPI, thread imbalance |
| **Размер сообщения** | Настраиваемый: 0 байт — 4 МБ+ | Определяется классом задачи и числом процессов | N/A |
| **Масштаб** | От 2 процессов | Классы S, W, A, B, C, D, E — рост ~4x на класс | От 1 процесса |
| **Гибрид MPI+OpenMP** | IMB-MT (ограниченно) | NPB-MZ (полноценно, 3 реализации) | Threading analysis |
| **Чувствительность к NUMA** | Косвенная (через shared memory transport) | Прямая — layout данных и доступ к памяти | Прямая — QPI bandwidth, remote/local DRAM ratio |
| **Чувствительность к SIMD** | Нет — нет вычислений | Прямая — SSE4.2 ускоряет BT, SP, LU | Vector Instruction Set (статический анализ) |
| **Реалистичность для OpenFOAM** | Низкая (микро-уровень) | Высокая (макро-уровень) | Максимальная (до строки кода) |
| **Время прогона** | Секунды-минуты | Минуты-часы (класс C/D) | Минуты (зависит от задачи) |
| **SHM-транспорт (I_MPI_SHM)** | Прямое влияние | Косвенное | Косвенное |

---

## Как применять к стеку

### Этап 1: Базовая диагностика сети и памяти — IMB

IMB даёт «чистые» числа, без шума от вычислений. Запускается перед любым сравнением конфигураций.

**Что гонять:**

```bash
# Латентность и пропускная способность, точечный обмен
mpirun -np 2 IMB-MPI1 PingPong

# Двунаправленный обмен соседями (модель halo-swap)
mpirun -np 8 IMB-MPI1 Exchange

# Глобальная редукция (норма невязки)
mpirun -np 8 IMB-MPI1 Allreduce

# Полный обмен (FFT-подобные операции)
mpirun -np 8 IMB-MPI1 Alltoall

# Односторонняя пропускная способность
mpirun -np 8 IMB-MPI1 Uniband

# Несколько бенчмарков за один прогон
mpirun -np 8 IMB-MPI1 PingPong,Exchange,Allreduce,Alltoall,Sendrecv
```

**Что получится:**
- Зависимость латентности от размера сообщения — выявит overhead MPI-стека
- Пропускную способность для shared memory — покажет потолок для halo-swap
- Сравнение `I_MPI_FABRICS=shm:ofi` (SHM включён) vs `ofi` (SHM отключён) — overhead SHM vs OFI для внутриузлового обмена
- Allreduce-профиль — насколько быстро идёт сбор нормы невязки при росте числа процессов

**NUMA-специфика:** запустите PingPong с привязкой процессов к разным NUMA-узлам:

```bash
# Процессы на одном NUMA-узле
mpirun -np 2 -genv I_MPI_PIN_DOMAIN=0 IMB-MPI1 PingPong

# Процессы на разных NUMA-узлах
mpirun -np 2 -genv I_MPI_PIN_DOMAIN=[0-1] IMB-MPI1 PingPong
```

Разница в латентности покажет overhead QPI/NUMA-переходов — критично для выбора схемы 4 MPI × 4 OpenMP vs 8 MPI × 2 OpenMP.

**Сравнение SHM-транспортов (для Westmere):**

```bash
# Автоопределение (должно выбрать SSE-based драйвер)
I_MPI_SHM=auto mpirun -np 8 IMB-MPI1 Exchange

# Явный SSE-драйвер
I_MPI_SHM=bdw_sse mpirun -np 8 IMB-MPI1 Exchange

# SHM полностью отключён (OFI даже внутри узла)
I_MPI_FABRICS=ofi mpirun -np 8 IMB-MPI1 Exchange
```

### Этап 2: Тестирование под вычислительной нагрузкой — NPB

После того как IMB показал «чистую» коммуникацию, NPB добавляет вычислительную составляющую.
Ответ на вопрос: «А как вся инфраструктура ведёт себя, когда процессы ещё и считают?»

**Что гонять и почему:**

| Тест NPB | Зачем для OpenFOAM | Класс |
|---|---|---|
| **MG** | GAMG-предобуславливатель: многосеточные циклы, локальные и глобальные обмены | C (512³) или B (256³) |
| **CG** | PCG/smoothSolver: нерегулярный доступ к памяти, чувствительность к локальности | C (150000 строк) |
| **BT** | Неявные решатели с блочной структурой 5×5: плотная локальная работа + коммуникации | C (162³) |
| **SP** | Пентадиагональные системы: баланс вычислений и обменов | C (162³) |
| **FT** | Alltoall-интенсивный: покажет, насколько сеть тянет тяжёлые коллективы | C (512³) |
| **EP** | Baseline без обменов: потолок производительности ядер | C (2³³) |

**Гибрид MPI+OpenMP — используйте NPB-MZ:**

```bash
# Чистый MPI: 16 процессов
mpirun -np 16 ./bt-mz.C.16

# Гибрид: 4 MPI × 4 OpenMP
OMP_NUM_THREADS=4 mpirun -np 4 ./bt-mz.C.4

# Гибрид: 2 MPI × 8 OpenMP
OMP_NUM_THREADS=8 mpirun -np 2 ./bt-mz.C.2
```

Исследования показывают, что гибридные версии NPB-MZ могут превосходить чистый MPI
на 8–21% на крупных системах за счёт снижения коммуникационного overhead.
Это прямой ориентир для выбора схемы распараллеливания.

**SIMD/векторизация — сравнение двух конфигураций (Intel SSE4.2 vs GCC SSE4.2):**

```bash
# Intel + SSE4.2 (Westmere)
mpirun -np 8 ./bin/bt.C.8.intel_sse42

# GCC + SSE4.2 (Westmere)
mpirun -np 8 ./bin/bt.C.8.gcc_sse42

# Baseline: Intel без векторизации (-no-vec)
mpirun -np 8 ./bin/bt.C.8.intel_novec
```

Сравнение Mop/s между конфигурациями покажет:
- Прирост от скалярного кода → SSE4.2 векторизации
- Разницу Intel vs GCC (обычно Intel быстрее на NPB за счёт лучшей автовекторизации)
- Эффективность векторизации: `-qopt-report=4` (Intel) или `-fopt-info-vec` (GCC) покажут, какие циклы векторизованы

### Этап 3: Совместная интерпретация

Ключевая идея — сопоставить микро- и макро-результаты:

1. **IMB PingPong** показал латентность 1 мкс → проверьте, насколько NPB-MG страдает от этой латентности при каждом V-cycle
2. **IMB Allreduce** показал 5 мкс на 8 процессах → сравните с NPB-CG, где Allreduce вызывается на каждой итерации CG
3. **IMB Exchange** показал 10 GB/s пропускной способности → проверьте, не упирается ли NPB-BT в этот потолок при halo-swap
4. **NPB-EP** дал 500 Mop/s на ядро → это потолок; NPB-MG на 60% от EP уже хороший результат
5. **NUMA-разница** из IMB PingPong (например, 0.5 мкс локально vs 2 мкс через QPI) → объяснит, почему NPB-MG с 8 MPI-процессами на одном NUMA-узле быстрее, чем 8 MPI на двух узлах
6. **SHM vs OFI** из IMB Exchange → overhead SHM vs OFI для внутриузлового обмена: если SHM быстрее на 30%+ → `I_MPI_FABRICS=shm:ofi` (включить SHM) предпочтительнее `ofi`

---

## Профилирование с VTune

### Ограничения железа: Westmere X5675

X5675 — архитектура Westmere-EP (32 нм, 2011 год): 6 ядер, 12 потоков, SSE4.2, **без AVX/AVX-512**.

**Критическое ограничение:** начиная с VTune 2020, Westmere официально **не поддерживается** как целевая платформа для расширенных типов анализа. Однако базовые типы анализа, использующие стандартные счётчики PMU, продолжают работать — Westmere имеет полный набор событий для L1/L2/LLC misses, branch prediction, clock cycles и инструкций.

**Известная проблема:** на Westmere с включёнными C-states (ACPI Cn) аппаратный сэмплинг может вызвать зависание системы из-за бага в процессоре (errata AAJ134). Перед профилированием нужно отключить C-states в BIOS.

### Матрица возможностей VTune на X5675

| Тип анализа VTune | Доступность на X5675 | Что даёт для OpenFOAM |
|---|---|---|
| **Hotspots** (EBS) | ✅ Полностью (VTune 2019; на 2021+ — через driverless perf) | Топ функций по CPU time — увидите, где OpenFOAM проводит время: PCG, GAMG, сборка матрицы, граничные условия |
| **Threading** (user-mode) | ✅ Полностью | Балансировка OpenMP-потоков, lock contention, время ожидания на барьерах |
| **HPC Performance** | ✅ Базовый уровень | Объединяет hotspots + MPI time + CPU utilization — общая картина |
| **Memory Access** (базовый) | ⚠️ Частично | Cache miss rates (L1/L2/LLC), DRAM bandwidth, QPI bandwidth — но без атрибуции по объектам памяти |
| **Memory Access** (с memory objects) | ❌ Нет | Требует Haswell+ — нельзя привязать cache misses к конкретным массивам OpenFOAM |
| **Microarchitecture Exploration** | ⚠️ Ограниченно | Базовые метрики (pipeline stalls, branch misses), но без продвинутой декомпозиции FMA/ports |
| **Vectorization / SIMD** | ⚠️ Частично | Vector Instruction Set — да (статический анализ бинарника); Scalar/Packed ratio — да (через PMU на VTune 2019); GFLOPS — нет (требует Ivy Bridge+) |
| **NUMA / QPI Bandwidth** | ✅ Да | Remote/Local DRAM ratio, QPI outgoing bandwidth — критично для двухсокетных систем |
| **MPI Analysis** | ✅ Да | Профилирование MPI-вызовов, время в MPI vs вычисления — через интеграцию с Intel MPI |

### Что VTune реально предоставляет для SSE-векторизации

**Уточнение по документации Intel:**

Анализ **HPC Performance Characterization** в VTune содержит раздел **FPU Utilization**, который напрямую работает с SSE-векторизацией:

- **Vector Instruction Set** — колонка, идентифицирующая используемый набор инструкций: SSE, SSE2, AVX, AVX2, AVX-512. Виден в Summary и в Bottom-up по каждому циклу/функции. Это **статический анализ бинарника** — работает независимо от PMU, не требует поддержки со стороны процессора.

- **Scalar vs Packed GFLOPS** — показывает долю скалярных и векторизованных (packed) инструкций. На Westmere доступно через PMU-события `FP_COMP_OPS_EXE.SSE_PACKED_DOUBLE`, `FP_COMP_OPS_EXE.SSE_SCALAR_DOUBLE` и т.д. — на VTune 2019 и ранее.

- **GFLOPS / FPU Utilization %** — **недоступно на Westmere**. Документация VTune 2018 указывает: «FPU and GFLOPS metrics are supported on 3rd Generation Intel Core processors and later» — Westmere (Nehalem-based) туда не входит.

- **Issue descriptions** — VTune генерирует предупреждения вида «vectorized with legacy instruction set» (SSE вместо AVX/AVX-512), «non-vectorized», «bandwidth bound — not benefiting from vectorization».

- **Top Loops/Functions with FPU Usage** — топ функций по использованию FPU с разбивкой на scalar/packed и указанием ISA.

**Вывод:** VTune умеет идентифицировать SSE/SSE2/SSE4.2 инструкции в коде (Vector Instruction Set — статический анализ), показывать соотношение scalar vs packed (через PMU на поддерживаемых версиях), флаговать «legacy instruction set» и показывать top функций/циклов по FPU utilisation. На Westmere ограничение не в «отсутствии анализа SSE», а в том, что часть FPU-метрик (GFLOPS) требует Ivy Bridge+, а EBS на VTune 2021+ официально не поддерживается на Westmere (но VTune 2019 или driverless perf collection работают).

### Что конкретно VTune покажет для OpenFOAM на X5675

#### 1. Подтверждение, что OpenFOAM — memory-bound

VTune Memory Access покажет:
- **DRAM Bound** — сколько clockticks процессор простаивает из-за ожидания данных из памяти
- **LLC (L3) Bound** — Westmere имеет 12 MB L3 на сокет; OpenFOAM с крупными сетками быстро переполняет его
- **Bandwidth Utilization Histogram** — насколько близко к пику ~32 GB/s (triple-channel DDR3-1333) вы подходите

#### 2. NUMA-эффекты

- **Remote/Local DRAM ratio** — если потоки с одного NUMA-узла лезут в память другого
- **QPI outgoing bandwidth** — трафик между сокетами; высокий QPI-трафик = плохая локальность
- Корреляция с конкретными функциями — какие функции OpenFOAM генерируют remote-доступы

#### 3. OpenMP threading — баланс и overhead

- **Thread imbalance** — если 4 OpenMP-потока внутри MPI-процесса работают неравномерно
- **Lock contention** — где потоки ждут друг друга
- **Barrier wait time** — время на неявных барьерах OpenMP

#### 4. MPI time vs Compute time

- Долю времени в MPI-вызовах (`MPI_Wait`, `MPI_Allreduce`, `MPI_Sendrecv`) vs вычислениях
- Какие именно MPI-функции доминируют — для OpenFOAM это обычно `MPI_Waitall` (halo-swap) и `MPI_Allreduce` (норма невязки)

### Как связать VTune с NPB и IMB

```
IMB → «чистая инфраструктура» → что в принципе может дать система
  ↓
NPB → «CFD-подобная нагрузка» → как инфраструктура работает под реальным паттерном
  ↓
VTune + OpenFOAM → «конкретный код» → почему именно этот код ведёт себя так на этом железе
```

### Практический запуск VTune на X5675

**Установка** — VTune бесплатен, скачивается standalone:
https://www.intel.com/content/www/cn/zh/developer/tools/oneapi/vtune-profiler-download.html

```bash
# Скачать standalone (Linux)
wget https://registrationcenter-download.intel.com/akdlm/IRC_NAS/<hash>/intel-vtune-<version>.sh
sudo sh ./intel-vtune-<version>.sh

# Или через APT (если добавлен репозиторий Intel)
sudo apt install intel-oneapi-vtune
```

**Перед запуском — обязательно отключить C-states в BIOS**, иначе система может зависнуть при EBS-сэмплинге на Westmere.

**Запуск Hotspots на OpenFOAM с MPI:**

```bash
# Базовый hotspot-профиль
mpirun -np 4 vtune -collect hotspots -r ./vtune_result \
  -- simpleFoam -parallel

# Для профилирования только rank 0
mpirun -np 4 vtune -collect hotspots -r ./vtune_rank0 \
  -trace-mpi=0 -- simpleFoam -parallel
```

**Memory Access (базовый, без object tracking):**

```bash
vtune -collect memory-access -r ./vtune_mem \
  -- simpleFoam -parallel
```

**HPC Performance (комбинированный):**

```bash
vtune -collect hpc-performance -r ./vtune_hpc \
  -- mpirun -np 4 simpleFoam -parallel
```

**Threading (для OpenMP):**

```bash
OMP_NUM_THREADS=4 vtune -collect threading -r ./vtune_threads \
  -- mpirun -np 4 simpleFoam -parallel
```

**Просмотр результатов:**

```bash
# Текстовый отчёт из CLI
vtune -report summary -r ./vtune_result
vtune -report hotspots -r ./vtune_result

# Или через GUI
vtune-gui ./vtune_result
```

### Альтернатива для глубокого векторизационного анализа SSE

Если нужна именно глубокая векторизационная аналитика SSE-кода (анализ зависимостей в циклах, рекомендации по векторизации), то **Intel Advisor** — более подходящий инструмент. Он специализируется именно на векторизации, тогда как VTune — более широкий профайлер.

### Driverless fallback через perf

Если VTune 2021+ не запускает EBS на Westmere:

```bash
# Базовые счётчики через perf
perf stat -e cache-misses,cache-references,LLC-load-misses,LLC-loads,\
branch-misses,branch-instructions,\
mem_load_retired.llc_miss,\
offcore_response.demand_data_rd.any_response \
  mpirun -np 4 simpleFoam -parallel

# NUMA-метрики (если поддерживается)
perf stat -e node-loads,node-load-misses,\
node-stores,node-store-misses \
  mpirun -np 4 simpleFoam -parallel
```

### Что VTune НЕ покажет на X5675 — и чем заменить

| Недоступно на X5675 | Альтернатива |
|---|---|
| GFLOPS / FPU Utilization % (требует Ivy Bridge+) | `perf stat` с Westmere-счётчиками; компилятор reports (`-qopt-report=5` для Intel, `-fopt-info-vec` для GCC) |
| Memory object attribution (Haswell+) | Ручной анализ: сопоставить hotspots с исходным кодом |
| Advanced Microarchitecture Exploration | `perf stat` с ручным набором Westmere-событий |
| GPU analysis | Неактуально — нет GPU |

---

## Практический чек-лист запуска

### IMB (быстро, 10–30 минут на все)

```bash
# Установка
git clone https://github.com/intel/mpi-benchmarks.git
cd mpi-benchmarks
make -f make_mpistub.txt

# Базовый прогон
mpirun -np 8 IMB-MPI1 PingPong,Exchange,Allreduce,Alltoall,Sendrecv

# С NUMA-привязкой
mpirun -np 8 -genv I_MPI_PIN=on -genv I_MPI_PIN_DOMAIN=auto IMB-MPI1 PingPong,Exchange,Allreduce

# Сравнение SHM-транспортов
I_MPI_SHM=auto mpirun -np 8 IMB-MPI1 Exchange
I_MPI_SHM=bdw_sse mpirun -np 8 IMB-MPI1 Exchange
I_MPI_FABRICS=ofi mpirun -np 8 IMB-MPI1 Exchange  # SHM отключён

# Неблокирующие коллективы (перекрытие)
mpirun -np 8 IMB-NBC Iallreduce,Ialltoall
```

### NPB (дольше, 1–4 часа на полный набор)

```bash
# Установка (рекомендуемый репозиторий — matthiasdiener/nas)
git clone https://github.com/matthiasdiener/nas.git
cd nas/NPB3.3-MPI
cp config/make.def.template make.def
# Отредактируйте make.def — см. раздел «Конфигурирование компиляторов»

# Компиляция класса C (Intel + SSE4.2)
make mg CLASS=C NPROCS=8
make bt CLASS=C NPROCS=9   # BT требует квадратное число процессов
make cg CLASS=C NPROCS=8

# Запуск
mpirun -np 8 ./bin/mg.C.8
```

### NPB-MZ (гибрид, MPI+OpenMP)

```bash
# Установка (из matthiasdiener/nas — есть все три реализации MZ)
cd nas/NPB3.3-MZ-MPI
cp config/make.def.template make.def
# Отредактируйте make.def — см. раздел «Конфигурирование компиляторов»

# Сборка
make bt-mz CLASS=C NPROCS=4

# Запуск
OMP_NUM_THREADS=4 mpirun -np 4 ./bin/bt-mz.C.4
```

### VTune (профилирование OpenFOAM)

```bash
# HPC Performance (комбинированный)
vtune -collect hpc-performance -r ./vtune_hpc \
  -- mpirun -np 4 simpleFoam -parallel

# Memory Access
vtune -collect memory-access -r ./vtune_mem \
  -- mpirun -np 4 simpleFoam -parallel

# Threading
OMP_NUM_THREADS=4 vtune -collect threading -r ./vtune_threads \
  -- mpirun -np 4 simpleFoam -parallel

# Отчёт
vtune -report summary -r ./vtune_hpc
vtune -report hotspots -r ./vtune_hpc
```

---

## Сводная таблица: что отвечает на какой вопрос

| Вопрос | Инструмент | Что смотреть |
|---|---|---|
| Какая латентность между NUMA-узлами? | IMB PingPong | t[usec] для 0-byte сообщений, два прогона с разной привязкой |
| Хватит ли пропускной способности для halo-swap? | IMB Exchange | Mbytes/sec для сообщений 4–64 KB |
| Какой SHM-транспорт лучше для Westmere? | IMB Exchange с разным `I_MPI_SHM` | `auto` vs `bdw_sse` vs `ofi` (SHM отключён) |
| `shm:ofi` или `ofi` для внутриузлового обмена? | IMB Exchange | `I_MPI_FABRICS=shm:ofi` (SHM включён) vs `ofi` (SHM отключён) |
| Какая схема MPI×OpenMP лучше для GAMG? | NPB-MG (MPI) vs NPB-MZ-BT (hybrid) | Mop/s, сравнение 8×1 vs 4×4 vs 2×8 |
| Упирается ли решатель в Allreduce? | IMB Allreduce + NPB-CG | Доля Allreduce в общем времени CG |
| Даёт ли SSE4.2-векторизация прирост? | NPB BT/SP/LU + VTune Vector Instruction Set | Scalar vs Packed ratio, Mop/s novec vs SSE4.2 |
| Intel или GCC для NPB на Westmere? | NPB BT/SP/LU | Mop/s Intel SSE4.2 vs GCC SSE4.2 |
| Сколько можно выиграть на перекрытии? | IMB-NBC Iallreduce | Разница между pure и overlap режимами |
| Где узкое место при масштабировании? | NPB EP vs MG vs FT | EP — потолок, FT — коммуникации, MG — баланс |
| Нужен ли многопутевой бинарник? | Сравнение `-xSSE4.2` vs `-axSSE4.2 -xSSE2` | Если запуски только на Westmere — `-xSSE4.2` |
| Где OpenFOAM теряет время? | VTune Hotspots | Топ функций по CPU time |
| OpenFOAM упирается в память? | VTune Memory Access | DRAM Bound %, LLC miss rate, bandwidth utilization |
| NUMA-локальность данных в OpenFOAM? | VTune Memory Access | Remote/Local DRAM ratio, QPI bandwidth |
| Баланс OpenMP-потоков? | VTune Threading | Thread imbalance, barrier wait time |
| Доля MPI vs вычислений? | VTune HPC Performance | MPI time %, топ MPI-функций |
| Векторизован ли код OpenFOAM? | VTune Vector Instruction Set | SSE/SSE2/Scalar, top functions |
| Где заблокированы FP-оптимизации? | Анализ wmake rules | `-fp-model precise` + `-frounding-math` = ~15–25% потерь |

---

## Итог

**IMB** — диагностика инфраструктуры (быстро, точно, изолированно).
**NPB** — проверка реальной работы (медленно, комплексно, реалистично).
**VTune** — профилирование конкретного кода (до строки, с микроархитектурными метриками).

Запускаете сначала IMB, получаете чистые числа.
Потом NPB — и если NPB ведёт себя хуже, чем ожидалось по IMB,
значит проблема в вычислительной части (SIMD, NUMA-локальность данных, балансировка),
а не в сети.
Потом VTune на OpenFOAM — и если OpenFOAM ведёт себя хуже, чем NPB,
значит проблема в коде OpenFOAM (hotspots, cache misses, thread imbalance),
а не в инфраструктуре.

---

## Ссылки

| Ресурс | URL |
|---|---|
| Intel MPI Benchmarks | https://github.com/intel/mpi-benchmarks |
| NPB (LLNL fork, версия 3.4 + 3.4-MZ) | https://github.com/llnl/NPB |
| NPB (matthiasdiener — все реализации MPI/OMP/MZ) | https://github.com/matthiasdiener/nas |
| NPB-MZ (tpatki — только MZ-MPI) | https://github.com/tpatki/NPB3.3.1-MZ |
| NPB (архивный миррор 3.0/3.3.1/3.4) | https://github.com/mbdevpl/nas-parallel-benchmarks |
| Официальный сайт NPB (NASA) | https://www.nas.nasa.gov/publications/npb.html |
| Документация IMB (Intel) | https://www.intel.com/content/www/us/en/docs/mpi-library/user-guide-benchmarks/2021-2/overview.html |
| Intel Compiler: `-ax` (automatic CPU dispatch) | https://www.intel.com/content/www/us/en/docs/dpcpp-cpp-compiler/developer-guide-reference/2025-1/ax-qax.html |
| LLNL: Intel Compiler Vectorization Guide | https://hpc.llnl.gov/software/development-environment-software/intel-compiler-vectorization |
| Intel Compiler: `-fp-model` (Corden FP control, CERN 2012) | https://indico.cern.ch/event/166141/sessions/125686/attachments/201416/282784/Corden_FP_control.pdf |
| Intel Compiler: Балансируя между точностью и производительностью (Habr) | https://habr.com/ru/companies/intel/articles/160747/ |
| Intel Community: ICX isinf bug | https://community.intel.com/t5/Intel-C-Compiler/icx-2021-3-0-bug-isinf-wrong-result/m-p/1317649 |
| LLVM Discussion: `-ffp-model=fast` and `-ffinite-math-only` | https://discourse.llvm.org/t/making-ffp-model-fast-more-user-friendly/78402 |
| Intel Porting Guide: ICC → ICX | https://www.intel.cn/content/www/cn/zh/developer/articles/guide/porting-guide-for-icc-users-to-dpcpp-or-icx.html |
| Intel Compiler: `-qopt-streaming-stores` | https://www.intel.com/content/www/us/en/docs/cpp-compiler/developer-guide-reference/2021-10/qopt-streaming-stores-qopt-streaming-stores.html |
| Intel Compiler: Quick Reference Guide v19 | https://wolke.img.univie.ac.at/documentation/general/Fortran/quick-reference-guide-intel-compilers-v19-1-final.pdf |
| GCC: `-ffast-math` optimization (Habr) | https://habr.com/ru/companies/ruvds/articles/586386/ |
| DAFoam: OpenFOAM compilation with Icc | https://dafoam.github.io/v3-pages/mydoc_installation_source_hpc.html |
| OpenFOAM Issue #3251: Icx -pthread | https://develop.openfoam.com/Development/openfoam/-/issues/3251 |
| VTune Profiler: загрузка | https://www.intel.com/content/www/cn/zh/developer/tools/oneapi/vtune-profiler-download.html |
| VTune: HPC Performance Characterization | https://www.intel.com/content/www/us/en/docs/vtune-profiler/tutorial-vtune-itac-mpi-openmp/2020/analyze-vector-instruction-set-with-intel-vtune.html |
| Intel MPI: Fabrics Control | https://www.intel.com/content/www/us/en/docs/mpi-library/user-guide/2021-8/fabrics-control.html |
| IXPUG: Best Practices for MPI on Multicore | https://www.ixpug.org/resources/download/tutorial-performance-tuning-and-best-practices-to-leverage-intel-mpi-on-multicore-processors |














































Дополнения и исправления
Контекст и введение:

Добавить краткое введение о важности сравнительного анализа и оптимизации для CFD-расчетов.
Указать цель документа: предоставить комплексный анализ и рекомендации по настройке OpenFOAM на платформе Westmere X5675.
Практический чек-лист запуска:

Включить раздел с пошаговыми инструкциями по запуску бенчмарков и профилирования с VTune.
Добавить рекомендации по мониторингу производительности.
Сводная таблица: что отвечает на какой вопрос:

Создать таблицу, где будут сопоставлены различные тесты и метрики, которые они измеряют.
Профилирование с VTune:

Добавить описание, как использовать VTune для анализа производительности OpenFOAM.
Включить примеры отчетов VTune и их интерпретацию.
FP-модель и OpenFOAM:

Добавить раздел о настройке FP-модели для OpenFOAM, особенно в контексте использования SSE4.2.
Указать на возможные проблемы и их решения.
Многопутевой бинарник:

Добавить описание и примеры использования многопутевых бинарников для автоматической диспетчеризации CPU.
Ссылки:

Добавить актуальные ссылки на репозитории и документацию.
Включить ссылки на дополнительные ресурсы по оптимизации и профилированию.
Исправленный и дополненный документ
Сравнительный анализ NPB, Intel MPI Benchmarks и VTune применительно к стеку OpenFOAM / гибридный MPI+OpenMP / NUMA / SSE4.2 (Westmere X5675)
Введение
В этом документе представлен комплексный анализ производительности стека OpenFOAM на платформе Westmere X5675 с использованием различных инструментов и методик. Основное внимание уделяется настройке и оптимизации для достижения максимальной производительности.

Оглавление
Репозитории и установка из исходного кода
Конфигурирование компиляторов: два варианта для Westmere
Многопутевой бинарник: automatic CPU dispatch
FP-модель и OpenFOAM: критический анализ
Переменные окружения Intel MPI
Что измеряет каждый набор
Сравнение по ключевым параметрам
Как применять к стеку
Профилирование с VTune
VTune:

Установка и настройка VTune.
Примеры использования для анализа OpenFOAM.
Интерпретация отчетов VTune.
Практический чек-лист запуска
Установка и настройка OpenFOAM.
Конфигурирование компиляторов.
Запуск бенчмарков (NPB, IMB).
Профилирование с VTune.
Мониторинг производительности.
Сводная таблица: что отвечает на какой вопрос
Вопрос	Инструмент/Метрика
Латентность MPI	IMB
Производительность CFD	NPB
Профилирование	VTune
Настройка компиляции	Конфигурирование
Ссылки
https://github.com/matthiasdi

