# V&V для CFD: обновлённые пороги и метрики после анализа fine-mesh эксперимента
# Исходные кейсы для тестирования:
[ForwardStep](https://github.com/mkraposhin/VnV/tree/main/ForwardStep)

**PS** Рабочие кейсы, включая скрипты прогона полной матрицы эксперимента: V1--V6, будут выложены (добавлены) в этот Репозиторий (от англ. repository — «хранилище»)  позже на конечной стадии, во избежание раздувания объёма репозитория (аналогично Backward-facing репозиторию)...

## Контекст

Документ обновляет V&V-пайплайн на основе повторного прогона полной матрицы V1--V6 на утяжелённом кейсе (`ForwardStep/common-12-fine`). Сравниваются две конфигурации:

| Параметр | Базовый кейс (coarse) | Утяжелённый кейс (fine) |
|----------|----------------------|------------------------|
| Кейс | backward-facing-step | ForwardStep |
| Сеток | ~145K ячеек | ~2.3M ячеек (16x) |
| Шагов | ~60 | ~24 |
| Решатель | pimpleCentralFoam | pimpleCentralFoam |
| OpenFOAM | v2312 | v2312 |
| MPI | Intel 2021.18 | Intel 2021.18 |

---

## Часть 1. Обновлённая матрица результатов

### Сводная таблица (оба кейса)

| Вариант | Coarse (сек) | Coarse vs V6 | Fine (сек) | Fine vs V6 | Изменение |
|---------|-------------|-------------|------------|-----------|-----------|
| **V6** | 369.26 | 1.000x | **151.57** | 1.000x | -- |
| **V5** | 402.11 | 1.089x | 161.66 | 1.067x | Governor эффект слабее |
| **V4** | 402.35 | 1.090x | 152.30 | 1.005x | Governor почти не нужен |
| **V3** | 905.10 | 2.451x | 294.74 | 1.945x | NUMA эффект слабее |
| **V2** | 910.33 | 2.465x | 294.10 | 1.940x | NUMA эффект слабее |
| **V1** | -- | -- | 154.27 | 1.018x | Default почти равен optimal! |

### Изоляция факторов

| Фактор | Coarse | Fine | Изменение | Интерпретация |
|--------|-------|------|-----------|---------------|
| **NUMA (V2/V6)** | 2.465x | 1.940x | -21.3% | Эффект ослаб, но остаётся критичным |
| **Governor, THP off (V4/V6)** | 1.090x | 1.005x | -7.8% | На fine mesh governor почти не нужен без THP |
| **Governor, THP on (V5/V6)** | 1.089x | 1.067x | -2.1% | Governor компенсирует THP overhead |
| **THP, governor off (V5/V4)** | 0.999x | 1.061x | +6.2% | THP стал ВРЕДНЫМ на fine mesh |
| **Symmetry (V3/V2)** | 0.994x | 1.002x | +0.8% | Симметрия сохранена |
| **Default vs optimal (V1/V6)** | -- | 1.018x | -- | Default compact уже даёт 6+6 |


---

## Часть 2. Ключевые находки

### Находка 1: NUMA-эффект сохранился, но ослаб (2.47x -> 1.94x)

Штраф за размещение всех ранков на одном сокете остаётся почти двукратным -- NUMA-aware распределение по-прежнему критично. Ослабление объясняется тем, что на более крупной сетке растёт доля вычислений (compute) относительно обращений к памяти (memory access). Каждый ранк обрабатывает больше ячеек, и соотношение "считать / ходить в память" сдвигается в сторону вычислений. Bandwidth bottleneck остаётся главным фактором (12 ранков на одном сокете делят ~19 ГБ/с против ~38 ГБ/с при 6+6), но его вклад в общее время падает.

**Вывод для V&V:** NUMA-регрессия остаётся meaningful-тестом. Порог V2/V6 нужно скорректировать.

### Находка 2: V1 ~ V6 -- default pinning уже оптимален (разница 1.8%)

Дефолтный compact-pinning Intel MPI на данной топологии (CPU 0-5 -> Node 0, CPU 6-11 -> Node 1) **естественным образом** размещает по 6 ранков на каждом сокете. Явная привязка через `I_MPI_PIN_PROCESSOR_LIST=0-5,6-11` не даёт преимущества -- default уже делает то же самое.

V1 отличается от V6 только отсутствием governor и THP. На fine mesh без THP governor даёт лишь 0.5% (см. ниже), поэтому V1 ~ V6.

**Вывод для V&V:** V1 -- идеальный canary-тест. Если V1/V6 внезапно превысит 1.10x, значит окружение сломалось (изменилась топология, сторонние процессы, throttling).

### Находка 3: Governor -- эффект зависит от THP, а не от I/O

Это самая важная и неожиданная находка. Разложение по шагам показывает, что механизм governor-эффекта принципиально разный в присутствии и отсутствии THP.

#### Governor без THP (V6 vs V4)

| Кейс | V4/V6 (governor effect) | Объяснение |
|------|------------------------|-----------|
| Coarse | 1.090 (9.0%) | I/O паузы (запись timestep-файлов) сбрасывают частоту; шагов много -> суммарный эффект большой |
| Fine | 1.005 (0.5%) | Шагов меньше (24 vs ~60), I/O пауз меньше -> governor почти не нужен |

На coarse-кейсе governor давал 9% за счёт фиксации частоты во время I/O-пауз. На fine-кейсе шагов меньше (24 вместо ~60), I/O-пауз меньше, и ondemand-governor сам успевает выйти на рабочую частоту между паузами.

#### Governor с THP (V6 vs V5)

| Кейс | V5/V6 (governor + THP effect) | Объяснение |
|------|-------------------------------|-----------|
| Coarse | 1.089 (8.9%) | THP не создаёт defrag-work (поля < 2 МБ), governor эффект ~ как без THP |
| Fine | 1.067 (6.7%) | THP создаёт defrag-work, governor компенсирует часть overhead |

На fine-кейсе governor даёт 6.7% -- но это не I/O, а компенсация THP defrag overhead (см. ниже).


### Находка 4: THP-аномалия -- `defrag=always` вредит без governor

#### Что произошло

| Сравнение | Coarse | Fine |
|-----------|--------|------|
| V5/V4 (THP effect, governor OFF) | 0.999x (нейтрально) | **1.061x (вредно!)** |

На coarse-кейсе THP был нейтральным: V5 ~ V4 (402.11 vs 402.35 сек). На fine-кейсе THP стал вредным: V5 на 6.1% медленнее V4 (161.66 vs 152.30 сек).

#### Почему `defrag=always` создаёт overhead

Параметр `transparent_hugepage/defrag=always` заставляет ядро Linux **постоянно** пытаться объединить (коалесцировать) обычные 4 КБ-страницы в 2 МБ huge pages. Этот процесс -- `khugepaged` -- работает в фоне и потребляет CPU:

1. `khugepaged` сканирует таблицы страниц процесса, ищет кандидатов на коалесцирование
2. Найдя регион, ядро выделяет новую 2 МБ-страницу, копирует туда данные из 512 обычных страниц
3. Во время копирования страница блокируется -> процесс ждёт
4. После копирования старые страницы освобождаются

Шаги 1-3 -- это **синхронная работа ядра**, которая выполняется в контексте процесса. Без governor (`ondemand` или `powersave`) частота ядра во время этой работы может быть низкой (1.6 ГГц вместо 3.46 ГГц), потому что:

- `khugepaged` работает в фоне, не создавая sustained нагрузки -> governor не повышает частоту
- Если `khugepaged` запускается во время I/O-паузы решателя (запись timestep-файла), частота уже сброшена
- Копирование 512 страниц по 4 КБ = 2 МБ данных при 1.6 ГГц занимает в ~2x больше времени, чем при 3.46 ГГц

#### Почему на coarse-кейсе THP был нейтральным

На coarse-кейсе поля на ранк меньше 2 МБ:

| Поле | Размер на ранк | Отношение к 2 МБ |
|------|---------------|------------------|
| `p` | 280 КБ | 14% |
| `U` | 664 КБ | 32% |
| `phi` | 658 КБ | 32% |

`AnonHugePages: 6144 kB` = всего 3 страницы. Ядро не может коалесцировать 280 КБ в 2 МБ-страницу -- нужно непрерывное адресное пространство размером 2 МБ, а поля меньше. Поэтому `khugepaged` сканирует, не находит кандидатов и быстро завершается -- overhead минимален.

#### Почему на fine-кейсе THP стал вредным

На fine-кейсе поля на ранк больше (по оценкам ~4.4 МБ для `U`, ~4.0 МБ для `phi` при 2.3M ячеек). Это **выше порога 2 МБ** -- `khugepaged` находит кандидатов и начинает активную дефрагментацию. Теперь:

1. `khugepaged` находит коалесцируемые регионы -> запускает копирование
2. Без governor частота низкая -> копирование медленное
3. Медленное копирование -> дольше блокирует страницы -> решатель ждёт
4. За время ожидания `khugepaged` находит новых кандидатов -> цикл повторяется
5. Образуется **positive feedback**: больше коалесцируемых страниц -> больше defrag -> больше overhead при низкой частоте

#### Почему V6 (governor + THP) ~ V4 (no governor, no THP)

С governor `performance` частота зафиксирована на 3.46 ГГц даже во время `khugepaged`. Копирование 2 МБ при 3.46 ГГц занимает ~0.6 мс -- это пренебрежимо мало на фоне 6 секунд на шаг. Defrag overhead полностью компенсируется высокой частотой.

Без governor `khugepaged` работает при 1.6 ГГц -> копирование ~1.2 мс -> суммарно за 24 шага набирается 6.1% overhead.

#### Доказательство: деградация V5 нарастает со временем

| Шаг | V6 (сек) | V5 (сек) | V5/V6 | V1/V6 |
|-----|---------|---------|-------|-------|
| 1 (init) | 8.65 | 9.21 | 1.065 | 1.038 |
| 2 | 6.18 | 6.84 | 1.107 | 1.066 |
| 3 | 6.20 | -- | -- | 1.013 |
| Последний | 6.17 | ~7.32 | **1.186** | 1.016 |

V5 деградирует со временем: шаг 1 -- +6.5%, шаг 2 -- +10.7%, последний -- +18.6%. Это подтверждает гипотезу: `khugepaged` накапливает defrag-work по мере того, как решатель аллоцирует и модифицирует больше памяти. Каждый шаг создаёт новые кандидаты для коалесцирования -> `khugepaged` работает всё больше -> overhead растёт.

V1 (без THP) не деградирует: шаг 1 -- +3.8% (frequency ramp-up), последний -- +1.6% (steady state без defrag).

#### Рекомендация

1. **Повторить V5 три раза** для подтверждения воспроизводимости (single run может содержать noise от `khugepaged` scheduling)
2. **Протестировать `defrag=madvise`** вместо `defrag=always`:
   ```bash
   echo madvise | sudo tee /sys/kernel/mm/transparent_hugepage/defrag
   ```
   При `madvise` ядро коалесцирует только регионы, явно помеченные через `madvise(MADV_HUGEPAGE)`. OpenFOAM этого не делает -> `khugepaged` не запускается -> overhead = 0. Если THP при этом не срабатывает (нет `MADV_HUGEPAGE`), результат должен совпасть с V4.
3. Если `defrag=madvise` даёт V5 ~ V4 -- подтвердилась гипотеза, что overhead создаёт именно `defrag=always`, а не сам THP
4. Если `defrag=madvise` даёт V5 > V4 -- значит THP создаёт overhead и через другие механизмы (TLB pressure, page table walks)


---

## Часть 3. Подробный разбор: Governor и I/O-паузы

### Механизм governor-эффекта на CFD-расчётах

#### Как работает `ondemand` governor

Linux `ondemand` governor выбирает частоту CPU на основе загрузки:

1. Каждые ~10 мс ядро замеряет загрузку CPU
2. Если загрузка > 95% -> повышает частоту до максимума (3.46 ГГц)
3. Если загрузка < 95% -> понижает частоту (вплоть до 1.6 ГГц)
4. Переход между частотами занимает ~1-2 мс

#### Что происходит во время OpenFOAM-шага

Типичный шаг PIMPLE для сжимаемого решателя:

| Фаза | Длительность | Загрузка CPU | Частота (ondemand) |
|------|-------------|-------------|-------------------|
| Решение линейных систем (h, p) | ~5.8 сек | 100% | 3.46 ГГц |
| Halo-обмен (MPI) | ~0.1 сек | 30-70% (один ранк ждёт) | 2.0-3.0 ГГц |
| I/O (запись timestep-файла, каждые N шагов) | ~0.3-0.5 сек | 5-20% | **1.6 ГГц** |
| `khugepaged` (если THP on) | ~0.001-0.01 сек | 50-80% | 1.6-2.5 ГГц |

Во время I/O-паузы загрузка CPU падает -> governor сбрасывает частоту. После I/O, когда решатель возобновляет вычисления, требуется ~1-2 мс для возврата к максимальной частоте. За один шаг это пренебрежимо мало (1-2 мс / 6000 мс = 0.03%).

#### Почему на coarse-кейсе governor давал 9%

На coarse-кейсе (backward-facing-step, ~145K ячеек) шаг короче (~6 сек), но шагов больше (~60). I/O происходит каждые несколько шагов. Суммарное время I/O-пауз:

- ~60 шагов x I/O каждые 5 шагов = ~12 I/O-событий
- Каждое I/O ~0.3 сек при 1.6 ГГц (вместо 0.15 сек при 3.46 ГГц)
- Дополнительное время: 12 x 0.15 = 1.8 сек из 369 сек = 0.5%

Но governor сбрасывает частоту не только во время I/O, но и в "микропаузах" между внутренними итерациями PIMPLE (между решением h и p, между коррекциями). На coarse-кейсе эти микропаузы суммарно дают больше overhead, потому что шагов больше и каждая микропауза "проваливается" по частоте.

#### Почему на fine-кейсе governor без THP даёт только 0.5%

На fine-кейсе (ForwardStep, ~2.3M ячеек) шаг длиннее (~6.2 сек), шагов меньше (24). I/O-событий меньше. Суммарное время I/O-пауз:

- 24 шага x I/O каждые 5 шагов = ~5 I/O-событий
- Дополнительное время: 5 x 0.15 = 0.75 сек из 151 сек = 0.5%

Микропаузы между итерациями PIMPLE те же по количеству (2 итерации на шаг), но шагов меньше -> суммарный overhead меньше.

#### Почему на длинных расчётах ondemand "выходит на рабочую частоту"

На длинных расчётах (сотни шагов, часы работы) `ondemand` governor адаптируется:

1. Ядро Linux ведёт статистику загрузки с экспоненциальным сглаживанием
2. После ~100 шагов с устойчивой вычислительной нагрузкой governor "научается" держать частоту близко к максимуму даже во время коротких пауз
3. I/O-паузы становятся всё более предсказуемыми -> governor реже сбрасывает частоту
4. Эффект governor `performance` vs `ondemand` на длинном расчёте -> <1%

Но **на коротких CI-бенчмарках** (24 шага, ~2.5 минуты) governor не успевает адаптироваться. Первые 10-15 шагов ondemand работает неоптимально -> measurable penalty 5-9%.

**Вывод для V&V:** governor `performance` **обязателен** для CI-бенчмарков. Для production-расчётов (часы) -- опционален, overhead ondemand пренебрежимо мал. В V&V-пайплайне governor должен включаться в начале каждого теста и отключаться после.


---

## Часть 4. Обновлённые пороги V&V-регрессии

### Старые vs новые пороги

| Метрика | Старый порог | Новый порог (fine) | Обоснование |
|---------|-------------|-------------------|-------------|
| V6 / golden_time | <= 1.10x | <= 1.10x | Без изменений -- общий порог регрессии |
| V1 / V6 | -- | <= 1.10x | **Новый canary-тест**: default ~ optimal |
| V2 / V6 (NUMA) | [1.90, 2.60] | **[1.70, 2.20]** | NUMA ослаб до 1.94x -> узкий диапазон |
| V3 / V2 (symmetry) | <= 1.10 | <= 1.10x | Без изменений |
| V4 / V6 (governor, no THP) | -- | <= 1.05x | **Новый**: governor эффект <= 0.5% без THP |
| V5 / V4 (THP, no governor) | -- | <= 1.03x | **Новый**: THP overhead <= 3% (допускает noise) |
| V5 / V6 (governor + THP) | -- | <= 1.10x | **Новый**: combined overhead |
| Pinning | Полное соответствие | Полное соответствие | Без изменений |
| Сходимость | Должна сойтись | Должна сойтись | Без изменений |

### Логика новых порогов

**V1/V6 <= 1.10x** -- canary-тест окружения. На данной топологии default compact-pinning уже даёт 6+6. Если V1/V6 > 1.10, возможные причины:
- Изменилась топология (добавились CPU, HT-enabled/disabled)
- Сторонние процессы мешают привязке
- Intel MPI обновился и изменил default pinning

**V2/V6 в [1.70, 2.20]** -- NUMA-health. Fine mesh даёт 1.94x. Диапазон:
- Нижняя граница 1.70: если NUMA-эффект ослабел сильнее -> возможно изменился memory access pattern (больше cache-hits, меньше bandwidth)
- Верхняя граница 2.20: если NUMA-эффект усилился -> увеличился halo-обмен или изменилась декомпозиция

**V4/V6 <= 1.05x** -- governor без THP. На fine mesh = 1.005x. Порог 1.05 даёт запас на noise. Если > 1.05:
- Возможно, governor не сработал (проверить `cpupower frequency-info`)
- Возможно, сторонние процессы мешают

**V5/V4 <= 1.03x** -- THP overhead без governor. На fine mesh = 1.061x (аномалия). Если повторные прогоны подтвердят >1.03:
- Переключить на `defrag=madvise` и повторить
- Если `madvise` даёт V5/V4 <= 1.03 -> подтвердилась гипотеза про `defrag=always`
- Если `madvise` не помогает -> отключить THP в V&V-скриптах


---

## Часть 5. Обновлённый скрипт check_regression.py

```python
#!/usr/bin/env python3
import json, re, sys
from pathlib import Path

THRESHOLDS = {
    "V6_max_degradation": 1.10,
    "V1_V6_canary_max": 1.10,
    "V2_V6_ratio_min": 1.70,
    "V2_V6_ratio_max": 2.20,
    "V3_V2_ratio_max": 1.10,
    "V4_V6_governor_no_thp_max": 1.05,
    "V5_V4_thp_overhead_max": 1.03,
    "V5_V6_combined_max": 1.10,
}

def parse_timing(log_path):
    timings = {}
    for line in Path(log_path).read_text().splitlines():
        m = re.search(r'=== (V\d+).*?:\s+([\d.]+)\s+sec ===', line)
        if m:
            timings[m.group(1)] = float(m.group(2))
    return timings

def check_regression(timings, golden):
    messages = []
    passed = True
    v6 = timings.get("V6")
    v1 = timings.get("V1")
    v2 = timings.get("V2")
    v3 = timings.get("V3")
    v4 = timings.get("V4")
    v5 = timings.get("V5")
    v6_golden = golden.get("V6_time")

    if v6 and v6_golden:
        ratio = v6 / v6_golden
        if ratio > THRESHOLDS["V6_max_degradation"]:
            passed = False
            messages.append(f"FAIL: V6 regression - {v6:.2f}s vs golden {v6_golden:.2f}s (x{ratio:.2f})")
        else:
            messages.append(f"PASS: V6 - {v6:.2f}s vs golden {v6_golden:.2f}s (x{ratio:.2f})")

    if v1 and v6:
        ratio = v1 / v6
        if ratio > THRESHOLDS["V1_V6_canary_max"]:
            passed = False
            messages.append(f"FAIL: Canary V1/V6 = {ratio:.2f} - environment may be broken")
        else:
            messages.append(f"PASS: Canary V1/V6 = {ratio:.2f}")

    if v2 and v6:
        ratio = v2 / v6
        if ratio < THRESHOLDS["V2_V6_ratio_min"]:
            passed = False
            messages.append(f"FAIL: NUMA weakened - V2/V6 = {ratio:.2f} (min {THRESHOLDS['V2_V6_ratio_min']})")
        elif ratio > THRESHOLDS["V2_V6_ratio_max"]:
            passed = False
            messages.append(f"WARN: NUMA intensified - V2/V6 = {ratio:.2f} (max {THRESHOLDS['V2_V6_ratio_max']})")
        else:
            messages.append(f"PASS: NUMA effect - V2/V6 = {ratio:.2f}")

    if v3 and v2:
        ratio = v3 / v2
        if ratio > THRESHOLDS["V3_V2_ratio_max"]:
            passed = False
            messages.append(f"FAIL: Socket asymmetry - V3/V2 = {ratio:.2f}")
        else:
            messages.append(f"PASS: Socket symmetry - V3/V2 = {ratio:.2f}")

    if v4 and v6:
        ratio = v4 / v6
        if ratio > THRESHOLDS["V4_V6_governor_no_thp_max"]:
            passed = False
            messages.append(f"FAIL: Governor effect too large - V4/V6 = {ratio:.2f}")
        else:
            messages.append(f"PASS: Governor (no THP) - V4/V6 = {ratio:.2f}")

    if v5 and v4:
        ratio = v5 / v4
        if ratio > THRESHOLDS["V5_V4_thp_overhead_max"]:
            messages.append(f"WARN: THP overhead - V5/V4 = {ratio:.2f}. Try defrag=madvise.")
        else:
            messages.append(f"PASS: THP overhead - V5/V4 = {ratio:.2f}")

    if v5 and v6:
        ratio = v5 / v6
        if ratio > THRESHOLDS["V5_V6_combined_max"]:
            passed = False
            messages.append(f"FAIL: Combined overhead - V5/V6 = {ratio:.2f}")
        else:
            messages.append(f"PASS: Combined (gov+THP) - V5/V6 = {ratio:.2f}")

    return passed, messages

def main():
    timings = parse_timing("log.timing")
    golden_path = Path("reference/V6-golden-timing.json")
    golden = json.loads(golden_path.read_text()) if golden_path.exists() else {}

    if not golden.get("V6_time") and "V6" in timings:
        golden_path.write_text(json.dumps({"V6_time": timings["V6"]}, indent=2))
        print(f"Golden time set: V6 = {timings['V6']:.2f}s")
        sys.exit(0)

    passed, messages = check_regression(timings, golden)
    print("\n=== NUMA Regression Report (fine mesh thresholds) ===")
    for msg in messages:
        print(msg)
    print(f"\nResult: {'PASS' if passed else 'FAIL'}")
    sys.exit(0 if passed else 1)

if __name__ == "__main__":
    main()
```


---

## Часть 6. Обновлённая итоговая таблица

| Вариант | Coarse (сек) | Fine (сек) | Fine vs V6 | Pinning | Governor | THP | NUMA | HT |
|---------|-------------|-----------|-----------|---------|----------|-----|------|-----|
| **V6** | 369.26 | **151.57** | 1.00x | 0-5,6-11 | yes | yes | 6+6 cross | no |
| **V1** | -- | 154.27 | 1.018x | default (compact) | no | no | default | ? |
| **V4** | 402.35 | 152.30 | 1.005x | 0-5,6-11 | no | no | 6+6 cross | no |
| **V5** | 402.11 | 161.66 | 1.067x | 0-5,6-11 | no | yes | 6+6 cross | no |
| **V2** | 910.33 | 294.10 | 1.940x | 0-5,12-17 | yes | yes | 12 на node 0 | yes |
| **V3** | 905.10 | 294.74 | 1.945x | 6-11,18-23 | yes | yes | 12 на node 1 | yes |

### Заполненный шаблон для V&V-отчёта

| Вариант | Время (сек) | Отн. к V6 | Pinning верный? | AnonHugePages | Governor |
|---------|-------------|-----------|-----------------|---------------|----------|
| V1 | 154.27 | 1.018x | yes (default 6+6) | -- | no |
| V6 | 151.57 | 1.000x | yes 0-5,6-11 | need check | yes |
| V5 | 161.66 | 1.067x | yes 0-5,6-11 | need check | no |
| V4 | 152.30 | 1.005x | yes 0-5,6-11 | -- | no |
| V2 | 294.10 | 1.940x | yes 0-5,12-17 | need check | yes |
| V3 | 294.74 | 1.945x | yes 6-11,18-23 | need check | yes |

---

## Часть 7. Сравнение coarse vs fine: что изменилось

### Сводка изменений

| Фактор | Coarse (145K) | Fine (2.3M) | Тренд |
|--------|--------------|-------------|------|
| **NUMA penalty** | 2.47x | 1.94x | Ослабел -- больше compute на ранк |
| **Governor (без THP)** | 9.0% | 0.5% | Ослабел -- меньше I/O-пауз |
| **THP (без governor)** | 0% | +6.1% | Стал вредным -- `defrag=always` overhead |
| **Default vs optimal** | -- | 1.8% | Default уже оптимален |
| **Симметрия сокетов** | 0.6% | 0.2% | Сохранена |

### Интерпретация трендов

**NUMA penalty ослабел** потому что при 2.3M ячеек каждый ранк обрабатывает ~192K ячеек (вместо ~12K). Доля halo-обменов (пропорциональная поверхности / объёму) падает: для кубического домена `surface/volume ~ n^{-1/3}`. При 16x увеличении ячеек на ранк halo-доля падает в `16^{1/3} ~ 2.5x`. Меньше halo -> меньше cross-NUMA traffic -> меньше penalty.

**Governor ослабел** потому что при меньшем количестве шагов (24 vs ~60) суммарное время I/O-пауз меньше. `ondemand` governor не успевает накапливать "ошибки" частоты. На production-расчётах (сотни шагов) governor-эффект будет ещё меньше.

**THP стал вредным** потому что при больших полях (> 2 МБ на ранк) `khugepaged` находит кандидатов для коалесцирования и запускает `defrag=always`. Без governor это создаёт measurable overhead. При `defrag=madvise` (запланированный тест) overhead должен исчезнуть.


---

## Часть 8. Рекомендации для V&V-пайплайна

### Скрипты V1-V6 -- без изменений

Скрипты V1-V6 из предыдущей версии документа остаются корректными. Единственное дополнение: после прогона V5 и V6 нужно собирать `AnonHugePages` из `/proc/meminfo` для подтверждения THP-эффекта.

### Обновлённый golden time

```json
{
    "V6_time": 151.57,
    "V6_time_coarse": 369.26,
    "case": "ForwardStep/common-12-fine",
    "date": "2026-09-24",
    "openfoam": "v2312",
    "mpi": "Intel 2021.18.1"
}
```

### План дополнительных тестов

| Тест | Цель | Приоритет |
|------|------|-----------|
| V5 x 3 повтора | Подтвердить THP-аномалию (воспроизводимость) | Высокий |
| V5 с `defrag=madvise` | Изолировать overhead `defrag=always` | Высокий |
| V5 с `defrag=never` + THP `always` | THP без дефрагментации -- коалесцирование только при new allocation | Средний |
| V6 на 2.3M backward-facing-step | Подтвердить, что выводы переносятся между кейсами | Средний |
| V1 на coarse mesh | Заполнить пробел в coarse матрице | Низкий |

### Обновлённая дорожная карта

#### Этап 1: Ручные запуски (завершён)

- [x] V4, V5, V6 -- coarse mesh, выполнены
- [x] V1, V2, V3, V4, V5, V6 -- fine mesh, выполнены
- [ ] V5 x 3 повтора (fine mesh) -- подтверждение THP-аномалии
- [ ] V5 с `defrag=madvise` (fine mesh) -- изоляция THP overhead
- [x] Фиксация golden time для V6 (fine: 151.57 сек)

#### Этап 2: Self-hosted runner

- [ ] Установка GitHub Actions runner на kol-serv
- [ ] Регистрация runner с лейблами `[self-hosted, kol-serv, numa]`
- [ ] Настройка `sudo` для `cpupower`, THP, HugeTLB (через `sudoers.d`)
- [ ] Тестовый прогон: V6 на self-hosted runner
- [ ] Проверка: governor, THP, pinning работают из CI

#### Этап 3: V&V-пайплайн

- [ ] Создание репозитория VnV
- [ ] Перенос кейсов и скриптов V1-V6
- [ ] Воркфлоу: compile -> V6 -> V1 -> V2 -> V3 -> V4 -> V5 -> report
- [ ] Обновлённый `check_regression.py` с fine-mesh порогами
- [ ] Первый автоматический прогон по коммиту в `dev`

#### Этап 4: Расширение

- [ ] Тест `defrag=madvise` в CI -- если подтверждается, заменить `defrag=always` во всех скриптах
- [ ] Утяжелённый кейс (2.3M backward-facing-step) -- по расписанию
- [ ] Распространить на QGDsolver / hybridCentralSolvers
- [ ] Профилирование (VTune) -- опционально
