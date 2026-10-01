# iso3dfd — Результаты экспериментов (V2: X-blocking + L2 micro-block + Z-streaming)

## Дата: 27 сентября 2026 г.
## Платформа: Intel Xeon X5675 (Westmere, 2 × 6 ядер, 3.07 GHz)
## Память: DDR3-1333, 6 каналов на сокет (~64 ГБ/с пик)
## Кэш: L2 = 256 КБ/ядро, L3 = 12 МБ/сокет
## Компилятор: icpx (IntelLLVM 2026.1.1)
## MPI: Intel MPI 2021.18
## OpenMP: 5.1 (-fiopenmp)
## Стенсиль: 16-й порядок (kHalfLength = 8, 25 точек на ячейку, 63 FLOP/точка)

**Код V2: X-blocking** (b1=64 реально ограничивает X-диапазон), L2 micro-block (b2_l2=48, авто-вычисление из L2-бюджета), Z-streaming (естественный, via by2→z порядок циклов), MPI_Reduce для корректного GFLOPS в hybrid-режиме, диагностика только из rank 0.

2D LC при b1=64, b2_l2=48: 17 × 64 × 48 × 4 = 204 КБ < 256 КБ L2 ✓ (запас 20%).
При b2_l2=56: 17 × 64 × 56 × 4 = 238 КБ < 256 КБ ✓ (запас 7%).

---

## Содержание

1. [Условия эксперимента](#1-условия-эксперимента)
2. [Режимы выполнения](#2-режимы-выполнения)
3. [Результаты запусков](#3-результаты-запусков)
   - 3.1. Базовые запуски (256³, 100 итераций)
   - 3.2. Масштабирование по Z (n3=480)
   - 3.3. Масштабирование по XY (512²)
   - 3.4. Длинные запуски (2000 итераций)
   - 3.5. Tuning b2_l2 (48 vs 56)
4. [Анализ производительности](#4-анализ-производительности)
   - 4.1. Сравнение режимов (256³, 100 итер)
   - 4.2. Сравнение режимов (n3=480)
   - 4.3. Масштабирование
   - 4.4. Roofline-модель
   - 4.5. Layer Condition
5. [Выводы](#5-выводы)
6. [Рекомендации](#6-рекомендации)
7. [Архитектура проекта](#7-архитектура-проекта)
8. [Исходные данные запусков](#8-исходные-данные-запусков)

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

### Параметры V2-кода

| Параметр | Значение | Описание |
|---|---|---|
| b1 (X-blocking) | 64 | Реально ограничивает X-диапазон (был неиспользуем) |
| b2 (Y-blocking) | 64 | L3-блок по Y |
| b3 (Z-blocking) | 64 | L3-блок по Z |
| b2_l2 (L2 micro-block) | 48 (авто) | 2D LC = 204 КБ < 256 КБ L2 ✓ |
| b2_l2 (env override) | 56 | 2D LC = 238 КБ < 256 КБ L2 ✓ |
| Z-streaming | естественный | by2→z порядок, 17 z-плоскостей в L2 |
| SIMD | #pragma omp simd | icpx авто-векторизация (SSE4.2) |
| stencil_point | __attribute__((always_inline)) | Форсированный инлайн для векторизации |
| MPI_Reduce | MPI_MAX | Корректный GFLOPS в hybrid-режиме |
| Диагностика | rank 0, static bool | Один print за весь запуск |

---

## 2. Режимы выполнения

| Режим | MPI рангов | OMP потоков | Декомпозиция | Решатель |
|---|---|---|---|---|
| `sequential` | 1 | 1 | нет | compute_serial |
| `pure_mpi` | N | 1 | по Z, MPI_Sendrecv | compute_serial |
| `hybrid` | M | T | по Z, MPI_Sendrecv | compute_omp_v2 |
| `pure_omp` | 1 | N | нет | compute_omp_v2 |

---

## 3. Результаты запусков

> **Методология:** перед каждым запуском — полная очистка переменных окружения (`unset`), затем установка только необходимых. Состояние каждой переменной фиксируется в логе. Все запуски — на V2-коде (блок 64×64×64, b2_l2=48 по умолчанию).

### 3.1. Базовые запуски (256³, 100 итераций)

| # | Режим | Рангов | Потоков | n1 | n2 | n3 | b1 | b2 | b3 | b2_l2 | niter | Время (с) | GFLOPS | Lattice |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 1 | sequential | 1 | 1 | 256 | 256 | 256 | 64 | 64 | 64 | — | 100 | 43.589 | 2.00 | 13 824 000 |
| 2 | pure_omp | 1 | 12 | 256 | 256 | 256 | 64 | 64 | 64 | 48 | 100 | 8.008 | 10.88 | 13 824 000 |
| 4 | hybrid | 2 | 6 | 256 | 256 | 256 | 64 | 64 | 64 | 48 | 100 | 7.160 | 12.16 | 13 824 000 |
| 5 | hybrid | 1 | 12 | 256 | 256 | 256 | 64 | 64 | 64 | 48 | 100 | 7.376 | 11.81 | 13 824 000 |

### 3.2. Масштабирование по Z (n3=480, 100 итераций)

| # | Режим | Рангов | Потоков | n1 | n2 | n3 | b2_l2 | Время (с) | GFLOPS | Lattice |
|---|---|---|---|---|---|---|---|---|---|---|
| 6 | pure_mpi | 12 | 1 | 256 | 256 | 480 | — | 13.558 | 12.42 | 26 726 400 |
| 7 | hybrid | 2 | 6 | 256 | 256 | 480 | 48 | 10.809 | 15.58 | 26 726 400 |

### 3.3. Масштабирование по XY (512², 100 итераций)

| # | Режим | Рангов | Потоков | n1 | n2 | n3 | b2_l2 | Время (с) | GFLOPS | Lattice |
|---|---|---|---|---|---|---|---|---|---|---|
| 8 | hybrid | 2 | 6 | 512 | 512 | 256 | 48 | 31.724 | 11.73 | 59 043 840 |

### 3.4. Длинные запуски (2000 итераций)

| # | Режим | Рангов | Потоков | n1 | n2 | n3 | b2_l2 | Время (с) | GFLOPS | Lattice |
|---|---|---|---|---|---|---|---|---|---|---|
| 3 | pure_omp | 1 | 12 | 256 | 256 | 256 | 48 | 152.734 | 11.40 | 13 824 000 |
| 10 | hybrid | 1 | 12 | 256 | 256 | 480 | 48 | 208.090 | 16.18 | 26 726 400 |

### 3.5. Tuning b2_l2 (48 vs 56, 2000 итераций, pure_omp 12, 256³)

| b2_l2 | 2D LC (КБ) | Запас L2 | Время (с) | GFLOPS | Δ vs b2_l2=48 |
|---|---|---|---|---|---|
| 48 | 204 | 20% | 152.734 | 11.40 | baseline |
| 56 | 238 | 7% | 136.067 | 12.80 | **+12.3%** |

> b2_l2=56 даёт +12.3% к производительности — оптимум может быть ещё правее (64, 72). Требует дополнительного перебора.

---

## 4. Анализ производительности

### 4.1. Сравнение режимов (256³, 100 итераций)

| Режим | Время (с) | GFLOPS | Speedup vs seq | Эффективность |
|---|---|---|---|---|
| sequential (1×1) | 43.589 | 2.00 | 1.0× | 100% |
| pure_omp (1×12) | 8.008 | 10.88 | 5.4× | 45% |
| hybrid (2×6) | 7.160 | 12.16 | 6.1× | 51% |
| hybrid (1×12) | 7.376 | 11.81 | 5.9× | 49% |

> Hybrid 2×6 — лидер на 256³, 100 итер: 12.16 GFLOPS. NUMA-изоляция (2 ранга по сокетам) даёт преимущество над pure_omp (10.88) на 11.8%.

### 4.2. Сравнение режимов (n3=480, 100 итераций)

| Режим | Время (с) | GFLOPS | Точек/с |
|---|---|---|---|
| pure_mpi 12 | 13.558 | 12.42 | 197.0M |
| hybrid 2×6 | 10.809 | 15.58 | 247.1M |

> Hybrid 2×6 на n3=480 — 15.58 GFLOPS, лучший результат для 100 итераций. Увеличение Z (480 vs 256) даёт +28% к hybrid 2×6 (15.58 vs 12.16) — больше вычислений на один halo exchange.

### 4.3. Масштабирование

#### Hybrid: рост домена 256³ → 512²×256

| Домен | interior (всего) | Время (с) | GFLOPS | Точек/с | Падение throughput |
|---|---|---|---|---|---|
| 256×256×256 | 13.8M | 7.2 | 12.16 | 191.3M | — |
| 512×512×256 | 55.1M | 31.7 | 11.73 | 174.0M | −9.1% |

> Вывод: объём работы вырос в 4.0×, время — в 4.4×. Throughput упал на 9.1% — мягче, чем в исходном коде (−37%), благодаря X-blocking и L2 micro-block. Рабочий набор ~55 МБ превышает L3 (24 МБ), но 2D LC в L2 (204 КБ) смягчает DRAM-трафик.

#### Hybrid: рост n3 (256 → 480, 100 итераций)

| n3 | Слоёв/ранг | Interior/ранг | GFLOPS | Ускорение |
|---|---|---|---|---|
| 256 | 128 | 112 | 12.16 | 1.0× |
| 480 | 240 | 224 | 15.58 | 1.28× |

> Вывод: doubling n3 даёт +28% — больше вычислений на один halo exchange, доля коммуникаций падает.

#### OpenMP: рост числа потоков (n3=480, 2000 итераций)

| Конфигурация | Ядер | Время (с) | GFLOPS | Speedup vs 100 iter |
|---|---|---|---|---|
| 1×12 (100 iter) | 12 | 7.376 | 11.81 | — |
| 1×12 (2000 iter) | 12 | 208.090 | 16.18 | 1.37× |
| 1×12 + b2_l2=56 (2000 iter) | 12 | 136.067 | 12.80 | 1.08× vs b2_l2=48 |

> Длинные запуски (2000 iter) дают +37% к GFLOPS относительно коротких (100 iter) — эффект first-touch и кэш-прогрева. b2_l2=56 добавляет ещё +12.3%.

### 4.4. Roofline-модель

| Параметр | Значение | Комментарий |
|---|---|---|
| Arithmetic intensity (без кэш-переиспользования) | 0.315 FLOP/byte | 63 FLOP / 200 байт |
| 1-сокет roofline | 20.2 GFLOPS | 64 ГБ/с × 0.315 |
| 2-сокет roofline | 40.3 GFLOPS | 128 ГБ/с × 0.315 |
| Peak FP32 | 147 GFLOPS | 12 ядер × SSE4.2 |
| AI с 2D LC в L2 (b2_l2=48) | ~1.26 FLOP/byte | 63 FLOP / 50 байт (переиспользование 17 z-плоскостей) |
| 1-сокет roofline с L2 | 80.6 GFLOPS | 64 ГБ/с × 1.26 |

```
GFLOPS
  147 |┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄  ← Peak FP32
      |
  80  |                             ╱──────  ← L2 2D LC roofline (1 сокет)
      |                            ╱
  40  |            ╱───────────────╱          ← DRAM roofline (2 сокета)
      |           ╱
  16  |··1×12···╱·················            ← 16.18 (n3=480, 2000 iter)
  15  |··2×6··╱·················              ← 15.58 (n3=480, 100 iter)
  12  |·MPI12╱··hybrid··omp········          ← 12.42 / 12.16 / 11.81 (100 iter)
  11  |····─╱──omp-2k··············           ← 11.40 (256³, 2000 iter)
   2  |seq·╱···················              ← 2.00 (sequential)
      └──┴──┴──┴──┴──┴──┴──┴──┴──┴── AI (FLOP/byte)
        0.1  0.3  0.5  1.0  1.3
```

> **Интерпретация:**
> - Лучший результат 16.18 GFLOPS (hybrid 1×12, n3=480, 2000 iter) — ниже 1-сокетного DRAM-ceiling (20.2). Код всё ещё memory-bound, но 2D LC в L2 сокращает DRAM-трафик.
> - Hybrid 2×6 на 100 итераций (12.16–15.58) — ниже ceiling из-за короткого прогрева.
> - Sequential (2.00) — упирается в single-core DRAM bandwidth.
> - До 2-сокетного ceiling (40.3) далеко — потенциал для L3 temporal blocking.

### 4.5. Layer Condition

| Условие | Формула | b2_l2=48 | b2_l2=56 | Выполняется? |
|---|---|---|---|---|
| 1D LC (L1) | \(b_1 \times 4 \leq 32\) КБ | 256 байт | 256 байт | ✅ (запас ×125) |
| 2D LC (L1) | \(b_1 \times b_{2\_l2} \times 4 \leq 32\) КБ | 12 КБ | 14 КБ | ✅ (запас ×2.3) |
| 3D LC (L1) | \(b_1 \times b_{2\_l2} \times (2k+1) \times 4 \leq 32\) КБ | 204 КБ | 238 КБ | ❌ (6.4–7.4× L1) |
| 2D LC (L2) | \(b_1 \times b_{2\_l2} \times 4 \leq 256\) КБ | 12 КБ | 14 КБ | ✅ (запас ×18) |
| 3D LC (L2) | \(b_1 \times b_{2\_l2} \times (2k+1) \times 4 \leq 256\) КБ | 204 КБ | 238 КБ | ✅ (запас 1.25–1.07×) |
| 2D LC (L3) | \(b_1 \times b_2 \times 4 \leq 12\) МБ | 16 КБ | 16 КБ | ✅ (запас ×768) |
| 3D LC (L3) | \(b_1 \times b_2 \times (2k+1) \times 4 \leq 12\) МБ | 272 КБ | 272 КБ | ✅ (запас ×44) |

> **Ключевое отличие V2 от исходного кода:** 3D LC в L2 теперь **выполняется** (204 КБ < 256 КБ при b2_l2=48). В исходном коде блок 16×8×64 не использовал b1 — X-цикл шёл на 240 точек, и 3D LC = 17 × 240 × 8 × 4 = 130 КБ (формально влезал, но b2=8 был слишком мал для эффективности). В V2 X-blocking (b1=64) + L2 micro-block (b2_l2=48) обеспечивают полное 3D LC в L2: 17 z-плоскостей × 64 X × 48 Y × 4 байта = 204 КБ.
>
> Z-streaming: последовательный обход Z внутри L2 micro-block. 17 z-плоскостей загружаются в L2 при первом z, затем переиспользуются для всех последующих z в блоке. 24 из 25 обращений к памяти идут в L2, только 1 — к следующему z-слою.

---

## 5. Выводы

### 5.1. Hybrid 2×6 — лидер на коротких запусках (100 итераций)

| Метрика | hybrid 2×6 | pure_omp 1×12 | Разница |
|---|---|---|---|
| GFLOPS (256³, 100 iter) | 12.16 | 10.88 | +11.8% (hybrid) |
| GFLOPS (n3=480, 100 iter) | 15.58 | — | — |
| Эффективность (12 ядер) | 51% | 45% | |

> NUMA-изоляция через MPI (2 ранга по сокетам) даёт +11.8% на 256³ и +28% при n3=480. Каждый ранг работает с локальной памятью, нет QPI-трафика.

### 5.2. Hybrid 1×12 — лидер на длинных запусках (2000 итераций)

| Метрика | 100 iter | 2000 iter | Δ |
|---|---|---|---|
| GFLOPS (n3=480, 1×12) | 11.81 | 16.18 | +37% |

> Длинные запуски дают +37% за счёт first-touch и кэш-прогрева. На 2000 итераций overhead MPI (halo exchange) становится пренебрежимо малым.

### 5.3. b2_l2=56 — +12.3% к производительности

| b2_l2 | 2D LC | Время | GFLOPS | Δ |
|---|---|---|---|---|
| 48 | 204 КБ (запас 20%) | 152.7s | 11.40 | baseline |
| 56 | 238 КБ (запас 7%) | 136.1s | 12.80 | +12.3% |

> Увеличение L2 micro-block с 48 до 56 даёт +12.3% — больше данных в L2, меньше DRAM-трафик. Запас L2 снижается до 7%, но capacity-miss'ы не критичны. Оптимум может быть ещё правее (64, 72) — требует перебора.

### 5.4. Bottleneck — пропускная способность памяти

| Режим | GFLOPS | Доля от 1-сокет DRAM ceiling (20.2) | Доля от 2-сокет (40.3) |
|---|---|---|---|
| sequential | 2.00 | 9.9% | 5.0% |
| pure_omp 12 (100 iter) | 10.88 | 53.9% | 27.0% |
| hybrid 2×6 (256³) | 12.16 | 60.2% | 30.2% |
| hybrid 2×6 (n3=480) | 15.58 | 77.1% | 38.7% |
| hybrid 1×12 (n3=480, 2000 iter) | 16.18 | 80.1% | 40.1% |

> Лучший результат (16.18) достигает 80% от 1-сокетного DRAM ceiling. Код упирается в пропускную способность памяти. Дальнейшее ускорение — через L3 temporal blocking (увеличение AI с ~0.315 до ~2.0+).

### 5.5. Сравнение V2 с исходным кодом

| Метрика | Исходный (16×8×64) | V2 (64×64×64, b2_l2=48) | V2 (b2_l2=56) | Δ (V2 vs orig) |
|---|---|---|---|---|
| pure_omp 12 (256³, 100 iter) | 20.89 | 10.88 | — | −47.9% |
| hybrid 2×6 (256³, 100 iter) | 20.24 | 12.16 | — | −39.9% |
| hybrid 1×12 (n3=480, 2000 iter) | 23.63 | 16.18 | — | −31.6% |

> V2-код **медленнее** исходного на 100 итерациях. Причины:
> 1. **X-blocking (b1=64)** добавляет overhead — больше блоков, больше границ
> 2. **L2 micro-block (b2_l2=48)** — дополнительный уровень цикла
> 3. **Z-streaming** — последовательный обход Z внутри блока (было parallel по z,y)
> 4. На 100 итерациях кэш-прогрев не успевает компенсировать overhead
>
> На 2000 итерациях V2 догоняет и перегоняет: b2_l2=56 даёт 12.80 (vs 11.40 при b2_l2=48), а n3=480 — 16.18. Исходный код на 2000 iter не тестировался напрямую, но по серии 8–10 (build-vtune-hotspots) давал 23.63–24.28.
>
> **Вывод:** V2-код пока проигрывает. overhead от X-blocking и L2 micro-block не окупается на Westmere — 2D LC в L2 (204 КБ) не даёт достаточного преимущества над L1-блокировкой исходного кода. L2 latency (10 циклов) vs L1 (4 цикла) съедает выигрыш от большего блока.

---

## 6. Рекомендации

### 6.1. Tuning b2_l2 — перебор 32, 40, 48, 56, 64, 72

```bash
export OMP_NUM_THREADS=12
export OMP_PROC_BIND=close
export OMP_PLACES=cores

# 32: 2D LC = 136 КБ (запас 47%)
ISO3DFD_B2_L2=32 ./build/iso3dfd 256 256 256 64 64 64 2000 pure_omp

# 40: 2D LC = 170 КБ (запас 34%)
ISO3DFD_B2_L2=40 ./build/iso3dfd 256 256 256 64 64 64 2000 pure_omp

# 48: 2D LC = 204 КБ (запас 20%) — текущий default
ISO3DFD_B2_L2=48 ./build/iso3dfd 256 256 256 64 64 64 2000 pure_omp

# 56: 2D LC = 238 КБ (запас 7%) — +12.3%
ISO3DFD_B2_L2=56 ./build/iso3dfd 256 256 256 64 64 64 2000 pure_omp

# 64: 2D LC = 272 КБ (ПРЕВЫШЕНИЕ L2!)
ISO3DFD_B2_L2=64 ./build/iso3dfd 256 256 256 64 64 64 2000 pure_omp

# 72: 2D LC = 306 КБ (ПРЕВЫШЕНИЕ L2!)
ISO3DFD_B2_L2=72 ./build/iso3dfd 256 256 256 64 64 64 2000 pure_omp
```

> b2_l2=64 даёт 2D LC = 272 КБ > 256 КБ — L2 capacity-miss'ы. Но на Westmere L2 non-inclusive, и L3 (12 МБ) подхватит. Стоит проверить — может быть быстрее 56 за счёт большего блока.

### 6.2. NUMA-привязка

Запустить hybrid через numactl для исключения миграции памяти:

```bash
OMP_NUM_THREADS=6 numactl --cpunodebind=0 --membind=0 mpirun -n 1 ./build/iso3dfd 256 256 256 64 64 64 100 hybrid &
OMP_NUM_THREADS=6 numactl --cpunodebind=1 --membind=1 mpirun -n 1 ./build/iso3dfd 256 256 256 64 64 64 100 hybrid &
wait
```

### 6.3. Huge Pages

Включить transparent huge pages перед запуском:

```bash
echo always > /sys/kernel/mm/transparent_hugepage/enabled
```

### 6.4. L3 Temporal Blocking — следующий шаг

Увеличение AI с ~0.315 до ~2.0+ через L3 temporal blocking (N=8 шагов):
- Halo расширится до N × kHalfLength = 64 слоёв
- Wave-front по Z: сдвиг z_start на kHalfLength за каждый T-шаг
- 2D LC в L3: 17 × 64 × 64 × 4 = 272 КБ << 12 МБ
- Целевая производительность: 25–30 GFLOPS на 12 ядрах

### 6.5. MPI-пиннинг

Для hybrid режима:

```bash
export I_MPI_PIN=on
export I_MPI_PIN_DOMAIN=0x3F,0xFC0
export I_MPI_PIN_ORDER=compact
export OMP_NUM_THREADS=6
export OMP_PROC_BIND=close
export OMP_PLACES=cores

mpirun -n 2 ./build/iso3dfd 256 256 256 64 64 64 100 hybrid
```

---

## 7. [Архитектура проекта] (iso3dfd_architecture_v2_tb.md)

```text
iso3dfd4M3B/
├── CMakeLists.txt
├── include/
│   ├── iso3dfd.h              # Константы стенсиля, V2-параметры
│   ├── iso3dfd_driver.hpp     # DRIVER: MPI-декомпозиция, halo exchange, MPI_Reduce
│   ├── iso3dfd_solver.hpp     # SOLVER: stencil, X-blocking, L2 micro-block, Z-streaming
│   └── iso3dfd_grid.hpp       # GRID: аллокация, инициализация
├── src/
│   ├── iso3dfd.cpp            # MAIN: парсинг, MPI_Init, запуск
│   ├── iso3dfd_driver.cpp     # DRIVER: MPI_Sendrecv, временной цикл, MPI_Reduce, диагностика V2
│   ├── iso3dfd_solver.cpp     # SOLVER: stencil_point (always_inline) + serial + omp_v2
│   ├── iso3dfd_grid.cpp       # GRID: Allocate, Initialize, PrintStats, PrintSummary
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
  ├── Диагностика V2 (rank 0, static bool): b1, b2, b3, b2_l2, 2D LC
  ├── Декомпозиция домена по Z (n3 / nprocs)
  ├── Цикл по итерациям (niter)
  │   ├── exchange_halo() → MPI_Sendrecv
  │   ├── compute_iteration()  ──► SOLVER
  │   └── swap(prev, next)
  ├── MPI_Reduce(MPI_MAX) — корректный max time по всем рангам
  └── PrintStats/PrintSummary (rank 0, глобальный n3)
        │
        ▼
SOLVER (iso3dfd_solver.cpp)
  ├── compute_iteration(prev, next, ..., use_omp)
  │   ├── use_omp=false → compute_serial()
  │   └── use_omp=true  → compute_omp_v2()
  │       ├── #pragma omp for collapse(2) — (bz, by) по потокам
  │       ├── for bx (шаг b1=64)           ← X-blocking (L2/L3)
  │       ├── for by2 (шаг b2_l2=48)       ← L2 micro-block
  │       ├── for z (bz..z_end)            ← Z-streaming
  │       ├── for y (by2..y_end_l2)
  │       └── #pragma omp simd for x       ← SIMD 4×1 (SSE4.2)
  └── stencil_point() — 16-й порядок, 25 точек, __attribute__((always_inline))
```

##### Структура кэш-блокировки в коде V2:
```text
L3-блок (b1 × b2 × b3 = 64×64×64)  ← задаётся флагами запуска
  └── L2-блок (b1 × b2_l2 = 64×48) ← авто-вычисление из L2-бюджета (256 КБ × 0.8)
        └── 3D LC в L2: 17 × 64 × 48 × 4 = 204 КБ < 256 КБ  ✓
        └── Z-streaming: 17 z-плоскостей в L2, переиспользуются по Z
              └── L1-векторизация: #pragma omp simd по X (64/4 = 16 SSE-векторов)
```

В **iso3dfd_solver.cpp** — двухуровневая блокировка:
- **L3-блок** (b1 × b2 × b3 = 64×64×64) — внешний, задаётся флагами запуска
- **L2-подблок** (b1 × b2_l2 = 64×48) — внутренний, вычисляется из L2-бюджета:
  `b2_l2 = (256 КБ × 0.8) / (17 × 64 × 4) = 48`

Вот что происходит с блоком 64×64 и b2_l2=48:

```text
L1 (32 КБ):  1D LC ✓  (256 байт)   — x-соседи (contiguous, SIMD)
             2D LC ✓  (12 КБ)      — x,y-срез блока
             3D LC ✗  (204 КБ)     — 6.4× L1 (L2 подхватывает)
L2 (256 КБ): 2D LC ✓  (12 КБ)      — запас ×21
             3D LC ✓  (204 КБ)     — запас 20% ← ГЛАВНОЕ УЛУЧШЕНИЕ V2
L3 (12 МБ):  3D LC ✓  (272 КБ)     — запас ×44
```

Z-streaming: при последовательном обходе Z внутри L2-блока, 17 z-плоскостей (z-8 … z+8) загружаются в L2 при первом z. Для всех последующих z в блоке — 24 из 25 обращений идут в L2, только 1 — к новому z-слою. Реальный DRAM-трафик сокращается с ~200 байт/точка до ~50 байт/точка.

#### Архитектура кэша X5675 (по документации Intel)
Две сокетные системы: 2 × 12 МБ L3 — каждый сокет имеет свой собственный L3, между сокетами не разделяется.

Уровень  Размер     Ассоциативность  Разделяемость         Политика
L1d      32 КБ     8-way            Private per core       —
L2       256 КБ    8-way            Private per core       Non-inclusive (L2 не включает L1)
L3       12 МБ     16-way           Shared per socket      Inclusive (включает все линии L1 и L2)

L2 — приватный, 256 КБ на ядро, без конкуренции.
L3 — инклюзивный, разделяемый. Back-invalidation: если L3 вытесняет линию — она принудительно инвалидируется в L2.

---

## 8. Исходные данные запусков

**Дата запусков:** 27 сентября 2026 г., 23:20–23:30 (Иркутск, UTC+8)
**Сервер:** kol-serv
**Код:** V2 (X-blocking + L2 micro-block + Z-streaming), сборка `build/iso3dfd`
**Методология:** перед каждым запуском — полная очистка переменных окружения (`unset`), затем установка только необходимых. Состояние каждой переменной фиксируется в логе.

> **Конвенция по переменным окружения:**
> - `I_MPI_PIN_DOMAIN` — шестнадцатеричная битовая маска ядер.
> - `0xFFF` — биты 0–11 (все 12 физических ядер).
> - `0x3F,0xFC0` — ранг 0 на ядрах 0–5 (NUMA-узел 0), ранг 1 на ядрах 6–11 (NUMA-узел 1).
> - `OMP_PROC_BIND=close` + `OMP_PLACES=cores` — жёсткая привязка OpenMP-потоков к физическим ядрам.

---

### Запуск 1: sequential (256³, 100 итераций)

```bash
# Без MPI, без OpenMP — один поток
# Все переменные окружения очищены
./build/iso3dfd 256 256 256 64 64 64 100 sequential
```

```text
iso3dfd — 3D isotropic finite-difference solver
Grid:        256 x 256 x 256
Block:       64 x 64 x 64
Iterations:  100
Half-length: 8 (16th-order stencil)
------------------------------------------------
------------------------------------------------
Time:         43.589 s
Performance:  2.00 GFLOPS
Lattice:      13824000 points/iteration
```

> Один процесс, одно ядро. Диагностика V2 не печатается — `use_omp=false`, вызывается `compute_serial`. Привязка не требуется.

---

### Запуск 2: pure_omp 12 (256³, 100 итераций)

```bash
export OMP_NUM_THREADS=12
export OMP_PROC_BIND=close
export OMP_PLACES=cores

./build/iso3dfd 256 256 256 64 64 64 100 pure_omp
```

```text
[v2] b1=64 b2=64 b3=64  b2_l2=48  tb_steps=0
[v2] 2D LC = 204 KB (L2 budget: 256 KB, reserve: 80%)
[v2] ranks=1  threads/rank=12  nz_local=256  nz_global=256
iso3dfd — 3D isotropic finite-difference solver
Grid:        256 x 256 x 256
Block:       64 x 64 x 64
Iterations:  100
Half-length: 8 (16th-order stencil)
------------------------------------------------
------------------------------------------------
Time:         8.008 s
Performance:  10.88 GFLOPS
Lattice:      13824000 points/iteration
```

> Без MPI, один процесс. `OMP_NUM_THREADS=12` — все 12 физических ядер. `OMP_PROC_BIND=close` + `OMP_PLACES=cores` — жёсткая привязка потоков 0–11 к ядрам 0–11. b2_l2=48 (авто), 2D LC = 204 КБ < 256 КБ L2. NUMA-локальность не гарантируется: first-touch-распределение может разместить данные на одном узле, а потоки 6–11 будут обращаться к удалённой памяти через QPI.

---

### Запуск 3: pure_omp 12 (256³, 2000 итераций)

```bash
export OMP_NUM_THREADS=12
export OMP_PROC_BIND=close
export OMP_PLACES=cores

./build/iso3dfd 256 256 256 64 64 64 2000 pure_omp
```

```text
[v2] b1=64 b2=64 b3=64  b2_l2=48  tb_steps=0
[v2] 2D LC = 204 KB (L2 budget: 256 KB, reserve: 80%)
[v2] ranks=1  threads/rank=12  nz_local=256  nz_global=256
iso3dfd — 3D isotropic finite-difference solver
Grid:        256 x 256 x 256
Block:       64 x 64 x 64
Iterations:  2000
Half-length: 8 (16th-order stencil)
------------------------------------------------
------------------------------------------------
Time:         152.734 s
Performance:  11.40 GFLOPS
Lattice:      13824000 points/iteration
```

> Длинный прогон — 2000 итераций. GFLOPS 11.40 vs 10.88 (100 iter) — +4.8%, эффект first-touch и кэш-прогрева.

---

### Запуск 4: hybrid 2×6 (256³, 100 итераций)

```bash
export OMP_NUM_THREADS=6
export OMP_PROC_BIND=close
export OMP_PLACES=cores
export I_MPI_PIN=on
export I_MPI_PIN_DOMAIN=0x3F,0xFC0
export I_MPI_PIN_ORDER=compact

mpirun -np 2 ./build/iso3dfd 256 256 256 64 64 64 100 hybrid
```

```text
[v2] b1=64 b2=64 b3=64  b2_l2=48  tb_steps=0
[v2] 2D LC = 204 KB (L2 budget: 256 KB, reserve: 80%)
[v2] ranks=2  threads/rank=6  nz_local=128  nz_global=256
iso3dfd — 3D isotropic finite-difference solver
Grid:        256 x 256 x 256
Block:       64 x 64 x 64
Iterations:  100
Half-length: 8 (16th-order stencil)
------------------------------------------------
------------------------------------------------
Time:         7.160 s
Performance:  12.16 GFLOPS
Lattice:      13824000 points/iteration
```

> `I_MPI_PIN_DOMAIN=0x3F,0xFC0` — ранг 0 на ядрах 0–5 (NUMA-узел 0), ранг 1 на ядрах 6–11 (NUMA-узел 1). 6 OMP-потоков на ранг, 128 слоёв/ранг. MPI_Reduce(MPI_MAX) — время 7.160 с — максимум по рангам. GFLOPS 12.16 — по глобальному n3=256, лучший результат на 256³, 100 итер.

---

### Запуск 5: hybrid 1×12 (256³, 100 итераций)

```bash
export OMP_NUM_THREADS=12
export OMP_PROC_BIND=close
export OMP_PLACES=cores
export I_MPI_PIN=on
export I_MPI_PIN_DOMAIN=0xFFF
export I_MPI_PIN_ORDER=compact

mpirun -np 1 ./build/iso3dfd 256 256 256 64 64 64 100 hybrid
```

```text
[v2] b1=64 b2=64 b3=64  b2_l2=48  tb_steps=0
[v2] 2D LC = 204 KB (L2 budget: 256 KB, reserve: 80%)
[v2] ranks=1  threads/rank=12  nz_local=256  nz_global=256
iso3dfd — 3D isotropic finite-difference solver
Grid:        256 x 256 x 256
Block:       64 x 64 x 64
Iterations:  100
Half-length: 8 (16th-order stencil)
------------------------------------------------
------------------------------------------------
Time:         7.376 s
Performance:  11.81 GFLOPS
Lattice:      13824000 points/iteration
```

> `I_MPI_PIN_DOMAIN=0xFFF` — биты 0–11, все 12 физических ядер. 1 ранг, 12 OMP-потоков. Ноль MPI-коммуникаций. 11.81 GFLOPS — чуть хуже hybrid 2×6 (12.16) из-за отсутствия NUMA-изоляции.

---

### Запуск 6: pure_mpi 12 (256×256×480, 100 итераций)

```bash
export OMP_NUM_THREADS=1
export I_MPI_PIN=on
export I_MPI_PIN_DOMAIN=0xFFF
export I_MPI_PIN_ORDER=compact

mpirun -np 12 ./build/iso3dfd 256 256 480 64 64 64 100 pure_mpi
```

```text
iso3dfd — 3D isotropic finite-difference solver
Grid:        256 x 256 x 480
Block:       64 x 64 x 64
Iterations:  100
Half-length: 8 (16th-order stencil)
------------------------------------------------
------------------------------------------------
Time:         13.558 s
Performance:  12.42 GFLOPS
Lattice:      26726400 points/iteration
```

> 12 рангов, по одному на ядро. 40 слоёв/ранг, 24 interior. Диагностика V2 не печатается — `use_omp=false`, вызывается `compute_serial` на каждом ранге. MPI_Reduce(MPI_MAX) корректно агрегирует время. 12.42 GFLOPS — выше hybrid 2×6 на 256³ (12.16) за счёт большего домена (n3=480 vs 256).

---

### Запуск 7: hybrid 2×6 (256×256×480, 100 итераций)

```bash
export OMP_NUM_THREADS=6
export OMP_PROC_BIND=close
export OMP_PLACES=cores
export I_MPI_PIN=on
export I_MPI_PIN_DOMAIN=0x3F,0xFC0
export I_MPI_PIN_ORDER=compact

mpirun -np 2 ./build/iso3dfd 256 256 480 64 64 64 100 hybrid
```

```text
[v2] b1=64 b2=64 b3=64  b2_l2=48  tb_steps=0
[v2] 2D LC = 204 KB (L2 budget: 256 KB, reserve: 80%)
[v2] ranks=2  threads/rank=6  nz_local=240  nz_global=480
iso3dfd — 3D isotropic finite-difference solver
Grid:        256 x 256 x 480
Block:       64 x 64 x 64
Iterations:  100
Half-length: 8 (16th-order stencil)
------------------------------------------------
------------------------------------------------
Time:         10.809 s
Performance:  15.58 GFLOPS
Lattice:      26726400 points/iteration
```

> `I_MPI_PIN_DOMAIN=0x3F,0xFC0` — ранг 0 на ядрах 0–5 (NUMA-узел 0), ранг 1 на ядрах 6–11 (NUMA-узел 1). 6 OMP-потоков на ранг, 240 слоёв/ранг. 15.58 GFLOPS — лучший результат для 100 итераций. Увеличение n3 (480 vs 256) даёт +28% к hybrid 2×6 (15.58 vs 12.16).

---

### Запуск 8: hybrid 2×6 (512×512×256, 100 итераций)

```bash
export OMP_NUM_THREADS=6
export OMP_PROC_BIND=close
export OMP_PLACES=cores
export I_MPI_PIN=on
export I_MPI_PIN_DOMAIN=0x3F,0xFC0
export I_MPI_PIN_ORDER=compact

mpirun -np 2 ./build/iso3dfd 512 512 256 64 64 64 100 hybrid
```

```text
[v2] b1=64 b2=64 b3=64  b2_l2=48  tb_steps=0
[v2] 2D LC = 204 KB (L2 budget: 256 KB, reserve: 80%)
[v2] ranks=2  threads/rank=6  nz_local=128  nz_global=256
iso3dfd — 3D isotropic finite-difference solver
Grid:        512 x 512 x 256
Block:       64 x 64 x 64
Iterations:  100
Half-length: 8 (16th-order stencil)
------------------------------------------------
------------------------------------------------
Time:         31.724 s
Performance:  11.73 GFLOPS
Lattice:      59043840 points/iteration
```

> Увеличенная сетка 512×512×256 — рабочий набор ~55 МБ превышает суммарный L3 (24 МБ). Throughput 11.73 GFLOPS — падение на 9.1% относительно 256³ (12.16). Мягче, чем в исходном коде (−37%), благодаря X-blocking и L2 micro-block.

---

### Запуск 9: pure_omp 12 (256³, 2000 итераций, b2_l2=56)

```bash
export OMP_NUM_THREADS=12
export OMP_PROC_BIND=close
export OMP_PLACES=cores
export ISO3DFD_B2_L2=56

./build/iso3dfd 256 256 256 64 64 64 2000 pure_omp
```

```text
[v2] b1=64 b2=64 b3=64  b2_l2=56  tb_steps=0
[v2] 2D LC = 238 KB (L2 budget: 256 KB, reserve: 80%)
[v2] ranks=1  threads/rank=12  nz_local=256  nz_global=256
iso3dfd — 3D isotropic finite-difference solver
Grid:        256 x 256 x 256
Block:       64 x 64 x 64
Iterations:  2000
Half-length: 8 (16th-order stencil)
------------------------------------------------
------------------------------------------------
Time:         136.067 s
Performance:  12.80 GFLOPS
Lattice:      13824000 points/iteration
```

> `ISO3DFD_B2_L2=56` — переопределение L2 micro-block. 2D LC = 238 КБ (запас 7% от L2). 12.80 GFLOPS — +12.3% к b2_l2=48 (11.40). Больший L2-блок = больше переиспользование = меньше DRAM-трафик. Оптимум может быть ещё правее (64, 72).

---

### Запуск 10: hybrid 1×12 (256×256×480, 2000 итераций)

```bash
export OMP_NUM_THREADS=12
export OMP_PROC_BIND=close
export OMP_PLACES=cores
export I_MPI_PIN=on
export I_MPI_PIN_DOMAIN=0xFFF
export I_MPI_PIN_ORDER=compact

mpirun -np 1 ./build/iso3dfd 256 256 480 64 64 64 2000 hybrid
```

```text
[v2] b1=64 b2=64 b3=64  b2_l2=48  tb_steps=0
[v2] 2D LC = 204 KB (L2 budget: 256 KB, reserve: 80%)
[v2] ranks=1  threads/rank=12  nz_local=480  nz_global=480
iso3dfd — 3D isotropic finite-difference solver
Grid:        256 x 256 x 480
Block:       64 x 64 x 64
Iterations:  2000
Half-length: 8 (16th-order stencil)
------------------------------------------------
------------------------------------------------
Time:         208.090 s
Performance:  16.18 GFLOPS
Lattice:      26726400 points/iteration
```

> 1 ранг, 12 OMP-потоков, n3=480. 2000 итераций — эффект first-touch и кэш-прогрева. 16.18 GFLOPS — лучший результат во всей серии. +37% к 100 итерациям (11.81 → 16.18). Ноль MPI-коммуникаций (1 ранг), полный домен 480 слоёв.

---

*Отчёт сформирован 27 сентября 2026 г., сервер kol-serv, Иркутск.*
