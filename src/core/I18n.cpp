#include "core/I18n.h"

#include <windows.h>

#include <atomic>
#include <string_view>
#include <unordered_map>

namespace infclick {

namespace {

std::atomic<Lang> g_lang{Lang::En};

// English source string -> Russian. Placeholders (%s, %u, %llu, %.1f...) must
// appear in the same order in both columns.
const std::pair<const char*, const char*> kRu[] = {
    // ---- app/App.cpp
    {"ERROR: keyboard hook could not be installed",
     "ОШИБКА: не удалось установить хук клавиатуры"},
    {"Could not save settings: ",
     "Не удалось сохранить настройки: "},
    {"Binding cancelled (timeout)",
     "Назначение отменено (время вышло)"},
    {"EMERGENCY STOP - output halted, held input released, disarmed",
     "АВАРИЙНАЯ ОСТАНОВКА — вывод остановлен, клавиши отпущены, охрана снята"},
    {"Trigger: ",
     "Триггер: "},
    {"Action: ",
     "Действие: "},
    {"Left Mouse alone cannot be the emergency stop",
     "Одна левая кнопка мыши не может быть аварийной остановкой"},
    {"Emergency stop: ",
     "Аварийная остановка: "},

    // ---- input/KeyChord.cpp
    {"Left Mouse",
     "Левая кнопка мыши"},
    {"Right Mouse",
     "Правая кнопка мыши"},
    {"Middle Mouse",
     "Средняя кнопка мыши"},
    {"Mouse Button 4",
     "Кнопка мыши 4"},
    {"Mouse Button 5",
     "Кнопка мыши 5"},
    {"Wheel Up",
     "Колесо вверх"},
    {"Wheel Down",
     "Колесо вниз"},
    {"Wheel Left",
     "Колесо влево"},
    {"Wheel Right",
     "Колесо вправо"},
    {"(none)",
     "(нет)"},

    // ---- lab/AutoTest.cpp
    {"INFINITY CLICKER AUTOTEST RUNNING - please don't touch mouse/keyboard.  Ctrl+Shift+F12 = abort",
     "ИДЁТ АВТОТЕСТ INFINITY CLICKER — не трогайте мышь и клавиатуру.  Ctrl+Shift+F12 — прервать"},

    // ---- lab/Bench.cpp
    {"BENCHMARK RUNNING - please don't touch mouse/keyboard.   Ctrl+Shift+F12 = abort",
     "ИДЁТ БЕНЧМАРК — не трогайте мышь и клавиатуру.   Ctrl+Shift+F12 — прервать"},

    // ---- lab/TestPad.cpp
    {"Infinity Clicker Test Pad - click target",
     "Infinity Clicker — тест-панель (мишень для кликов)"},
    {"INFINITY CLICKER  -  INPUT LAB TEST PAD",
     "INFINITY CLICKER  —  ТЕСТ-ПАНЕЛЬ ЛАБОРАТОРИИ"},
    {"synthetic events received by this window (GetMessageExtraInfo = Infinity Clicker tag)",
     "синтетических событий получило это окно (GetMessageExtraInfo = тег Infinity Clicker)"},
    {"L %llu/%llu   R %llu/%llu   M %llu/%llu   X1 %llu/%llu   X2 %llu/%llu   (down/up)",
     "Л %llu/%llu   П %llu/%llu   С %llu/%llu   X1 %llu/%llu   X2 %llu/%llu   (нажато/отпущено)"},
    {"Wheel up %llu  down %llu  left %llu  right %llu     Keys down %llu  up %llu",
     "Колесо вверх %llu  вниз %llu  влево %llu  вправо %llu     Клавиши: нажато %llu  отпущено "
     "%llu"},
    {"Your own (non-Infinity Clicker) input here: %llu",
     "Ваш собственный ввод (не Infinity Clicker): %llu"},

    // ---- profiles/Profile.cpp
    {"Default",
     "Стандартный"},
    {"Browser",
     "Браузер"},
    {"Testing",
     "Тест"},
    {"Fast",
     "Быстрый"},
    {"Custom",
     "Свой"},

    // ---- ui/AdvancedView.cpp
    {"TIMING",
     "ТАЙМИНГ"},
    {"ECO",
     "ЭКО"},
    {"STANDARD",
     "СТАНДАРТ"},
    {"ULTRA",
     "УЛЬТРА"},
    {"Timer only. ~0.3-1 ms jitter, ~0% CPU.",
     "Только таймер. Джиттер ~0.3–1 мс, нагрузка ~0%."},
    {"Timer + short QPC spin (<= 10% of the interval). Exact up to ~100 CPS, low CPU.",
     "Таймер + короткий QPC-спин (≤ 10% интервала). Точно до ~100 CPS, низкая нагрузка."},
    {"Timer + spin to the deadline. Microsecond timing at any rate - HIGH CPU above ~250 CPS.",
     "Таймер + спин до дедлайна. Микросекундная точность на любой частоте — ВЫСОКАЯ нагрузка выше "
     "~250 CPS."},
    {"Precision",
     "Точность"},
    {"ms",
     "мс"},
    {"us",
     "мкс"},
    {"Speed unit",
     "Единица скорости"},
    {"Unit of the speed value on the main screen.",
     "В чём показывается скорость на главном экране."},
    {"Down time",
     "Удержание"},
    {"Down time >= interval: it will be clamped to 50% of the interval.",
     "Удержание ≥ интервала: будет уменьшено до 50% интервала."},
    {"How long the button stays down (us). Games that read the button once per frame need 10 ms "
     "or more.",
     "Сколько кнопка остаётся нажатой (мкс). Играм, которые проверяют кнопку раз в кадр, нужно 10 "
     "мс и больше."},
    {"%u ms",
     "%u мс"},
    {"%u us",
     "%u мкс"},
    {"Presses per action",
     "Нажатий за действие"},
    {"x2 = double click, x3 = triple click.",
     "x2 — двойной клик, x3 — тройной."},
    {"Gap between presses (us)",
     "Пауза между нажатиями (мкс)"},
    {"SCHEDULER AND INPUT",
     "ПЛАНИРОВЩИК И ВВОД"},
    {"Normal",
     "Обычный"},
    {"Above normal",
     "Выше обычного"},
    {"Highest",
     "Наивысший"},
    {"Time critical",
     "Критический"},
    {"MMCSS (Games)",
     "MMCSS (Игры)"},
    {"Scheduler thread priority",
     "Приоритет потока планировщика"},
    {"Priority inside the NORMAL process class (Infinity Clicker never uses REALTIME_PRIORITY_CLASS). "
     "Matters mostly when the CPU is fully loaded - see BENCHMARK.md.",
     "Приоритет внутри класса процесса NORMAL (REALTIME_PRIORITY_CLASS Infinity Clicker не использует). "
     "Важен в основном при полной загрузке процессора — см. BENCHMARK.md."},
    {"Catch up (batch due actions)",
     "Догонять (пакетом)"},
    {"Strict (skip missed slots)",
     "Строго (пропускать)"},
    {"Late policy",
     "При опоздании"},
    {"The timeline is absolute (slot k = start + k * interval), so lateness never accumulates "
     "into drift. Catch up: slots that are already due go out in one SendInput call (max 8). "
     "Strict: overdue slots are skipped and counted as missed.",
     "Шкала времени абсолютная (слот k = старт + k × интервал), поэтому опоздания не "
     "накапливаются в дрейф. Догонять: слоты, которые уже наступили, уходят одним вызовом "
     "SendInput (до 8). Строго: просроченные слоты пропускаются и считаются пропущенными."},
    {"Scan code (games, DirectInput/raw input)",
     "Скан-код (игры, DirectInput/raw input)"},
    {"Virtual key",
     "Виртуальный код (VK)"},
    {"Keyboard injection",
     "Ввод клавиатуры"},
    {"TRIGGER",
     "ТРИГГЕР"},
    {"Low-level hook (WH_MOUSE_LL)",
     "Low-level хук (WH_MOUSE_LL)"},
    {"Raw Input (no hook)",
     "Raw Input (без хука)"},
    {"Mouse trigger backend",
     "Источник мышиного триггера"},
    {"Hook: can block the trigger and tells physical from injected input exactly, but every mouse "
     "event in the system passes through it and SendInput gets slower (measured). Raw Input: "
     "cheaper, cannot block the trigger and cannot flag input injected by other programs. "
     "Infinity Clicker's own events are always recognised by their tag.",
     "Хук: умеет блокировать триггер и точно отличает физический ввод от эмулированного, но через "
     "него проходит каждое событие мыши в системе, и SendInput становится медленнее (измерено). "
     "Raw Input: дешевле, не умеет блокировать триггер и не распознаёт ввод, эмулированный "
     "другими программами. Собственные события Infinity Clicker распознаются всегда — по тегу."},
    {"Block the trigger from other applications",
     "Не пускать триггер в другие программы"},
    {"The trigger key/button is swallowed, e.g. Mouse Button 4 stops navigating 'Back' in a "
     "browser while armed. Uses a low-level hook.",
     "Клавиша или кнопка триггера перехватывается: например, кнопка мыши 4 перестаёт листать "
     "«Назад» в браузере, пока Infinity Clicker включён. Использует low-level хук."},
    {"Accept input injected by other programs",
     "Принимать ввод, эмулированный другими программами"},
    {"Treat keys sent by macro tools or remote desktop as real presses. Infinity Clicker's own clicks "
     "never count.",
     "Считать клавиши от макро-программ и удалённого рабочего стола настоящими нажатиями. "
     "Собственные клики Infinity Clicker никогда не считаются."},
    {"MODE VALUES",
     "ЗНАЧЕНИЯ РЕЖИМОВ"},
    {"Burst size",
     "Размер серии"},
    {"Actions per press in Burst mode.",
     "Сколько действий за одно нажатие в режиме «Серия»."},
    {"Fixed count",
     "Кол-во"},
    {"Actions per run in Fixed count mode.",
     "Сколько действий за запуск в режиме «Кол-во»."},
    {"Duration",
     "Время"},
    {"Length of a run in Duration mode (ms).",
     "Длительность запуска в режиме «Время» (мс)."},
    {"TARGET APPLICATION",
     "ПРИЛОЖЕНИЕ"},
    {"Only click while an application is in front",
     "Кликать, только когда приложение впереди"},
    {"Output pauses automatically whenever another window is in front, and resumes when the "
     "target returns. Several names can be separated with ';' (e.g. javaw.exe; java.exe).",
     "Вывод автоматически приостанавливается, когда впереди другое окно, и продолжается, когда "
     "целевое окно возвращается. Несколько имён — через «;» (например: javaw.exe; java.exe)."},
    {"process name, e.g. javaw.exe",
     "имя процесса, например javaw.exe"},
    {"Pick",
     "Выбрать"},
    {"[admin]",
     "[админ]"},
    {"No windows found",
     "Окна не найдены"},
    {"Foreground now: %s",
     "Сейчас впереди: %s"},
    {"SAFETY",
     "БЕЗОПАСНОСТЬ"},
    {"Emergency stop",
     "Аварийная остановка"},
    {"Stops output and disarms regardless of mode, profile or window; releases every held "
     "key/button.",
     "Останавливает вывод и выключает Infinity Clicker независимо от режима, профиля и окна; отпускает все "
     "удерживаемые клавиши и кнопки."},
    {"Crash guardian process",
     "Процесс-сторож"},
    {"A tiny helper process that releases every held key and button if Infinity Clicker is killed or "
     "crashes. Takes effect on the next start.",
     "Крошечный вспомогательный процесс: если Infinity Clicker убит или упал, он отпустит все зажатые "
     "клавиши и кнопки. Действует со следующего запуска."},
    {"EXPERIMENTAL",
     "ЭКСПЕРИМЕНТАЛЬНО"},
    {"MAX speed",
     "Скорость MAX"},
    {"No target rate: actions are sent as fast as SendInput accepts them. High CPU usage (one "
     "core). The real throughput is shown as Actual on the main screen.",
     "Без целевой частоты: действия отправляются так быстро, как их принимает SendInput. Высокая "
     "нагрузка на процессор (одно ядро). Реальная скорость показывается на главном экране как "
     "«Фактически»."},
    {"Actions per SendInput call",
     "Действий за вызов SendInput"},
    {"Backpressure",
     "Backpressure"},
    {"Limits in-flight events (accepted by SendInput but not yet seen by Infinity Clicker's hook), so "
     "physical input, including the emergency key, never waits behind a pile of synthetic events.",
     "Ограничивает события «в полёте» (принятые SendInput, но ещё не увиденные хуком Infinity Clicker), "
     "чтобы физический ввод, включая аварийную клавишу, не ждал в очереди за грудой синтетических "
     "событий."},
    {"Metric",
     "Метрика"},
    {"Value",
     "Значение"},
    {"Target CPS",
     "Целевой CPS"},
    {"Actual CPS - run average",
     "Фактический CPS — среднее за запуск"},
    {"Actual CPS - last 1 s / 5 s",
     "Фактический CPS — за 1 с / 5 с"},
    {"Generated actions (run / total)",
     "Сгенерировано действий (запуск / всего)"},
    {"SendInput calls / s",
     "Вызовов SendInput в секунду"},
    {"Events accepted / s",
     "Принято событий в секунду"},
    {"Events requested (run)",
     "Запрошено событий (запуск)"},
    {"Events accepted by SendInput (run)",
     "Принято SendInput (запуск)"},
    {"Failed SendInput calls (run)",
     "Неудачных вызовов SendInput (запуск)"},
    {"  (last error %u)",
     "  (последняя ошибка %u)"},
    {"Missed deadlines (run)",
     "Пропущено дедлайнов (запуск)"},
    {"SendInput call time p50 / p99 / max",
     "Время вызова SendInput p50 / p99 / макс"},
    {"Share of time blocked in SendInput",
     "Доля времени внутри SendInput"},
    {"Interval min / avg / max",
     "Интервал мин / сред / макс"},
    {"Jitter (interval std-dev)",
     "Джиттер (СКО интервала)"},
    {"%s  (last s: %s)",
     "%s  (1 с: %s)"},
    {"Mean interval error vs target",
     "Ошибка среднего интервала от цели"},
    {"Lateness p50 / p99 / p99.9",
     "Опоздание p50 / p99 / p99.9"},
    {"Lateness max, >100 us late",
     "Опоздание макс., >100 мкс"},
    {"Spin / sleep share of time",
     "Доля спина / сна"},
    {"Wake-up margin (current)",
     "Запас пробуждения (текущий)"},
    {"Timer oversleep p50 / p99",
     "Пересып таймера p50 / p99"},
    {"  (no HR timer!)",
     "  (нет HR-таймера!)"},
    {"Trigger -> first action (last/avg/max)",
     "Триггер → первое действие (посл./сред./макс.)"},
    {"CPU: process (% of machine / cores)",
     "CPU: процесс (% машины / ядер)"},
    {"CPU: scheduler thread (cores)",
     "CPU: поток планировщика (ядер)"},
    {"Hooks: keyboard / mouse / raw input",
     "Хуки: клавиатура / мышь / raw input"},
    {"on",
     "вкл"},
    {"OFF",
     "ВЫКЛ"},
    {"off",
     "выкл"},
    {"Emergency hotkey backup (RegisterHotKey)",
     "Резервная аварийка (RegisterHotKey)"},
    {"registered",
     "зарегистрирована"},
    {"not registered",
     "не зарегистрирована"},
    {"Reset totals",
     "Сбросить итоги"},
    {"View log",
     "Показать лог"},
    {"The Input Lab measures the whole pipeline without any game: Infinity Clicker's own clicks go into a "
     "separate Test Pad window (own thread) that counts what an application actually receives.",
     "Лаборатория измеряет весь путь ввода без всякой игры: клики Infinity Clicker уходят в отдельное "
     "окно-мишень (тест-панель в своём потоке), которое считает, что приложение реально получило."},
    {"Close Test Pad",
     "Закрыть тест-панель"},
    {"Open Test Pad",
     "Открыть тест-панель"},
    {"Observe via mouse hook",
     "Наблюдать через хук мыши"},
    {"Installs WH_MOUSE_LL so stage 3 (events delivered through the system input chain) can be "
     "counted. The hook itself costs throughput - that is part of what you measure.",
     "Ставит WH_MOUSE_LL, чтобы посчитать этап 3 (события, прошедшие через системную цепочку "
     "ввода). Хук сам снижает пропускную способность — это тоже часть измерения."},
    {"Reset counters",
     "Сбросить счётчики"},
    {"1. REQUESTED",
     "1. ЗАПРОШЕНО"},
    {"events the scheduler asked for",
     "событий запросил планировщик"},
    {"2. ACCEPTED",
     "2. ПРИНЯТО"},
    {"SendInput return value",
     "возврат SendInput"},
    {"  (%.0f/s)",
     "  (%.0f/с)"},
    {"3. OBSERVED",
     "3. ПРОШЛО"},
    {"seen by our LL hook",
     "увидел наш LL-хук"},
    {"enable hook to measure",
     "включите хук для замера"},
    {"4. RECEIVED",
     "4. ПОЛУЧЕНО"},
    {"messages the Test Pad got",
     "сообщений получила тест-панель"},
    {"Accepted but not received by the pad: %llu (lost or still queued)",
     "Принято, но не получено панелью: %llu (потеряно или ещё в очереди)"},
    {"RUN A TEST WITH THE CURRENT PROFILE INTO THE TEST PAD",
     "ТЕСТОВЫЙ ЗАПУСК ТЕКУЩЕГО ПРОФИЛЯ В ТЕСТ-ПАНЕЛЬ"},
    {"Output only flows while the Test Pad is in front AND the cursor is over it.",
     "Вывод идёт, только пока тест-панель впереди И курсор находится над ней."},
    {"Run 1 s",
     "Запуск на 1 с"},
    {"Run 5 s",
     "Запуск на 5 с"},
    {"Run 1000 actions",
     "1000 действий"},
    {"Stop",
     "Стоп"},
    {"running...",
     "идёт..."},
    {"FULL BENCHMARK SUITE",
     "ПОЛНЫЙ БЕНЧМАРК"},
    {"Runs `InfinityClicker.exe --bench quick` in a separate process: timers, spin threshold, SendInput "
     "throughput, CPS matrix, priorities, hold-time visibility, trigger latency, stress. Covers "
     "the screen with a full-screen pad for ~3 minutes. Results: benchmarks\\latest.md",
     "Запускает `InfinityClicker.exe --bench quick` отдельным процессом: таймеры, порог спина, пропускная "
     "способность SendInput, матрица CPS, приоритеты, видимость коротких кликов, задержка "
     "триггера, стресс. Примерно на 3 минуты закрывает экран полноэкранной панелью. Результаты: "
     "benchmarks\\latest.md"},
    {"Benchmark running...",
     "Бенчмарк идёт..."},
    {"Run benchmark (quick)",
     "Запустить бенчмарк (быстрый)"},
    {"Open results",
     "Открыть результаты"},
    {"Advanced",
     "Дополнительно"},
    {"Engine",
     "Движок"},
    {"Diagnostics",
     "Диагностика"},
    {"Input Lab",
     "Лаборатория"},

    // ---- ui/MainWindow.cpp
    {"ACTIVE %.1f CPS",
     "АКТИВЕН %.1f CPS"},
    {"PAUSED",
     "ПАУЗА"},
    {"READY",
     "ГОТОВ"},
    {"STOPPED",
     "ОСТАНОВЛЕН"},
    {"Profile: ",
     "Профиль: "},
    {"Direct3D 11 initialisation failed.",
     "Не удалось инициализировать Direct3D 11."},

    // ---- ui/SettingsView.cpp
    {"Settings",
     "Настройки"},
    {"General",
     "Общие"},
    {"Hotkeys",
     "Горячие клавиши"},
    {"Behavior",
     "Поведение"},
    {"Appearance",
     "Вид"},
    {"Auto (system)",
     "Авто (как в Windows)"},
    {"Language",
     "Язык"},
    {"Auto follows the Windows display language.",
     "Авто — язык интерфейса Windows."},
    {"Start with Windows",
     "Запускать вместе с Windows"},
    {"Starts Infinity Clicker minimized to the tray when you sign in.",
     "При входе в систему Infinity Clicker стартует свёрнутым в трей."},
    {"Could not change the startup entry",
     "Не удалось изменить автозапуск"},
    {"Minimize to tray",
     "Сворачивать в трей"},
    {"Close button hides to tray",
     "Крестик прячет в трей"},
    {"Exit from the tray menu.",
     "Выход — через меню трея."},
    {"Start minimized to tray",
     "Запускать свёрнутым в трей"},
    {"Stops output and disarms regardless of mode, profile or window; releases every held "
     "key/button. Detected by the keyboard hook, backed up by RegisterHotKey.",
     "Останавливает вывод и снимает с охраны независимо от режима, профиля и окна; отпускает все "
     "удерживаемые клавиши и кнопки. Ловится хуком клавиатуры, продублирована через "
     "RegisterHotKey."},
    {"The trigger and the action of each profile are set on the main screen.",
     "Триггер и действие каждого профиля задаются на главном экране."},
    {"Arm automatically when Infinity Clicker starts",
     "Взводить автоматически при запуске"},
    {"Pause output while Infinity Clicker's own window is in front",
     "Пауза, пока впереди окно самого Infinity Clicker"},
    {"Prevents clicks from landing on Infinity Clicker's own controls.",
     "Чтобы клики не попадали в элементы самого Infinity Clicker."},
    {"Always on top",
     "Поверх всех окон"},
    {"Write log file",
     "Писать лог-файл"},
    {"Debug logging",
     "Отладочный лог"},
    {"Adds one-line engine summaries every second while clicking.",
     "Добавляет ежесекундные сводки движка, пока идут клики."},
    {"Logs and data",
     "Логи и данные"},
    {"Open logs",
     "Открыть логи"},
    {"Data folder",
     "Папка данных"},
    {"Running as administrator - can send input to elevated windows too.",
     "Запущено от администратора — ввод доходит и до окон с правами администратора."},
    {"Privileges",
     "Права"},
    {"Windows blocks input from a normal process into windows running as administrator (User "
     "Interface Privilege Isolation). SendInput still reports success - the events are silently "
     "dropped. If your target app runs elevated, restart Infinity Clicker elevated.",
     "Windows не пропускает ввод из обычного процесса в окна, запущенные от администратора (User "
     "Interface Privilege Isolation). SendInput при этом сообщает об успехе, а события молча "
     "теряются. Если нужное приложение запущено от администратора, перезапустите Infinity Clicker так же."},
    {"Restart as administrator",
     "Перезапустить от администратора"},
    {"Infinity Clicker %s - native C++20 / Win32 / Direct3D 11 / Dear ImGui %s. No network, no telemetry.",
     "Infinity Clicker %s — нативный C++20 / Win32 / Direct3D 11 / Dear ImGui %s. Без сети и без "
     "телеметрии."},
    {"Log",
     "Лог"},
    {"Close",
     "Закрыть"},

    // ---- ui/Tray.cpp
    {"Start (arm)",
     "Старт (взвести)"},
    {"Profile",
     "Профиль"},
    {"Open Infinity Clicker",
     "Открыть Infinity Clicker"},
    {"Exit",
     "Выход"},

    // ---- ui/Views.cpp
    {"Paused: Infinity Clicker's own window is in front",
     "Пауза: впереди окно самого Infinity Clicker"},
    {"Paused: move the cursor over the Test Pad",
     "Пауза: наведите курсор на тест-панель"},
    {"Paused: the target application is not in front",
     "Пауза: целевое приложение не на переднем плане"},
    {"Release %s to stop",
     "Отпустите «%s», чтобы остановить"},
    {"Hold %s",
     "Удерживайте «%s»"},
    {"Press %s to stop",
     "Нажмите «%s», чтобы остановить"},
    {"Press %s to start / stop",
     "Нажмите «%s», чтобы включить / выключить"},
    {"Running...",
     "Работает..."},
    {"Press %s for %llu actions",
     "Нажмите «%s» — %llu действий"},
    {"Press %s to run %llu actions",
     "Нажмите «%s», чтобы выполнить %llu действий"},
    {"Press %s to run for %s",
     "Нажмите «%s», чтобы работать %s"},
    {"Manage profiles",
     "Управление профилями"},
    {"ACTION",
     "ДЕЙСТВИЕ"},
    {"MODE",
     "РЕЖИМ"},
    {"actions",
     "действий"},
    {"More",
     "Ещё"},
    {"Hold",
     "Удержание"},
    {"Toggle",
     "Вкл/Выкл"},
    {"Burst",
     "Серия"},
    {"Stop after a set number of actions",
     "Остановиться после заданного числа действий"},
    {"Run for a set amount of time",
     "Работать заданное время"},
    {"SPEED",
     "СКОРОСТЬ"},
    {"Experimental: as fast as Windows accepts input. Turn it off in Advanced settings.",
     "Экспериментально: так быстро, как Windows принимает ввод. Выключается в дополнительных "
     "настройках."},
    {"= one click every %s",
     "= один клик раз в %s"},
    {"= %s CPS",
     "= %s CPS"},
    {"Actual",
     "Фактически"},
    {"Windows is slowing input down right now",
     "Сейчас Windows замедляет ввод"},
    {"STOP",
     "СТОП"},
    {"ARMED",
     "ВКЛЮЧЁН"},
    {"ARM",
     "ВКЛЮЧИТЬ"},
    {"Enable the trigger",
     "Включить триггер"},
    {"Stop clicking",
     "Остановить клики"},
    {"Click to disarm",
     "Нажмите, чтобы выключить"},
    {"Only while %s is in front",
     "Только пока «%s» впереди"},
    {"Windows rejected the input (screen locked or a UAC prompt is open)",
     "Windows отклонила ввод (экран заблокирован или открыт запрос UAC)"},
    {"Windows rejected some input events",
     "Windows отклонила часть событий ввода"},
    {"Emergency stop: %s",
     "Аварийная остановка: %s"},
    {"Advanced settings",
     "Дополнительные настройки"},
    {"Profiles",
     "Профили"},
    {"Trigger: %s (%s)",
     "Триггер: %s (%s)"},
    {"Action: %s, %s",
     "Действие: %s, %s"},
    {"Activate",
     "Сделать активным"},
    {"Active",
     "Активен"},
    {"Rename",
     "Переименовать"},
    {"Duplicate",
     "Дублировать"},
    {" copy",
     " (копия)"},
    {"Delete",
     "Удалить"},
    {"Delete profile '%s'?",
     "Удалить профиль «%s»?"},
    {"Cancel",
     "Отмена"},
    {"New profile",
     "Новый профиль"},
    {"Reset to defaults",
     "Сбросить к стандартным"},
    {"Replace all profiles with the built-in set?",
     "Заменить все профили встроенным набором?"},
    {"Replace",
     "Заменить"},

    // ---- ui/Widgets.cpp
    {"%.3f s",
     "%.3f с"},
    {"%.3f ms",
     "%.3f мс"},
    {"%.1f us",
     "%.1f мкс"},
    {"%.3g s",
     "%.3g с"},
    {"%.3g ms",
     "%.3g мс"},
    {"%.3g us",
     "%.3g мкс"},
    {"Listening...",
     "Нажмите клавишу..."},
    {"%.0f s",
     "%.0f с"},
    {"Click, then press the key or mouse button you want to use.\nLeft clicks on this window are "
     "ignored while listening; click elsewhere to bind Left Mouse.",
     "Нажмите, затем нажмите нужную клавишу или кнопку мыши.\nПока идёт ожидание, левые клики по "
     "этому окну игнорируются; чтобы назначить левую кнопку мыши, кликните вне окна."},
    {"Pick from the full key list",
     "Выбрать из полного списка клавиш"},
    {"Modifiers",
     "Модификаторы"},
    {"Mouse",
     "Мышь"},
    {"ACTIVE",
     "АКТИВЕН"},
    {"CPS history (10 s)",
     "История CPS (10 с)"},
    {"Back",
     "Назад"},

    // ---- looked up dynamically: key / button names, picker groups, mode and precision names
    {"Count", "Количество"},
    {"Space", "Пробел"},
    {"Left Shift", "Левый Shift"},
    {"Right Shift", "Правый Shift"},
    {"Left Ctrl", "Левый Ctrl"},
    {"Right Ctrl", "Правый Ctrl"},
    {"Left Alt", "Левый Alt"},
    {"Right Alt", "Правый Alt"},
    {"Left Win", "Левый Win"},
    {"Right Win", "Правый Win"},
    {"Menu", "Меню"},
    {"Up", "Вверх"},
    {"Down", "Вниз"},
    {"Left", "Влево"},
    {"Right", "Вправо"},
    {"Volume Mute", "Без звука"},
    {"Volume Down", "Громкость −"},
    {"Volume Up", "Громкость +"},
    {"Media Next", "Следующий трек"},
    {"Media Previous", "Предыдущий трек"},
    {"Media Stop", "Стоп (медиа)"},
    {"Media Play/Pause", "Воспроизведение/пауза"},
    {"Browser Back", "Браузер: назад"},
    {"Browser Forward", "Браузер: вперёд"},
    {"Letters", "Буквы"},
    {"Digits", "Цифры"},
    {"Function", "Функциональные"},
    {"Main", "Основные"},
    {"Navigation", "Навигация"},
    {"System", "Системные"},
    {"OEM", "Символы (OEM)"},
    {"Media", "Медиа"},
};

const std::unordered_map<std::string_view, const char*>& ruMap()
{
    static const std::unordered_map<std::string_view, const char*> m = [] {
        std::unordered_map<std::string_view, const char*> r;
        r.reserve(std::size(kRu));
        for (const auto& [en, ru] : kRu) r.emplace(en, ru);
        return r;
    }();
    return m;
}

} // namespace

namespace i18n {

void setLang(Lang l) { g_lang.store(l, std::memory_order_relaxed); }
Lang lang() { return g_lang.load(std::memory_order_relaxed); }

Lang systemLang()
{
    const LANGID id = GetUserDefaultUILanguage();
    switch (PRIMARYLANGID(id)) {
    case LANG_RUSSIAN:
    case LANG_UKRAINIAN:
    case LANG_BELARUSIAN:
    case LANG_KAZAK:
    case LANG_KYRGYZ:
    case LANG_UZBEK:
    case LANG_ARMENIAN:
    case LANG_AZERI:
    case LANG_GEORGIAN:
    case LANG_TAJIK:
    case LANG_TURKMEN: return Lang::Ru;
    default: return Lang::En;
    }
}

Lang resolve(const std::string& s)
{
    if (s == "ru") return Lang::Ru;
    if (s == "en") return Lang::En;
    return systemLang();
}

} // namespace i18n

const char* tr(const char* en)
{
    if (!en || g_lang.load(std::memory_order_relaxed) != Lang::Ru) return en;
    const auto& m = ruMap();
    auto it = m.find(en);
    return it == m.end() ? en : it->second;
}

} // namespace infclick
