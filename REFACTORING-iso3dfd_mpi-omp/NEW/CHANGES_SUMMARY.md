## Сводка изменений для MPI-рефакторинга iso3dfd

### Изменённые файлы

| Файл | Что изменено | Объём |
|---|---|---|
| `CMakeLists.txt` | Добавлены опции `ENABLE_MPI`, `ENABLE_PROFILING`, `find_package(MPI)`, линковка `MPI::MPI_CXX` | +15 строк |
| `src/iso3dfd.cpp` | Добавлены: `#include <mpi.h>`, функция `ExchangeHaloZ()`, MPI-путь в `Iso3dfd()`, MPI-инициализация/финализация/декомпозиция в `main()`, `MPI_Reduce` для метрик | +120 строк |
| `src/utils.cpp` | Добавлены: `#include <mpi.h>`, функция `InitializeLocal()` для инициализации локальной подобласти | +55 строк |
| `include/iso3dfd.h` | Добавлено объявление `InitializeLocal()` (под `#ifdef USE_MPI`) | +8 строк |

### Не изменённые файлы

| Файл | Причина |
|---|---|
| `src/iso3dfd_verify.cpp` | Верификация отключена в MPI-режиме (`#if defined(VERIFY_RESULTS) && !defined(USE_MPI)`) |
| Все варианты `Iso3dfdIteration` | Ядро не меняется — меняются только параметры вызова (n3_alloc вместо n3) |

### Сборка

```bash
# Без MPI (оригинальный режим)
cmake -DNO_OFFLOAD=ON -DVERIFY_RESULTS=OFF ..
make -j$(nproc)

# С MPI (рефакторинг)
cmake -DNO_OFFLOAD=ON -DVERIFY_RESULTS=OFF -DENABLE_MPI=ON ..
make -j$(nproc)

# С MPI + профилирование
cmake -DNO_OFFLOAD=ON -DVERIFY_RESULTS=OFF -DENABLE_MPI=ON -DENABLE_PROFILING=ON ..
make -j$(nproc)
```

### Запуск

```bash
# 1 процесс (baseline)
./src/iso3dfd 256 256 256 16 8 64 100

# 12 рангов, 2 потока на ранг
OMP_NUM_THREADS=2 mpirun -n 12 ./src/iso3dfd 256 256 256 16 8 64 100

# 6 рангов, 4 потока на ранг
OMP_NUM_THREADS=4 mpirun -n 6 ./src/iso3dfd 256 256 256 16 8 64 100

# NUMA-привязка (Intel MPI)
I_MPI_PIN_DOMAIN=omp OMP_NUM_THREADS=2 mpirun -n 12 ./src/iso3dfd 256 256 256 16 8 64 100
```

### Ключевые изменения в коде

1. **Декомпозиция по Z**: `n3_user / nprocs` с обработкой остатка
2. **Локальный массив**: `n1 × n2 × (n3_local + 2*kHalfLength)` — kHalfLength слоёв гало сверху и снизу
3. **Гало-обмен**: `MPI_Isend/Irecv/Waitall` — kHalfLength слоёв в каждую сторону перед каждой итерацией
4. **Инициализация**: `InitializeLocal()` — каждый ранг инициализирует свою подобласть, источник размещается по глобальным координатам
5. **Метрики**: `MPI_Reduce` с `MPI_MAX` — время берётся по самому медленному рангу
6. **Верификация**: отключена в MPI-режиме (требует сбора данных через `MPI_Gatherv`)
