# Стратегия перехода на L3 Temporal Blocking в iso3dfd V2

**Дата:** 28 сентября 2026 г.
**Платформа:** Intel Xeon X5675 (Westmere-EP), 2 × 6 ядер, 3.07 GHz
**Сервер:** kol-serv
**Цель:** переход от memory-bound (~17 GFLOPS, AI = 0.42) к compute-bound (~80–100 GFLOPS, AI = 3.36–8.40) через temporal tiling с N=8–12 шагов в L3.

---

## Содержание

1. [Текущая архитектура блочной структуры кода](#1-текущая-архитектура-блочной-структуры-кода)
2. [Терминология loop tiling](#2-терминология-loop-tiling)
3. [Целевая архитектура: трёхуровневый tiling](#3-целевая-архитектура-трёхуровневый-tiling)
4. [Изменения по модулям](#4-изменения-по-модулям)
5. [Волновой фронт: алгоритм и псевдокод](#5-волновой-фронт-алгоритм-и-псевдокод)
6. [Расширение halo и MPI](#6-расширение-halo-и-mpi)
7. [План реализации по этапам](#7-план-реализации-по-этапам)
8. [Бенчмарк-план](#8-бенчмарк-план)
9. [Структура отчёта](#9-структура-отчёта)

---

## 1. Текущая архитектура блочной структуры кода

### Иерархия блоков (V2, без temporal blocking)

```
Домен (256×256×256 или 256×256×480)
  │
  ├── MPI-декомпозиция по Z (driver)
  │   └── nz_local слоёв на ранг
  │
  └── Spatial tiling — L3-блок (b1=64 × b2=64 × b3=64)
        │   2D LC = 17×64×64×4 = 272 КБ → L3 (12 МБ) ✅
        │
        └── Nested spatial tiling — L2-микроблок (b1=64 × b2_l2=48 × b3=64)
              │   2D LC = 17×64×48×4 = 204 КБ → L2 (256 КБ) ✅ (запас 20%)
              │
              └── L1-векторизация — цикл по X (64/4 = 16 SSE-векторов)
                    1D LC = 64×4 = 256 байт → L1 (32 КБ) ✅
```

### Карта вызовов

```
DRIVER (iso3dfd_driver.cpp)
  ├── exchange_halo() — MPI_Sendrecv, 8 слоёв
  ├── for iter in 1..niter:
  │     exchange_halo()
  │     compute_iteration()  ──► SOLVER
  │     swap(prev, next)
  └── PrintStats()

SOLVER (iso3dfd_solver.cpp)
  compute_iteration(prev, next, ..., use_omp)
    ├── use_omp=false → compute_serial()
    │     for bz, by, bx:  ← spatial tiling (L3-блок)
    │       for bz2, by2:  ← nested spatial tiling (L2-микроблок)
    │         for z, y, x:  ← L1-векторизация
    │           stencil_point()
    │
    └── use_omp=true  → compute_omp()
          #pragma omp parallel for
          for bz, by, bx:  ← spatial tiling (L3-блок)
            for bz2, by2:  ← nested spatial tiling (L2-микроблок)
              for z, y, x:  ← L1-векторизация
                stencil_point()
```

### Текущие параметры

| Параметр | Значение | Где задаётся |
|---|---|---|
| b1, b2, b3 | 64, 64, 64 | аргументы запуска |
| b2_l2 | 48 (авто: ⌊256×0.8/(17×64×4)⌋) | env ISO3DFD_B2_L2 |
| tb_steps | 0 (выключено) | env ISO3DFD_TB_STEPS |
| halo | 8 слоёв (kHalfLength) | константа в коде |
| niter | 100 или 2000 | аргумент запуска |

### Узкое место

AI = 0.42 FLOP/byte, DRAM ceiling = 0.42 × 64 = 26.9 GFLOPS.
Код memory-bound: каждое чтение из DRAM стоит ~200 циклов, 24 из 25 обращений к данным — мимо L3 на больших доменах.

---

## 2. Терминология loop tiling

> **Loop tiling** (тайлинг, разбиение циклов на плитки; также известен как cache blocking) — оптимизирующее преобразование, при котором итерационное пространство цикла разбивается на небольшие блоки (tiles), целиком помещающиеся в кэш. Цель — максимизация переиспользования данных до их вытеснения из кэша.

| Уровень | Термин | Что делает | Параметр |
|---|---|---|---|
| L3-блок | **Spatial tiling** | Разбиение по (X, Y, Z) — блок 64×64×64 | b1, b2, b3 |
| L2-микроблок | **Nested spatial tiling** | Вложенное разбиение по Y — 64×48×64 | b2_l2 |
| Волновой фронт | **Temporal tiling** | Разбиение по времени — N=8–12 шагов | tb_steps |

---

## 3. Целевая архитектура: трёхуровневый tiling

```
Домен (256×256×N3)
  │
  ├── MPI-декомпозиция по Z (driver)
  │   └── nz_local слоёв на ранг
  │
  └── Spatial tiling — L3-блок (b1=64 × b2=64 × b3=64)
        │   2D LC = 17×64×64×4 = 272 КБ → L3 ✅
        │
        └── Temporal tiling — N=8 шагов в волновом фронте
              │   Рабочий набор = (16+N)×64×64×4 = 384 КБ/поток
              │   6 потоков × 384 КБ = 2,3 МБ << 10,5 МБ L3 ✅ (запас 78%)
              │
              └── Nested spatial tiling — L2-микроблок (b1=64 × b2_l2=48)
                    │   2D LC = 17×64×48×4 = 204 КБ → L2 ✅
                    │
                    └── L1-векторизация — цикл по X
                          1D LC = 64×4 = 256 байт → L1 ✅
```

### Ключевое отличие от текущего кода

| Аспект | Текущий код (N=1) | Целевой код (N=8) |
|---|---|---|
| Итераций на блок | 1 | 8 (волновой фронт) |
| Halo (слои) | 8 | 64 (8×8) |
| AI | 0.42 | 3.36 |
| DRAM-трафик | ~150 Б/точка | ~19 Б/точка |
| Bottleneck | DRAM bandwidth | SSE4.2 compute |
| Прогноз GFLOPS | ~17 | 80–100 |

---

## 4. Изменения по модулям

### 4.1. SOLVER (iso3dfd_solver.cpp) — ~120 строк

**Добавить функцию `compute_temporal_block()`:**

```cpp
// Обработка N временных шагов внутри одного L3-блока
// Волновой фронт по (Z, T)
static void compute_temporal_block(
    float* prev, float* next,
    float* vel, float* coeff,
    int b1, int b2, int b3,
    int b2_l2, int tb_steps,
    int z_start, int z_end,    // границы блока в глобальных Z
    int n1, int n2,             // размеры сетки
    int halo                   // kHalfLength = 8
) {
    // Двойной буфер: prev ↔ next, N шагов
    float* curr = prev;
    float* curr_next = next;

    for (int t = 0; t < tb_steps; t++) {
        // Волновой фронт: на шаге t можно вычислять z от
        //   z_start + t*halo до z_end - (tb_steps - t)*halo
        int z_lo = z_start + t * halo;
        int z_hi = z_end - (tb_steps - t) * halo;

        // Nested spatial tiling внутри блока
        for (int by2 = 0; by2 < b2; by2 += b2_l2) {
            int by2_hi = min(by2 + b2_l2, b2);
            for (int z = z_lo; z < z_hi; z++) {
                for (int y = by2; y < by2_hi; y++) {
                    #pragma omp simd
                    for (int x = 0; x < b1; x++) {
                        stencil_point(curr, curr_next, vel, coeff,
                                      x, y, z, n1, n2);
                    }
                }
            }
        }

        // Swap буферов для следующего временного шага
        float* tmp = curr;
        curr = curr_next;
        curr_next = tmp;
    }
}
```

**Изменить `compute_iteration()`:**

```cpp
void compute_iteration(...) {
    if (tb_steps > 0) {
        // Temporal tiling: обработка блоков с N шагами
        #pragma omp parallel for collapse(2) schedule(static)
        for (int bz = 0; bz < nz_local; bz += b3) {
            for (int by = 0; by < n2; by += b2) {
                // Один L3-блок, N временных шагов
                compute_temporal_block(prev, next, vel, coeff,
                                       b1, b2, b3, b2_l2, tb_steps,
                                       bz, bz + b3, n1, n2, kHalfLength);
            }
        }
    } else {
        // Текущий код (N=1)
        // ... без изменений ...
    }
}
```

### 4.2. DRIVER (iso3dfd_driver.cpp) — ~50 строк

**Расширить halo с 8 до 8×N слоёв:**

```cpp
int halo_layers = kHalfLength * (tb_steps > 0 ? tb_steps : 1);
// halo_size = halo_layers × n1 × n2 × sizeof(float)
```

**Изменить MPI-обмен:**

```cpp
void exchange_halo(...) {
    if (tb_steps > 0) {
        // Обменивать 8×N слоёв вместо 8
        MPI_Sendrecv(send_top, halo_layers * n1 * n2, MPI_FLOAT, top_rank, ...,
                     recv_bot,  halo_layers * n1 * n2, MPI_FLOAT, bot_rank, ...);
        // Аналогично для bottom → top
    } else {
        // Текущий обмен 8 слоёв — без изменений
    }
}
```

**Изменить временной цикл:**

```cpp
// Было:
for (int iter = 0; iter < niter; iter++) {
    exchange_halo();
    compute_iteration();  // 1 шаг
    swap(prev, next);
}

// Стало (при tb_steps > 0):
for (int iter = 0; iter < niter; iter += tb_steps) {
    exchange_halo();  // 8×N слоёв
    compute_iteration();  // N шагов внутри блока
    swap(prev, next);
    // После N шагов — следующий exchange_halo
}
```

### 4.3. GRID (iso3dfd_grid.cpp) — ~10 строк

**Расширить буферы:**

```cpp
// Было: halo = kHalfLength = 8
// Стало: halo = kHalfLength * max(tb_steps, 1) = 64 (при N=8)

int halo_size = kHalfLength * (tb_steps > 0 ? tb_steps : 1);
// Аллокация: n1 × n2 × (nz_local + 2 × halo_size)
```

### 4.4. MAIN (iso3dfd.cpp) — ~10 строк

**Чтение параметра tb_steps:**

```cpp
// Из переменной окружения
const char* tb_env = getenv("ISO3DFD_TB_STEPS");
int tb_steps = tb_env ? atoi(tb_env) : 0;
```

**Передача в driver:**

```cpp
run_driver(..., tb_steps);
```

### 4.5. Сводка изменений

| Модуль | Функция | Что меняется | Новых строк |
|---|---|---|---|
| solver | `compute_temporal_block()` | Новая функция | ~50 |
| solver | `compute_iteration()` | Ветвление tb_steps | ~20 |
| driver | `exchange_halo()` | Расширенный halo | ~20 |
| driver | Временной цикл | Шаг по tb_steps | ~10 |
| grid | `allocate()` | Буфер 8N слоёв | ~10 |
| main | парсинг | ISO3DFD_TB_STEPS | ~5 |
| **Итого** | | | **~115** |

---

## 5. Волновой фронт: алгоритм и псевдокод

### Зависимость данных

Точка (x, y, z, t+1) зависит от точек (x±0..8, y±0..8, z±0..8, t).
По оси Z зависимость распространяется на 8 слоёв.

### Схема волнового фронта в одном L3-блоке (b3=64, N=4)

```
Z-слой →  0  1  ...  8  9  ... 16 17 ... 24 25 ... 32 33 ... 40 41 ... 48 49 ... 56 57 ... 63
T=0:     [████████████████████████████████████████████████████████████████████████████████]
T=1:          [████████████████████████████████████████████████████████████████████████████]
T=2:               [████████████████████████████████████████████████████████████████████████]
T=3:                    [████████████████████████████████████████████████████████████████████]
T=4:                         [████████████████████████████████████████████████████████████]
```

На шаге t можно вычислять Z от `t×8` до `b3 - (N-t)×8`.
Размер вычисляемой области на шаге t: `b3 - N×8` = 64 - 32 = 32 слоя (при N=4).

### Псевдокод (упрощённый)

```
для каждого L3-блока (bz, by):
    curr = prev
    curr_next = next
    для t = 0 .. N-1:
        z_lo = bz + t × 8          // начало волнового фронта
        z_hi = bz + b3 - (N-1-t) × 8  // конец
        // Вычисление доступной области
        для by2 = 0 .. b2 step b2_l2:   // nested spatial tiling
            для z = z_lo .. z_hi:
                для y = by2 .. by2+b2_l2:
                    #pragma omp simd
                    для x = 0 .. b1:
                        stencil_point(curr, curr_next, ...)
        swap(curr, curr_next)
```

### Ограничение волнового фронта

При N=8, b3=64: полезных слоёв на шаг = 64 - 8×8 = 0.
**Блок b3=64 слишком мал для N=8!**

| N | b3=64 | b3=128 | b3=256 |
|---|---|---|---|
| 4 | 32 слоя (50%) | 96 (75%) | 224 (87%) |
| 8 | 0 (0%) ❌ | 64 (50%) | 192 (75%) |
| 12 | -32 ❌ | 32 (25%) | 160 (62%) |

**Вывод:** при b3=64 максимум N=4 (32 полезных слоя).
Для N=8 нужно b3≥128, для N=12 — b3≥256.

### Корректировка плана

| Параметр | Вариант A | Вариант B | Вариант C |
|---|---|---|---|
| b3 | 64 | 128 | 256 |
| N (max) | 4 | 8 | 12 |
| Полезных слоёв | 32 (50%) | 64 (50%) | 160 (62%) |
| L3 budget (6 потоков) | 1,75 МБ | 1,75 МБ | 1,75 МБ |
| Рабочий набор N=4 | 320 КБ/поток ✅ | — | — |
| Рабочий набор N=8 | — | 384 КБ/поток ✅ | — |
| AI при N=4 | 1,68 | — | — |
| AI при N=8 | — | 3,36 | — |
| Прогноз GFLOPS | 50–70 | 80–100 | 80–100 |

**Рекомендация:** начать с **Варианта A** (b3=64, N=4) — минимальные изменения кода, быстрая проверка концепции. Затем перейти к **Варианту B** (b3=128, N=8) для полного прорыва.

---

## 6. Расширение halo и MPI

### Текущий halo (N=1)

```
... | H H H H H H H H | D D D D ... D D D D | H H H H H H H H | ...
    8 слоёв halo      nz_local слоёв         8 слоёв halo
```

MPI-обмен: 8 × 256 × 256 × 4 = 2 МБ за итерацию.

### Расширенный halo (N=8, b3=128)

```
... | H H ... H H | D D D D ... D D D D | H H ... H H | ...
    64 слоя halo    nz_local слоёв        64 слоя halo
```

MPI-обмен: 64 × 256 × 256 × 4 = 16 МБ за N итераций (вместо 8×8=64 МБ при N=1).

**Частота обмена:** 1 обмен на N итераций (вместо N обменов).
При N=8: 16 МБ за 8 итераций vs 16 МБ за 8 итераций (N=1) — **трафик MPI не меняется!**

### Ограничение: nz_local ≥ 8×N + b3

| N | b3 | Минимум nz_local | n3 для 2 рангов |
|---|---|---|---|
| 4 | 64 | 96 | 192 |
| 8 | 128 | 192 | 384 |
| 12 | 256 | 352 | 704 |

---

## 7. План реализации по этапам

### Этап 0: Подготовка (сегодня)

- [ ] Внедрить терминологию loop tiling в комментарии кода
- [ ] Проверить, что tb_steps уже читается из env (ISO3DFD_TB_STEPS)
- [ ] Создать ветку `tb_v3` в git

### Этап 1: Temporal tiling, b3=64, N=4 (~115 строк)

**Цель:** доказать концепцию, получить AI = 1.68, прогноз 50–70 GFLOPS.

| Компонент | Что делать | Строк |
|---|---|---|
| `compute_temporal_block()` | Новая функция, волновой фронт N=4 | 50 |
| `compute_iteration()` | Ветвление: if tb_steps > 0 | 20 |
| `exchange_halo()` | Расширить до 32 слоёв (8×4) | 15 |
| `allocate()` | Буфер +32 слоёв с каждой стороны | 10 |
| Временной цикл | Шаг по tb_steps | 10 |
| Парсинг tb_steps | Из env (если ещё нет) | 5 |

**Бенчмарк:**
```bash
# N=4, b3=64, 2000 итераций
for TB in 0 4; do
  ISO3DFD_TB_STEPS=$TB ./build/iso3dfd 256 256 256 64 64 64 2000 pure_omp
done
```

**Ожидаемый результат:** N=4 даёт 50–70 GFLOPS vs N=0 (17 GFLOPS), 3–4× ускорение.

### Этап 2: Глубокий temporal tiling, b3=128, N=8 (~30 доп. строк)

**Цель:** AI = 3.36, прогноз 80–100 GFLOPS.

| Компонент | Что менять | Строк |
|---|---|---|
| `exchange_halo()` | Расширить до 64 слоёв (8×8) | 5 |
| `allocate()` | Буфер +64 слоёв | 5 |
| Бенчмарк-скрипт | b3=128, n3=384 или 480 | 10 |
| Валидация | Проверка результата при N=8 | 10 |

**Бенчмарк:**
```bash
# N=8, b3=128, 2000 итераций
for TB in 0 4 8; do
  ISO3DFD_TB_STEPS=$TB ./build/iso3dfd 256 256 384 64 64 128 2000 pure_omp
done

# Hybrid 2×6
for TB in 0 4 8; do
  ISO3DFD_TB_STEPS=$TB mpirun -n 2 ./build/iso3dfd 256 256 384 64 64 128 2000 hybrid
done
```

### Этап 3: Документация

- Обновить отчёт с результатами temporal tiling
- Сравнить с прогнозом (80–100 GFLOPS)
- Профилировать VTune (compute-bound?)

---

## 8. Бенчмарк-план

### Скрипт: ISO3DFD_TB_STEPS_tuning.sh

```bash
#!/bin/bash
# Temporal blocking tuning: N=0 (baseline), 2, 4, 8
# Тест: pure_omp 12, 2000 итераций

export OMP_NUM_THREADS=12
export OMP_PROC_BIND=close
export OMP_PLACES=cores

GRID=256
ITER=2000

# --- Серия 1: b3=64 (N_max=4) ---
echo "=== b3=64 ==="
for TB in 0 2 4; do
  echo "--- tb_steps=$TB ---"
  ISO3DFD_TB_STEPS=$TB ./build/iso3dfd \
    $GRID $GRID $((GRID + TB * 16)) \
    64 64 64 $ITER pure_omp
done

# --- Серия 2: b3=128 (N_max=8) ---
echo "=== b3=128 ==="
for TB in 0 4 8; do
  echo "--- tb_steps=$TB ---"
  ISO3DFD_TB_STEPS=$TB ./build/iso3dfd \
    $GRID $GRID $((GRID + TB * 16)) \
    64 64 128 $ITER pure_omp
done

# --- Серия 3: Hybrid 2×6, b3=128 ---
echo "=== hybrid 2×6, b3=128 ==="
export I_MPI_PIN=on
export I_MPI_PIN_DOMAIN=0x3F,0xFC0
export I_MPI_PIN_ORDER=compact
export OMP_NUM_THREADS=6
for TB in 0 4 8; do
  echo "--- tb_steps=$TB ---"
  ISO3DFD_TB_STEPS=$TB mpirun -n 2 ./build/iso3dfd \
    $GRID $GRID $((GRID + TB * 16)) \
    64 64 128 $ITER hybrid
done
```

### Ожидаемая таблица результатов

| tb_steps | b3 | AI | Режим | Прогноз GFLOPS | Ускорение |
|---|---|---|---|---|---|
| 0 | 64 | 0.42 | pure_omp | ~17 | 1× |
| 2 | 64 | 0.84 | pure_omp | ~30–40 | 2× |
| 4 | 64 | 1.68 | pure_omp | ~50–70 | 3–4× |
| 0 | 128 | 0.42 | pure_omp | ~17 | 1× |
| 4 | 128 | 1.68 | pure_omp | ~50–70 | 3–4× |
| 8 | 128 | 3.36 | pure_omp | ~80–100 | 5–6× |
| 8 | 128 | 3.36 | hybrid 2×6 | ~80–100 | 5–6× |

---

## 9. Структура отчёта

```markdown
# Отчёт: L3 Temporal Blocking в iso3dfd V3

## 1. Терминология
   - Loop tiling (определение)
   - Иерархия: spatial → nested spatial → temporal

## 2. Текущее состояние (V2)
   - Блочная структура кода (diagram)
   - Результаты tuning b2_l2 (28.09, 3 серии)
   - Узкое место: DRAM bandwidth, AI=0.42

## 3. Архитектура L3 temporal blocking
   - Кэш-бюджеты L3 (1,75 МБ/поток)
   - Волновой фронт: схема + ограничения
   - Ограничение b3 ≥ 8×N (корректировка плана)

## 4. Реализация
   - Изменения по модулям (таблица)
   - compute_temporal_block() — псевдокод
   - Расширение halo и MPI

## 5. Результаты бенчмарков
   - Серия 1: b3=64, N=0/2/4
   - Серия 2: b3=128, N=0/4/8
   - Серия 3: hybrid 2×6, b3=128, N=0/4/8
   - Сводная таблица: GFLOPS, AI, ускорение

## 6. Анализ
   - Roofline: переход memory-bound → compute-bound
   - Сравнение прогноз vs факт
   - VTune (если доступно): IPC, L2-miss rate

## 7. Выводы и рекомендации
   - Оптимальный N
   - Оптимальный b3
   - Применение к OpenFOAM (передача опыта)
```

---

## Сводка: что нужно сделать

| # | Что | Строк кода | Время |
|---|---|---|---|
| 1 | Создать ветку tb_v3 | — | 5 мин |
| 2 | Написать compute_temporal_block() | 50 | 2 часа |
| 3 | Интегрировать в compute_iteration() | 20 | 1 час |
| 4 | Расширить halo в driver | 15 | 1 час |
| 5 | Расширить буферы в grid | 10 | 30 мин |
| 6 | Бенчмарк-скрипт | 30 | 30 мин |
| 7 | Запуск серии 1 (b3=64, N=4) | — | 15 мин |
| 8 | Анализ результатов | — | 1 час |
| 9 | Запуск серии 2 (b3=128, N=8) | — | 30 мин |
| 10 | Отчёт | — | 2 часа |
| **Итого** | | **~125 строк** | **~8 часов** |

---

*Стратегия подготовлена 28 сентября 2026 г., сервер kol-serv, Иркутск.*
