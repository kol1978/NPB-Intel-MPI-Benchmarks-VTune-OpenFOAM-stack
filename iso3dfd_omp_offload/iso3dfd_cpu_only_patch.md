# Патч iso3dfd_omp_offload для CPU-only режима (NO_OFFLOAD)

## Описание проблемы

Пример `iso3dfd_omp_offload` из репозитория `oneAPI-samples` предназначен для
демонстрации OpenMP offload на GPU (Intel). При сборке на сервере **без GPU**
программа падает с ошибкой:

```
--------------------------------------
 No OpenMP Offload device found
--------------------------------------
 Incorrect parameters
 Usage: ./iso3dfd n1 n2 n3 n1_block n2_block n3_block Iterations
```

### Корневые причины

1. **`src/iso3dfd.cpp`** — проверка `omp_get_num_devices()` выполняется
   **безусловно**, даже если определён `NO_OFFLOAD`. При отсутствии GPU
   функция возвращает 0, и программа завершается с `return 1`.

2. **`src/CMakeLists.txt`** — флаг `-fopenmp-targets=spir64` добавляется
   **всегда**, независимо от опции `NO_OFFLOAD`. Это заставляет компилятор
   генерировать SPIR-V код для GPU, который не нужен в CPU-only режиме.

---

## Патч 1: `src/CMakeLists.txt`

### Было

```cmake
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fiopenmp -std=c++17 -fopenmp-targets=spir64 -O3 -D__STRICT_ANSI__ ")
```

### Стало

```cmake
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fiopenmp -std=c++17 -O3 -D__STRICT_ANSI__ ")
if(NOT NO_OFFLOAD)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fopenmp-targets=spir64")
endif()
```

### Логика

Флаг `-fopenmp-targets=spir64` (генерация offload-кода для GPU) добавляется
только если `NO_OFFLOAD` выключен. При `NO_OFFLOAD=ON` компилятор собирает
чистый CPU-код с `-fiopenmp` (OpenMP на хосте).

---

## Патч 2: `src/iso3dfd.cpp`

### Было

```cpp
  // Check for available omp offload capable device
  int num_devices = omp_get_num_devices();
  if (num_devices <= 0) {
    std::cout << "--------------------------------------\n";
    std::cout << " No OpenMP Offload device found\n";
    Usage(argv[0]);
    return 1;
  }
```

### Стало

```cpp
#ifndef NO_OFFLOAD
  // Check for available omp offload capable device
  int num_devices = omp_get_num_devices();
  if (num_devices <= 0) {
    std::cout << "--------------------------------------\n";
    std::cout << " No OpenMP Offload device found\n";
    Usage(argv[0]);
    return 1;
  }
#endif
```

### Логика

Проверка устройства обёрнута в `#ifndef NO_OFFLOAD ... #endif`. При сборке
с `-DNO_OFFLOAD` препроцессор полностью вырезает этот блок — программа
не вызывает `omp_get_num_devices()` и не падает на серверах без GPU.

---

## Применение патчей

### Вариант A: через скрипт (автоматический)

```bash
cd ~/OpenFOAM/kol-v2312/applications/PARALLELISM/oneAPI-samples/DirectProgramming/C++/StructuredGrids/iso3dfd_omp_offload

# ── Backup оригиналов ──
cp src/CMakeLists.txt src/CMakeLists.txt.orig
cp src/iso3dfd.cpp    src/iso3dfd.cpp.orig

# ── Patch 1: CMakeLists.txt ──
sed -i 's/-fiopenmp -std=c++17 -fopenmp-targets=spir64 -O3/-fiopenmp -std=c++17 -O3/' src/CMakeLists.txt
sed -i '/-D__STRICT_ANSI__ /a if(NOT NO_OFFLOAD)\n    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fopenmp-targets=spir64")\nendif()' src/CMakeLists.txt

# ── Patch 2: iso3dfd.cpp ──
python3 << 'PYEOF'
with open('src/iso3dfd.cpp', 'r') as f:
    lines = f.readlines()

insert_idx = None
for i, line in enumerate(lines):
    if 'Check for available omp offload capable device' in line:
        insert_idx = i
        break

if insert_idx is None:
    print("ERROR: marker not found")
    exit(1)

end_idx = None
for j in range(insert_idx, min(insert_idx + 20, len(lines))):
    if 'return 1;' in lines[j]:
        for k in range(j + 1, min(j + 5, len(lines))):
            if lines[k].strip() == '}':
                end_idx = k
                break
        break

if end_idx is None:
    print("ERROR: closing brace not found")
    exit(1)

lines.insert(insert_idx, '#ifndef NO_OFFLOAD\n')
end_idx += 1
lines.insert(end_idx + 1, '#endif\n')

with open('src/iso3dfd.cpp', 'w') as f:
    f.writelines(lines)

print(f"OK: inserted #ifndef at line {insert_idx+1}, #endif at line {end_idx+2}")
PYEOF
```

### Вариант B: вручную

1. В `src/CMakeLists.txt` заменить строку с `-fopenmp-targets=spir64`
   согласно разделу «Патч 1» выше.
2. В `src/iso3dfd.cpp` обернуть проверку устройства в `#ifndef NO_OFFLOAD`
   и `#endif` согласно разделу «Патч 2» выше.

---

## Сборка

```bash
cd ~/OpenFOAM/kol-v2312/applications/PARALLELISM/oneAPI-samples/DirectProgramming/C++/StructuredGrids/iso3dfd_omp_offload

# Полная зачистка
rm -rf build
mkdir build
cd build

# Инициализация компилятора Intel
export PATH=/opt/intel/oneapi/compiler/2026.1/bin:$PATH

# Конфигурация: NO_OFFLOAD=ON, VERIFY_RESULTS=OFF
cmake -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
      -DNO_OFFLOAD=ON \
      -DVERIFY_RESULTS=OFF \
      ..

# Сборка
make -j$(nproc)

# Проверка флага
grep NO_OFFLOAD CMakeCache.txt
# Ожидаемый вывод: NO_OFFLOAD:BOOL=ON
```

---

## Запуск

### Прямой запуск

```bash
./src/iso3dfd 256 256 256 16 8 64 100
```

### Аргументы

| Позиция | Имя           | Описание                          |
|---------|---------------|-----------------------------------|
| 1       | `n1`          | Размер сетки по оси X             |
| 2       | `n2`          | Размер сетки по оси Y             |
| 3       | `n3`          | Размер сетки по оси Z             |
| 4       | `n1_block`    | Размер блока (тайла) по X         |
| 5       | `n2_block`    | Размер блока (тайла) по Y         |
| 6       | `n3_block`    | Размер блока (тайла) по Z         |
| 7       | `Iterations`  | Число шагов по времени             |

### Управление числом потоков OpenMP

```bash
# По умолчанию — все ядра
OMP_NUM_THREADS=$(nproc) ./src/iso3dfd 256 256 256 16 8 64 100

# Ограничить число потоков
OMP_NUM_THREADS=12 ./src/iso3dfd 256 256 256 16 8 64 100

# Привязка к одному NUMA-узлу
numactl --membind=0 --cpunodebind=0 \
    OMP_NUM_THREADS=12 ./src/iso3dfd 256 256 256 16 8 64 100
```

---

## Ожидаемый вывод

```
Grid Sizes: 256 256 256
Tile sizes ignored for OMP Offload
--Using Baseline version with omp target with collapse
Memory Usage (MBytes): 230
--------------------------------------
time         : 2.96 secs
throughput   : 566.798 Mpts/s
flops        : 34.5747 GFlops
bytes        : 6.80157 GBytes/s
--------------------------------------
```

---

## Метрики

| Метрика         | Единица   | Описание                                            |
|-----------------|-----------|-----------------------------------------------------|
| `time`          | сек       | Время выполнения kernel-цикла                       |
| `throughput`    | Mpts/s    | Миллионов точек сетки в секунду                     |
| `flops`         | GFlops    | Гигафлопс — эффективность использования SIMD        |
| `bytes`         | GBytes/s  | Пропускная способность памяти                       |

---

## Файлы

| Файл                  | Описание                                   |
|-----------------------|---------------------------------------------|
| `src/CMakeLists.txt`  | Патч: условный `-fopenmp-targets=spir64`   |
| `src/iso3dfd.cpp`     | Патч: `#ifndef NO_OFFLOAD` вокруг проверки |
| `*.orig`              | Резервные копии оригиналов                   |

---

## Среда

- **Компилятор**: Intel LLVM (icpx) 2026.1.1
- **OS**: Ubuntu (Linux 6.x)
- **CPU**: 24 потока (Intel Xeon)
- **GPU**: отсутствует (CPU-only режим)
- **OpenMP**: `-fiopenmp` (host-only, без offload)
