Infinity Clicker - autoclicker for 64-bit Windows
=================================================

Developed and tested on Windows 11. It should also run on Windows 10 1809 or newer (not tested).

Run InfinityClicker.exe. There is no installer; the program keeps its settings in a "data" folder next to the exe
(or in %LOCALAPPDATA%\InfinityClicker if that folder is not writable).

Quick start
  1. Trigger  - click the field and press the key or mouse button that should start the clicking.
  2. Action   - what gets clicked or pressed.
  3. Mode     - Hold (click while the trigger is held), Toggle (press to start, press to stop), Burst.
  4. Speed    - clicks per second; the measured rate is shown under "Actual".
  5. Press ARM (Infinity Clicker starts armed). Emergency stop: Ctrl+Shift+F12.

Things worth knowing
  - Windows blocks synthetic input into windows running as administrator. If the target application is
    elevated, restart Infinity Clicker as administrator (Settings -> Diagnostics).
  - Nothing is sent while the screen is locked or a UAC prompt is open.
  - Games that read the mouse button only once per frame may miss very short clicks; raise the down time
    (Advanced -> Engine) to 10-20 ms for those.
  - The exe is not code-signed, so SmartScreen may warn on first launch.

InfinityClicker.exe --bench        runs the benchmark suite (covers the screen for a few minutes)
InfinityClicker.exe --autotest     runs the functional tests

Source, benchmarks and issues: https://github.com/ilyasaffonovv-commits/Infinity-Clicker
License: MIT (see LICENSE.txt). Third-party: see THIRD_PARTY_LICENSES.txt.

------------------------------------------------------------------------------------------------

Infinity Clicker - автокликер для 64-разрядной Windows (проверялся на Windows 11)

Запустите InfinityClicker.exe. Установка не нужна; настройки лежат в папке "data" рядом с exe.

Быстрый старт
  1. Триггер  - нажмите на поле и затем на клавишу или кнопку мыши, которая запускает клики.
  2. Действие - что кликать или нажимать.
  3. Режим    - Удержание, Вкл/Выкл или Серия.
  4. Скорость - кликов в секунду; измеренная частота показана в строке "Фактически".
  5. Нажмите ВКЛЮЧИТЬ (Infinity Clicker стартует уже включённым). Аварийная остановка: Ctrl+Shift+F12.

Windows не пропускает ввод в окна, запущенные от администратора: если нужное приложение запущено от
администратора, перезапустите Infinity Clicker так же (Настройки -> Диагностика). Пока экран заблокирован или открыт
запрос UAC, ввод не отправляется. Играм, которые проверяют кнопку мыши раз в кадр, может понадобиться
удержание 10-20 мс (Дополнительно -> Движок).
