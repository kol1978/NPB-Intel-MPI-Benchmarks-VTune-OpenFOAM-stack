# Пример `Guided iso3dfd OpenMP Offload`
Пример `Guided iso3dfd OpenMP Offload` демонстрирует, как можно:

&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;1. Анализировать производительность приложения при работе на центральном процессоре<br>
&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;2. Добавьте директивы OpenMP * для увеличения параллелизма<br>
&nbsp;&nbsp;&nbsp;&nbsp;3. Загрузите оптимизированный код на графические процессоры Intel<br>

В этом рабочем процессе используются компоненты oneAPI, доступные в Intel® oneAPI Base Toolkit и Intel® HPC Toolkit.

"iso3dfd" - это трехмерный трафарет, предназначенный для имитации волны, распространяющейся в трехмерной изотропной среде. В этом примере показаны некоторые типичные проблемы, с которыми вы можете столкнуться при переносе приложений на устройства OpenMP* (OMP) Offload. В примере также представлены методы решения этих проблем для достижения высокой производительности.

 Чтобы узнать больше об оптимизации приложений OpenMP Offload для графических процессоров Intel®, см. статью «Перенос и оптимизация приложений OpenMP* с помощью инструментов разработчика Intel».


 **Что вы узнаете:**
 1. Анализ производительности процессора <br>
 2. Найдите циклы и функции для загрузки на графический процессор Intel<br>
 3. Проанализируйте загрузку вашего графического процессора
<br>
<br>

## `Структура образца iso3dfd`

Существует пять версий проекта iso3dfd.

- `Реализация только для центрального процессора`: базовая последовательная реализация для центрального процессора.
- `Неоптимизированная разгрузка на графический процессор`: в этой версии демонстрируется использование параллельной конструкции OpenMP offload target с collapse.
- `Оптимизированная разгрузка на графический процессор 1`: в этой версии демонстрируется использование распределенной конструкции OpenMP offload teams, а также параметров `num_teams` и `thread_limit`.
- `GPU Offload Optimized 2`: в этой версии демонстрируется использование конструкции OpenMP offload teams distribute с улучшенным шаблоном доступа к данным.
- `GPU Offload Optimized 3`: в этой версии итерации выполняются по третьему измерению.

 В примере iso3dfd есть один исполняемый файл. Чтобы запустить каждую реализацию, используйте соответствующие команды cmake для перекомпиляции исполняемого файла. Инструкции можно найти в разделе 'Сборка примера iso3dfd`.

<br>
<br>
## `iso3dfd Рабочий процесс разгрузки OpenMP`
 Вы начинаете рабочий процесс с базовой версии примера под названием `Реализация только для центрального процессора`. Эта версия представляет собой последовательную реализацию для центрального процессора. По мере прохождения рабочего процесса пример претерпевает изменения, которые фиксируются в разных версиях.

В этой таблице описаны этапы рабочего процесса iso3dfd. Вы также можете ознакомиться с инструментами oneAPI, используемыми на каждом этапе, и соответствующей реализацией примера iso3dfd.

 |Шаги| Используемая версия примера & nbsp; & nbsp; & nbsp; & nbsp; & nbsp; & nbsp; & nbsp; & nbsp; & nbsp; & nbsp; & nbsp; & nbsp; & nbsp; & nbsp; & nbsp; & nbsp; & nbsp; & nbsp; & nbsp; & nbsp; & nbsp; & nbsp; & nbsp; & nbsp; & nbsp; & nbsp; & nbsp; & nbsp; & nbsp; & nbsp; & nbsp; & nbsp; & nbsp; & nbsp; | Тип анализа | Intel Analysis Tool & nbsp; & nbsp; & nbsp; & nbsp; & nbsp; & nbsp; & nbsp; & nbsp; & nbsp; & nbsp; & nbsp; & nbsp; & nbsp; & nbsp; & nbsp; & nbsp; & nbsp; & nbsp; & nbsp; & nbsp; & nbsp; & nbsp; & nbsp; & nbsp; & nbsp; & nbsp; & nbsp; & nbsp; & nbsp; & nbsp; ; & nbsp; &nbsp; & nbsp; & nbsp; & nbsp; & nbsp; & nbsp; & nbsp; &nbsp; &nbsp; &nbsp;|
 |:------|:------:|:------:|:------:|
 |**Шаг 1:** [Установка переменных среды](https://github.com/clevels/oneAPI-samples/blob/development/DirectProgramming/C%2B%2B/StructuredGrids/iso3dfd_omp_offload/README.md#step-1-set-environment-variables)| --- | --- | ---|
 |**Шаг 2:** [Создание примера iso3dfd](https://github.com/clevels/oneAPI-samples/blob/development/DirectProgramming/C%2B%2B/StructuredGrids/iso3dfd_omp_offload/README.md#step-2-build-the-iso3dfd-sample)| --- | --- | --- |
 |**Шаг 3:** [Определите циклы, которые отнимают больше всего времени](https://github.com/clevels/oneAPI-samples/blob/development/DirectProgramming/C%2B%2B/StructuredGrids/iso3dfd_omp_offload/README.md#step-3-identify-the-most-time-consuming-loops)| `Реализация только для центрального процессора`| Анализ горячих точек | Intel VTune™ Profiler|
 |**Шаг 4:** [Анализ векторизации](https://github.com/clevels/oneAPI-samples/blob/development/DirectProgramming/C%2B%2B/StructuredGrids/iso3dfd_omp_offload/README.md#step-4-analyze-vectorization)| `Реализация только для центрального процессора`| Анализ векторизации|Intel® Advisor|
 |**Шаг 5:** [Определите прибыльный код для разгрузки](https://github.com/clevels/oneAPI-samples/blob/development/DirectProgramming/C%2B%2B/StructuredGrids/iso3dfd_omp_offload/README.md#step-5-identify-profitable-code-to-offload) | `Реализация только для центрального процессора` | Advisor по разгрузке | Intel® Advisor |
 |**Шаг 6:** [Определите, зависит ли работа приложения от центрального или графического процессора](https://github.com/clevels/oneAPI-samples/blob/development/DirectProgramming/C%2B%2B/StructuredGrids/iso3dfd_omp_offload/README.md#step-6-determine-if-the-application-is-cpugpu-bound)| `GPU Offload Unoptimized`| Анализ GPU Offload| Intel VTune™ Profiler|
 |**Шаг 7:** [Анализ разгрузки для оптимизации ядра графического процессора](https://github.com/clevels/oneAPI-samples/blob/development/DirectProgramming/C%2B%2B/StructuredGrids/iso3dfd_omp_offload/README.md#step-7-examine-offload-for-gpu-kernel-optimization)| `Разгрузка графического процессора без оптимизации` | Анализ горячих точек вычислений и мультимедиа на графическом процессоре | Intel VTune™ Profiler|
 |**Шаг 8:** [Повышение производительности приложения](https://github.com/clevels/oneAPI-samples/blob/development/DirectProgramming/C%2B%2B/StructuredGrids/iso3dfd_omp_offload/README.md#step-8-maximize-application-performance) | `Оптимизация разгрузки на GPU 2` | Анализ максимальной производительности GPU | Intel® Advisor |


<br>\
<br>
## `Подход`
 В руководстве по работе с образцом `iso3dfd` широко используются Intel® VTune™ Profiler и Intel® Advisor для профилирования производительности программного обеспечения. В этом руководстве эти инструменты используются для:
 1. Выявления наиболее ресурсоемких циклов и функций в образце.
 2. Проведения моделирования разгрузки для определения наиболее выгодных участков кода, которые можно перенести на устройство Gen11.
 3. Анализа участков кода OpenMP*, которые были перенесены

`iso3dfd` — это конечно-разностное ядро, которое можно использовать для решения трехмерного уравнения акустической изотропной волны. Это уравнение можно использовать в качестве приблизительной модели распространения сейсмической волны. В этом примере ядра реализованы как схема 16-го порядка в пространстве с симметричными коэффициентами и схема 2-го порядка во времени без граничных условий. С помощью OpenMP Offload этот пример можно запустить на графическом процессоре для распространения сейсмической волны, что является ресурсоемкой задачей.

Код ищет доступный графический процессор или другое устройство, подходящее для разгрузки OpenMP. Если совместимое устройство не обнаружено, код завершает работу. По умолчанию в выводе отображается текущая выполняемая реализация (`Реализация только для процессора`, `Неоптимизированная разгрузка на GPU`, `Оптимизированная разгрузка на GPU 1`, `Оптимизированная разгрузка на GPU 2`, `Оптимизированная разгрузка на GPU 3`) вместе с показателями грид-вычислений — количеством операций с плавающей запятой и эффективной пропускной способностью. Чтобы проверить результаты, запустите версию приложения, использующую только OpenMP и центральный процессор, на хост-компьютере и сравните результаты с версией OpenMP Offload.

 В коде также продемонстрированы некоторые распространенные методы оптимизации, которые можно использовать для улучшения кода 3D-трассировки, выполняемого на графическом процессоре.

 **Пример вывода:**
```
Размеры сетки: 256 256 256
Размеры тайлов: 16 8 64
Использование реализации без разгрузки
--Только на процессоре
Использование памяти (Мбайт): 230
--------------------------------------
время: 3,915 секунды
пропускная способность: 428,537 млн точек в секунду
флопс: 26,1407 Гфлопс
байты: 5,14244 Гбайт/с

--------------------------------------

--------------------------------------
```

<br>

## **`Требования`**

| Операционная система | Аппаратное обеспечение | Программное обеспечение
|:--- |:--- |:---
|Ubuntu* 18.04 | Skylake с GEN9 или новее | Компилятор Intel® oneAPI DPC ++ / C ++
|-- |-- | Профилировщик Intel VTune™
|-- | -- | Советник Intel®

<br>\
<br>

<h1 align="center">-----Начать рабочий процесс-----</h1>
<br>
<br>
# `Шаг 1. Установите переменные среды`
 При работе с интерфейсом командной строки (CLI) настройте программные инструменты с помощью переменных среды. Чтобы настраивать среду CLI при каждом открытии нового окна терминала, используйте скрипт setvars. Это гарантирует, что ваш компилятор, библиотеки и инструменты будут готовы к разработке.

> Linux*:
> - Для общесистемных установок: `. /opt/intel/oneapi/setvars.sh`
> - Для частных установок: ` . ~/intel/oneapi/setvars.sh`
> - Для оболочек, не соответствующих стандарту POSIX, таких как csh, используйте следующую команду: `bash -c 'source <install-dir>/setvars.sh ; exec csh'`
>
> Дополнительную информацию о настройке переменных среды см. в разделе [Использование скрипта setvars в Linux*](https://www.intel.com/content/www/us/en/develop/documentation/oneapi-programming-guide/top/oneapi-development-environment-setup/use-the-setvars-script-with-linux-or-macos.html).


> **Примечание**: для анализа графического процессора в системах Linux* включите опцию сбора аппаратных показателей графического процессора. Установите значение параметра dev.i915 perf_stream_paranoidsysctl равным 0 следующим образом. Выполните следующую команду:
>
> `sudo sysctl -w dev.i915.perf_stream_paranoid=0`
>
> Приведенная выше команда вносит временное изменение, которое будет потеряно после перезагрузки. Чтобы внести постоянное изменение, выполните команду:
>
> `sudo echo dev.i915.perf_stream_paranoid=0 > /etc/sysctl.d/60-mdapi.conf`

### Используйте Visual Studio Code* (VS Code) (необязательно)

Вы можете использовать расширения Visual Studio Code* (VS Code) для настройки среды,
создания конфигураций запуска, а также просмотра и загрузки примеров.

Чтобы собрать и запустить пример с помощью VS Code, выполните следующие действия:
 1. Настройте среду oneAPI с помощью расширения **Environment Configurator for Intel Software Developer Tools**.
 2. Загрузите пример с помощью расширения **Code Sample Browser for Intel Software Developer Tools**.
 3. Откройте терминал в VS Code (**Terminal > New Terminal**).
 4. Запустите пример в терминале VS Code, следуя приведенным ниже инструкциям.

Чтобы узнать больше о расширениях и о том, как настроить среду oneAPI, см. Раздел
[Использование кода Visual Studio с пользователем Intel® oneAPI Toolkits Guide](https://www.intel.com/content/www/us/en/develop/documentation/using-vs-code-with-intel-oneapi/top.html).
<br>
<br>

# `Шаг 2: создайте образец iso3dfd`
### В Linux*
1. Перейдите в каталог с примером.
2. Соберите программу.
 ```
 mkdir build
 cd build
 cmake <пример реализации> ..
 ```

 По умолчанию исполняемый файл собирается с опцией `GPU Offload Unoptimized`. При желании вы можете собрать версию только для центрального процессора или ядро с оптимизированными выгружаемыми версиями.

 **Сборка `только для центрального процессора`:**
 ```
 cmake -DNO_OFFLOAD=1 -DVERIFY_RESULTS=0 ..
 make -j
 ```
 **Сборка `GPU Offload Unpotimized`:**
 ```
 cmake -DVERIFY_RESULTS=0 ..
 make -j
 ```
 **Сборка `GPU Offload Optimized 1`:**
 ```
 cmake -DUSE_OPT1=1 -DVERIFY_RESULTS=0 ..
 make -j
 ```
 **Сборка `GPU Offload Optimized 2`:**
 ```
 cmake -DUSE_OPT2=1 -DVERIFY_RESULTS=0 ..
 make -j
 ```
 **Сборка `GPU Offload Optimized 3`:**
 ```
 cmake -DUSE_OPT3=1 -DVERIFY_RESULTS=0 ..
 make -j
 ```
 Если возникнет ошибка, вы получите более подробную информацию, запустив `make` с аргументом `VERBOSE=1`:
 ```
 make VERBOSE=1
 ```
 Запустите программу в Linux:
 ```
 make run
 ```
 Очистите программу. (опционально)
 ```
 make clean
 ```

4. Измените оптимизированную версию (при необходимости)

 Если вы уже скомпилировали код с использованием пути оптимизации, например "*cmake -DUSE_OPT1=1 ..*", используйте "*cmake -DUSE_OPT1=0 ..*", чтобы вернуться к базовой версии.

#### Устранение неполадок

Если вы получили сообщение об ошибке, устраните проблему с помощью **утилиты диагностики для наборов инструментов Intel® oneAPI**. Утилита диагностики позволяет выполнять проверку конфигурации и системы, чтобы найти отсутствующие зависимости, ошибки разрешений и другие проблемы. Дополнительную информацию см. в [Руководстве пользователя утилиты диагностики для наборов инструментов Intel® oneAPI](https://www.intel.com/content/www/us/en/develop/documentation/diagnostic-utility-user-guide/top.html).


### Параметры приложения

Программа поддерживает несколько настраиваемых входных параметров. Общий синтаксис выглядит следующим образом:
```
src/iso3dfd n1 n2 n3 n1_block n2_block n3_block Итерации
```

|Параметр |Описание
|:--- |:---
|n1 n2 n3 |Размеры сетки для трафарета. В примере в качестве значений по умолчанию используются `256 256 256`.
|n1_block n2_block n3_block |Размеры блоков кэша для **процессора** или **размеры тайлов** для OpenMP Offload. В примере используются значения по умолчанию: `16 8 64`.
|Iterations |Количество временных шагов. В примере используется значение по умолчанию: `100`.

Синтаксис по умолчанию: `src/iso3dfd 256 256 256 16 8 64 100`.

# `Шаг 3. Определите циклы, которые отнимают больше всего времени`
Чтобы найти наиболее ресурсоемкие функции в версии `Реализация только для процессора`, используйте VTune™ Profiler для анализа горячих точек.

**Команда сборки:**\
`cmake -DNO_OFFLOAD=1 -DVERIFY_RESULTS=0 ..`\
`make -j`

**Команда:**\
`vtune -collect hotspots -knob sampling-mode=hw --result-dir=./vtune_hotspots -- src/iso3dfd 256 256 256 16 8 64 100`

После завершения сбора данных перейдите на страницу **Сводка**, чтобы просмотреть такие показатели производительности, как:\
-**Прошедшее время** <br>
-**Время работы процессора**<br>
-**Процент использования микроархитектуры**
<br>
<br>

В разделе **Самые ресурсоёмкие функции** самой ресурсоёмкой функцией является **`_Z16Iso3dfdIterarionPfS_S_S_mmmmmm.DIR.OMP.PARALLEL2`****. Эта функция занимает 94,2% общего времени работы процессора. **Гистограмма эффективного использования процессора** показывает идеальное использование всех процессоров.

![Hotspots-Summary](img/261384680-1709f642-f0ac-4e8e-bb00-675ec28a112b.png)

Посмотрите на раздел **Hotspots Insights** в правом верхнем углу окна «Сводка». Показатели **Использование микроархитектуры** и **Векторизация** отмечены как неэффективные. Для каждого отмеченного показателя есть рекомендация выполнить другой тип анализа и дополнительно изучить проблему.

 Показатель «Использование микроархитектуры» составляет 29,9 %. Это оценка того, насколько эффективно код использует текущую микроархитектуру.

Показатель векторизации равен 100 % — все операции с плавающей запятой векторизованы, но это не означает, что они на 100 % эффективны.

![Hotspots-bottom up](img/261385154-a8688154-d7cc-4057-b112-7d5038126b0a.png)

Затем перейдите в окно **Снизу вверх**. В таблице представлена разбивка показателей по всем выявленным проблемным участкам. На временной шкале под таблицей показана визуализация активности потоков на протяжении всего времени выполнения приложения.

 Судя по этим результатам, основные проблемы с производительностью связаны с неэффективным использованием платформы и возможной неэффективностью векторизации. Микроархитектура используется слабо, графический процессор не задействован.

Прежде чем оценивать потенциальную производительность графического процессора, изучите векторизацию в этом коде. Используйте Intel Advisor для высокоуровневого анализа процессора.


# `Шаг 4. Анализ векторизации`
С помощью анализа векторизации в Intel Advisor вы можете изучить потенциальные возможности повышения производительности процессора, прежде чем оценивать потенциальную производительность графического процессора.

В командной строке выполните следующие команды:
**Команда сборки:**\
`cmake -DNO_OFFLOAD=1 -DVERIFY_RESULTS=0 ..`\
`make -j`

**Команда:**\
`advisor --collect=survey --project-dir=./adv_vectorization -- ./src/iso3dfd 256 256 256 16 8 64 100`\
`advisor --collect=tripcounts --flop --stacks --project-dir=./adv_vectorization -- ./src/iso3dfd 256 256 256 16 8 64 100`

В результате вы увидите техническую разбивку общей векторизации приложения. Дополнительная информация о функции **`_Z16Iso3dfdIterarionPfS_S_S_mmmmmm.DIR.OMP.PARALLEL2`**, выявленной в ходе анализа «горячих точек».

 На вкладке «Рекомендации» представлены советы по возможной оптимизации.

![Векторизация-summary-new](img/275656782-bd33b40d-6ca3-491a-8d6b-23b3596ef915.png)

Теперь, когда вы провели высокоуровневое сканирование производительности центрального процессора, изучите потенциальную производительность при переносе приложения iso3dfd на графический процессор.

# `Шаг 5. Определите прибыльный код для переноса`
Используйте Intel Advisor для запуска модуля моделирования переноса. Вы можете увидеть **прогнозируемую** производительность при переносе кода на графический процессор без доступа к оборудованию. Этот модуль также показывает, какой именно код будет наиболее выгодно перенести.

 В этом упражнении проверяется прогнозируемая производительность графики Intel® Iris® Xe 12-го поколения. Обратите внимание, что для запуска Offload Modeling Perspective вам **не** нужен доступ к оборудованию.

**Команда сборки:**\
`cmake -DNO_OFFLOAD=1 -DVERIFY_RESULTS=0 ..`\
`make -j`

**Команда:**\
`advisor --collect=offload --config=gen12_tgl --project-dir=./adv_offload_model -- ./src/iso3dfd 256 256 256 16 8 64 100`

После отображения результатов в разделе **Основные показатели** отображаются показатели, относящиеся к общей прогнозируемой производительности выгруженного кода. Судя по приведенным ниже результатам, если выгрузить оба рекомендуемых фрагмента кода, можно ожидать ускорения выгруженного кода примерно в 1,976 раза, а всего приложения — в 1,929 раза. Выгруженный код составит 96 % от общего кода.

В разделе **Метрики программы** представлено наглядное сравнение исходного времени выполнения (6,22 с) с прогнозируемым временем ускоренного выполнения (3,323 с). В этом разделе также представлена разбивка по дополнительным характеристикам разгрузки.

 В разделе **Ограничения разгрузки** указаны потенциальные ограничивающие факторы для разгруженного кода. Используйте эту информацию для разработки стратегии разгрузки.

![Offload-adv-gen12](img/264219171-97ff1f48-f5e6-4883-aa46-ea70f025e6c5.png)

 В нижней части результата в разделе **Top Offloaded** показаны циклы, которые рекомендуется разгрузить. Вы также можете посмотреть конкретные показатели для каждого цикла.

В области **Top Non-Offloaded** обычно отображаются циклы, которые можно разгрузить, но это не принесет прибыли. В данном примере таких циклов нет.

![Offload-adv-gen12-2](img/275947439-7826f4c4-7b61-4e45-8eb0-340135ecc1bc.png)

 В разделе Offload Modeling Perspective также представлена техническая информация о циклах, рекомендуемых для разгрузки, а также рекомендации по началу работы с разгрузкой на графический процессор. Обратите внимание, что **`_Z16Iso3dfdIterarionPfS_S_S_mmmmmm.DIR.OMP.PARALLEL2`** является лучшим кандидатом для разгрузки. Используйте [модель программирования с разгрузкой OpenMP*](https://www.intel.com/content/www/us/en/docs/oneapi/programming-guide/current/c-c-or-fortran-with-openmp-offload-programming.html), чтобы начать работу с этой разгрузкой.

### Как была реализована первоначальная разгрузка
Для первоначальной разгрузки исходная прагма была изменена таким образом, чтобы обеспечить разгрузку OpenMP на целевое устройство, указанное в опции компилятора. Модифицированная прагма указывает приложению, что цикл должен выполняться на графическом процессоре, а не на центральном процессоре. Дополнительную информацию о целях OpenMP см. в [Руководстве и справочнике для разработчиков компилятора Intel® oneAPI DPC++/C++](https://www.intel.com/content/www/us/en/docs/dpcpp-cpp-compiler/developer-guide-reference/2023-0/fopenmp-targets-qopenmp-targets.html)

 **Прайма перед выгрузкой:**
 ```
 #pragma omp parallel default(shared)
 ```
**Pragma, используемая для разгрузки OpenMP:**
```
#pragma omp target parallel for simd collapse(3)
```

# `Шаг 6. Определите, зависит ли приложение от центрального процессора или графического процессора`
Начните анализировать выгруженную версию образца и определите, зависит ли он от центрального процессора или графического процессора. Создайте образец и запустите анализ разгрузки графического процессора в профилировщике VTune, чтобы начать анализ [`Разгрузки графического процессора Unoptimized`](https://github.com/intel-sandbox/clevels/blob/main/iso3dfd_omp_offload_new/README.md#iso3dfd-sample-structure) версии образца iso3dfd.

** Команда Build:**\
`cmake -DVERIFY_RESULTS=0 ..`\
`make -j`

**Команда:**\
`vtune -collect gpu-offload --result-dir=./vtune_gpu_offload_unopt -- ./src/iso3dfd 256 256 256 16 8 64 100`

После завершения анализа в окне **Сводка** отображается процент общего времени выполнения на графическом процессоре, а также основные задачи, выполнявшиеся на графическом процессоре и на центральном процессоре.

В разделе **Рекомендации** вы видите, что:

- общая загрузка графического процессора ниже оптимальной. <br>
- Задачи на графическом процессоре, скорее всего, также выполняются неоптимально. <br>

<br>
Это подтверждает, что приложение привязано к графическому процессору. Чтобы подробнее изучить эти показатели, перейдите в окно **Графика**.

![BASELINE-GPU-OFFLOAD-SUMMARY](img/275984951-d8f26d73-b2bb-4a9a-a106-38525b547577.png)

В таблице в окне «Графика» представлены подробные показатели по каждой вычислительной задаче на графическом процессоре. Процент зависаний в массиве EU отмечен как проблема с производительностью. Чтобы получить более подробную информацию об этой проблеме с производительностью, вы можете запустить анализ горячих точек вычислений и мультимедиа на графическом процессоре.

 На временной шкале под таблицей показано, как меняются показатели в процессе работы приложения. В верхней части временной шкалы основной поток OpenMP начинает работу на центральном процессоре. Примерно на отметке в 2 секунды этот поток начинает подготовку к разгрузке. В нижней части временной шкалы показано, когда начинается выполнение на графическом процессоре.

![BASELINE-GPU-OFFLOAD-GRAPHICS2](img/276409585-ee89da63-43d9-4737-b5c5-04cd565fb1e7.png)

# `Шаг 7. Анализ разгрузки для оптимизации ядра графического процессора`
 Теперь, когда приложение привязано к графическому процессору, запустите анализ горячих точек вычислений и мультимедиа на графическом процессоре в VTune Profiler, чтобы получить более подробную информацию о ядрах, работающих на графическом процессоре. Выполните следующие команды:

**Команда сборки:**\
`cmake -DVERIFY_RESULTS=0 ..`\
`make -j`

**Команда:**\
`vtune -collect gpu-hotspots --result-dir=./vtune_gpu_hotspots_unopt -- ./src/iso3dfd 256 256 256 16 8 64 100`

После завершения анализа в окне «Сводка» отобразится общая информация о производительности графического процессора.

- Процент зависаний EU Array был отмечен как основная проблема с производительностью.
- Кроме того, разгрузка ограничена пропускной способностью третьего уровня кэша.

![GPU-HOTSPots-Baseline-SUMMARY](img/276416174-e9f03b26-ab38-48f3-9f1f-e91e433675f6.png)

В окне «Графика» диаграмма иерархии памяти иллюстрирует поток данных от центрального процессора к графическому. Используйте эту диаграмму, чтобы выявить потенциальные узкие места. Обратите внимание, что, когда EU простаивал, пропускная способность L3 была высокой.

 Показатели и диаграмма свидетельствуют о том, что каждый раз, когда инструкции требуются данные, она должна обращаться к кэшу L3. Повторное использование данных может уменьшить количество простоев EU и тем самым повысить производительность графического процессора.

![Новая базовая графика](img/276416139-b2cbadf5-0f77-4466-954a-6a28950f03ef.png)

# `Guided iso3dfd OpenMP Offload` Sample

The `Guided iso3dfd OpenMP Offload ` Sample demonstrates how you can:

&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;1. Analyze the performance of an application when running on CPU<br>
&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;2. Add OpenMP* directives to increase parallelism<br>
&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;3.Offload the optimized code onto Intel GPUs<br>

This workflow makes use of oneAPI components available in the Intel® oneAPI Base Toolkit and Intel® HPC Toolkit.

`iso3dfd` is a three-dimensional stencil designed to simulate a wave which propagates in a 3D isotropic medium.  This sample exposes some typical challenges you face when offloading applications onto OpenMP* (OMP) Offload devices. The sample also introduces techniques to resolve these challenges in order to achieve good performance.

To learn more about optimizing OpenMP Offload applications for Intel® GPUs, see Offload and Optimize OpenMP* Applications with Intel Developer Tools.


  **What You Will Learn:**
 1. Analyze CPU Performance <br>
 2. Find loops and functions to offload onto an Intel GPU<br>
 3. Analyze your GPU Offload
<br>
<br>

## `iso3dfd Sample Structure`

There are five versions of the iso3dfd project.

- `CPU Only Implementation`: This is the basic serial CPU implementation.
- `GPU Offload Unoptimized`: This version demonstrates the use of the OpenMP offload target parallel for construct with the collapse.
- `GPU Offload Optimized 1`: This version demonstrates the use of the OpenMP offload teams distribute construct and use of `num_teams` and `thread_limit` clause.
- `GPU Offload Optimized 2`: This vesion demonstrates the use of the OpenMP offload teams distribute construct with an improved pattern for data-access.
- `GPU Offload Optimized 3`: In this version, iterations are over a third dimension.

The iso3dfd sample has a single executable. To run each implementation, use appropriate cmake commands to rebuild the executable. You can find instructions in 'Build The iso3dfd Sample`.

<br>
<br>

## `iso3dfd OpenMP Offload Workflow`
You start the workflow with the basic version of the sample, called `CPU Only Implementation`. This version has serial CPU implementation. As you proceed through the workflow, the sample goes through modifications which are captured in different versions.

This table describes the steps in the iso3dfd workflow. You can also see the oneAPI tools used at each step and the relevant implementation of the iso3dfd sample.

 |Steps| Sample Version Used &nbsp; &nbsp; &nbsp; &nbsp; &nbsp; &nbsp; &nbsp; &nbsp; &nbsp; &nbsp; &nbsp; &nbsp; &nbsp; &nbsp; &nbsp; &nbsp; &nbsp; &nbsp; &nbsp; &nbsp; &nbsp; &nbsp; &nbsp; &nbsp; &nbsp; &nbsp; &nbsp; &nbsp; |Analysis Type|Intel Analysis Tool &nbsp; &nbsp; &nbsp; &nbsp; &nbsp; &nbsp; &nbsp; &nbsp; &nbsp; &nbsp; &nbsp; &nbsp; &nbsp; &nbsp; &nbsp; &nbsp; &nbsp; &nbsp; &nbsp; &nbsp; &nbsp; &nbsp; &nbsp; &nbsp; &nbsp; &nbsp; &nbsp; &nbsp; &nbsp; &nbsp;  &nbsp;|
  |:------|:------:|:------:|:------:|
 |**Step 1:** [Set Environment Variables](https://github.com/clevels/oneAPI-samples/blob/development/DirectProgramming/C%2B%2B/StructuredGrids/iso3dfd_omp_offload/README.md#step-1-set-environment-variables)| --- | --- | ---|
 |**Step 2:** [Build the iso3dfd Sample](https://github.com/clevels/oneAPI-samples/blob/development/DirectProgramming/C%2B%2B/StructuredGrids/iso3dfd_omp_offload/README.md#step-2-build-the-iso3dfd-sample)| --- | --- | --- |
 |**Step 3:** [Identify the Most Time-Consuming Loops](https://github.com/clevels/oneAPI-samples/blob/development/DirectProgramming/C%2B%2B/StructuredGrids/iso3dfd_omp_offload/README.md#step-3-identify-the-most-time-consuming-loops)| `CPU Only Implementation`| Hotspots Analysis | Intel VTune™ Profiler|
 |**Step 4:** [Analyze Vectorization](https://github.com/clevels/oneAPI-samples/blob/development/DirectProgramming/C%2B%2B/StructuredGrids/iso3dfd_omp_offload/README.md#step-4-analyze-vectorization)| `CPU Only Implementation`| Vectorization Analysis|Intel® Advisor|
 |**Step 5:** [Identify Profitable Code to Offload](https://github.com/clevels/oneAPI-samples/blob/development/DirectProgramming/C%2B%2B/StructuredGrids/iso3dfd_omp_offload/README.md#step-5-identify-profitable-code-to-offload) | `CPU Only Implementation` | Offload Advisor |Intel® Advisor |
 |**Step 6:** [Determine if the Application is CPU/GPU Bound](https://github.com/clevels/oneAPI-samples/blob/development/DirectProgramming/C%2B%2B/StructuredGrids/iso3dfd_omp_offload/README.md#step-6-determine-if-the-application-is-cpugpu-bound)| `GPU Offload Unoptimized`| GPU Offload analysis| Intel VTune™ Profiler|
 |**Step 7:** [Examine offload for GPU Kernel Optimization](https://github.com/clevels/oneAPI-samples/blob/development/DirectProgramming/C%2B%2B/StructuredGrids/iso3dfd_omp_offload/README.md#step-7-examine-offload-for-gpu-kernel-optimization)| `GPU Offload Unoptimized` | GPU Compute/Media Hotspots Analysis | Intel VTune™ Profiler|
 |**Step 8:** [Maximize Application Performance](https://github.com/clevels/oneAPI-samples/blob/development/DirectProgramming/C%2B%2B/StructuredGrids/iso3dfd_omp_offload/README.md#step-8-maximize-application-performance) | `GPU Offload Optimized 2`            | GPU Roofline Analysis               | Intel® Advisor                  |


<br>\
<br>
## `Approach`
The `iso3dfd` guided sample workflow makes extensive use of Intel® VTune™ Profiler and Intel® Advisor to profile software performance. The workflow uses these tools to:
     1. Identify the most time-consuming loops and functions in the sample
     2. Conduct offload modeling  to identify profitable code regions to offload onto a Gen11 device.
     3. Analyze the OpenMP* code portions that were offloaded

`iso3dfd` is a finite difference stencil kernel that you can use to solve the 3D acoustic isotropic wave equation. You can use this equation as a proxy for propagating a seismic wave. In this sample, kernels are implemented as 16th order in space, with symmetric coefficients, and 2nd order in time scheme without boundary conditions. Using OpenMP Offload, the sample can explicitly run on the GPU to propagate a seismic wave, which is a compute-intensive task.

The code searches for an available GPU or an alternate device that is suitable for OpenMP Offload. If a compatible device is not detected, the code exits. By default, the output prints the current implementation getting executed (`CPU Only Implementation`, `GPU Offload Unoptimized`, `GPU Offload Optimized 1`, `GPU Offload Optimized 2`, `GPU Offload Optimized 3`) along with the grid computation metrics - flops and effective throughput. To validate results, run an OpenMP/CPU-only version of the applicarion on host machine, and compare results to the OpenMP Offload version.

The code also demonstrates some of the common optimization techniques that you can use to improve 3D-stencil code running on a GPU device.

 **Example Output:**
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

--------------------------------------
```

<br>

## **`Requirements`**

| Operating System        | Hardware                   | Software
|:---                     |:---                        |:---
|Ubuntu* 18.04            |Skylake with GEN9 or newer  |Intel® oneAPI DPC++/C++ Compiler
|--                       |--                          |Intel VTune™ Profiler
|--                       |--                          |Intel® Advisor

<br>\
<br>

<h1 align="center">-----Begin Workflow-----</h1>
<br>
<br>

# `Step 1: Set Environment Variables`
When working with the command-line interface (CLI), configure the software tools using environment variables. To set up your CLI environment every time you open a new terminal window, source the `setvars` script. This practice ensures that your compiler, libraries, and tools are ready for development.

> Linux*:
> - For system wide installations: `. /opt/intel/oneapi/setvars.sh`
> - For private installations: ` . ~/intel/oneapi/setvars.sh`
> - For non-POSIX shells, like csh, use the following command: `bash -c 'source <install-dir>/setvars.sh ; exec csh'`
>
> For more information on configuring environment variables, see [Use the setvars Script with Linux*](https://www.intel.com/content/www/us/en/develop/documentation/oneapi-programming-guide/top/oneapi-development-environment-setup/use-the-setvars-script-with-linux-or-macos.html).


> **Note**: For GPU analysis on Linux* systems, enable the option to collect GPU hardware metrics. Setting the value of the dev.i915 perf_stream_paranoidsysctl option to 0 as follows. Run the following command:
>
> `sudo sysctl -w dev.i915.perf_stream_paranoid=0`
>
>The above command makes a temporary change that is lost after reboot. To make a permanent change,run:
>
> `sudo echo dev.i915.perf_stream_paranoid=0 > /etc/sysctl.d/60-mdapi.conf`

### Use Visual Studio Code* (VS Code) (Optional)

You can use Visual Studio Code* (VS Code) extensions to set your environment,
create launch configurations, and browse and download samples.

Follow these steps to build and run a sample using VS Code:
 1. Configure the oneAPI environment with the extension **Environment Configurator for Intel Software Developer Tools**.
 2. Download a sample using the extension **Code Sample Browser for Intel Software Developer Tools**.
 3. Open a terminal in VS Code (**Terminal > New Terminal**).
 4. Run the sample in the VS Code terminal using the instructions below.

To learn more about the extensions and how to configure the oneAPI environment, see the
[Using Visual Studio Code with Intel® oneAPI Toolkits User Guide](https://www.intel.com/content/www/us/en/develop/documentation/using-vs-code-with-intel-oneapi/top.html).
<br>
<br>

# `Step 2: Build The iso3dfd Sample`
### On Linux*
1. Change to the sample directory.
2. Build the program.
    ```
    mkdir build
    cd build
    cmake <sample implementation> ..
    ```

    By default, the executable is built with `GPU Offload Unoptimized`. Optionally, you can build the strictly CPU version, or build the kernel with optimized offloaded versions.

    **Build `CPU Only Implementation`:**
    ```
    cmake -DNO_OFFLOAD=1 -DVERIFY_RESULTS=0 ..
    make -j
    ```
    **Build `GPU Offload Unpotimized`:**
    ```
    cmake -DVERIFY_RESULTS=0 ..
    make -j
    ```
    **Build `GPU Offload Optimized 1`:**
    ```
    cmake -DUSE_OPT1=1 -DVERIFY_RESULTS=0 ..
    make -j
    ```
    **Build `GPU Offload Optimized 2`:**
    ```
    cmake -DUSE_OPT2=1 -DVERIFY_RESULTS=0 ..
    make -j
    ```
   **Build `GPU Offload Optimized 3`:**
    ```
    cmake -DUSE_OPT3=1 -DVERIFY_RESULTS=0 ..
    make -j
    ```
    If an error occurs, get more details by running `make` with the `VERBOSE=1` argument:
    ```
    make VERBOSE=1
    ```
    Run the program on Linux:
   ```
   make run
   ```
   Clean the program. (Optional)
   ```
   make clean
   ```

4. Change the optimized version (if necessary)

   If you already compiled with the optimization path, for example "*cmake -DUSE_OPT1=1 ..*", use "*cmake -DUSE_OPT1=0 ..*" can go back to the baseline version.

#### Troubleshooting

If you receive an error message, troubleshoot the problem using the **Diagnostics Utility for Intel® oneAPI Toolkits**. The diagnostic utility provides configuration and system checks to help find missing dependencies, permissions errors, and other issues. For more information, see the [Diagnostics Utility for Intel® oneAPI Toolkits User Guide](https://www.intel.com/content/www/us/en/develop/documentation/diagnostic-utility-user-guide/top.html).


### Application Parameters

The program supports several configurable input parameters. The general syntax is as follows:
```
src/iso3dfd n1 n2 n3 n1_block n2_block n3_block Iterations
```

|Parameter                      |Description
|:---                           |:---
|n1 n2 n3                       |Grid sizes for the stencil. The sample uses `256 256 256` as the default values.
|n1_block n2_block n3_block     |Cache block sizes for **CPU** or **tile sizes** for OpenMP Offload. The sample uses as `16 8 64` the default values.
|Iterations                    	|Number of timesteps. The sample uses `100` as the default value.

The default syntax is `src/iso3dfd 256 256 256 16 8 64 100`.

# `Step 3: Identify the Most Time-Consuming Loops`
To find the most time consuming functions in the `CPU Only Implementation` version, use the VTune™ Profiler to run a Hotspots Analysis.

**Build Command:**\
`cmake -DNO_OFFLOAD=1 -DVERIFY_RESULTS=0 ..`\
`make -j`

**Command:**\
`vtune -collect hotspots -knob sampling-mode=hw --result-dir=./vtune_hotspots -- src/iso3dfd 256 256 256 16 8 64 100`

Once the data collection completes, review the **Summary** page to see such performance metrics as:\
-**Elapsed Time** <br>
-**CPU Time**<br>
-**Microarchitecture Usage Percentage**
<br>
<br>

In the **Top Hotspot** section the most time consuming function is **`_Z16Iso3dfdIterarionPfS_S_S_mmmmmm.DIR.OMP.PARALLEL2`**.  This function occupies 94.2% of the total CPU time. The **Effective CPU Utilization Histogram** shows the ideal usage of all the CPUs.

![Hotspots-Summary](img/261384680-1709f642-f0ac-4e8e-bb00-675ec28a112b.png)

Look at the **Hotspots Insights** section in the top right corner of the Summary window. Both the **Microarchitecture Usage** and **Vectorization** have been flagged as poor performing metrics. For each flagged metric, there is a recommendation to run another analysis type and further examine the issue.

The Microarchitecture Usage metric is flagged at 29.9%. This is an estimation of how effectively the code utilizes the current microarchitecture.

The Vectorization metric is flagged at 100%- All of the floating point operations are vectorized, but this value does not neccisarily imply that they operations are 100% efficient.

![Hotspots-bottom up](img/261385154-a8688154-d7cc-4057-b112-7d5038126b0a.png)

Next, switch to the **Bottom-Up** window. The table shows a breakdown of metrics reported for all of the identified hotspots. The timeline below the table shows a visualization of the thread activity throughout the execution time of the application.

Based on these results, the biggest performance problems point to the inefficient use of the platform and possible inefficiencies in the vectorization. Microarchitecture usage is low, and there is no use of a GPU.

Before you evaluate potential performance on the GPU, examine the vectorization in this code. Use Intel Advisor to complete a high-level CPU analysis.


# `Step 4: Analyze Vectorization`
When you run a vectorization analysis with Intel Advisor, yopu can investigate potential CPU performance improvements before you evaluate potential performance on the GPU.

At the command prompt, run these commands:
**Build Command:**\
`cmake -DNO_OFFLOAD=1 -DVERIFY_RESULTS=0 ..`\
`make -j`

**Command:**\
`advisor --collect=survey --project-dir=./adv_vectorization -- ./src/iso3dfd 256 256 256 16 8 64 100`\
`advisor --collect=tripcounts --flop --stacks --project-dir=./adv_vectorization -- ./src/iso3dfd 256 256 256 16 8 64 100`

In the result, you can see a technical breakdown of the overall vectorization of the application. There is more information about the **`_Z16Iso3dfdIterarionPfS_S_S_mmmmmm.DIR.OMP.PARALLEL2`** function identified in the Hotspots Analysis.

The **Recommendations** tab provides guidance for potential optimizations.

![Vectorization-summary-new](img/275656782-bd33b40d-6ca3-491a-8d6b-23b3596ef915.png)

Now that your high-level scan of CPU performance is complete, investigate the potential performance when you offload the iso3dfd application onto a GPU.

# `Step 5: Identify Profitable Code to Offload`
Use Intel Advisor to run the Offload Modeling Perspective. You can see the **projected** performance of the offload on the GPU, without needing access to the hardware. This perspective also shows the exact code that would be most profitable to offload.

 This exercise looks for projected performance on the 12th Generation Intel® Iris® Xe graphics. Note that you do **not** need to access the hardware to run the Offload Modeling Perspective.

**Build Command:**\
`cmake -DNO_OFFLOAD=1 -DVERIFY_RESULTS=0 ..`\
`make -j`

**Command:**\
`advisor --collect=offload --config=gen12_tgl --project-dir=./adv_offload_model -- ./src/iso3dfd 256 256 256 16 8 64 100`

When the results displat, the **Top Metrics** section shows metrics that pertain to the overall projected performance of the offloaded code. Looking at the results below, if you were to offload both of the recommended code regions, you could expect an approximated 1.976x speed-up of the offloaded code and a 1.929x speed-up of the entire application. The offloaded code would be 96% of the overall code.

In the **Program Metrics** section, see a visual comparison of the original execution time (6.22s) with the projected accelerated execution time (3.323s). This section also shows a breakdown of additional characteristics of the offload.

In the **Offload Bounded By** section, see the potential limiting factors of the offloaded code. Use this to form your offload strategy.

![Offload-adv-gen12](img/264219171-97ff1f48-f5e6-4883-aa46-ea70f025e6c5.png)

At the bottom of the result in the **Top Offloaded** section, see the loops that are recommended for offload. You can also  see specific metrics for each loop.

The **Top Non-Offloaded** region typically shows loops that could be offloaded but would not be profitable. In this example, there are no such loops.

![Offload-adv-gen12-2](img/275947439-7826f4c4-7b61-4e45-8eb0-340135ecc1bc.png)

The Offload Modeling Perspective also provides a technical breakdown of the loops recommended for offloading, as well as recommendations to get started with the offload to the GPU. Notice that **`_Z16Iso3dfdIterarionPfS_S_S_mmmmmm.DIR.OMP.PARALLEL2`** is the top candidate for offload. use the [OpenMP* Offload Programming Model](https://www.intel.com/content/www/us/en/docs/oneapi/programming-guide/current/c-c-or-fortran-with-openmp-offload-programming.html) to get started with this offload.

### How the Initial Offload was Done
For the initial offload, the original pragma was changed to do an OpenMP offload to a target device specified in the compiler option. The modified pragma instructs the application to run the loop on the GPU instead of the CPU. For more information on OpenMP Targets see the [Intel® oneAPI DPC++/C++ Compiler Developer Guide and Reference](https://www.intel.com/content/www/us/en/docs/dpcpp-cpp-compiler/developer-guide-reference/2023-0/fopenmp-targets-qopenmp-targets.html)

 **Pragma before Offload:**
 ```
 #pragma omp parallel default(shared)
 ```
**Pragma used for OpenMP Offload:**
```
#pragma omp target parallel for simd collapse(3)
```

# `Step 6: Determine if the Application is CPU/GPU Bound`
Start analyzing the offloaded version of the sample and determine if it is CPU or GPU-bound. Build the sample and run the GPU Offload Analysis in VTune Profiler to begin analyzing the [`GPU Offload Unoptimized`](https://github.com/intel-sandbox/clevels/blob/main/iso3dfd_omp_offload_new/README.md#iso3dfd-sample-structure) version of the iso3dfd sample.

**Build Command:**\
`cmake -DVERIFY_RESULTS=0 ..`\
`make -j`

**Command:**\
`vtune -collect gpu-offload  --result-dir=./vtune_gpu_offload_unopt -- ./src/iso3dfd 256 256 256 16 8 64 100`

When the analysis finishes, the **Summary** window displays the percentage of the overall execution time on the GPU as well as top tasks that were executed on the GPU vs the CPU.

In the **Recommendations** section you see that:

-The overall utilization of the GPU is lower than optimal. <br>
-The tasks on the GPU are likely performing suboptimal as well.<br>

<br>
This confirms that the application is GPU-bound. To further examine these metrics, switch to the **Graphics** window.

![BASELINE-GPU-OFFLOAD-SUMMARY](img/275984951-d8f26d73-b2bb-4a9a-a106-38525b547577.png)

The table in the Graphics window shows detailed metrics about each GPU computing task. The percentage of stalls in the EU array have been flagged as a performance issue. To get more details about this performance issue, you can run the GPU Compute/Media Hotspots Analysis.

The timeline below the table shows how the metrics change through the runtime of the application. At the top of the timeline, the `OpenMP Primary Thread` starts by executing on the CPU. Around the 2 second mark, this thread begins prepare the offload process. The lower portion of the timeline shows when execution starts on the GPU.

![BASELINE-GPU-OFFLOAD-GRAPHICS2](img/276409585-ee89da63-43d9-4737-b5c5-04cd565fb1e7.png)

# `Step 7: Examine offload for GPU Kernel Optimization`
 Now that the application is GPU-bound, run the GPU Compute/Media Hotspots Analysis in VTune Profiler to do a deeper dive focused on the kernels running on the GPU. Run these commands:

**Build Command:**\
`cmake -DVERIFY_RESULTS=0 ..`\
`make -j`

**Command:**\
`vtune -collect gpu-hotspots --result-dir=./vtune_gpu_hotspots_unopt -- ./src/iso3dfd 256 256 256 16 8 64 100`

When the analysis finishes, the Summary window displays a comprehensive view of GPU performance.

-The percentage of EU Array stalls has been flagged as a top performance issue.
-Additionally, the offload is L3 Bandwidth Bound.

![GPU-HOTSPots-Baseline-SUMMARY](img/276416174-e9f03b26-ab38-48f3-9f1f-e91e433675f6.png)

In the Graphics window, the Memory Hierarchy Diagram illustrates the flow of data from the CPU to the GPU. Use this diagram to identify potential bottlenecks. Notice that when the EU was stalled, the L3 Bandwidth was high.

The metrics and diagram inform that any time an instruction needs data, the instruction has to go to L3 cache. Using data reuse could possibly decrease the number of stalls on the EU and thereby increase GPU performance.

![NEW Graphics Baseline](img/276416139-b2cbadf5-0f77-4466-954a-6a28950f03ef.png)


# `Step 8: Maximize Application Performance`
Build the sample to analyze GPU Optimized 2. Then use Intel Advisor to run a GPU Roofline Analysis. Use this analysis to visualize the performance of the kernels against the maximum achievable performance of the system.

In this optimized version we can see that the offload is L3 Bandwidth Bound. Intel Advisor recommends that you optimize the memory access patterns.

**Build Command:**\
`cmake -DUSE_OPT2=1 -DVERIFY_RESULTS=0 ..`\
`make -j`

**Command:**\
`advisor --collect=roofline --profile-gpu --search-dir src:r=src --project-dir=./adv_gpu_roofline_opt2 -- ./src/iso3dfd 256 256 256 16 8 64 100`

![opt2-roofline](img/274476155-0f02f76f-16d4-4ced-aa4d-2b74669d817b.png)

## Output
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


Grid Sizes: 256 256 256
Tile sizes ignored for OMP Offload
--Using Baseline version with omp target with collapse
Memory Usage (MBytes): 230
--------------------------------------
time         : 4.132 secs
throughput   : 406.031 Mpts/s
flops        : 24.7679 GFlops
bytes        : 4.87238 GBytes/s

--------------------------------------


Grid Sizes: 256 256 256
Tile sizes: 16 8 64
Using Optimized target code - version 1:
--OMP_Offload with Tiling
Memory Usage (MBytes): 230
--------------------------------------
time         : 4.14 secs
throughput   : 405.247 Mpts/s
flops        : 24.7201 GFlops
bytes        : 4.86296 GBytes/s

--------------------------------------


Grid Sizes: 256 256 256
Tile sizes: 16 8 64
Using Optimized target code - version 2:
--OMP_Offload with Tiling and Z Window
Memory Usage (MBytes): 230
--------------------------------------
time         : 2.806 secs
throughput   : 597.905 Mpts/s
flops        : 36.4722 GFlops
bytes        : 7.17486 GBytes/s

--------------------------------------


Grid Sizes: 256 256 256
Tile sizes: 16 8 64
Using Optimized target code - version 3:
--OMP Threads + OMP_Offload with Tiling and Z Window
Memory Usage (MBytes): 230
--------------------------------------
time         : 2.846 secs
throughput   : 589.502 Mpts/s
flops        : 35.9596 GFlops
bytes        : 7.07402 GBytes/s

--------------------------------------
```

## License

Code samples are licensed under the MIT license. See [License.txt](License.txt) for details.

Third-party program Licenses can be found here: [third-party-programs.txt](third-party-programs.txt)

# `Шаг 8. Повышение производительности приложения`
Соберите пример для анализа GPU Optimized 2. Затем с помощью Intel Advisor выполните анализ максимальной производительности графического процессора. С помощью этого анализа можно визуализировать производительность ядер в сравнении с максимально достижимой производительностью системы.

 В этой оптимизированной версии мы видим, что разгрузка ограничена пропускной способностью третьего уровня. Intel Advisor рекомендует оптимизировать шаблоны доступа к памяти.

 **Команда сборки:**\
`cmake -DUSE_OPT2=1 -DVERIFY_RESULTS=0 ..`\
`make -j`

**Команда:**\
`advisor --collect=roofline --profile-gpu --search-dir src:r=src --project-dir=./adv_gpu_roofline_opt2 -- ./src/iso3dfd 256 256 256 16 8 64 100`

![opt2-roofline](img/274476155-0f02f76f-16d4-4ced-aa4d-2b74669d817b.png)

## Результат
```
Размеры сетки: 256 256 256
Размеры тайлов: 16 8 64
Используется реализация без разгрузки
--Только на процессоре
Использование памяти (Мбайт): 230
--------------------------------------
время: 3,915 секунды
пропускная способность: 428,537 Mpts / s
флопы: 26.1407 ГФлопс
байты: 5.14244 Гбайт / с

--------------------------------------


Размеры сетки: 256 256 256
Размеры листов игнорируются при выгрузке OMP
--Использование базовой версии с целью omp с сворачиванием
Использование памяти (Мбайт): 230
--------------------------------------
время: 4.132 секунды
пропускная способность: 406.031 Mpts / s
flops: 24.7679 GFlops
байты: 4.87238 GBytes / s

--------------------------------------


Размеры сетки: 256 256 256
Размеры плиток: 16 8 64
Использование оптимизированного целевого кода - версия 1:
--OMP_Offload с разделением на листы
Использование памяти (Мбайт): 230
--------------------------------------
время: 4,14 секунды
пропускная способность: 405,247 Мбит / с
скорость передачи данных: 24.7201 ГФлопс
байт: 4.86296 Гбайт / с

--------------------------------------


Размеры сетки: 256 256 256
Размеры тайлов: 16 8 64
Использование оптимизированного целевого кода — версия 2:
--OMP_Offload с тайлингом и Z-окном
Использование памяти (Мбайт): 230
--------------------------------------
время: 2,806 секунды
пропускная способность: 597,905 млн точек в секунду
флопс: 36,4722 Гфлопс
байты: 7,17486 Гбайт/с

--------------------------------------


Размеры сетки: 256 256 256
Размеры тайлов: 16 8 64
Использование оптимизированного целевого кода — версия 3:
--OMP Threads + OMP_Offload с тайлингом и Z-окном
Использование памяти (Мбайт): 230
--------------------------------------
время: 2,846 секунды
пропускная способность: 589,502 млн точек в секунду
FLOPS: 35,9596 гигафлопс
Байты: 7,07402 гигабайта/с

--------------------------------------
```

## Лицензия

Примеры кода распространяются по лицензии MIT. Подробнее см. [License.txt](License.txt).

Лицензии на сторонние программы можно найти здесь: [third-party-programs.txt](third-party-programs.txt)
