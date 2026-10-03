# Changelog

## 1.0.0

- Rising, refracting soap bubbles overlaid on every monitor (15 per monitor by default).
- `settings.ini`: bubble count, diameter, speed, sway, refraction strength, exit key (combinations such as `LShift+Esc`), battery behavior, per-monitor overrides.
- Settings are reloaded automatically when the file is saved.
- Emergency exit: Esc + Left Shift + Left Ctrl always exits.
- Tray icon: pause/resume, reload settings, open settings file, run at Windows startup, exit.
- Automatic re-initialization when the monitor configuration changes.
- Invalid settings are reported (message / tray balloon) and logged to `%LOCALAPPDATA%\APB\APB.log`.
- Single instance. Shaders are embedded and the C++ runtime is statically linked, so the exe runs standalone.
- Bubbles start below the screen and rise into view when the app starts.
- Product name: Anti-Productivity Bubbles (APB). Author: DoraNeko.
