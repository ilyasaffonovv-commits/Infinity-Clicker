
# Infinity Clicker benchmark results

Mode: quick. Produced by `InfinityClicker.exe --bench` - raw output, no manual edits.


## 1. Test system

| Property | Value |
|---|---|
| Date | 2026-09-25 16:54 |
| OS | Windows 11 10.0 build 26200.9550 (25H2) |
| CPU | AMD Ryzen AI 9 HX 370 w/ Radeon 890M |
| Logical processors | 24 |
| QPC frequency | 10000000 Hz (100.0 ns/tick) |
| Timer resolution (NtQueryTimerResolution) | coarsest 15.625 ms, finest 0.500 ms, current 1.000 ms |
| Power source | AC |
| Build | MSVC 195136257, x64 |


## 2. Timer / sleep precision (actual wait vs requested)

Each row: N waits of the requested length on a thread at HIGHEST priority. `error` = actual - requested (microseconds). CPU = thread CPU time / wall time for that row (100% = one core busy).

NtSetTimerResolution is undocumented and is used here only as a research data point - Infinity Clicker itself only uses documented APIs.

| Method | Requested | mean error | p50 error | p99 error | max error | CPU |
|---|---|---|---|---|---|---|
| Sleep() default resolution | 1000 us | 14438.1 | 14586.5 | 15125.4 | 15125.4 | 0% |
| Sleep() default resolution | 2000 us | 13469.1 | 13536.7 | 14145.0 | 14145.0 | 0% |
| Sleep() default resolution | 5000 us | 10494.4 | 10554.6 | 11131.8 | 11131.8 | 0% |
| Sleep() + timeBeginPeriod(1) | 1000 us | 934.8 | 1002.1 | 1507.5 | 1507.5 | 0% |
| Sleep() + timeBeginPeriod(1) | 2000 us | 811.8 | 996.7 | 1070.9 | 1070.9 | 0% |
| Sleep() + timeBeginPeriod(1) | 5000 us | 655.9 | 511.0 | 1516.0 | 1516.0 | 0% |
| std::this_thread::sleep_for | 100 us | 15261.5 | 15423.5 | 16017.7 | 16180.4 | 0% |
| std::this_thread::sleep_for | 500 us | 14991.3 | 15037.3 | 15588.1 | 15589.7 | 0% |
| std::this_thread::sleep_for | 1000 us | 14514.7 | 14525.2 | 15130.5 | 15933.4 | 0% |
| std::this_thread::sleep_for | 2000 us | 13497.8 | 13499.9 | 14402.8 | 14533.1 | 0% |
| std::this_thread::sleep_for | 5000 us | 10517.9 | 10514.6 | 11281.8 | 11346.8 | 0% |
| Waitable timer (legacy) | 100 us | 15175.2 | 15209.3 | 15747.0 | 15840.8 | 0% |
| Waitable timer (legacy) | 500 us | 15012.5 | 15019.8 | 15818.0 | 16051.4 | 0% |
| Waitable timer (legacy) | 1000 us | 14570.3 | 14529.5 | 15113.8 | 15114.6 | 0% |
| Waitable timer (legacy) | 2000 us | 13459.1 | 13446.4 | 14116.4 | 14137.1 | 0% |
| Waitable timer (legacy) | 5000 us | 10552.4 | 10475.4 | 11568.1 | 11746.7 | 0% |
| Waitable timer HIGH_RESOLUTION | 100 us | 420.5 | 414.8 | 549.5 | 561.3 | 1% |
| Waitable timer HIGH_RESOLUTION | 500 us | 499.3 | 506.6 | 661.5 | 773.6 | 0% |
| Waitable timer HIGH_RESOLUTION | 1000 us | 526.5 | 517.1 | 687.6 | 869.3 | 0% |
| Waitable timer HIGH_RESOLUTION | 2000 us | 458.4 | 508.2 | 637.2 | 655.1 | 0% |
| Waitable timer HIGH_RESOLUTION | 5000 us | 302.9 | 314.0 | 896.4 | 925.1 | 0% |
| HIGH_RESOLUTION + timeBeginPeriod(1) | 100 us | 427.4 | 422.0 | 549.9 | 576.5 | 1% |
| HIGH_RESOLUTION + timeBeginPeriod(1) | 500 us | 562.3 | 509.6 | 990.9 | 1007.5 | 0% |
| HIGH_RESOLUTION + timeBeginPeriod(1) | 1000 us | 519.9 | 513.4 | 624.8 | 639.8 | 0% |
| HIGH_RESOLUTION + timeBeginPeriod(1) | 2000 us | 426.9 | 505.0 | 659.7 | 762.3 | 0% |
| HIGH_RESOLUTION + timeBeginPeriod(1) | 5000 us | 357.7 | 392.3 | 661.7 | 832.7 | 0% |
| HIGH_RESOLUTION + NtSetTimerResolution(0.5ms) | 100 us | 399.2 | 399.5 | 536.8 | 581.1 | 1% |
| HIGH_RESOLUTION + NtSetTimerResolution(0.5ms) | 500 us | 545.6 | 528.6 | 728.8 | 768.7 | 0% |
| HIGH_RESOLUTION + NtSetTimerResolution(0.5ms) | 1000 us | 477.0 | 509.7 | 658.2 | 952.4 | 0% |
| HIGH_RESOLUTION + NtSetTimerResolution(0.5ms) | 2000 us | 495.6 | 508.9 | 659.4 | 696.2 | 0% |
| HIGH_RESOLUTION + NtSetTimerResolution(0.5ms) | 5000 us | 378.4 | 492.1 | 656.6 | 665.1 | 0% |
| Hybrid STANDARD (timer+spin) | 100 us | 0.1 | 0.0 | 0.4 | 0.8 | 100% |
| Hybrid STANDARD (timer+spin) | 500 us | 0.1 | 0.0 | 0.2 | 0.5 | 100% |
| Hybrid STANDARD (timer+spin) | 1000 us | 0.1 | 0.0 | 0.4 | 0.5 | 100% |
| Hybrid STANDARD (timer+spin) | 2000 us | 0.2 | 0.2 | 0.5 | 0.6 | 34% |
| Hybrid STANDARD (timer+spin) | 5000 us | 0.4 | 0.3 | 0.7 | 1.0 | 10% |
| Hybrid ULTRA (timer+spin) | 100 us | 0.0 | 0.0 | 0.1 | 0.2 | 100% |
| Hybrid ULTRA (timer+spin) | 500 us | 0.0 | 0.0 | 0.2 | 0.2 | 100% |
| Hybrid ULTRA (timer+spin) | 1000 us | 0.0 | 0.0 | 0.1 | 0.1 | 100% |
| Hybrid ULTRA (timer+spin) | 2000 us | 0.2 | 0.2 | 0.5 | 0.7 | 28% |
| Hybrid ULTRA (timer+spin) | 5000 us | 0.2 | 0.2 | 0.7 | 0.7 | 14% |
| Pure QPC spin | 100 us | 0.0 | 0.0 | 0.1 | 0.2 | 100% |
| Pure QPC spin | 500 us | 0.5 | 0.1 | 0.1 | 25.8 | 100% |
| Pure QPC spin | 1000 us | 0.1 | 0.1 | 0.1 | 0.2 | 100% |
| Pure QPC spin | 2000 us | 0.1 | 0.1 | 0.1 | 0.2 | 100% |
| Pure QPC spin | 5000 us | 0.1 | 0.1 | 0.2 | 0.3 | 100% |


## 5. CPS matrix (full engine path: timeline -> waiter -> SendInput -> test pad)

Left click, down time 0 (DOWN+UP in one SendInput call). `actual` = actions generated / run time; `received` = WM_LBUTTONDOWN messages the pad processed / same run time; `err` = |mean interval - target| / target; `jitter` = standard deviation of the interval between consecutive actions; `late` = fire time - scheduled deadline. CPU in cores (1.00 = one logical CPU fully busy) for the whole process.

| Target | Precision | Run s | Actual CPS | Received CPS | err | jitter us | late p50 us | late p99 us | late max us | missed | failed calls | SendInput p50/p99 us | CPU cores | sched cores |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 1 | STANDARD | 3.00 | 1.00 | 1.00 | 0.00% | 42.9 | 0.4 | 94.2 | 91.0 | 0 | 0 | 410 / 3277 | 0.01 | 0.00 |
| 5 | STANDARD | 2.80 | 5.00 | 5.00 | 0.00% | 10.7 | 0.4 | 43.0 | 41.7 | 0 | 0 | 442 / 623 | 0.01 | 0.00 |
| 10 | STANDARD | 1.90 | 10.00 | 10.00 | 0.00% | 77.4 | 0.5 | 221.2 | 229.2 | 0 | 0 | 442 / 475 | 0.01 | 0.00 |
| 20 | STANDARD | 1.95 | 20.00 | 20.00 | 0.00% | 8.1 | 0.4 | 51.2 | 51.6 | 0 | 0 | 508 / 819 | 0.02 | 0.01 |
| 50 | STANDARD | 1.98 | 50.00 | 50.01 | 0.00% | 9.6 | 0.2 | 38.9 | 41.8 | 0 | 0 | 311 / 688 | 0.04 | 0.03 |
| 100 | STANDARD | 1.49 | 100.00 | 100.00 | 0.00% | 1.5 | 0.1 | 3.7 | 17.4 | 0 | 0 | 205 / 557 | 0.08 | 0.07 |
| 250 | STANDARD | 1.50 | 250.00 | 250.07 | 0.00% | 42.8 | 0.1 | 204.8 | 224.2 | 0 | 0 | 188 / 819 | 0.08 | 0.05 |
| 500 | STANDARD | 1.50 | 499.91 | 500.03 | 0.02% | 111.0 | 38.9 | 442.4 | 662.0 | 0 | 0 | 139 / 623 | 0.07 | 0.04 |
| 1000 | STANDARD | 1.50 | 1000.23 | 1000.52 | 0.02% | 238.2 | 237.6 | 753.7 | 1622.4 | 0 | 0 | 127 / 623 | 0.10 | 0.03 |
| 2000 | STANDARD | 1.50 | 1999.92 | 2000.49 | 0.00% | 290.8 | 139.3 | 884.7 | 7341.3 | 0 | 0 | 139 / 688 | 0.23 | 0.11 |
| 5000 | STANDARD | 1.50 | 5000.26 | 5014.44 | 0.01% | 95.3 | 0.0 | 507.9 | 4511.6 | 0 | 0 | 94 / 557 | 0.67 | 0.45 |
| 10000 | STANDARD | 1.65 | 9078.13 | 6580.17 | 10.15% | 300.7 | 32505.9 | 159383.6 | 153087.1 | 0 | 0 | 819 / 1507 | 0.36 | 0.12 |
| 20000 | STANDARD | 1.62 | 9235.12 | 6781.26 | 116.56% | 294.3 | 113246.2 | 243269.6 | 249914.9 | 15017 | 0 | 819 / 1507 | 0.37 | 0.12 |
| 50000 | STANDARD | 1.54 | 9289.41 | 6852.73 | 438.25% | 292.6 | 121634.8 | 243269.6 | 249927.0 | 62587 | 0 | 819 / 1507 | 0.37 | 0.12 |
| 20 | ECO | 1.95 | 20.00 | 20.00 | 0.00% | 321.5 | 278.5 | 1245.2 | 1243.4 | 0 | 0 | 410 / 885 | 0.01 | 0.00 |
| 20 | ULTRA | 1.95 | 20.00 | 20.01 | 0.02% | 57.7 | 0.3 | 376.8 | 365.3 | 0 | 0 | 344 / 819 | 0.03 | 0.02 |
| 100 | ECO | 1.49 | 99.99 | 100.00 | 0.01% | 172.8 | 311.3 | 622.6 | 656.2 | 0 | 0 | 279 / 688 | 0.02 | 0.01 |
| 100 | ULTRA | 1.49 | 100.00 | 100.04 | 0.00% | 4.6 | 0.1 | 25.6 | 43.0 | 0 | 0 | 254 / 950 | 0.09 | 0.08 |
| 1000 | ECO | 1.50 | 999.93 | 1000.56 | 0.01% | 180.8 | 311.3 | 688.1 | 1007.0 | 0 | 0 | 127 / 557 | 0.09 | 0.03 |
| 1000 | ULTRA | 1.50 | 1000.09 | 1000.32 | 0.01% | 3.7 | 0.0 | 0.1 | 141.6 | 0 | 0 | 111 / 557 | 0.93 | 0.88 |
| 5000 | ECO | 1.50 | 5000.76 | 5009.39 | 0.02% | 225.1 | 110.6 | 753.7 | 1255.1 | 0 | 0 | 156 / 688 | 0.31 | 0.09 |
| 5000 | ULTRA | 1.50 | 5000.12 | 5003.57 | 0.00% | 87.8 | 0.0 | 475.1 | 1479.0 | 0 | 0 | 111 / 557 | 0.63 | 0.39 |
| MAX (batch 1) | - | 1.50 | 6330.77 | 6252.07 | - | 65.8 | - | - | - | 0 | 0 | 139 / 442 | 0.48 | 0.15 |
| MAX (batch 8) | - | 1.50 | 9253.56 | 6948.58 | - | 24.5 | - | - | - | 0 | 0 | 819 / 1507 | 0.37 | 0.12 |
| MAX (batch 64) | - | 1.57 | 1609.64 | 1464.42 | - | 1279.9 | - | - | - | 0 | 0 | 6029 / 285213 | 0.07 | 0.02 |

Same engine path, but with Infinity Clicker's own WH_MOUSE_LL hook installed (what happens when the trigger is a mouse button) and MAX mode with hook-based backpressure:

| Target | Hook | Actual CPS | Received CPS | jitter us | late p99 us | hook saw events | CPU cores |
|---|---|---|---|---|---|---|---|
| 1000 | WH_MOUSE_LL | 999.86 | 1000.38 | 305.9 | 1245.2 | 3000 | 0.13 |
| 5000 | WH_MOUSE_LL | 5000.13 | 4999.69 | 302.5 | 8912.9 | 15000 | 0.50 |
| 20000 | WH_MOUSE_LL | 5113.18 | 5107.57 | 527.4 | 243269.6 | 17214 | 0.47 |
| MAX, no backpressure | WH_MOUSE_LL | 5171.98 | 5166.80 | 36.0 | 0.0 | 15504 | 0.47 |
| MAX, backpressure 2000 | WH_MOUSE_LL | 5081.93 | 5077.16 | 33.7 | 0.0 | 15232 | 0.47 |

Total benchmark time: 74.5 s

