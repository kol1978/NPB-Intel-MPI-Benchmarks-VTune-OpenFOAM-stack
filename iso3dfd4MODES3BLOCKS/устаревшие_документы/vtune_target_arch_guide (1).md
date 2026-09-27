# Профилирование iso3dfd: выбор TARGET_ARCH и команды VTune

## Почему `Generic` без векторизации — ошибка для cache-анализа

Векторизованный цикл и скалярный — это **разный по размеру код**:

- **Векторизованный:** пролог (выравнивание), основной векторный цикл, эпилог (хвост) — больше инструкций, шире регистры.
- **Скалярный:** простой цикл — меньше инструкций.

Это влияет на заполнение кэшей:

| Кэш | Как векторизация меняет картину |
|---|---|
| **L1i** | Векторизованный код больше — может не влезть в L1i, если блок маленький. Скалярный влезает легко. |
| **L2** | L2 общий для данных и инструкций. Больше кода → меньше места для данных. |
| **L3** | Аналогично — код конкурирует с данными за L3. |

Если подбираешь размеры блоков (n1, n2, n3) под кэш, то профиль **без векторизации** покажет другую картину: код меньше, кэш свободнее, блоки можно сделать больше. А в проде с `-xSSE4.2` код вырастет — и те же блоки начнут не влезать.

---

## Правильный подход: два типа профилирования — два билда

### 1. Поиск алгоритмических hotspots (без векторизации)

```bash
cmake -S . -B build-vtune -DVERIFY_RESULTS=1 \
    -DTARGET_ARCH=Generic \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo
```

**Цель:** увидеть, какая функция жрёт время, где избыточные вычисления, где MPI-барьеры. Векторизация тут мешает — она «размазывает» циклы.

### 2. Cache analysis и подбор блоков (с векторизацией)

```bash
cmake -S . -B build-vtune -DVERIFY_RESULTS=1 \
    -DTARGET_ARCH=Westmere \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo
```

**Цель:** измерить cache misses, TLB misses, bandwidth — с реальным кодом, который пойдёт в прод. Тут `-xSSE4.2` обязателен, потому что размер кода влияет на заполнение кэша.

---

## Что меняется в CMakeLists.txt

Ничего — он уже поддерживает оба варианта через `TARGET_ARCH`. Просто в документации описано, когда какой выбирать.

---

## Как это выглядит в практике VTune

| Тип анализа VTune | `TARGET_ARCH` | Что смотрим |
|---|---|---|
| **Hotspots (SW sampling)** | `Generic` | Имена функций, строки, время CPU |
| **Memory Access (cache misses)** | `Westmere` | L1/L2/L3 misses, bandwidth |
| **Microarchitecture (uOps)** | `Westmere` | Port utilization, stalls |
| **Threading (OpenMP)** | `Generic` | Load imbalance, lock waits |
| **MPI (imbalance)** | `Generic` | MPI time, wait time |

---

## Важное перед запуском

1. **Активируй oneAPI** (чтобы был `vtune` и корректные пути):
   ```bash
   source /opt/intel/oneapi/setvars.sh --force
   ```
2. **Проверь права:** аппаратный сэмплинг (HPC) требует `sudo`. Если не хочешь `sudo` — используй `-mode=sw` (программный сэмплинг), он менее точен, но работает без root.
3. **Убедись, что билд свежий** и флаги `-gline-tables-only -fdebug-info-for-profiling` применены (в `RelWithDebInfo`).

---

## 1. Hotspots (алгоритмические узкие места) — сборка Generic

**Цель:** понять, какие функции и строки кода тратят время, без искажений от агрессивной векторизации.

```bash
# Сборка (если ещё не сделана)
rm -rf build-vtune-hotspots
cmake -S . -B build-vtune-hotspots \
    -DVERIFY_RESULTS=1 \
    -DTARGET_ARCH=Generic \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build-vtune-hotspots -j$(nproc)

# Профилирование (программный сэмпл, без sudo)
vtune -collect hotspots \
      -result-dir r000hs_generic \
      -mode=sw \
      numactl --membind=0 --cpunodebind=0 \
      mpirun -np 12 ./build-vtune-hotspots/iso3dfd
```

**Что смотреть в GUI VTune:**
- Top Functions (по Exclusive CPU Time) — где реально тратится время.
- Source View — строки кода, соответствующие горячим функциям.
- Call Stack — кто вызывает «тяжёлые» функции.

---

## 2. Memory Access (кэши, bandwidth) — сборка Westmere

**Цель:** измерить L1/L2/L3 misses, TLB, пропускную способность памяти — именно с тем кодом, который пойдёт в прод (векторизованным).

```bash
# Сборка
rm -rf build-vtune-mem
cmake -S . -B build-vtune-mem \
    -DVERIFY_RESULTS=1 \
    -DTARGET_ARCH=Westmere \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build-vtune-mem -j$(nproc)

# Профилирование (аппаратный сэмпл — лучше для кэшей)
sudo vtune -collect memory-access \
           -result-dir r000ma_westmere \
           numactl --membind=0 --cpunodebind=0 \
           mpirun -np 12 ./build-vtune-mem/iso3dfd
```

**Что смотреть:**
- Memory Bandwidth (GB/s) — насколько близко к пиковой для твоего NUMA-узла.
- L1/L2/L3 Cache Misses (на инструкцию) — где кэш-промахи «убивают» производительность.
- Memory Access Pattern — последовательный/случайный доступ, полезно для подбора размеров блоков.

> Если `sudo` недоступен, замени `memory-access` на `memory-access -mode=sw`, но точность по кэшам будет ниже.

---

## 3. Microarchitecture (uOps, порты, stalls) — сборка Westmere

**Цель:** увидеть, где процессор простаивает из-за зависимостей, промахов кэша или узких мест конвейера.

```bash
sudo vtune -collect uarch-exploration \
           -result-dir r000ue_westmere \
           numactl --membind=0 --cpunodebind=0 \
           mpirun -np 12 ./build-vtune-mem/iso3dfd
```

**Что смотреть:**
- Port Utilization — какие порты перегружены (часто ALU, Load/Store).
- Stalls (Frontend/Backend) — из-за чего процессор ждёт (инструкции, кэш, память).
- Retiring vs Bad Speculation — сколько полезной работы vs отброшенные спекулятивные инструкции.

---

## 4. Threading (OpenMP/MPI imbalance) — любой билд (лучше Generic)

**Цель:** проверить балансировку нагрузки между MPI-рангами и OpenMP-потоками.

```bash
vtune -collect threading \
      -result-dir r000th_generic \
      -mode=sw \
      numactl --membind=0 --cpunodebind=0 \
      mpirun -np 12 ./build-vtune-hotspots/iso3dfd
```

**Что смотреть:**
- Load Imbalance — насколько равномерно нагрузка распределена по рангам.
- Wait Time (Lock, Barrier) — сколько времени потоки ждут синхронизации.
- Thread Activity Timeline — визуализация, где потоки простаивают.

---

## 5. Быстрый свод метрик через MPS (MPI Performance Snapshot)

Это не VTune, но очень полезно как быстрый чек перед долгим сбором данных. Добавляется одним флагом к `mpirun`:

```bash
mpirun -np 12 -mps ./build-vtune-mem/iso3dfd
```

**Выдаст для каждого ранга:**
- Время выполнения (Total Time).
- GFLOPS (вычислительная эффективность).
- CPI (Cycles Per Instruction).
- Оценка «Memory Bound» (насколько код ограничен памятью).
- Потребление памяти (среднее и максимальное).

Используй это, чтобы быстро понять: «у нас проблема с вычислениями (низкие GFLOPS) или с памятью (высокий Memory Bound)».

---

## Как открыть результаты в GUI

После сбора (например, `r000ma_westmere`):

```bash
amplxe-cl -open-result r000ma_westmere
```

или просто открой папку в VTune GUI: `File → Open Result…` и выбери директорию результата.

---

## Чек-лист: чтобы профилирование было «честным»

- **NUMA-привязка:** `numactl` обязателен, иначе процессы будут мигрировать и портить статистику.
- **Huge pages / THP:** зафиксируй настройки до запуска (THP=disabled для предсказуемости).
- **Governor:** для коротких бенчмарков ставь `performance`, иначе частоты будут «плавать».
- **Повторяемость:** запусти 2–3 раза, убедись, что тайминги и метрики стабильны (разброс <5%).
- **Длительность:** профиль должен захватывать основную вычислительную часть (не только инициализацию).

---

## Пример полного цикла проверки (для кейса подбора блоков)

1. Запускаешь MPS: видишь высокий Memory Bound → подозреваешь кэш.
2. Делаешь `memory-access` с `Westmere`-билдом → подтверждаешь L3 misses.
3. Меняешь размеры блоков, пересбираешь `Westmere`, снова `memory-access`.
4. Проверяешь, что Hotspots не «поехали» (новые блоки не создали новых узких мест).
5. Проверяешь Threading — балансировка осталась или ухудшилась.
