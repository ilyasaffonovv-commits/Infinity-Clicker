# Infinity Clicker functional acceptance tests

Produced by `InfinityClicker.exe --autotest` (real app + UI, isolated data folder, simulated physical input).

| Result | Test | Details |
|---|---|---|
| PASS | HOLD: keyboard trigger (F13) held 500 ms at 100 CPS | 51 downs / 51 ups received, stopped 0.6 ms after release |
| PASS | HOLD: release stops immediately | 0.59 ms |
| PASS | TOGGLE: mouse trigger (Mouse Button 4) on/off | running after 1st press: yes, 61 clicks in ~300 ms at 200 CPS |
| PASS | BURST 10 | 10 clicks |
| PASS | FIXED COUNT 25 | 25 clicks |
| PASS | DURATION 500 ms at 200 CPS | 100 clicks (ideal 100) |
| PASS | RATE 10 CPS for 1 s (target vs actual) | received 10, engine actual 10.00 CPS, interval sd 225.3 us |
| PASS | RATE 50 CPS for 1 s (target vs actual) | received 50, engine actual 50.00 CPS, interval sd 13.6 us |
| PASS | RATE 1000 CPS for 1 s (target vs actual) | received 1000, engine actual 1000.04 CPS, interval sd 229.3 us |
| PASS | KEY action 'A' x20 (scan-code injection) | 20 key-downs with vk=A scan=0x1E, 20 key-ups |
| PASS | COMBO Ctrl+Shift+K x5, modifiers released afterwards | 5 K presses seen with Ctrl+Shift held |
| PASS | KEY action F5 x3 | 3 received |
| PASS | KEY action Space x3 | 3 received |
| PASS | KEY action Up x3 | 3 received |
| PASS | KEY action Numpad 7 x3 | 3 received |
| PASS | KEY action . > x3 | 3 received |
| PASS | MOUSE action Right Mouse x5 | 5 received |
| PASS | MOUSE action Middle Mouse x5 | 5 received |
| PASS | MOUSE action Mouse Button 4 x5 | 5 received |
| PASS | MOUSE action Mouse Button 5 x5 | 5 received |
| PASS | MOUSE action Wheel Up x5 | 5 received |
| PASS | MOUSE action Wheel Down x5 | 5 received |
| PASS | NO FEEDBACK LOOP: Left Mouse trigger + Left click action | kept running while held: yes, runs started: 1 (must be 1), clicks 41 |
| PASS | EMERGENCY STOP (Ctrl+Shift+F12) while button held | stopped in 3.0 ms, disarmed: yes, VK_LBUTTON up |
| PASS | NO STUCK KEYS: stop during Ctrl+A hold | Ctrl was down mid-action: yes; after stop Ctrl/A: released |
| PASS | ACTIVE WINDOW FILTER | wrong app in front: paused=yes, 0 clicks; target in front: 40 clicks |
| PASS | MAX mode 3 s: GUI stays responsive | 4546 CPS: 13632 clicks generated, 13632 received by the pad; UI message round-trip p99 0.6 ms, max 2.2 ms (155 pings) |
| PASS | IDLE CPU (armed, UI visible) | 0.0025 cores = 0.245% of one core / 0.0102% of the machine |
| PASS | PROFILES & SETTINGS saved and reloaded | reloaded 'AutoTest': 77.5 CPS, action Alt + F24, emergency Ctrl + Pause |
| PASS | BIND: keyboard key (F14) as trigger | captured: F14 |
| PASS | BIND: mouse button (Mouse Button 4) as action | captured: Mouse Button 4 |
| PASS | BIND: combination Ctrl+Shift+K | captured: Ctrl + Shift + K |
| PASS | BIND: modifier alone (Right Ctrl) | captured: Right Ctrl |
| PASS | BIND: emergency stop (Ctrl+Alt+F11) | captured: Ctrl + Alt + F11 |
| PASS | BIND: cancel leaves the binding unchanged | still Right Ctrl |
| PASS | UI: all pages, tabs and modals render without a hang | 9 of 9 pages produced a frame, UI thread alive: yes |
| PASS | TRAY: icon present, minimize hides to tray, restore shows the window | icon at start: yes, hidden after minimize: yes, icon while hidden: yes, visible after restore: yes |
| PASS | RESOURCES after all tests (leak check) | private 63.9 -> 68.9 MB, handles 642 -> 655, threads 90 -> 91, GDI 18 -> 26 |

**38 passed, 0 failed**
