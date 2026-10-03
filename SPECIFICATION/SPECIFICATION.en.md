# Anti-Productivity Bubbles (APB) Specification

## 1. Purpose

Show multiple soap bubbles over the Windows desktop, each slowly floating from below the bottom edge of the screen to above the top edge. Inside each bubble the desktop behind it is refracted, and edge highlights and thin-film color shifts make it look like a transparent sphere.

This document records the implemented specification (Japanese version: [SPECIFICATION.ja.md](SPECIFICATION.ja.md)). Quality items not yet verified on real hardware are listed in "9. Items to verify".

## 2. Environment and usage

- An application running on the desktop of Windows 10/11 (an environment that supports Windows Graphics Capture).
- The currently displayed desktop is captured in real time as the background, and the bubbles are composited on top of it.
- All monitors are targeted. One overlay window is shown per monitor, and each monitor uses only its own screen content as the refraction background.
- In normal operation the overlay does not interfere with desktop interaction.

## 3. Functional requirements

### 3.1 Bubble display and movement

- Bubbles appear from below the bottom edge of the screen, move upward, and leave above the top edge.
- A bubble that has left through the top reappears below the bottom edge with a varied height, horizontal position and speed, which avoids a regular pattern.
- The default number of simultaneous bubbles is **15 per monitor** (1 monitor = 15, 2 = 30, 3 = 45).
- Defaults: diameter 250 px ±50 px (200–300 px), rise speed 30–60 px/s. Values are chosen randomly per bubble.
- At startup (including after a settings reload) every bubble waits outside the screen below the bottom edge and rises into view at staggered times. Horizontal positions are spread by dividing the screen into a grid of columns × rows, assigning each bubble to a shuffled cell and adding a small offset within the cell. The waiting height varies within about one screen height according to the cell row. The number of columns is computed from the aspect ratio and the bubble count (5 columns × 3 rows for 15 bubbles at 16:9).
- Horizontal sway is a sine wave with an amplitude of 24–48 px and an angular speed of 0.35–0.7 rad/s (random per bubble). The phase differs per bubble.
- The target frame rate is 60 FPS, with an upper limit of 120 FPS. The render timer interval is about 16 ms.

### 3.2 Appearance

- Bubbles are drawn as transparent spheres, not as opaque discs that hide the background.
- A thin reflective highlight runs along the rim, and a small light reflection is placed on part of the bubble.
- A subtle iridescent thin-film color is added to the rim and surface for a realistic look.
- No dense fill inside the bubble, no color cast over the whole screen, and no strong halo.
- Overlapping bubbles stay distinguishable and are composited so that they do not become excessively bright.

### 3.3 Background refraction

- Refraction is expressed by locally resampling the captured desktop image inside the bubble.
- The refraction amount is `local * radius * strength * sqrt(1 - r^2)`, varying smoothly from the center toward the rim.
- The default strength is 0.55 (strengthened from the initial 0.18). It can be changed between 0 and 3 in the settings file.
- The background outside a bubble is not modified.
- Sampling near the screen edges is clamped to the screen so that the image does not wrap around from the opposite side.
- Self-feedback, where the overlay appears in its own capture, is prevented with `WDA_EXCLUDEFROMCAPTURE`.

### 3.4 Operation and exit

- The overlay does not take focus, and mouse input passes through to the applications underneath.
- The default exit key is Esc. It is detected with a low-level keyboard hook (`WH_KEYBOARD_LL`), so exiting works even when the overlay is not active.
- The exit key can be changed in the settings file, and combinations such as `LShift+Esc` or `Ctrl+Alt+Q` can be specified (see 3.5).
- On exit, the overlays of all monitors are closed.
- As an emergency exit, pressing `Esc` + `Left Shift` + `Left Ctrl` together is always enabled (independent of the `ExitKey` setting).
- Multiple instances are prevented with a named mutex. Starting a second instance shows a message and exits.

### 3.5 Settings file

Settings are in `settings.ini` (INI format) in the same folder as the executable. If the file is missing or an item is absent, the default is used. If a value is invalid, the default is also used, and a warning is shown and logged (see 3.8).

- The file is read at startup. While running, its modification time is checked about once per second; when a change is detected, the settings are reloaded automatically and the overlays of all monitors are rebuilt (bubble placement returns to the initial state). Settings can also be reloaded from the tray menu.
- Comments are only lines starting with `;` (comments after a value are not allowed).

| Section | Key | Default | Valid range / notes |
|---|---|---|---|
| Bubbles | Count | 15 | 1–64 (per monitor) |
| Bubbles | DiameterMin / DiameterMax | 200 / 300 | px. Minimum 10 |
| Bubbles | SpeedMin / SpeedMax | 30 / 60 | px/s. Minimum 0 |
| Sway | AmplitudeMin / AmplitudeMax | 24 / 48 | px. Minimum 0 |
| Sway | RateMin / RateMax | 0.35 / 0.7 | rad/s. Minimum 0 |
| Refraction | Strength | 0.55 | 0–3 |
| Input | ExitKey | Esc | See below |
| Power | OnBattery | Normal | `Normal` / `Reduce` / `Pause` (see 3.7) |
| Power | BatteryFps | 30 | 5–60. Frame rate used with `Reduce` |
| Monitor*n* | Enabled | 1 | 0 hides bubbles on that monitor |
| Monitor*n* | Count | (Bubbles/Count) | Bubble count for that monitor (1–64). If all monitors are disabled, a warning is shown and all monitors are used |

- If Min and Max are reversed, they are swapped automatically.
- `ExitKey` is a key name (`Esc`, `Q`, `F12`, `Space`, `Enter`, `Pause`, ...) or a hexadecimal virtual-key code such as `0x1B`.
- Joining keys with `+` makes a combination. The last key is the exit trigger; the preceding keys must be held down.
  - Modifier names: `Shift`/`LShift`/`RShift`, `Ctrl`/`LCtrl`/`RCtrl`, `Alt`/`LAlt`/`RAlt`, `Win`/`LWin`/`RWin`.
  - If a key name cannot be interpreted, the default Esc is used.
- The *n* in `Monitor<n>` is the display number shown by Windows (the n of the device name `\\.\DISPLAYn`).
- At build time, `settings.ini` is copied to the output folder (only if the source is newer).

### 3.6 Tray icon

An icon is shown in the notification area, and clicking it opens a menu. Items: pause/resume, reload settings, open settings file, run at Windows startup (registers/removes `HKCU\...\Run`), and exit. The menu language follows the OS UI language (Japanese, otherwise English).

### 3.7 Power saving

With `OnBattery` set to `Reduce`, rendering is throttled to `BatteryFps` while on battery. With `Pause`, the overlays are hidden and rendering stops while on battery. When AC power returns, normal operation resumes automatically. The power state is checked about once per second. This feature does not affect what the shader draws.

### 3.8 Error reporting and log

- If a setting value is invalid, the affected item is reported (a message box at startup, a tray notification on reload) and that item runs with its default.
- An activity log is written to `%LOCALAPPDATA%\APB\APB.log` (overwritten on each launch). It contains only startup, monitor configuration, settings warnings and errors; screen content is never logged.

### 3.9 Display configuration changes

On `WM_DISPLAYCHANGE`, the overlays and captures of all monitors are rebuilt after waiting about one second. On failure it retries up to 3 times at 1.5-second intervals; if it still fails, an error is shown and the app exits.

## 4. Implementation approach

- C++17 / Win32, Direct3D 11, HLSL Shader Model 5.0. MSVC v143, Windows SDK 10.0.26100.0.
- Shaders are compiled with fxc at build time and embedded in the exe as headers (bytecode arrays). The C++ runtime is statically linked (`/MT`), so no additional runtime is required on the target machine.
- A hidden control window handles the render timer, the tray icon and message processing. Overlay windows are destroyed and recreated when monitors are rebuilt.
- Windows Graphics Capture (`CreateForMonitor`) captures the desktop per monitor, and the pixel shader resamples the background and composites the bubbles.
- The D3D11 device, shaders, sampler, blend state, rasterizer state and constant buffer are shared by all monitors. Render targets, staging textures and DIBs are per monitor.
- Bubbles are drawn with instancing (`DrawInstanced(6, count)`). The bubble array in the shader constant buffer is fixed at 64 entries; the actual count is set by the settings.
- The render result is read back to the CPU through a STAGING texture, written to a DIB, and displayed with `UpdateLayeredWindow` (ULW_ALPHA, premultiplied alpha).
- Window styles: `WS_EX_LAYERED | TOPMOST | NOACTIVATE | TRANSPARENT | TOOLWINDOW`.
- The process is Per-Monitor DPI Aware V2 and renders in physical pixels of each monitor.
- A single timer updates and draws all monitors in turn. The elapsed time per frame is capped at 0.1 s.
- `D3D11_CULL_NONE` is used so that back-face culling does not make bubbles disappear.

## 5. File layout

| File | Contents |
|---|---|
| `src\Main.cpp` | Application body (settings, capture, rendering, windows, exit key handling) |
| `src\BubbleVS.hlsl` / `src\BubblePS.hlsl` | Vertex and pixel shaders |
| `src\APB.rc` / `src\APB.ico` | Version info and icon resources |
| `APB.vcxproj` | Project |
| `settings.ini` | Settings file |
| `README.md` | Short description (English) |
| `README\README.ja.md` / `README\README.en.md` | Manuals (Japanese / English) |
| `SPECIFICATION\SPECIFICATION.ja.md` / `SPECIFICATION\SPECIFICATION.en.md` | Specification (Japanese / English; this document) |
| `LICENSE` | MIT License |
| `CHANGELOG.md` | Change log |
| `scripts\package.ps1` | Script that creates the release zip and SHA-256 checksum (output to `dist\`) |
| `.gitignore` | Excludes build artifacts, etc. |

At run time, place `APB.exe` and `settings.ini` in the same folder (the app also works with defaults if `settings.ini` is missing).

## 5.1 Distribution and license

- Source code and executables are all published and distributed free of charge. The license is the MIT License (copyright: DoraNeko).
- The release is `APB-v<version>.zip` (exe, settings.ini, README folder, LICENSE, CHANGELOG.md). SHA-256 is written to `SHA256SUMS.txt`. The code is not signed.
- The version is managed in `kAppVersion` in `src\Main.cpp`, `src\APB.rc`, and the default in `scripts\package.ps1`.
- Privacy: screen captures are used only in memory and are never saved or sent. No network communication is performed.

## 6. Performance and quality acceptance criteria

- Bubbles keep moving from bottom to top without interruption while the desktop stays visible.
- Bubbles partly off-screen are not cut off unnaturally at the edges, and their entry and exit are visible.
- The background is clearly distorted inside bubbles, with no unintended distortion outside.
- No self-feedback, color cast or noticeable flicker.
- Mouse and normal keyboard operation continue to work in the applications underneath.
- With 15 bubbles per monitor, aim for 60 FPS. CPU readback load grows with the number of monitors.

## 7. Out of scope (current version)

- A GUI such as a settings screen. Settings are edited in the file only.
- Per-monitor settings other than bubble count and enable/disable, and bubbles spanning multiple monitors.
- An installer.
- Automatically hiding when a full-screen app is detected.
- Physics simulation, bubble-to-bubble collisions, reactions to wind or user input.
- Code signing. ARM64 and older Windows support (only Windows 11 x64 has been tested).

## 8. References

- The author's own earlier raindrop overlay demo (C++ / D3D11 / HLSL) was used as a reference for screen capture, background texture input and transparent overlay techniques.

## 9. Items to verify

- FPS and CPU/GPU load with three or more monitors (60 FPS is a target, not guaranteed, because of the CPU readback).
- Behavior on high-resolution and high-refresh-rate monitors.
- Load with a large bubble count (up to 64).
- Display and exit-key behavior over full-screen apps (games, exclusive full-screen video).
- Key detection while an app running with administrator privileges is in the foreground.
