# openslinky

**openslinky — Slinky client, but open source.**

- **Reversed from:** `slinky_beta` (`libslinky.dll`, 64-bit Windows PE64 hybrid C++/JNI/JVMTI/OpenGL Minecraft 1.7.10 & 1.8.9 ghost client, `46,087,920` bytes, SHA-256 `cc96a657c49c9aac0da696c3c61cde1b44ec39a0ae08f9a1d2198dc16a13cf81`)
- **Attribution:** **Zero human — 100% reversed, unpacked, devirtualized, deobfuscated, and rewritten from scratch by Gemini 4 Neon (xhigh).**
- **Backup Mirror (DMCA / Takedown Fallback):** [https://deobf.su/yxzero/openslinky](https://deobf.su/yxzero/openslinky)

Every packing layer, VMProtect 3.8 virtual machine, control-flow mutation, obfuscated import thunk, embedded `.class` payload, JNI mapping table, and module was recovered **purely through static analysis and offline symbolic/concrete emulation** without executing `slinky_beta`. All vendor DRM, licensing, HWID fingerprinting, telemetry, and anti-debug logic has been cleanly reconstructed in [`src/vendor/`](src/vendor/) for inspection only and is **completely disconnected** from `libslinky.dll`.

---

## Protections in `slinky_beta` & How Each One Was Defeated

### 1. VMProtect 3.8 Section Packing & Virtualized Loader (`TLS_cb1`)
* **Protection:** On disk, `.text`, `.rdata`, `.buildid`, `.data`, `.pdata`, `.tls`, and `.slinky0` all had `RawSize = 0`. The entire executable image and VM bytecode were compressed (LZMA) and encrypted inside `.slinky2` (`0x2BF2000` bytes / ~44 MiB) and unpacked at runtime by a virtualized TLS callback (`TLS_cb1`) running on VMProtect 3.8's stack-machine architecture.
* **Defeat:** Built a custom offline Python x86-64 concrete/symbolic CPU + SSE/XMM emulator paired with Z3. Synthesized a complete offline Windows x64 usermode environment (`TEB`, `PEB`, `PEB_LDR_DATA` with `slinky_beta` linked into `InLoadOrderModuleList`, `KUSER_SHARED_DATA` at `0x7FFE0000`, and synthetic `ntdll.dll` / `kernel32.dll` images) and stepped `TLS_cb1` through the LZMA unpacker until the full `0x4D47000`-byte (~77.3 MiB) unpacked virtual memory image was dumped cleanly to disk.

### 2. Virtualized Anti-Debug, Anti-VM, Syscall-Stub & Timing Checks
* **Protection:** Inside the VMProtect 3.8 loader and runtime security guards (`0x18006E600` -> `0x180158D60`), virtualized handlers performed multi-layered environment checks:
  * `PEB.BeingDebugged`, `CheckRemoteDebuggerPresent`, and `KUSER_SHARED_DATA` kernel-debugger flags (`0x7FFE02D4`).
  * Direct `syscall` probes via `ZwQueryInformationProcess` (`ProcessDebugPort = 7` expecting `0`, `ProcessDebugObjectHandle = 0x1E` expecting `STATUS_PORT_NOT_SET = 0xC0000353`) and `ZwClose` invalid-handle tampering checks.
  * Hardware breakpoint inspection (`CONTEXT_DEBUG_REGISTERS` `Dr0`–`Dr3`) and active window checks (`FindWindowW(nullptr, L"x64dbg")`).
  * `RDTSC` single-step timing delta checks and `CPUID` hypervisor leaf (`0x1` / `0x40000000`) VM detection.
  * Prologue integrity verification on `ntdll.dll` syscall stubs (`4C 8B D1 B8 <ssn> 0F 05 C3`) and read-only `.text` / header CRC checks.
* **Defeat:** Populated exact clean x64 `ntdll.dll` syscall stubs (`4C 8B D1 ...`) and `kernel32.dll` export tables in the offline emulator, intercepted `SYSCALL`, `RDTSC`, and `CPUID` instructions deterministically (`0` for `ProcessDebugPort`, `0xC0000353` for `ProcessDebugObjectHandle`, monotonic low-delta TSC ticks), and used Z3 constraint solving to resolve environment-gated VM branches. In `openslinky`, these checks live solely in [`src/vendor/anti_debug_tamper.cpp`](src/vendor/anti_debug_tamper.cpp) for reference and are never compiled into `libslinky.dll`.

### 3. Stripped IAT & Obfuscated `.slinky0` Import Thunks
* **Protection:** Standard PE import tables were stripped. Calls from `.text` routed through 16-byte PLT-style slots (`0x180158D90..0x180159150`) and `257` obfuscated `.text -> .slinky0` thunks that dynamically computed API addresses with arithmetic obfuscation and stack adjustments instead of reading an IAT.
* **Defeat:** Logged all `310` EAT resolutions and pointer writes performed during unpacking, micro-traced every `.text -> .slinky0` thunk in the emulator with sentinel return addresses to recover exact `(DLL, export, stack_delta)` tuples, rebuilt a clean `.idata2` import directory, and patched all `257` obfuscated call/jump sites back to direct `call/jmp [rip + iat_slot]` instructions.

### 4. VMProtect Control-Flow Mutation & Basic-Block Shuffling (`.slinky0`)
* **Protection:** Three critical core routines — `initialize_mappings` (`0x180039EC0`), the 419-slot multi-version JNI resolver `resolve_all_mappings` (`0x180039FB0`), and the JVMTI `ClassFileLoadHook` installer `install_jvmti_hooks` (`0x1800426B0`) — were shattered into thousands of non-contiguous basic blocks scattered across `.slinky0`, chained together with unconditional jumps and instruction-level mutations to break linear disassembly and Hex-Rays CFG construction.
* **Defeat:** Wrote a Capstone-based CFG walker and relocator that BFS-traversed all reachable blocks from each entry point, folded unconditional jump chains, recomputed `RIP`-relative displacements and `Jcc` offsets, and emitted contiguous, de-mutated functions into a new `.demut` PE section (`0x184D50000`). Loaded into headless IDA Pro + Hex-Rays, `1,221 / 1,222` first-party functions decompiled cleanly to C (with `0x1800426B0` recovered from its clean 1,941-line de-mutated linear disassembly).

### 5. Full Bytecode Virtualization (`.slinky2` VM) & Encrypted Cloud Licensing
* **Protection:** `12` functions — including `authenticate_and_validate_license` (`0x180001290`), `https_post_encrypted` (`0x180030E40`), and client bootstrap/teardown helpers — executed entirely inside VMProtect 3.8's `.slinky2` bytecode VM with rolling decryption keys and polymorphic handlers. Licensing (`slinky_init` / `SLINKY_LICENSE`) fingerprinted CPU, disk serial, `MachineGuid`, and SMBIOS Type-1 UUID into a SHA-256 HWID and exchanged AES-128-CBC + PKCS#7 encrypted `cJSON` payloads (`[16-byte IV || ciphertext]`, hardcoded key `FB A9 69 98 DE B6 99 F6 B0 2A 50 C0 0D 5E 71 43` at `.rdata` `0x18015A0C4`) with `https://api.slinky.gg/v1/client/authenticate` and `/v1/client/telemetry`.
* **Defeat:** Traced the `.slinky2` virtualized functions from post-`DllMain` VM snapshots while hooking external `.data`/`.rdata` memory accesses, native helper transitions (`cJSON`, AES-128 at `0x18006F3C0`/`0x18006F4D0`/`0x18006FB70`, `libcurl`, WinCrypt, BCrypt), and Windows syscalls. Reconstructed the entire protocol in [`src/vendor/`](src/vendor/), then wired [`src/main.cpp`](src/main.cpp) directly into [`slinky::core::initialize_client`](src/core/client.cpp) with zero network, license, or HWID checks.

### 6. Stripped Symbols, Raw Asset Blobs & Embedded Java Bytecode
* **Protection:** All symbols and RTTI were stripped. Fonts, textures, Minecraft 1.7.10/1.8.9 item lookup tables, JVMTI constant-pool/method patch specs for 9 Minecraft classes, and precompiled Java hook classes (`SlinkyHooks13` and Netty `SlinkyChannelHandler`) were embedded as unnamed raw byte ranges in `.rdata` and `.data`.
* **Defeat:** Extracted the exact `Inter-Medium.ttf`, `Inter-Bold.ttf`, and `logo.png` assets, dumped and disassembled the `0xCAFEBABE` classfiles into clean Java 8 sources ([`java/src/SlinkyHooks13.java`](java/src/SlinkyHooks13.java), [`java/src/SlinkyChannelHandler.java`](java/src/SlinkyChannelHandler.java)), extracted all `192` (1.8.9) and `771` (1.7.10) item/block entries and all `67` module descriptors (`0x180302010..0x180319000`), and rewrote the entire client into idiomatic C++20.

---

## Architecture & Repository Layout

```text
openslinky/
├── CMakeLists.txt          # CMake build: javac -> asset embedding -> libslinky.dll (+ isolated libslinky_vendor.a)
├── cmake/
│   └── embed_assets.py     # Generates embedded_assets.hpp/.cpp from fonts, PNGs, and compiled .class files
├── assets/
│   ├── fonts/              # Inter-Medium.ttf, Inter-Bold.ttf
│   └── images/             # logo.png
├── java/
│   ├── src/                # SlinkyHooks13.java (23 JNI hook trampolines) & SlinkyChannelHandler.java (Netty queue)
│   └── stubs/              # Minimal compile-time io.netty.* stubs for standalone javac --release 8 builds
├── third_party/            # jni, cjson, glad, nanovg, minhook, curl (header + Win32 shim for vendor reference target)
└── src/
    ├── main.cpp            # Clean DllMain & slinky_init entrypoint (zero vendor calls)
    ├── core/               # JVM/JVMTI attach, window hook lifecycle, keybind & command dispatch
    ├── bytecode/           # JVM classfile constant-pool/method rewriter & JVMTI ClassFileLoadHook for 9 MC classes
    ├── hooks/              # 23 JNI hook callbacks, Netty packet bridge, OpenGL 3D/2D passes, custom raycast
    ├── minecraft/          # 419-slot dynamic mappings (1.7.10 & 1.8.9: Vanilla, MCP/Lunar, Forge SRG), JNI wrappers, helpers
    ├── events/             # 27-channel priority-sorted event bus & Module base structure
    ├── config/             # cJSON settings & .slinky/ profile manager
    ├── account/            # Alt manager: editable MC username & offline Session swap (reflection + JNI)
    ├── commands/           # Chat command processor (!f, !gui, !! escape)
    ├── gui/                # OpenGL 3 FBO + NanoVG ClickGUI, widgets, color picker, modals, HUD/notifications
    ├── modules/            # All 67 modules: combat (17), movement (10), visual (15), world (7), utility (18)
    ├── util/               # Math/AABB, xoshiro128+/drift RNG, timers, Win32 WndProc/GLFW input, filesystem
    └── vendor/             # Isolated reference reconstruction of Slinky DRM/auth/HWID/telemetry/anti-debug (unreferenced)
```

---

## Building from Source

### Prerequisites
- **MinGW-w64 GCC** (`gcc` / `g++` with C++20 support)
- **CMake 3.20+** and **Ninja**
- **JDK 9+** (`javac` supporting `--release 8`)
- **Python 3.8+**

### Build Commands
```powershell
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

### Outputs
- `build/libslinky.dll` — Clean, standalone x64 client DLL ready to inject into Minecraft 1.7.10 or 1.8.9 (Vanilla, Forge, OptiFine, or Lunar/MCP). Contains **zero** vendor/auth/telemetry/anti-debug code.
- `build/SlinkyClient/SlinkyClient.exe` — Launcher/injector that finds the game's JVM and injects `libslinky.dll`.
- `build/libslinky_vendor.a` — Standalone static archive built solely to verify that the reconstructed reference code in [`src/vendor/`](src/vendor/) compiles cleanly.

### Running `SlinkyClient.exe`

Start Minecraft first (1.7.10 or 1.8.9, 64-bit JVM), then run `SlinkyClient.exe`, or drag it into a terminal:

```powershell
# Attach to an already running javaw.exe with loaded jvm.dll and a visible game window
.\SlinkyClient.exe

# Explicit pid / custom DLL
.\SlinkyClient.exe --pid 12345 --dll C:\path\to\libslinky.dll

# Start a launcher command first, then attach
.\SlinkyClient.exe --launch "C:\path\to\your\launcher.exe --profile slinky"

# Inspect candidates
.\SlinkyClient.exe --list
```

The launcher waits up to 60 seconds (`--wait`) for the JVM and game window, verifies both are 64-bit, then injects with `LoadLibraryW`. `libslinky.dll`'s `DllMain` starts the client on its own worker thread. Keep `libslinky.dll` next to the executable, or pass `--dll`.

On success the window shows a 5-second countdown and closes by itself:

```
  [*] Looking for a running Minecraft (64-bit JVM)...
  [*] Target: javaw.exe  (PID 10160)
  [*] Injecting Slinky...
  [+] Injection successful! Slinky is loading in the game.
  [*] Closing in 5 seconds...
```

Errors still pause so you can read them. `--no-pause` skips both the countdown and the error pause.

### Alt Manager (cracked / offline usernames)

Open the ClickGUI (default `Right Shift`) and switch to the **Profiles** tab. The **MC Username** card sits at the top of the tab:

- Click the field, then type a name. Valid names are 3–16 characters: `A–Z`, `a–z`, `0–9`, `_`.
- `Backspace` edits the text, `Enter` applies it, `Esc` cancels.
- Applying swaps the live Minecraft `Session` immediately (offline name, random offline UUID, empty token) — no game restart needed.
- The name is saved to `%USERPROFILE%\.slinky\account.txt` and re-applied automatically on the next injection.

Delete `%USERPROFILE%\.slinky\account.txt` to return to the launcher's original online session on the next launch. A premium session is never modified unless an alt name has been saved.

`build.bat` performs the full configure/build/copy into `dist\` on a machine with MinGW-w64 GCC, CMake, JDK 9+, and Python 3.8+ on `PATH`.
