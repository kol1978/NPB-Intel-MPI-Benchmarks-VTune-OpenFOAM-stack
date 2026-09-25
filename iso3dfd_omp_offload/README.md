# Пример `Guided iso3dfd OpenMP Offload`

Пример `Guided iso3dfd OpenMP Offload` демонстрирует, как можно:

1. Анализировать производительность приложения при работе на центральном процессоре
2. Добавить директивы OpenMP для увеличения параллелизма
3. Загрузить оптимизированный код на графические процессоры Intel

В этом рабочем процессе используются компоненты oneAPI, доступные в Intel® oneAPI Base Toolkit и Intel® HPC Toolkit.

`iso3dfd` — трёхмерный трафарет, предназначенный для имитации волны, распространяющейся в трёхмерной изотропной среде. Пример показывает типичные проблемы, возникающие при переносе приложений на устройства OpenMP Offload, и методы их решения для достижения высокой производительности.

---

## Расширения данного репозитория

Оригинальный пример Intel рассчитан на наличие Intel GPU. В данном репозитории добавлены расширения для работы на **CPU-only серверах** без GPU, а также инструменты автоматизированного профилирования через VTune и APS.

| Документ | Назначение |
| --- | --- |
| [`NO_OFFLOAD_PATCH.md`](NO_OFFLOAD_PATCH.md) | Патч для CPU-only режима: обёртка проверки GPU в `#ifndef NO_OFFLOAD` и условный флаг `-fopenmp-targets=spir64` в CMakeLists.txt |
| [`vtune_profiling_guide.md`](vtune_profiling_guide.md) | Руководство по профилированию: скрипт `run_vtune_hotspots.sh`, экспорт CSV, анализ результатов, APS |
| [`profiling_compilation_levels.md`](profiling_compilation_levels.md) | Уровни модификации компиляции для профилирования: флаги `-g`, `-mllvm -parallel-source-info=2`, `-fdebug-info-for-profiling`, CMake-опция `ENABLE_PROFILING` |
| `vtune` | Скрипт запуска VTune hotspots с автоматическим управлением ptrace_scope (требует `sudo`) | [vtune](https://github.com/kol1978/NPB-Intel-MPI-Benchmarks-VTune-OpenFOAM-stack/blob/main/iso3dfd_omp_offload/build/src/vtune) |
| `analyze` | Автоматический парсинг CSV-отчётов VTune: сводные метрики, топ горячих функций, таблица рангов MPI, статистика и оценки | [analyze](https://github.com/kol1978/NPB-Intel-MPI-Benchmarks-VTune-OpenFOAM-stack/blob/main/iso3dfd_omp_offload/build/src/analyze) |


---

## Структура образца iso3dfd

Существует пять версий проекта iso3dfd:

- **CPU Only Implementation** — базовая последовательная реализация для CPU.
- **GPU Offload Unoptimized** — OpenMP offload `target parallel for` с `collapse`.
- **GPU Offload Optimized 1** — OpenMP offload `teams distribute` с `num_teams` и `thread_limit`.
- **GPU Offload Optimized 2** — `teams distribute` с улучшенным шаблоном доступа к данным.
- **GPU Offload Optimized 3** — итерации по третьему измерению.

Пример имеет один исполняемый файл. Для запуска каждой реализации используйте соответствующие команды cmake.

---

## Рабочий процесс iso3dfd OpenMP Offload

| Шаг | Версия примера | Тип анализа | Инструмент |
| --- | --- | --- | --- |
| **Шаг 1:** Установка переменных среды | --- | --- | --- |
| **Шаг 2:** Сборка примера iso3dfd | --- | --- | --- |
| **Шаг 3:** Определение самых ресурсоёмких циклов | CPU Only | Hotspots Analysis | Intel VTune Profiler |
| **Шаг 4:** Анализ векторизации | CPU Only | Vectorization Analysis | Intel Advisor |
| **Шаг 5:** Определение прибыльного кода для разгрузки | CPU Only | Offload Advisor | Intel Advisor |
| **Шаг 6:** Определение зависимости от CPU/GPU | GPU Offload Unoptimized | GPU Offload Analysis | Intel VTune Profiler |
| **Шаг 7:** Анализ разгрузки для оптимизации ядра GPU | GPU Offload Unoptimized | GPU Compute/Media Hotspots | Intel VTune Profiler |
| **Шаг 8:** Повышение производительности приложения | GPU Offload Optimized 2 | GPU Roofline Analysis | Intel Advisor |

---

## Подход

В руководстве по образцу `iso3dfd` используются Intel VTune Profiler и Intel Advisor для профилирования производительности. Инструменты применяются для:

1. Выявления наиболее ресурсоёмких циклов и функций.
2. Моделирования разгрузки для определения выгодных участков кода.
3. Анализа участков кода OpenMP, перенесённых на GPU.

`iso3dfd` — конечно-разностное ядро для решения трёхмерного уравнения акустической изотропной волны. Ядра реализованы как схема 16-го порядка в пространстве с симметричными коэффициентами и схема 2-го порядка во времени без граничных условий.

Код ищет доступный GPU или другое устройство, подходящее для OpenMP Offload. Если совместимое устройство не обнаружено — код завершает работу. **При сборке с `NO_OFFLOAD=ON`** (см. [`NO_OFFLOAD_PATCH.md`](NO_OFFLOAD_PATCH.md)) проверка GPU пропускается, и программа работает в режиме CPU-only.

По умолчанию в выводе отображается текущая реализация (`CPU Only`, `GPU Offload Unoptimized`, `GPU Offload Optimized 1/2/3`) с метриками: время, пропускная способность, флопс, байты/с.

---

## Требования

| Операционная система | Аппаратное обеспечение | Программное обеспечение |
| --- | --- | --- |
| Ubuntu 18.04+ | Skylake с GEN9 или новее (для GPU-offload) | Intel oneAPI DPC++/C++ Compiler |
| | **CPU-only сервер без GPU** (с патчем `NO_OFFLOAD`) | Intel VTune Profiler |
| | | Intel Advisor |
| | | Intel MPI (для MPI-профилирования) |

---

# Шаг 1. Установка переменных среды

При работе с CLI настройте инструменты через переменные среды:

```bash
# Общесистемная установка
source /opt/intel/oneapi/setvars.sh

# Частная установка
source ~/intel/oneapi/setvars.sh
```

> **Примечание:** для анализа GPU на Linux включите сбор аппаратных метрик:
>
> ```bash
> sudo sysctl -w dev.i915.perf_stream_paranoid=0
> ```
>
> Для постоянного изменения:
>
> ```bash
> echo dev.i915.perf_stream_paranoid=0 | sudo tee /etc/sysctl.d/60-mdapi.conf
> ```

---

# Шаг 2. Сборка примера iso3dfd

## Стандартная сборка (оригинальные версии)

```bash
mkdir build && cd build

# CPU Only
cmake -DNO_OFFLOAD=1 -DVERIFY_RESULTS=0 ..
make -j

# GPU Offload Unoptimized (по умолчанию)
cmake -DVERIFY_RESULTS=0 ..
make -j

# GPU Offload Optimized 1
cmake -DUSE_OPT1=1 -DVERIFY_RESULTS=0 ..
make -j

# GPU Offload Optimized 2
cmake -DUSE_OPT2=1 -DVERIFY_RESULTS=0 ..
make -j

# GPU Offload Optimized 3
cmake -DUSE_OPT3=1 -DVERIFY_RESULTS=0 ..
make -j
```

## Сборка для CPU-only сервера без GPU (с патчем)

Подробное описание патча — в [`NO_OFFLOAD_PATCH.md`](NO_OFFLOAD_PATCH.md).

```bash
mkdir build && cd build

# Применение патчей (см. NO_OFFLOAD_PATCH.md)
# Затем сборка:
cmake -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
      -DNO_OFFLOAD=ON \
      -DVERIFY_RESULTS=OFF \
      ..
make -j$(nproc)
```

## Сборка с флагами профилирования для VTune

Подробное описание уровней компиляции — в [`profiling_compilation_levels.md`](profiling_compilation_levels.md).

```bash
mkdir build && cd build

# С профилировочными флагами
cmake -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
      -DNO_OFFLOAD=ON \
      -DVERIFY_RESULTS=OFF \
      -DENABLE_PROFILING=ON \
      ..
make -j$(nproc)
```

Флаг `ENABLE_PROFILING=ON` добавляет: `-g -mllvm -parallel-source-info=2 -fdebug-info-for-profiling`

Без профилирования (release):
```bash
cmake -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
      -DNO_OFFLOAD=ON \
      -DVERIFY_RESULTS=OFF \
      ..
make -j$(nproc)
```

## Параметры приложения

```
src/iso3dfd n1 n2 n3 n1_block n2_block n3_block Iterations
```

| Параметр | Описание |
| --- | --- |
| `n1 n2 n3` | Размеры сетки. По умолчанию: `256 256 256` |
| `n1_block n2_block n3_block` | Размеры блоков кэша (CPU) или тайлов (GPU Offload). По умолчанию: `16 8 64` |
| `Iterations` | Количество временных шагов. По умолчанию: `100` |

Синтаксис по умолчанию: `src/iso3dfd 256 256 256 16 8 64 100`

---

# Шаг 3. Определение самых ресурсоёмких циклов

Используйте VTune Profiler для анализа горячих точек в версии `CPU Only`.

## Стандартный запуск VTune (оригинальный workflow)

**Сборка:**
```bash
cmake -DNO_OFFLOAD=1 -DVERIFY_RESULTS=0 ..
make -j
```

**Команда:**
```bash
vtune -collect hotspots -knob sampling-mode=hw \
  --result-dir=./vtune_hotspots \
  -- src/iso3dfd 256 256 256 16 8 64 100
```

## Расширенный запуск VTune с MPI (данный репозиторий)

Полное руководство — в [`vtune_profiling_guide.md`](vtune_profiling_guide.md).

**Сборка с профилировочными флагами:**
```bash
cmake -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
      -DNO_OFFLOAD=ON \
      -DVERIFY_RESULTS=OFF \
      -DENABLE_PROFILING=ON \
      ..
make -j$(nproc)
```

**Запуск через скрипт:**
```bash
sudo ./run_vtune_hotspots.sh
```

Скрипт `run_vtune_hotspots.sh` автоматически:
- Проверяет root-права
- Подключает Intel oneAPI окружение
- Проверяет Intel MPI
- Сохраняет и восстанавливает `ptrace_scope`
- Запускает `mpirun -n 12` под VTune с `-trace-mpi`
- Сохраняет результат в `vtune_hotspots_mpi/`

**Автоматический анализ результатов:**
```bash
./analyze_vtune.sh vtune_hotspots_mpi
```

Скрипт `analyze_vtune.sh`:
- Создаёт три CSV-файла (`vtune_summary.csv`, `vtune_ranks.csv`, `vtune_hotspots.csv`)
- Выводит сводку по производительности (Elapsed, CPU, Effective, Spin, Overhead)
- Парсит и отображает распределение по MPI рангам
- Показывает горячие функции (Top Hotspots)
- Считает статистику: разброс, stddev, коэффициент вариации, эффективность

## Результаты VTune

После завершения сбора в окне **Summary** VTune:

- **Elapsed Time** — реальное время выполнения
- **CPU Time** — суммарное время по всем потокам
- **Microarchitecture Usage** — эффективность использования микроархитектуры

Самая ресурсоёмкая функция — **`Iso3dfdIteration`** (~94% CPU Time).

![Hotspots-Summary](img/261384680-1709f642-f0ac-4e8e-bb00-675ec28a112b.png)

Метрики **Microarchitecture Usage** и **Vectorization** отмечены как неэффективные. Микроархитектура используется на ~30%.

![Hotspots-bottom up](img/261385154-a8688154-d7cc-4057-b112-7d5038126b0a.png)

В окне **Bottom-Up** — разбивка по проблемным участкам и временная шкала активности потоков.

---

# Шаг 4. Анализ векторизации

Используйте Intel Advisor для анализа векторизации перед оценкой производительности GPU.

**Сборка:**
```bash
cmake -DNO_OFFLOAD=1 -DVERIFY_RESULTS=0 ..
make -j
```

**Команды:**
```bash
advisor --collect=survey --project-dir=./adv_vectorization -- ./src/iso3dfd 256 256 256 16 8 64 100
advisor --collect=tripcounts --flop --stacks --project-dir=./adv_vectorization -- ./src/iso3dfd 256 256 256 16 8 64 100
```

Вкладка **Recommendations** содержит советы по оптимизации.

![Vectorization-summary-new](img/275656782-bd33b40d-6ca3-491a-8d6b-23b3596ef915.png)

---

# Шаг 5. Определение прибыльного кода для разгрузки

Используйте Intel Advisor Offload Modeling для прогноза производительности на GPU **без доступа к оборудованию**.

**Сборка:**
```bash
cmake -DNO_OFFLOAD=1 -DVERIFY_RESULTS=0 ..
make -j
```

**Команда:**
```bash
advisor --collect=offload --config=gen12_tgl \
  --project-dir=./adv_offload_model \
  -- ./src/iso3dfd 256 256 256 16 8 64 100
```

Прогноз: ускорение ~1.97x для разгруженного кода, ~1.93x для всего приложения. 96% кода может быть разгружено.

![Offload-adv-gen12](img/264219171-97ff1f48-f5e6-4883-aa46-ea70f025e6c5.png)

![Offload-adv-gen12-2](img/275947439-7826f4c4-7b61-4e45-8eb0-340135ecc1bc.png)

### Как была реализована первоначальная разгрузка

**Pragma до разгрузки:**
```cpp
#pragma omp parallel default(shared)
```

**Pragma для OpenMP Offload:**
```cpp
#pragma omp target parallel for simd collapse(3)
```

---

# Шаг 6. Определение зависимости от CPU или GPU

Анализ версии `GPU Offload Unoptimized` через VTune GPU Offload Analysis.

**Сборка:**
```bash
cmake -DVERIFY_RESULTS=0 ..
make -j
```

**Команда:**
```bash
vtune -collect gpu-offload \
  --result-dir=./vtune_gpu_offload_unopt \
  -- ./src/iso3dfd 256 256 256 16 8 64 100
```

В окне **Summary** — процент времени на GPU и основные задачи. Приложение привязано к GPU.

![BASELINE-GPU-OFFLOAD-SUMMARY](img/275984951-d8f26d73-b2bb-4a9a-a106-38525b547577.png)

![BASELINE-GPU-OFFLOAD-GRAPHICS2](img/276409585-ee89da63-43d9-4737-b5c5-04cd565fb1e7.png)

---

# Шаг 7. Анализ разгрузки для оптимизации ядра GPU

GPU Compute/Media Hotspots Analysis в VTune Profiler.

**Сборка:**
```bash
cmake -DVERIFY_RESULTS=0 ..
make -j
```

**Команда:**
```bash
vtune -collect gpu-hotspots \
  --result-dir=./vtune_gpu_hotspots_unopt \
  -- ./src/iso3dfd 256 256 256 16 8 64 100
```

EU Array stalls — основная проблема. Разгрузка ограничена пропускной способностью L3.

![GPU-HOTSPots-Baseline-SUMMARY](img/276416174-e9f03b26-ab38-48f3-9f1f-e91e433675f6.png)

![NEW Graphics Baseline](img/276416139-b2cbadf5-0f77-4466-954a-6a28950f03ef.png)

---

# Шаг 8. Повышение производительности приложения

GPU Roofline Analysis в Intel Advisor для версии `GPU Offload Optimized 2`.

**Сборка:**
```bash
cmake -DUSE_OPT2=1 -DVERIFY_RESULTS=0 ..
make -j
```

**Команда:**
```bash
advisor --collect=roofline --profile-gpu --search-dir src:r=src \
  --project-dir=./adv_gpu_roofline_opt2 \
  -- ./src/iso3dfd 256 256 256 16 8 64 100
```

![opt2-roofline](img/274476155-0f02f76f-16d4-4ced-aa4d-2b74669d817b.png)

---

# Дополнение: Быстрый анализ MPI через Intel APS

Для быстрой оценки MPI-эффективности без полного VTune-профилирования используется Intel Application Performance Snapshot (APS). Подробности — в [`vtune_profiling_guide.md`](vtune_profiling_guide.md).

**Шаг 1 — Сбор данных:**

```bash
# Вариант A: флаг -aps
mpirun -n 12 -aps ./iso3dfd 256 256 256 16 8 64 100

# Вариант B: переменная окружения
export APS_ENABLE=1
mpirun -n 12 ./iso3dfd 256 256 256 16 8 64 100
```

APS создаёт директорию `aps_result_<дата>_<время>/` с raw-данными по каждому рангу.

**Шаг 2 — Генерация HTML-отчёта:**

```bash
aps --report aps_result_YYYYMMDD_HHMMSS
```

Создаёт файл `aps_report_YYYYMMDD_HHMMSS.html` — интерактивный отчёт с метриками MPI.

**Шаг 3 — Просмотр:**

```bash
firefox aps_report_YYYYMMDD_HHMMSS.html
```

### Сравнение APS и VTune

| Характеристика | VTune | APS |
| --- | --- | --- |
| Уровень детализации | Высокий (функции, потоки, инструкции) | Средний (MPI-уровень, ранги) |
| Требуемые права | root (для `sampling-mode=sw`) | Обычно не требует root |
| Время сбора | Дольше (глубокий профиль) | Быстро (лёгкая трассировка) |
| Ключевые метрики | Hotspots, CPI, Memory Bound, Spin Time | Время в MPI, дисбаланс рангов |
| Формат отчёта | CSV, GUI, CLI | HTML (интерактивный) |

---

# Дополнение: Управление потоками OpenMP

```bash
# По умолчанию — все ядра
OMP_NUM_THREADS=$(nproc) ./src/iso3dfd 256 256 256 16 8 64 100

# Ограничение числа потоков
OMP_NUM_THREADS=12 ./src/iso3dfd 256 256 256 16 8 64 100

# Привязка к NUMA-узлу
OMP_NUM_THREADS=12 numactl --membind=0 --cpunodebind=0 ./src/iso3dfd 256 256 256 16 8 64 100

# Interleave по двум NUMA-узлам
OMP_NUM_THREADS=24 numactl --interleave=0,1 ./src/iso3dfd 256 256 256 16 8 64 100
```

---

# Ожидаемый вывод

```
Grid Sizes: 256 256 256
Tile sizes: 16 8 64
Using no-offload implementation
--CPU-Only
Memory Usage (MBytes): 230
--------------------------------------
time         : 3.915 secs
throughput   : 428.537 Mpts/s
flops        : 26.1407 GFlops
bytes        : 5.14244 GBytes/s
--------------------------------------
```

### Метрики

| Метрика | Единица | Описание |
| --- | --- | --- |
| `time` | сек | Время выполнения kernel-цикла |
| `throughput` | Mpts/s | Миллионов точек сетки в секунду |
| `flops` | GFlops | Гигафлопс — эффективность SIMD |
| `bytes` | GBytes/s | Пропускная способность памяти |

---

# Шпаргалка: полный цикл профилирования

```bash
# ── Подготовка ──
source /opt/intel/oneapi/setvars.sh --force

# ── Сборка с профилированием ──
cd build
cmake -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
      -DNO_OFFLOAD=ON \
      -DVERIFY_RESULTS=OFF \
      -DENABLE_PROFILING=ON \
      ..
make -j$(nproc)

# ── VTune hotspots (MPI) ──
sudo ./run_vtune_hotspots.sh
./analyze_vtune.sh vtune_hotspots_mpi

# ── APS (быстрый MPI-анализ) ──
export APS_ENABLE=1
mpirun -n 12 ./iso3dfd 256 256 256 16 8 64 100
aps --report aps_result_YYYYMMDD_HHMMSS
firefox aps_report_YYYYMMDD_HHMMSS.html
```

---

## Файлы репозитория

| Файл | Описание |
| --- | --- |
| `src/CMakeLists.txt` | Патч: условный `-fopenmp-targets=spir64`, опция `ENABLE_PROFILING` |
| `src/iso3dfd.cpp` | Патч: `#ifndef NO_OFFLOAD` вокруг проверки GPU |
| `*.orig` | Резервные копии оригиналов |
| `run_vtune_hotspots.sh` | Скрипт запуска VTune (root, ptrace_scope, MPI) |
| `analyze_vtune.sh` | Скрипт анализа CSV-отчётов VTune |
| `NO_OFFLOAD_PATCH.md` | Документация патча CPU-only |
| `vtune_profiling_guide.md` | Руководство по профилированию VTune + APS |
| `profiling_compilation_levels.md` | Уровни модификации компиляции для профилирования |

---

## Среда

- **Компилятор**: Intel LLVM (icpx) 2026.1.1
- **ОС**: Ubuntu (Linux 6.x)
- **CPU**: 24 потока (Intel Xeon)
- **GPU**: отсутствует (CPU-only режим, `NO_OFFLOAD=ON`)
- **OpenMP**: `-fiopenmp` (host-only, без offload)
- **MPI**: Intel MPI

---

## Лицензия

Примеры кода распространяются по лицензии MIT. Подробнее см. [License.txt](License.txt).

Лицензии на сторонние программы: [third-party-programs.txt](third-party-programs.txt)
