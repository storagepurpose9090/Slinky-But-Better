# Slinky (Reconstructed) — But Better

A rebuilt, open-source version of the Slinky ghost client for **Minecraft 1.7.10 and 1.8.9**, with a new **Alt Manager** (cracked/offline usernames), a clean launcher/injector, and a full PvP combat module suite.

No license keys, no HWID checks, no telemetry — the original DRM/auth code was reconstructed for reference only and is **not** compiled into the client.

---

## What's inside

- **Alt Manager** — change your in-game username to any cracked/offline name on the fly.
- **Launcher / injector** (`SlinkyClient.exe`) — finds the game's JVM and injects the client, with a 5-second auto-close countdown.
- **Combat modules** — Aim Assist (Regular/Linear/Silent), Auto Clicker (Jitter/Butterfly), Velocity (4 modes), Reach, Criticals, Auto Block, Hitboxes, Backtrack, and more.
- **Visual / movement / world / utility modules** — 60+ modules total.
- **ClickGUI** — `Right Shift` to open, with a Profiles tab that now includes the MC Username card.

---

## Where it works

| | Supported |
|---|---|
| Minecraft | **1.7.10**, **1.8.9** |
| Java | **64-bit** JDK/JRE (the game must be running on a 64-bit JVM) |
| Launchers | Vanilla, Forge, OptiFine, Lunar, MCP |
| OS | Windows x64 |

The game must be **running** before you inject (or use `--launch` to start it for you).

---

## Quick start (no coding) — compiled build

A ready-to-run copy is in [`compiled-slinky/`](compiled-slinky/):

1. Download the whole `compiled-slinky` folder.
2. Keep `SlinkyClient.exe` and `libslinky.dll` in the **same folder**.
3. Start Minecraft 1.7.10 or 1.8.9 (64-bit Java) and get into the game.
4. Double-click `SlinkyClient.exe`.
5. Wait for `Injection successful` — the window closes by itself.
6. Press **Right Shift** in game to open the menu.

If Windows SmartScreen warns, choose **More info → Run anyway**.

### Alt Manager (cracked / offline username)

1. Open the ClickGUI (**Right Shift**) → **Profiles** tab.
2. At the top is the **MC Username** card.
3. Click the field, type a name, press **Enter** to apply, **Esc** to cancel.
4. Rules: **3–16 characters**, letters `A–Z a–z`, numbers `0–9`, and `_`.
5. It swaps the live session instantly (offline name, random offline UUID, empty token) — no restart needed.
6. Saved to `%USERPROFILE%\.slinky\account.txt` and re-applied on the next injection.

To go back to your normal online account, delete `%USERPROFILE%\.slinky\account.txt`. A premium session is never touched unless an alt name has been saved.

### Loader options

```
SlinkyClient.exe [options]

  --pid <id>           Inject into this process id.
  --process <name>     Target executable name (default: javaw.exe).
  --dll <path>         Client DLL (default: libslinky.dll next to this exe).
  --wait <seconds>     Wait for a ready Minecraft JVM (default: 60, 0 = no wait).
  --no-window          Do not require a visible game window.
  --launch <command>   Start a command line first, then inject.
  --list               List candidate Java processes and exit.
  --no-pause           Do not pause before exiting.
  --help               Show this help.
```

---

## Compiling from source

### Prerequisites

- **MinGW-w64 GCC** (C++20 capable) — e.g. MSYS2 UCRT64
- **CMake 3.20+** and **Ninja** (or MinGW Makefiles)
- **JDK 9+** (`javac` with `--release 8`)
- **Python 3.8+**
- Git (optional)

### Build

```powershell
cd openslinky-main/openslinky-main
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

Or simply run `build.bat` (does configure + build + copy into `dist\`).

### Outputs

- `build/libslinky.dll` — the client DLL (inject this).
- `build/SlinkyClient/SlinkyClient.exe` — the launcher/injector.
- `build/libslinky_vendor.a` — reference archive (verifies the reconstructed DRM code compiles; **never** linked into the client).

---

## Project layout

```
Slinky-But-Better/
├── compiled-slinky/            # ready-to-run loader + DLL (for non-coders)
│   ├── SlinkyClient.exe
│   └── libslinky.dll
├── openslinky-main/openslinky-main/
│   ├── src/                    # all client C++ source
│   │   ├── account/            # Alt Manager (Session swap)
│   │   ├── core/               # JVM attach, init, keybinds
│   │   ├── gui/                # ClickGUI + widgets (MC Username card)
│   │   ├── hooks/              # JNI hooks, packet bridge, GL passes
│   │   ├── minecraft/          # 419-slot mappings (1.7.10 & 1.8.9)
│   │   ├── modules/            # combat / movement / visual / world / utility
│   │   ├── events/             # event bus + Module base
│   │   ├── config/             # settings + profile manager
│   │   ├── commands/           # chat command processor
│   │   ├── util/               # math, RNG, input, filesystem
│   │   └── vendor/             # reconstructed DRM (reference only)
│   ├── launcher/main.cpp       # SlinkyClient.exe source
│   ├── java/                   # embedded hook classes
│   ├── assets/                 # fonts + logo
│   ├── third_party/            # jni, cjson, glad, nanovg, minhook, curl
│   ├── CMakeLists.txt
│   └── README.md               # deep technical notes
└── alt_test/                   # JNI test harness (mocks 1.7.10 / 1.8.9 / vanilla)
```

---

## Notes & disclaimer

- Provided for **educational / research** purposes. Use responsibly and at your own risk.
- The client is a reconstruction; the original vendor DRM/auth/telemetry code is isolated in `src/vendor/` and is **not** part of `libslinky.dll`.
- Keep `SlinkyClient.exe` and `libslinky.dll` together, or pass `--dll` with a full path.