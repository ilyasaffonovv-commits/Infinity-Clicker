# Benchmarks

Everything here was measured on one machine with Infinity Clicker's own tooling (`InfinityClicker.exe --bench`,
`InfinityClicker.exe --autotest`). The raw, unedited reports are in [`benchmarks/results/`](benchmarks/results):

| File | What it is |
|---|---|
| `bench_20260924_235228.md` | full run, all sections, 60 s stress tests (23:40-23:52) |
| `bench_20260925_000016.md` | hold-time test with a corrected method, and the stress test rerun with the "time inside SendInput" metric |
| `bench_20260925_000248_quick.md`, `cps_matrix.csv` | quick rerun of SendInput and the CPS matrix while the system was in a "slow" state |
| `smoke_v1.0.0.md` | smoke run of the release build (timers, spin threshold, CPS matrix) |
| `autotest_results.md` | functional tests of the release build (38 checks) |

Russian version: [docs/ru/BENCHMARK.ru.md](docs/ru/BENCHMARK.ru.md).

**Test machine:** AMD Ryzen AI 9 HX 370 (12 cores / 24 threads), Windows 11 25H2 (build 26200.9550), on AC power,
QPC at 10 MHz, system timer resolution at 1.0 ms during the runs (some background program had requested it; it has
no effect on Infinity Clicker, see section 1). Steam, NVIDIA Overlay, Armoury Crate and other background software were running.

> **Read the numbers with this in mind.** The cost of a single `SendInput` call on this machine changed by about
> 6x within an hour, from roughly 0.16 ms to 1 ms per click. Other programs' global low-level hooks, drivers and
> background load are the reason. So the maximum click rate is a property of the system at that moment, not a
> constant of the autoclicker. Infinity Clicker measures it while running (the "time inside SendInput" metric) and always
> shows the target and the measured rate side by side.

## 1. Timers: how precisely Windows sleeps

Wait error (actual minus requested), in microseconds, 300 samples per row, thread at HIGHEST priority:

| Mechanism | 1 ms: mean / p99 | 5 ms: mean / p99 | CPU |
|---|---|---|---|
| `Sleep()` without `timeBeginPeriod` | **+14 498 / +15 106** | +10 542 / +11 051 | 0% |
| `Sleep()` + `timeBeginPeriod(1)` | +989 / +1 516 | +890 / +1 509 | 0% |
| `std::this_thread::sleep_for` | **+14 526 / +15 297** | +10 586 / +11 527 | 0% |
| waitable timer (regular) | +14 529 / +15 143 | +10 567 / +11 121 | 0% |
| **waitable timer, HIGH_RESOLUTION** | **+517 / +617** | +426 / +603 | 0% |
| HIGH_RESOLUTION + `timeBeginPeriod(1)` | +526 / +636 | +432 / +600 | 0% |
| HIGH_RESOLUTION + `NtSetTimerResolution(0.5 ms)` | +503 / +600 | +443 / +588 | 0% |
| **hybrid STANDARD (timer + spin)** | +6.1 / +101.8 | **+0.6 / +1.5** | 54% / 8% |
| **hybrid ULTRA** | **+1.9 / +0.5** | +0.3 / +0.8 | 87% / 23% |
| pure QPC spin | +0.0 / +0.1 | +0.0 / +0.2 | 100% |

- A process that did not ask for a finer timer resolution does not get one on Windows 11, even if another process
  already raised the system resolution to 1 ms. `Sleep(1)` and `sleep_for` really sleep about 15.6 ms, so anything
  built on them cannot go above roughly 64 clicks per second.
- The high-resolution waitable timer is the best sleeping primitive (about 0.5 ms of error). `timeBeginPeriod` and the
  undocumented `NtSetTimerResolution` barely help it, so Infinity Clicker uses neither.
- Microsecond precision needs a QPC spin for the last stretch.

## 2. Choosing the spin threshold (1000 and 250 actions/s, no input sent)

| Rate | Wake-up margin | late p50 | late p99 | over 100 us late | thread CPU |
|---|---|---|---|---|---|
| 1000/s | 0 (ECO) | 291 us | 824 us | 85% | 0% |
| 1000/s | 300 us | 0.2 us | 517 us | 28% | 8% |
| 1000/s | 500 us | 0.0 | 216 us | 2% | 43% |
| 1000/s | 1000 us | 0.0 | **0.1 us** | 0% | 100% |
| 1000/s | adaptive ULTRA | 0.0 | **0.1 us** | 0% | 100% |
| 250/s | 500 us | 0.1 | 18.6 us | 0.5% | 8% |
| 250/s | 1000 us | 0.1 | 0.4 us | 0% | 19% |
| 250/s | adaptive STANDARD | 0.1 | 116 us | 4.8% | **3%** |
| 250/s | adaptive ULTRA | 0.1 | **0.8 us** | 0% | 10% |

The timer's oversleep reaches about 1 ms, so a full-precision wake-up needs roughly 1 ms of spinning per event.
That gives the three precision modes: **ECO** has no spin. **STANDARD** wakes up p99(oversleep) + 100 us early but
never spins for more than 10% of the interval (exact up to about 100 CPS, at most ~0.1 core). **ULTRA** has no CPU
budget (microsecond timing at any rate, up to a full core).

## 3. CPS matrix: the whole path (scheduler, SendInput, receiving window)

Full run (23:40, system in its "fast" state). Left click, down time 0, STANDARD unless stated. *Actual* is the rate
the engine generated, *Received* is what the test window really got:

| Target | Precision | Actual CPS | Received CPS | Error | Jitter | Late p50 / p99 | Missed | CPU (cores) |
|---|---|---|---|---|---|---|---|---|
| 1 | STANDARD | 1.00 | 1.11* | 0.00% | 28 us | 0.6 / 86 us | 0 | 0.07 |
| 5 | STANDARD | 5.00 | 5.17* | 0.00% | 8 us | 0.5 / 43 us | 0 | 0.07 |
| 10 | STANDARD | 10.00 | 10.25* | 0.00% | 9 us | 0.5 / 55 us | 0 | 0.07 |
| 20 | STANDARD | 20.00 | 20.25* | 0.00% | 4.7 us | 0.5 / 43 us | 0 | 0.08 |
| 50 | STANDARD | 50.00 | 50.25* | 0.00% | 1.9 us | 0.4 / 1.0 us | 0 | 0.09 |
| 100 | STANDARD | 100.00 | 100.32* | 0.00% | 18 us | 0.2 / 24 us | 0 | 0.13 |
| 250 | STANDARD | 250.02 | 250.30* | 0.01% | 47 us | 0.2 / 156 us | 0 | 0.14 |
| 500 | STANDARD | 500.00 | 500.31* | 0.00% | 91 us | 111 / 377 us | 0 | 0.13 |
| 1000 | STANDARD | 1000.01 | 1000.18* | 0.00% | 216 us | 238 / 754 us | 0 | 0.15 |
| 2000 | STANDARD | 1999.82 | 2000.05* | 0.01% | 280 us | 139 / 754 us | 0 | 0.27 |
| **5000** | STANDARD | **5000.03** | **5000.08*** | 0.00% | 77 us | 0 / 377 us | 0 | 0.75 |
| 10000 | STANDARD | 8971.8 | **7275.6** | - | - | - | 2501 | 0.41 |
| 20000 | STANDARD | 9174.9 | 6518.4 | - | - | - | 35024 | 0.41 |
| 20 | ECO | 20.00 | 20.25* | 0.01% | 160 us | 238 / 508 us | 0 | 0.07 |
| 20 | ULTRA | 20.00 | 20.25* | 0.00% | 29 us | 0.5 / 172 us | 0 | 0.09 |
| 100 | ULTRA | 100.00 | 100.32* | 0.00% | **1.6 us** | 0.3 / **0.8 us** | 0 | 0.16 |
| 1000 | ECO | 999.87 | 1000.15* | 0.01% | 238 us | 377 / 885 us | 0 | 0.15 |
| 1000 | ULTRA | 1000.01 | 1000.27* | 0.00% | **4.1 us** | 0.0 / **0.2 us** | 0 | 1.00 |
| MAX (1 click per call) | - | 6366 | 6365 | - | - | - | - | 0.55 |
| MAX (8 clicks per call) | - | **8976** | **6950** | - | - | - | - | 0.42 |
| MAX (64 clicks per call) | - | 163 | 144 | - | - | - | - | 0.08 |

\* In this run "Received" was computed as clicks received divided by the run length, and the run length spans N-1
intervals, so at low rates it is higher by exactly one click (1.11 at 1 CPS means 10 clicks in 9 s). The counts
themselves match: the window received exactly as many clicks as were generated. The current build computes both
values the same way (N-1 intervals between the first and the last event); see the smoke run below.

- **1 to 5000 CPS are held accurately**: mean interval error at most 0.01%, and the window receives exactly what was
  generated.
- Above about 7000 per second the gap between "accepted by Windows" and "received by the application" opens up:
  at 10 000 CPS SendInput accepted 8972/s while the window got 7276/s.
- A batch of 64 clicks in one call is pathological (about 50 times slower), which is why the engine catches up in
  batches of at most 8.
- ULTRA at 1000 CPS gives 4 us of jitter at the price of one core; STANDARD at 100 CPS costs 0.13 core.

With Infinity Clicker's own `WH_MOUSE_LL` hook installed (what happens when the trigger is a mouse button): 1000 CPS gives
1000.2 received, 5000 CPS gives 4643, MAX gives about 4900. The hook costs roughly 30% of the throughput, which is
why it is installed only when needed.

### Smoke run of the 1.0.0 build

`smoke_v1.0.0.md`, taken on the release build after the interface rework (system in a "fast" state, so
SendInput calls took 0.1-0.5 ms):

| Target | Precision | Actual CPS | Received CPS | Error | Jitter | Late p99 | SendInput p50 / p99 | CPU (cores) |
|---|---|---|---|---|---|---|---|---|
| 1 | STANDARD | 1.00 | 1.00 | 0.00% | 43 us | 94 us | 410 / 3277 us | 0.01 |
| 20 | STANDARD | 20.00 | 20.00 | 0.00% | 8 us | 51 us | 508 / 819 us | 0.02 |
| 100 | STANDARD | 100.00 | 100.00 | 0.00% | 1.5 us | 3.7 us | 205 / 557 us | 0.08 |
| 250 | STANDARD | 250.00 | 250.07 | 0.00% | 43 us | 205 us | 188 / 819 us | 0.08 |
| 1000 | STANDARD | 1000.23 | 1000.52 | 0.02% | 238 us | 754 us | 127 / 623 us | 0.10 |
| 2000 | STANDARD | 1999.92 | 2000.49 | 0.00% | 291 us | 885 us | 139 / 688 us | 0.23 |
| 5000 | STANDARD | 5000.26 | 5014.44 | 0.01% | 95 us | 508 us | 94 / 557 us | 0.67 |
| 10000 | STANDARD | 9078 | 6580 | - | - | - | 819 / 1507 us | 0.36 |
| 100 | ULTRA | 100.00 | 100.04 | 0.00% | 4.6 us | 26 us | 254 / 950 us | 0.09 |
| 1000 | ULTRA | 1000.09 | 1000.32 | 0.01% | 3.7 us | 0.1 us | 111 / 557 us | 0.93 |
| MAX (8 per call) | - | 9254 | 6949 | - | - | - | - | 0.37 |

## 4. SendInput throughput and how unstable it is

Clicks injected directly from the benchmark thread into the test window, in the system's "fast" state (23:4x) and
about 20 minutes later:

| Call pattern | Call time, 23:4x | Call time, 00:02 | Clicks/s delivered, 23:4x | Delivered, 00:02 |
|---|---|---|---|---|
| `SendInput(1)` twice per click | 101 us | 662 us | 4930 | 755 |
| `SendInput(2)` per click | 157 us | 967 us | 6241 | 1034 |
| batch of 8 clicks | 845 us | 3353 us | 6941 (**18 587 of 24 000 arrived**) | 2386 |
| batch of 64 clicks | 58.9 ms | 101.9 ms | 1080 | 624 |

- Every injected event passes synchronously through all global low-level hooks in the system. Closing Discord (in an
  earlier session) sped `SendInput(2)` up from 333 to 203 us.
- An external process (PowerShell) sending a zero-distance mouse move measured p50 = 254 us in the slow state.
- The stress rerun with the "time inside SendInput" metric (section 8) showed p50 = 3 ms and a worst case of 35 ms
  per call: the engine spent almost all of its time inside the system call while using only 0.07-0.15 core itself.
- With batched sending the receiving window can lose events (18 587 of 24 000): the thread's input queue overflows.
  The Input Lab shows this as the difference between "accepted" and "received".

## 5. Scheduler thread priority (1000 CPS, STANDARD)

"Loaded" means one busy NORMAL-priority thread per logical processor (24 of them):

| Condition | Priority | Actual CPS | late p99 | late max |
|---|---|---|---|---|
| idle | Normal | 999.95 | 754 us | 6.1 ms |
| idle | Highest | 999.91 | 688 us | 1.8 ms |
| **loaded** | **Normal** | 999.98 | **4981 us** | **30.7 ms** |
| loaded | Above normal | 1000.01 | 442 us | 3.7 ms |
| **loaded** | **Highest** | 999.95 | **442 us** | **1.2 ms** |
| loaded | Time critical | 1000.01 | 475 us | 3.9 ms |
| loaded | **MMCSS (Games)** | 999.94 | **79 692 us** | **106 ms** |

Priority does not matter on an idle machine. Under full load Normal has outliers up to 30 ms; Highest is the best
compromise and is the default. MMCSS is the worst: the multimedia scheduler throttles its threads when the CPU is
short (SystemResponsiveness). `REALTIME_PRIORITY_CLASS` is not used.

## 6. Down time: who still sees a short click

37 clicks/s (not a divisor of the polling rates, so click phases spread evenly), 120 clicks per row:

| Down time | Event-driven window (WM_LBUTTONDOWN) | State polling at 1000 Hz | Polling at 60 Hz ("once per frame") | Expected for an ideal 60 Hz poller |
|---|---|---|---|---|
| 0 us | **120/120** | 22% | 2% | 0% |
| 1 ms | **120/120** | 32% | 15% | 6% |
| 5 ms | **120/120** | 92% | 6% | 30% |
| 10 ms | **120/120** | 100% | 32% | 60% |
| 16.7 ms | **120/120** | 100% | 62% | 100% |
| 20 ms | **120/120** | 100% | 62% | 100% |

- Event-driven programs (browsers, ordinary desktop software, GLFW/LWJGL, so Minecraft Java) receive **every** click,
  even with a 0 us down time.
- Games that poll the button *state* once per frame miss short clicks. For those, set the down time to at least one
  frame (10-20 ms). This is why the default profiles use 10-12 ms.
- In the slow state Windows delivers DOWN and UP in bursts, so pollers see fewer clicks than the ideal (32% instead of
  100% at 1 ms with a 1000 Hz poller).

## 7. Trigger latency (100 presses of each kind)

| Mode | Stage | mean | p50 | p99 |
|---|---|---|---|---|
| Hold | injected F13 to our keyboard hook | 165 us | 147 us | 359 us |
| Hold | hook to the engine's first SendInput | **130 us** | **112 us** | 285 us |
| Hold | injected key to the window receiving the click (end to end) | 4.2 ms | 3.7 ms | 8.9 ms |
| Hold | release to engine stopped | 2.4 ms | 2.2 ms | 5.6 ms |
| Toggle | hook to first SendInput | 153 us | 135 us | 395 us |
| Toggle | second press to engine stopped | 2.4 ms | 2.2 ms | 4.6 ms |

Infinity Clicker's own latency (hook to first click) is about 0.1 ms. Most of the end-to-end time is Windows delivering the
event, before any USB polling of a real mouse. In the functional tests Hold stops 0.5 ms after the release
(measured inside the app).

## 8. Stress tests (60 s): memory, handles, threads, drift, stuck buttons

| Speed | Result | Timeline drift | Memory / handles / threads | Stuck input |
|---|---|---|---|---|
| **100 CPS, 60 s** | 6000 actions in 59.99 s = **100.00 CPS**, 6000/6000 received | **+0.9 actions** out of 6000 | +0.00 MB / +0 / +0 | none |
| 1000 CPS, 60 s | 826 CPS: the system slowed SendInput (below); 10 407 slots correctly counted as missed | the grid does not shift, misses are visible | +0.05 MB / +0 / +2* | none |
| 5000 CPS ULTRA, 60 s | 1338 CPS (bound by SendInput) | - | -0.29 MB / +0 / -2* | none |
| MAX with backpressure, 30 s | 1112 CPS, 33 352/33 352 received | - | +0.07 MB / +0 / +1* | none |

\* A change of plus or minus 2 threads is the Windows thread pool; there is no leak (exactly 0 over 60 s at 100 CPS).

The rerun with the new metric (00:00, 30 s each) confirmed why 1000 and 5000 CPS fell short: the engine spent 77-124%
of the time inside `SendInput` (values slightly above 100% are an accounting artifact at the statistics publish
boundary), and the worst single call
took 29-35 ms. 100 CPS stayed perfect at the same time: 3000/3000 in 29.993 s, drift +0.7. After every stop
`VK_LBUTTON` was up and the engine held nothing.

## 9. Functional tests (`--autotest`)

The real application runs with its UI and a scratch data folder; physical input is simulated by injecting events with
a separate tag. The final run on the release build: **38 of 38 passed.**

| Check | Result |
|---|---|
| Hold, trigger F13 held 500 ms at 100 CPS | 51 downs / 51 ups, stopped **0.5 ms** after release |
| Toggle with Mouse Button 4 as trigger | starts, 61 clicks in ~300 ms at 200 CPS, stops |
| Burst 10 / Fixed count 25 / Duration 500 ms at 200 CPS | exactly 10 / 25 / 100 |
| 10 / 50 / 1000 CPS for 1 s | 10 / 50 / 1000 received (actual 10.00 / 50.00 / 999.73) |
| Key A x20 injected by scan code | 20 downs with vk=A scan=0x1E, 20 ups |
| Combination Ctrl+Shift+K x5 | 5 presses of K with Ctrl+Shift held, modifiers released after |
| Keys F5, Space, Up, Numpad 7, "." | 3 of 3 each |
| Mouse Right, Middle, 4, 5, wheel up/down | 5 of 5 each |
| No feedback loop: trigger Left Mouse, action Left click | runs while held, exactly 1 run |
| Emergency stop Ctrl+Shift+F12 with a button held | stopped in **3.5 ms**, disarmed, `VK_LBUTTON` up |
| No stuck keys: stop in the middle of a Ctrl+A hold | Ctrl was down, released after the stop |
| Foreground application filter | other app in front: paused, 0 clicks; target in front: 40 clicks |
| MAX for 3 s: UI stays responsive | 4979 CPS, 14 936 of 14 936 received; UI round trip p99 **0.5 ms**, max 2.8 ms |
| Idle (armed, window visible) | **0.0021 core** (0.21% of one core) |
| Profiles and settings saved and reloaded (incl. language, always on top) | ok |
| Binding: key (F14), mouse button, Ctrl+Shift+K, modifier alone, emergency stop, cancel | all captured as expected |
| Every page, tab and modal renders, UI thread stays alive | 9 of 9 pages |
| Tray: icon exists, minimize hides, restore shows the window | ok |
| Resources after all tests | +5 MB, +18 handles (fonts, windows), threads within the pool |

Also verified by hand on the release build: settings, profiles, language, window size and always-on-top survive a
restart; a second launch hands over to the running instance; clicking through the real UI (binding by click and key
press, mode switch, "More", Settings, Advanced, the profile manager, ARM/ARMED, speed presets) behaves as designed.

**Stuck-key protection when the process is killed** (`InfinityClicker.exe --kill-test`): the process holds Left Shift down
and terminates itself with `TerminateProcess`, without any cleanup. Result: Shift was down before the kill and was
**released by the guardian process** afterwards.

Bugs found and fixed while testing:

- Numpad 7 arrived as Home: with NumLock off, scan code 0x47 is Home. Numpad digits are now injected by virtual key.
- The measured CPS of short runs was N divided by the run time, giving 11.1 instead of 10. It is now
  (N-1) intervals between the first and the last action.

## Reproducing

```bat
InfinityClicker.exe --bench            :: full run (~12 min), report in benchmarks\bench_<date>.md
InfinityClicker.exe --bench quick      :: quick run (~3 min)
InfinityClicker.exe --bench quick only=sys,timer,cps
InfinityClicker.exe --autotest         :: functional tests (~30 s)
```

Do not touch the mouse or keyboard while they run. Close programs that install global hooks (Discord, overlays) if
you want the highest throughput.
