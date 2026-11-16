# openrazer-win32-deathadder-elite (`da-elite.exe`)

A minimal, pure Win32 C++23 utility for configuring a **Razer DeathAdder Elite** (`1532:005C`) on Windows 11 without installing Razer Synapse.

Based on the USB HID protocol reverse-engineered by [OpenRazer](https://github.com/openrazer/openrazer) and the user-space Windows HID approach from [openrazer-win](https://github.com/Ar4ikov/openrazer-win).

## Features

- **Polling Rate:** `125 Hz`, `500 Hz`, or `1000 Hz`.
- **DPI Adjustment:** `100–16000` DPI (linked X/Y or independent X/Y), quick presets, and customizable DPI stages.
- **Button Assignment:**
  - `Middle Click`, `Side Back (X1)`, `Side Forward (X2)` via a conditional low-level mouse hook (`WH_MOUSE_LL`, only installed when at least one of those buttons is remapped).
  - `DPI Up` / `DPI Down` (top buttons behind the scroll wheel) via **Driver Mode (`0x03`)**, allowing software DPI stage cycling, mouse button remaps, or keyboard/media shortcuts.
- **Auto-Restore:** Re-applies your saved `data\settings.json` on launch, USB plug/replug (`WM_DEVICECHANGE`), and sleep/wake (`WM_POWERBROADCAST`).
- **Tiny Footprint:** Pure Win32 controls + system tray service + one-shot CLI (`~700 KB` standalone binary, no Direct2D/Electron/Synapse bloat).

## Does It Need to Keep Running?

- **Polling rate & DPI only:** The DeathAdder Elite has **no onboard flash memory**. The mouse holds the polling rate and DPI you send in volatile RAM until it loses USB power (unplug, reboot, or sleep if USB power is cut). You can use the one-shot CLI (`--poll`, `--dpi`, `--apply`) and exit immediately, or leave `da-elite.exe` in the tray so settings are automatically restored on boot/wake/replug.
- **Button assignment:** Requires `da-elite.exe` to remain running in the tray.

## What Is "Driver Mode"?

Razer mice expose two firmware device modes (`0x00 / 0x04`):

- **Normal Mode (`0x00`):** The mouse firmware handles the top `DPI Up` / `DPI Down` buttons internally using its built-in default DPI steps.
- **Driver Mode (`0x03`):** Tells the mouse firmware to stop cycling DPI on those two buttons internally and instead send vendor HID reports (`Report ID 0x04`, codes `0x20` / `0x21`) to the PC so software can remap them.

`da-elite.exe` **only** enables Driver Mode when `DPI Up` or `DPI Down` has a non-default assignment, and automatically restores **Normal Mode (`0x00`)** on exit so the buttons return to normal hardware behavior.

## Usage

### GUI / System Tray

Run `da-elite.exe`:

- Opens the Win32 settings window (respects Windows 11 dark/light mode).
- Closing or clicking **Hide to Tray** keeps it running in the notification area with quick submenus for **Polling Rate**, **DPI Preset**, **Re-apply Settings**, and **Launch on Windows Startup**.
- Settings are saved next to the executable in `data\settings.json`.

### Launch on Windows Startup

Enabling **Launch on Windows startup** (either in the tray menu or by checking the box and clicking **Apply & Save** in Settings) writes:

```text
HKCU\Software\Microsoft\Windows\CurrentVersion\Run\da-elite = "<path>\da-elite.exe" --tray
```

Once saved, it appears in **Task Manager → Startup apps** and **Windows Settings → Apps → Startup** as **Razer DeathAdder Elite Controller** (`da-elite.exe`).

### CLI (One-Shot / Scripting)

```text
da-elite.exe                 Start tray service and open settings window
da-elite.exe --tray          Start silently in system tray
da-elite.exe --show          Open settings window on running instance
da-elite.exe --quit          Quit running tray instance
da-elite.exe --status        Query and print current mouse status, then exit
da-elite.exe --apply         Apply saved data\settings.json once, then exit
da-elite.exe --poll <hz>     Set polling rate (125 | 500 | 1000), save & exit
da-elite.exe --dpi <x> [y]   Set DPI (100..16000), save & exit
```

## Building

Requires CMake 3.31+, Ninja, and MSVC (C++23). Run from a Windows Developer Command Prompt or via `build.bat`:

```bat
build.bat
```

Outputs:
- `build\bin\da-elite.exe`
- `build\bin\da-elite-tests.exe`

Run tests:

```bat
ctest --test-dir build --output-on-failure
```
