# Anti-Productivity Bubbles Manual (English)

[日本語](README.ja.md)

## Overview

**Anti-Productivity Bubbles** (hereafter **APB**) is the world's first (probably) bubble overlay engineered to lower your productivity. The name says it all: bubbles, against productivity.

These days AI writes your code, drafts your emails, and probably does your laundry too. Everything is getting more efficient, and frankly, it's getting out of hand. So we asked ourselves: *what if we deliberately made it worse?* The answer is a swarm of wobbling bubbles that float lazily across your screen, warp whatever you are trying to read, and make you stop and think: *"...wow, that one's pretty."* Efficiency: down. Serenity: up. Deadline: not our problem.

Technically: it is a Windows desktop overlay that shows soap bubbles slowly rising from the bottom of the screen to the top. Inside each bubble, the desktop behind it appears distorted as if by refraction. Bubbles are shown on every connected monitor, and they are click-through, so you can still work (just worse).

*Author: DoraNeko. Side effects may include staring at bubbles for 20 minutes. Not responsible for missed meetings.*

## Requirements

- Windows 10 (version 1903 or later) or Windows 11 (64-bit, x64)
- A Direct3D 11 capable GPU
- No extra runtime needs to be installed.
- Tested on Windows 11 (x64). ARM64 and older Windows versions are untested.

## Usage

1. Extract the zip anywhere (keep `APB.exe` and `settings.ini` in the same folder).
2. Double-click `APB.exe` to start.
3. Press the **Esc key** to exit (default). You can also exit from the tray icon.
4. Only one instance can run; starting a second one shows a message.

> **Emergency exit:** pressing `Esc` + `Left Shift` + `Left Ctrl` together always exits, regardless of settings.

Bubbles are click-through, so you can keep using other applications normally while they are shown.

## Number of bubbles

The default is **15 bubbles per monitor**.

| Monitors | Bubbles shown |
|---|---|
| 1 | 15 |
| 2 | 30 |
| 3 | 45 |

## Tray menu

Click the bubble icon in the notification area to open this menu.

| Item | Description |
|---|---|
| Pause / Resume | Hides or shows the bubbles |
| Reload settings | Re-reads `settings.ini` |
| Open settings file | Opens `settings.ini` in your default editor |
| Run at Windows startup | Toggles autostart at sign-in (current user Run registry key) |
| Exit | Quits the app |

## Settings

Edit `settings.ini` in the same folder as `APB.exe` with any text editor. **Changes are applied automatically about a second after you save** (no restart needed). Missing values fall back to the defaults. Invalid values also fall back to the defaults and a message is shown.

| Section | Key | Default | Description |
|---|---|---|---|
| Bubbles | Count | 15 | Bubbles per monitor (1-64) |
| Bubbles | DiameterMin / DiameterMax | 200 / 300 | Bubble diameter in pixels, chosen randomly in this range |
| Bubbles | SpeedMin / SpeedMax | 30 / 60 | Rising speed in pixels per second |
| Sway | AmplitudeMin / AmplitudeMax | 24 / 48 | Horizontal sway width in pixels |
| Sway | RateMin / RateMax | 0.35 / 0.7 | Sway speed in radians per second (higher = faster wobble) |
| Refraction | Strength | 0.55 | Refraction strength (0-3). 0 = none, higher = stronger distortion |
| Input | ExitKey | Esc | Exit key |
| Power | OnBattery | Normal | Behavior on battery: `Normal` (no change) / `Reduce` (lower frame rate) / `Pause` (hide bubbles) |
| Power | BatteryFps | 30 | Frame rate used when `Reduce` is set (5-60) |
| Monitor*n* | Enabled / Count | on / global Count | Per-monitor settings (see below) |

### Per-monitor settings

Create a section such as `[Monitor2]` using the number shown in Windows Settings > System > Display > Identify (the n of `\\.\DISPLAYn`).

```ini
; Enabled=0 hides bubbles on this monitor
; Count overrides [Bubbles] Count for this monitor
[Monitor2]
Enabled=1
Count=10
```

> INI comments must start at the beginning of a line with `;`. Do not put a comment after a value.
### Exit key

- Use a key name, e.g. `Esc`, `Q`, `F12`, `Space`, `Enter`, `Pause`.
- A hexadecimal virtual-key code such as `0x1B` also works.
- Join keys with `+` for a key combination. The last key triggers the exit; the preceding keys must be held down.

```ini
ExitKey=LShift+Esc
ExitKey=Ctrl+Alt+Q
```

Modifier keys: `Shift` / `LShift` / `RShift`, `Ctrl` / `LCtrl` / `RCtrl`, `Alt` / `LAlt` / `RAlt`, `Win`. Names without a side (such as `Shift`) accept either the left or right key.

> **Note:** Remember the exit key you set. An unrecognized key name falls back to `Esc`. If you cannot exit, end `APB.exe` from Task Manager.

## Troubleshooting

| Symptom | What to do |
|---|---|
| An error appears right after launch | Check that your Windows version is supported. Details are written to `%LOCALAPPDATA%\APB\APB.log` |
| Nothing is shown | Bubbles enter from the bottom, so wait a few seconds. If still nothing, check `Count` and `DiameterMin` |
| Performance is poor | Lower `Count` or `DiameterMax`. More monitors means more load |
| Settings are not applied | Make sure you edited the `settings.ini` next to the exe, or use "Reload settings" in the tray menu. Mistakes are recorded in `APB.log` |
| An "invalid settings" message appears | Fix the listed items. Until then those items use their defaults |
| Antivirus or SmartScreen shows a warning | The app is not code-signed. Compare the SHA-256 on the download page with the hash of the zip you downloaded (e.g. with `Get-FileHash`) |
| Not visible over full-screen games or videos | Exclusive full-screen apps may hide the overlay. Try windowed or borderless mode |

## Known limitations

- If the monitor layout or resolution changes, the app reinitializes itself after about a second. If that fails, it shows an error and exits.
- Applying new settings restarts the bubble layout.
- While an app running as administrator is in the foreground, the exit key may not be detected (you can still exit from the tray menu).

## Privacy

The app uses the Windows screen-capture feature, so the whole screen is continuously captured. Captured data is used only in memory to compute the bubble refraction and is **never saved or sent anywhere**. The app makes no network connections. The only thing written is an activity log (`%LOCALAPPDATA%\APB\APB.log`, containing startup and settings information only).

## License

MIT License. See the bundled `LICENSE`. The source code is public.
