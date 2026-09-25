# Changelog

## 1.0.0

First public release.

- Native Win32 / Direct3D 11 / Dear ImGui application, single portable exe.
- Compact main screen: trigger, action, mode, speed and one ARM button. Advanced settings, diagnostics
  and the Input Lab are on a separate page. English and Russian interface (follows the Windows language).
- Modes: Hold, Toggle, Burst, Fixed count, Duration, and an experimental MAX speed mode.
- Any key, mouse button, wheel notch or combination as trigger and as action; binding by pressing the key.
- Timing engine: QueryPerformanceCounter timeline with absolute deadlines, high-resolution waitable
  timer plus a short spin near the deadline (ECO / STANDARD / ULTRA), adaptive batching when SendInput
  falls behind.
- Trigger engine on low-level keyboard/mouse hooks (or Raw Input for mouse triggers); the app's own
  input is tagged and never triggers itself. Emergency stop hotkey.
- Held keys and buttons are released on stop, exit, crash, and by a watchdog process if the app is killed.
- Optional restriction to a foreground application.
- Profiles stored as JSON, tray icon, optional start with Windows.
- Telemetry (requested / accepted / observed / received events, interval jitter, lateness, time spent
  inside SendInput), a built-in test pad and a benchmark suite (`--bench`), functional tests (`--autotest`).
