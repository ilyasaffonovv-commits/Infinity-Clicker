[English version](../RESEARCH.md)

# Infinity Clicker — исследование: Windows input / timing API и существующие автокликеры

Все утверждения ниже либо взяты из официальной документации Microsoft (ссылки в конце), либо
**измерены** на тестовой машине встроенным бенчмарком (`InfinityClicker.exe --bench`, результаты — в
[../../BENCHMARK.md](../../BENCHMARK.md)). Где вывод — гипотеза, это сказано явно.

Тестовая машина: AMD Ryzen AI 9 HX 370 (12C/24T, гибрид Zen 5 + Zen 5c), Windows 11 25H2
(build 26200), питание от сети, QPC = 10 МГц.

---

## 1. Генерация ввода

| Способ | Что делает | Плюсы | Минусы | Вывод |
|---|---|---|---|---|
| **`SendInput`** | Вставляет INPUT[] в системный поток ввода (RIT) | Официальный, события неотличимы от «железа» для приложений (кроме флага injected), атомарность массива: события одного вызова *не перемешиваются* с другим вводом | Подчиняется UIPI; каждое событие синхронно проходит через все LL-хуки системы (замерено, см. ниже) | **Выбран** |
| `mouse_event` / `keybd_event` | Устаревшие обёртки | — | Нет батчинга, нет атомарности, помечены как superseded | Нет |
| `PostMessage(WM_LBUTTONDOWN)` | Кладёт сообщение в очередь окна | Работает в фоне | Не меняет async key state, игнорируется raw-input приложениями, GLFW/LWJGL/DirectInput его не видят, не проходит через систему ввода | Нет (не универсально) |
| Драйвер (Interception и т.п.) | Ввод на уровне ядра | Не помечается как injected | Подписанный драйвер, риск стабильности, по сути anti-cheat bypass | Запрещено ТЗ |

Факты из документации `SendInput`:
- возвращает число вставленных событий; **UIPI-блокировку не сообщает ни возвратом, ни GetLastError** — ввод в окно
  процесса с более высоким integrity level (запущенного от администратора) молча отбрасывается;
- события одного вызова вставляются последовательно и не перемежаются с другим вводом — поэтому Infinity Clicker кладёт
  `DOWN+UP` (и модификаторы комбинации) в **один** вызов, когда down-time = 0.

Замерено (раздел 4 BENCHMARK.md):
- стоимость одного вызова `SendInput(2)` (клик) на этой машине — от **~0.16 мс до ~1 мс**, причём она менялась в 6 раз
  в течение одного часа, а отдельные вызовы блокировались до 35 мс. Всё зависит от программ с глобальными
  low-level хуками, драйверов и фоновой нагрузки. После закрытия Discord `SendInput(2)` ускорился с 333 до 203 мкс,
  т.е. **чужие LL-хуки напрямую режут пропускную способность любого автокликера** (документация MS: LL-хук
  вызывается синхронно, переключением контекста в процесс-владелец хука и обратно);
- батч из 8 кликов в одном вызове даёт максимум пропускной способности (~12 500 событий/с), батч 64 — хуже, а при
  установленном в том же процессе `WH_MOUSE_LL` приводит к патологии (≈0.6 с на вызов) и **потере ~3% событий**;
- при заблокированном экране `SendInput` возвращает 0 с `ERROR_ACCESS_DENIED` (5) — рабочий стол ввода недоступен.

## 2. Приём триггера (горячих клавиш)

| Способ | Hold (нужен key-up) | Мышь | Отличает injected | Может заблокировать клавишу | Задержка/CPU | Вывод |
|---|---|---|---|---|---|---|
| `RegisterHotKey` | нет (только нажатие) | нет | нет | съедает комбинацию | 0 | Только **резервный** путь для аварийной остановки |
| `GetAsyncKeyState` polling | да | да | **нет** (видит и свои клики → feedback loop) | нет | задержка = период опроса, CPU на опрос (AlphaClicker: 200 мс!) | Нет |
| **`WH_KEYBOARD_LL` / `WH_MOUSE_LL`** | да | да | **да**: `LLKHF_INJECTED`/`LLMHF_INJECTED` + наш `dwExtraInfo` тег | **да** | событие мгновенно; стоимость — каждое событие мыши в системе проходит через хук | **Выбран** (клавиатура всегда, мышь — только когда нужна) |
| Raw Input (`RIDEV_INPUTSINK`) | да | да | только свои (по `ulExtraInformation`); чужой injected ≠ отличим: `hDevice == 0` бывает и у precision touchpad (документация RAWINPUTHEADER) | нет | асинхронно, дешевле хука | **Альтернативный** бэкенд мышиного триггера |

Решения:
- Хук-поток отдельный, `THREAD_PRIORITY_TIME_CRITICAL`, колбэк делает только сравнение и `SetEvent` — иначе Windows
  молча удаляет хук по `LowLevelHooksTimeout` (Win10 1709+: максимум 1000 мс, «There is no way for the application
  to know whether the hook is removed»). Infinity Clicker дополнительно делает health-check: если мы инжектим, а собственный
  хук перестал видеть наши события — переустанавливает хуки.
- `WH_MOUSE_LL` ставится **только** когда триггер/аварийка — кнопка мыши, идёт захват бинда или наблюдение в Input Lab.
  Для клавиатурного триггера каждое движение мыши в системе не платит за наш хук.
- `WM_MOUSEMOVE` в хуке возвращается сразу, без какой-либо работы.
- Отсутствие feedback loop: все наши события несут `dwExtraInfo = 'INFC'`; хук их только считает (стадия 3 телеметрии)
  и никогда не принимает за триггер. Проверено автотестом «Left Mouse Hold → Left click».

## 3. Тайминг

Замерено (раздел 2 BENCHMARK.md), ошибка ожидания = факт − запрошено:

| Механизм | Типичная ошибка | Вывод |
|---|---|---|
| `Sleep(1)` без `timeBeginPeriod` | **+14…15 мс** | квант 15.6 мс: такие кликеры физически не могут дать >64 CPS |
| `std::this_thread::sleep_for` | **+10…15 мс** | то же самое (MSVC STL спит через тот же механизм) |
| legacy waitable timer | +10…15 мс | то же |
| `Sleep` + `timeBeginPeriod(1)` | +0.7…1.0 мс | с Win10 2004 влияет только на свой процесс; с Win11 игнорируется у свёрнутых/невидимых окон |
| **`CREATE_WAITABLE_TIMER_HIGH_RESOLUTION`** (Win10 1803+) | +0.4…0.6 мс (p99 ≈ 0.9 мс) | лучший «спящий» механизм, не трогает глобальное разрешение таймера |
| HR-таймер + `timeBeginPeriod(1)` / `NtSetTimerResolution(0.5 мс)` | практически то же | повышение разрешения таймера HR-таймеру почти не помогает → Infinity Clicker его не использует |
| **Гибрид: HR-таймер + QPC spin** | **< 1–2 мкс** | выбран; цена — CPU на спин |
| Чистый spin по QPC | < 1 мкс | 100% ядра всегда — только для MAX/очень высоких CPS |

Отсюда три режима точности (порог спина подобран сканированием, раздел 3 BENCHMARK.md):

- **ECO** — только HR-таймер, без спина: ~0.3–1 мс джиттер, ~0% CPU.
- **STANDARD** — запас пробуждения = p99 измеренного «пересыпа» таймера + 100 мкс, но **не больше 10% периода**
  (бюджет ≤ 0.1 ядра). До ~100 CPS события точные до микросекунд, выше точность плавно деградирует, CPU не растёт.
- **ULTRA** — запас = max(p99 + 200 мкс, max пересыпа), без бюджета: микросекундная точность на любой частоте, до 1 ядра.

Модель пересыпа адаптивная: каждое пробуждение таймера меряется и последние 256 значений дают p50/p99/max.

**Нулевой дрейф**: дедлайн k-го действия всегда `t0 + round(k · период)` (дробный период в тиках QPC, `double` —
точно на миллиарды действий), а не «после предыдущего клика». Опоздание никогда не сдвигает сетку. Юнит-тест
проверяет, что 1000 действий с 30%-ным опозданием каждое оставляют 1000-й дедлайн ровно на месте, а 60-секундный
стресс-тест на 100 CPS дал 6000 действий при идеале 5999.1 (+0.9). Если система замедляет `SendInput` сильнее, чем
позволяет частота, сетка всё равно не сдвигается: непоспевшие слоты честно считаются как *missed*.

**Опоздания**: политика *Catch up* (по умолчанию) — если несколько слотов уже просрочены, они уходят одним
`SendInput` (≤ 8 действий), поэтому фактический CPS совпадает с целевым даже выше лимита «один вызов на клик»
(замерено: 5000.03 CPS при цели 5000, пока система не тормозит `SendInput`). Политика *Strict* — просроченные
слоты пропускаются и считаются как missed.

**Энергосбережение Windows 11**: на время работы движок отключает EcoQoS для своего потока и процесса
(`SetThreadInformation/SetProcessInformation(PowerThrottling)`), иначе свёрнутое окно может получить урезанную
частоту/E-ядра и проигнорированные запросы разрешения таймера (документировано в SetProcessInformation).

**Приоритет потока**: только внутри NORMAL process class (Normal / Above normal / Highest / Time critical / MMCSS
"Games"); `REALTIME_PRIORITY_CLASS` не используется — ТЗ и здравый смысл. Сравнение — раздел 6 BENCHMARK.md.

## 4. Существующие open-source автокликеры (исходники прочитаны, не скопированы)

| Проект | Язык / UI | Тайминг | Дрейф | Ввод | Триггер | Замеры | Сильное | Слабое |
|---|---|---|---|---|---|---|---|---|
| [Blur009/Blur-AutoClicker](https://github.com/Blur009/Blur-AutoClicker) (2k★) | Rust + Tauri (WebView2) | абсолютные дедлайны, но ожидание — `sleep` тиками по 5 мс + `NtSetTimerResolution` | нет | `SendInput`, батч 1–3 по порогам CPS, тег `dwExtraInfo` | LL-хуки + резервный опрос `GetAsyncKeyState` каждые 4 мс | CPS = итерации цикла / время, **без проверки возврата SendInput** | правильная модель дедлайнов, тег против feedback loop | утверждение «Windows limit ~500 CPS» без методики — **опровергнуто** нашими замерами (точные 1000–5000 CPS); ~100 МБ RAM; без спина → джиттер ~1 мс |
| [oriash93/AutoClicker](https://github.com/oriash93/AutoClicker) (517★) | C# WPF | `System.Timers.Timer` | **да** (относительный) | `mouse_event` + `SetCursorPos` | `RegisterHotKey` (нет Hold) | нет | простой, координаты | 15.6 мс квант, дрейф, устаревший API |
| [lalakii/MouseClickTool](https://github.com/lalakii/MouseClickTool) (1.4k★) | C# WinForms | `Task.Delay` | да | `SendInput(1)` | `RegisterHotKey` + LL-хук для средней кнопки | нет | крошечный размер | квант 15.6 мс, случайный джиттер 0.8–1.2 как «фича» |
| [robiot/AlphaClicker](https://github.com/robiot/AlphaClicker) (329★) | C# WPF | `Thread.Sleep` | да | через WinApi | **опрос `GetAsyncKeyState` раз в 200 мс** | нет | современный вид | до 200 мс задержки старта, feedback-loop-уязвимость |
| [b1scoito/clicker](https://github.com/b1scoito/clicker) (144★) | C++ ImGui DX9 | относительные `sleep_for`/PreciseSleep, половина периода на фазу | да | абстракция input::click | опрос `GetAsyncKeyState` | нет | ImGui, рандомизация для Minecraft | дрейф, polling |
| [MrTanoshii/rusty-autoclicker](https://github.com/MrTanoshii/rusty-autoclicker) (127★) | Rust egui | кадровый тайминг | да | rdev | device_query polling | нет | кроссплатформенность | точность привязана к кадрам UI |

Идеи, взятые (идея, не код): абсолютные дедлайны и тег `dwExtraInfo` (Blur), LL-хук как основной путь (Blur,
MouseClickTool), ImGui для лёгкого нативного UI (b1scoito).
Чего нет ни у одного проекта и что сделано в Infinity Clicker: гибридное ожидание с адаптивным порогом, разделение
requested / accepted / observed / received, честный Target vs Actual, адаптивный батчинг, процесс-сторож
против залипших клавиш, бенчмарк, который воспроизводит все цифры.

## 5. Ограничения Windows (важно для пользователя)

- **UIPI**: обычный процесс не может слать ввод в окно, запущенное «от администратора». `SendInput` при этом
  *сообщает об успехе* — события молча теряются. Решение: Settings → *Restart as administrator*.
- **Заблокированный экран / UAC-запрос / secure desktop**: `SendInput` → 0, `ERROR_ACCESS_DENIED`. Infinity Clicker
  показывает это в UI и пишет в лог (агрегировано).
- **Приложение ≠ Windows**: «принято SendInput» не значит «обработано приложением». На 10 000 CPS Windows приняла
  8 972 клика/с, а окно получило 7 276/с; при пакетной отправке 24 000 событий окно получило только 18 587.
  Разница теряется в переполненной очереди ввода. Поэтому Input Lab показывает 4 стадии отдельно.
- **Игры, опрашивающие состояние кнопки раз в кадр**, не увидят клик с down-time короче кадра (раздел 7
  BENCHMARK.md). Событийные приложения (браузеры, GLFW/LWJGL — Minecraft Java) видят все клики.
- Античиты серверов/игр могут запрещать автокликеры — Infinity Clicker ничего не скрывает (события помечены как injected
  самой Windows и нашим тегом) и ничего не обходит.

## Источники

- SendInput — https://learn.microsoft.com/windows/win32/api/winuser/nf-winuser-sendinput
- CreateWaitableTimerExW — https://learn.microsoft.com/windows/win32/api/synchapi/nf-synchapi-createwaitabletimerexw
- timeBeginPeriod — https://learn.microsoft.com/windows/win32/api/timeapi/nf-timeapi-timebeginperiod
- SetProcessInformation (power throttling) — https://learn.microsoft.com/windows/win32/api/processthreadsapi/nf-processthreadsapi-setprocessinformation
- LowLevelMouseProc — https://learn.microsoft.com/windows/win32/winmsg/lowlevelmouseproc
- MSLLHOOKSTRUCT — https://learn.microsoft.com/windows/win32/api/winuser/ns-winuser-msllhookstruct
- RAWINPUTHEADER — https://learn.microsoft.com/windows/win32/api/winuser/ns-winuser-rawinputheader
- RAWINPUTDEVICE — https://learn.microsoft.com/windows/win32/api/winuser/ns-winuser-rawinputdevice
- Windows and high resolution timers (siliceum) — https://www.siliceum.com/en/blog/post/windows-high-resolution-timers/
