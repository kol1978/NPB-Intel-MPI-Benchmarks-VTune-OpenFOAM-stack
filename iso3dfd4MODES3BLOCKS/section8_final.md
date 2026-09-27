## 8. Исходные данные запусков

**Дата запусков:** 27 сентября 2026 г., 15:26–15:37 (Иркутск, UTC+8)  
**Сервер:** kol-serv  
**Методология:** перед каждым запуском — полная очистка переменных окружения (`unset`), затем установка только необходимых. Состояние каждой переменной фиксируется в логе.

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
# Все переменные окружения очищены
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
Time:         47.095 s
Performance:  1.85 GFLOPS
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
iso3dfd — 3D isotropic finite-difference solver
Grid:        256 x 256 x 40
Block:       16 x 8 x 40
Iterations:  100
Half-length: 8 (16th-order stencil)
------------------------------------------------
------------------------------------------------
Time:         12.282 s
Performance:  0.71 GFLOPS
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
iso3dfd — 3D isotropic finite-difference solver
Grid:        256 x 256 x 128
Block:       16 x 8 x 128
Iterations:  100
Half-length: 8 (16th-order stencil)
------------------------------------------------
------------------------------------------------
Time:         21.547 s
Performance:  1.89 GFLOPS
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
iso3dfd — 3D isotropic finite-difference solver
Grid:        256 x 256 x 128
Block:       16 x 8 x 128
Iterations:  100
Half-length: 8 (16th-order stencil)
------------------------------------------------
------------------------------------------------
Time:         4.017 s
Performance:  10.12 GFLOPS
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
iso3dfd — 3D isotropic finite-difference solver
Grid:        256 x 256 x 256
Block:       16 x 8 x 64
Iterations:  100
Half-length: 8 (16th-order stencil)
------------------------------------------------
------------------------------------------------
Time:         4.169 s
Performance:  20.89 GFLOPS
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
iso3dfd — 3D isotropic finite-difference solver
Grid:        256 x 256 x 80
Block:       16 x 8 x 80
Iterations:  100
Half-length: 8 (16th-order stencil)
------------------------------------------------
------------------------------------------------
Time:         25.132 s
Performance:  0.92 GFLOPS
Lattice:      3686400 points/iteration
```

> `I_MPI_PIN_DOMAIN=0xFFF` — биты 0–11, все 12 физических ядер. 12 рангов, по одному на ядро: ранги 0–5 на NUMA-узле 0, ранги 6–11 на NUMA-узле 1. Увеличенный домен (n3=960, 80 слоёв/ранг, 64 interior) снижает долю коммуникаций до 20%.

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
iso3dfd — 3D isotropic finite-difference solver
Grid:        512 x 512 x 128
Block:       16 x 8 x 128
Iterations:  100
Half-length: 8 (16th-order stencil)
------------------------------------------------
------------------------------------------------
Time:         27.202 s
Performance:  6.38 GFLOPS
Lattice:      27553792 points/iteration
```

> `I_MPI_PIN_DOMAIN=0x3F,0xFC0` — ранг 0 на ядрах 0–5 (NUMA-узел 0), ранг 1 на ядрах 6–11 (NUMA-узел 1). 6 OMP-потоков на ранг. Увеличенная сетка 512×512×256 — рабочий набор ~55 МБ превышает суммарный L3 (24 МБ), throughput падает из-за DRAM-traffic.

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
iso3dfd — 3D isotropic finite-difference solver
Grid:        256 x 256 x 20
Block:       16 x 8 x 20
Iterations:  100
Half-length: 8 (16th-order stencil)
------------------------------------------------
------------------------------------------------
Time:         6.325 s
Performance:  0.23 GFLOPS
Lattice:      230400 points/iteration
```

> `I_MPI_PIN_DOMAIN=0xFFF` — биты 0–11, все 12 физических ядер. 12 рангов, по одному на ядро: ранги 0–5 на NUMA-узле 0, ранги 6–11 на NUMA-узле 1.
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
iso3dfd — 3D isotropic finite-difference solver
Grid:        256 x 256 x 480
Block:       64 x 64 x 64
Iterations:  2000
Half-length: 8 (16th-order stencil)
------------------------------------------------
------------------------------------------------
Time:         220.806 s
Performance:  15.25 GFLOPS
Lattice:      26726400 points/iteration
```

> `I_MPI_PIN_DOMAIN=0xFF` — биты 0–7 (8 ядер). Ядра 0–5 на NUMA-узле 0, ядра 6–7 на NUMA-узле 1. 1 ранг получает весь домен 256×256×480 (480 слоёв), 8 OMP-потоков распределены по двум NUMA-узлам: 6 на узле 0, 2 на узле 1. Два потока на узле 1 работают с удалённой памятью через QPI — штраф за межузловой доступ. Результат: 15.25 GFLOPS — хуже, чем 2×6 (24.28 total). Нечётное разделение по NUMA-узлам без MPI — антипаттерн для 2-NUMA-машин с 6 ядрами на сокет.

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
iso3dfd — 3D isotropic finite-difference solver
Grid:        256 x 256 x 480
Block:       64 x 64 x 64
Iterations:  2000
Half-length: 8 (16th-order stencil)
------------------------------------------------
------------------------------------------------
Time:         142.511 s
Performance:  23.63 GFLOPS
Lattice:      26726400 points/iteration
```

> `I_MPI_PIN_DOMAIN=0xFFF` — биты 0–11, все 12 физических ядер. 1 ранг, 12 OMP-потоков на всех физических ядрах. Ноль MPI-коммуникаций. 3D Layer Condition в L3 нарушен (рабочий набор ~120 МБ > 12 МБ L3), но 2D LC в L1 выполняется (8,5 КБ < 32 КБ) — внутренний цикл по Z работает из L1, L3 служит фильтром для L2-эвиктов.

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
iso3dfd — 3D isotropic finite-difference solver
Grid:        256 x 256 x 240
Block:       64 x 64 x 64
Iterations:  2000
Half-length: 8 (16th-order stencil)
------------------------------------------------
------------------------------------------------
Time:         133.893 s
Performance:  12.14 GFLOPS
Lattice:      12902400 points/iteration
```

> `I_MPI_PIN_DOMAIN=0x3F,0xFC0` — ранг 0 на ядрах 0–5 (NUMA-узел 0), ранг 1 на ядрах 6–11 (NUMA-узел 1). 6 OMP-потоков на ранг, 240 слоёв/ранг. Каждый ранг работает исключительно на своей NUMA-ноде. Total GFLOPS = 12.14 × 2 = 24.28 — лучший результат в серии 8–10. NUMA-изоляция через два MPI-ранга компенсирует накладные расходы на `MPI_Sendrecv`.

---
