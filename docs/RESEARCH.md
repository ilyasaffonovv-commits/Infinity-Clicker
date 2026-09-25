# Research notes: Windows input and timing APIs, and other autoclickers

Notes I collected before writing the engine. Statements come either from Microsoft's documentation (links at the
end) or from measurements taken with the bundled benchmark (see [../BENCHMARK.md](../BENCHMARK.md)). Where something
is a guess, it says so. Russian version: [ru/RESEARCH.ru.md](ru/RESEARCH.ru.md).

Test machine: AMD Ryzen AI 9 HX 370, Windows 11 25H2 (build 26200), QPC at 10 MHz.

## 1. Generating input

| Method | What it does | Pros | Cons | Verdict |
|---|---|---|---|---|
| **`SendInput`** | Inserts an INPUT[] array into the system input stream | Documented; the events look like hardware input to applications (apart from the injected flag); events from one call are inserted in order and are not interleaved with other input | Subject to UIPI; every event passes through all low-level hooks in the system (measured) | **used** |
| `mouse_event` / `keybd_event` | Legacy wrappers | - | No batching, no atomicity, marked superseded | no |
| `PostMessage(WM_LBUTTONDOWN)` | Puts a message into a window's queue | Works on background windows | Does not change the async key state, ignored by raw-input applications, invisible to GLFW/LWJGL/DirectInput | no |
| A driver (Interception and the like) | Input at kernel level | Not flagged as injected | Needs a signed driver, stability risk, effectively an anti-cheat bypass | out of scope |

From the `SendInput` documentation:

- it returns the number of events inserted, and **UIPI blocking is reported neither by the return value nor by
  `GetLastError`**: input into a window of a process with a higher integrity level (running as administrator) is
  silently dropped;
- events of one call are inserted serially and are not interleaved with other input, which is why Infinity Clicker puts
  DOWN+UP (and the modifiers of a combination) into a **single** call when the down time is 0.

Measured (BENCHMARK.md, section 4):

- The cost of one `SendInput(2)` (a click) on this machine ranged from about 0.16 ms to 1 ms, changing by 6x within
  one hour, and single calls blocked for up to 35 ms. The cause is other programs' global low-level hooks, drivers
  and background load. Closing Discord took `SendInput(2)` from 333 to 203 us, so **other programs' LL hooks directly
  limit the throughput of any autoclicker** (Microsoft's docs: a LL hook is called synchronously, by switching to the
  hook owner's process and back).
- A batch of 8 clicks per call gave the best throughput (about 12 500 events/s in the fast state); a batch of 64 is
  worse, and with a `WH_MOUSE_LL` hook in the same process it becomes pathological (about 0.6 s per call) and loses
  about 3% of the events.
- With the screen locked `SendInput` returns 0 with `ERROR_ACCESS_DENIED`: the input desktop is not accessible.

## 2. Receiving the trigger

| Method | Hold (needs key-up) | Mouse | Tells injected input apart | Can block the key | Latency / CPU | Verdict |
|---|---|---|---|---|---|---|
| `RegisterHotKey` | no (press only) | no | no | swallows the combination | 0 | **backup** path for the emergency stop only |
| `GetAsyncKeyState` polling | yes | yes | **no** (sees its own clicks, feedback loop) | no | latency equals the poll period (AlphaClicker polls every 200 ms) | no |
| **`WH_KEYBOARD_LL` / `WH_MOUSE_LL`** | yes | yes | **yes**: `LLKHF_INJECTED` / `LLMHF_INJECTED` plus our own `dwExtraInfo` tag | **yes** | immediate; the cost is that every mouse event in the system goes through the hook | **used** (keyboard always, mouse only when needed) |
| Raw Input (`RIDEV_INPUTSINK`) | yes | yes | only our own events (by `ulExtraInformation`); events injected by others cannot be told apart: `hDevice == 0` also occurs for precision touchpads (RAWINPUTHEADER docs) | no | asynchronous, cheaper than a hook | **alternative** backend for a mouse trigger |

Decisions:

- The hook lives on its own thread at `THREAD_PRIORITY_TIME_CRITICAL`, and the callback only compares values and sets
  an event. Otherwise Windows silently removes the hook after `LowLevelHooksTimeout` (Windows 10 1709+: at most
  1000 ms, and "there is no way for the application to know whether the hook is removed"). Infinity Clicker also runs a health
  check: if it keeps injecting but its own hook stops seeing those events, it reinstalls the hooks.
- `WH_MOUSE_LL` is installed **only** when a mouse trigger or emergency key is configured, a binding is being
  captured, or the Input Lab observer is on. A keyboard trigger does not make every mouse move in the system pay for
  our hook. `WM_MOUSEMOVE` returns immediately.
- No feedback loop: all our events carry `dwExtraInfo = 'INFC'`; the hook only counts them (stage 3 of the telemetry)
  and never treats them as a trigger. Covered by an automated test (trigger Left Mouse, action Left click).

## 3. Timing

Measured (BENCHMARK.md, section 1); error is actual minus requested wait:

| Mechanism | Typical error | Note |
|---|---|---|
| `Sleep(1)` without `timeBeginPeriod` | **+14 ... 15 ms** | 15.6 ms quantum: such clickers cannot exceed ~64 CPS |
| `std::this_thread::sleep_for` | **+10 ... 15 ms** | same (the MSVC STL sleeps through the same mechanism) |
| legacy waitable timer | +10 ... 15 ms | same |
| `Sleep` + `timeBeginPeriod(1)` | +0.7 ... 1.0 ms | since Windows 10 2004 it only affects the calling process; on Windows 11 it is ignored for minimized/invisible windows |
| **`CREATE_WAITABLE_TIMER_HIGH_RESOLUTION`** (Windows 10 1803+) | +0.4 ... 0.6 ms (p99 about 0.9 ms) | the best sleeping mechanism, does not touch the global timer resolution |
| HR timer + `timeBeginPeriod(1)` / `NtSetTimerResolution(0.5 ms)` | almost the same | raising the timer resolution barely helps the HR timer, so Infinity Clicker does not do it |
| **Hybrid: HR timer + QPC spin** | **< 1-2 us** | used; the price is CPU for the spin |
| Pure QPC spin | < 1 us | a whole core, always |

That gives three precision modes (the spin threshold was chosen by a sweep, BENCHMARK.md section 2):

- **ECO**: HR timer only, no spin. About 0.3-1 ms of jitter, ~0% CPU.
- **STANDARD**: wake up p99(oversleep) + 100 us early, but spin for at most 10% of the period (at most ~0.1 core).
  Events are exact up to ~100 CPS; above that precision degrades gracefully while CPU stays flat.
- **ULTRA**: wake up max(p99 + 200 us, max oversleep) early, no budget: microsecond precision at any rate, up to one
  core.

The oversleep model is adaptive: every timer wake-up is measured and the last 256 values give p50 / p99 / max.

**Zero drift.** The deadline of action *k* is always `t0 + round(k * period)` (a fractional period in QPC ticks, a
`double`, exact for billions of actions), never "after the previous click". Being late does not move the grid. A unit
test checks that 1000 actions that are each 30% late leave the 1000th deadline exactly where it was, and a 60-second
stress run at 100 CPS gave 6000 actions against an ideal 5999.1 (+0.9). If the system slows `SendInput` down more than
the rate allows, the grid still does not shift: the slots that could not be met are counted as *missed*.

**Being late.** The *Catch up* policy (default): if several slots are already overdue they go out in one `SendInput`
call (at most 8 actions), so the measured rate matches the target even above the "one call per click" limit
(measured: 5000.03 CPS for a 5000 target, as long as the system does not slow `SendInput` down). The *Strict* policy
skips overdue slots and counts them as missed.

**Windows 11 power management.** While running, the engine turns off EcoQoS for its thread and process
(`SetThreadInformation` / `SetProcessInformation` with power throttling), otherwise a minimized window may be moved to
efficiency cores and have timer-resolution requests ignored (documented for `SetProcessInformation`).

**Thread priority.** Only inside the NORMAL process class (Normal / Above normal / Highest / Time critical / MMCSS
"Games"). `REALTIME_PRIORITY_CLASS` is never used. Comparison: BENCHMARK.md section 5. Highest is the default; MMCSS
turned out to be the worst under CPU load.

## 4. Other open-source autoclickers (sources read, nothing copied)

| Project | Language / UI | Timing | Drift | Input | Trigger | Measurements | Good | Weak |
|---|---|---|---|---|---|---|---|---|
| [Blur009/Blur-AutoClicker](https://github.com/Blur009/Blur-AutoClicker) (2k stars) | Rust + Tauri (WebView2) | absolute deadlines, but the wait is `sleep` in 5 ms ticks plus `NtSetTimerResolution` | no | `SendInput`, batches of 1-3 depending on CPS, `dwExtraInfo` tag | LL hooks plus a `GetAsyncKeyState` fallback every 4 ms | CPS is loop iterations over time, **the `SendInput` return value is not checked** | proper deadline model, tag against feedback loops | the claim "Windows limit is ~500 CPS" has no method behind it and is **contradicted by these measurements** (1000-5000 CPS held accurately); ~100 MB of RAM; no spin, so about 1 ms of jitter |
| [oriash93/AutoClicker](https://github.com/oriash93/AutoClicker) (517 stars) | C# WPF | `System.Timers.Timer` | **yes** (relative) | `mouse_event` + `SetCursorPos` | `RegisterHotKey` (no Hold) | none | simple, coordinates | 15.6 ms quantum, drift, legacy API |
| [lalakii/MouseClickTool](https://github.com/lalakii/MouseClickTool) (1.4k stars) | C# WinForms | `Task.Delay` | yes | `SendInput(1)` | `RegisterHotKey` plus a LL hook for the middle button | none | tiny | 15.6 ms quantum, random jitter of 0.8-1.2x offered as a feature |
| [robiot/AlphaClicker](https://github.com/robiot/AlphaClicker) (329 stars) | C# WPF | `Thread.Sleep` | yes | through WinApi | **polls `GetAsyncKeyState` every 200 ms** | none | modern look | up to 200 ms of start latency, prone to feedback loops |
| [b1scoito/clicker](https://github.com/b1scoito/clicker) (144 stars) | C++ ImGui DX9 | relative `sleep_for` / PreciseSleep, half a period per phase | yes | an `input::click` abstraction | `GetAsyncKeyState` polling | none | ImGui, randomization for Minecraft | drift, polling |
| [MrTanoshii/rusty-autoclicker](https://github.com/MrTanoshii/rusty-autoclicker) (127 stars) | Rust egui | frame-based | yes | rdev | device_query polling | none | cross-platform | accuracy tied to UI frames |

Ideas taken (the ideas, not the code): absolute deadlines and a `dwExtraInfo` tag (Blur), a LL hook as the main path
(Blur, MouseClickTool), ImGui as a light native UI (b1scoito). What none of them has and Infinity Clicker does: a hybrid wait
with an adaptive threshold, separate requested / accepted / observed / received counters, honest target versus
measured rate, adaptive batching, a watchdog process against stuck keys, and a benchmark that reproduces the numbers.

## 5. Windows limitations worth knowing

- **UIPI.** A normal process cannot send input into a window running as administrator. `SendInput` *reports
  success* while the events are dropped. Fix: Settings, Diagnostics, "Restart as administrator".
- **Locked screen / UAC prompt / secure desktop.** `SendInput` returns 0 with `ERROR_ACCESS_DENIED`. Infinity Clicker says so in
  the UI and logs it (aggregated).
- **Windows accepting an event is not the same as the application handling it.** At 10 000 CPS Windows accepted
  8972 clicks/s and the window received 7276/s; with batched sending of 24 000 events the window got 18 587. The
  difference is lost in an overflowing input queue. The Input Lab therefore shows the four stages separately.
- **Games that read the button state once per frame** miss clicks shorter than a frame (BENCHMARK.md section 6).
  Event-driven applications (browsers, GLFW/LWJGL, so Minecraft Java) see every click.
- Anti-cheat on servers and in games may forbid autoclickers. Infinity Clicker hides nothing (its events are flagged as
  injected by Windows itself and carry our tag) and bypasses nothing.

## Sources

- SendInput: https://learn.microsoft.com/windows/win32/api/winuser/nf-winuser-sendinput
- CreateWaitableTimerExW: https://learn.microsoft.com/windows/win32/api/synchapi/nf-synchapi-createwaitabletimerexw
- timeBeginPeriod: https://learn.microsoft.com/windows/win32/api/timeapi/nf-timeapi-timebeginperiod
- SetProcessInformation (power throttling): https://learn.microsoft.com/windows/win32/api/processthreadsapi/nf-processthreadsapi-setprocessinformation
- LowLevelMouseProc: https://learn.microsoft.com/windows/win32/winmsg/lowlevelmouseproc
- MSLLHOOKSTRUCT: https://learn.microsoft.com/windows/win32/api/winuser/ns-winuser-msllhookstruct
- RAWINPUTHEADER: https://learn.microsoft.com/windows/win32/api/winuser/ns-winuser-rawinputheader
- RAWINPUTDEVICE: https://learn.microsoft.com/windows/win32/api/winuser/ns-winuser-rawinputdevice
- Windows and high resolution timers (siliceum): https://www.siliceum.com/en/blog/post/windows-high-resolution-timers/
