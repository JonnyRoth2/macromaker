# MacroMaker

A tiny Windows GUI tool to **hold or auto-click several keys and mouse buttons at
the same time** (e.g. hold `W` + left click) until you toggle it off.

## How to use
1. Build `macromaker.exe` (see below) and run it.
2. Tick the boxes for the keys/buttons you want — e.g. `W` and `Mouse L`.
3. Pick a mode:
   - **Hold** — the selected inputs are held down until you stop.
   - **Click every _N_ ms, hold _M_ ms** — the selected inputs are pressed
     together every *N* milliseconds, held down for *M* milliseconds, then
     released (an auto-clicker). E.g. `1000` / `50` clicks once a second.
4. Press **F8** (a global hotkey — works even while your game window is focused),
   or click **Start**.
5. Press **F8** / **Stop** again to stop and release everything.

- **Re-press while held**: tick this if a game ignores a plain held key; it
  re-sends the keydown ~30x/sec while holding (Hold mode only).
- Closing the window always releases anything still held, so keys never get stuck.

To change the available keys, edit the `g_items[]` table near the top of `main.cpp`.

## Build

**MSVC** — open *"x64 Native Tools Command Prompt for VS"*, then:
```
build.bat
```

**MinGW-w64** — with `g++` on your PATH:
```
build_mingw.bat
```

Either produces `macromaker.exe`.

> This must run on Windows (it uses the Win32 `SendInput` API). If you only have
> WSL, install a cross-compiler (`sudo apt install g++-mingw-w64-x86-64`) and build
> with `x86_64-w64-mingw32-g++ main.cpp -o macromaker.exe -O2 -mwindows -static -luser32 -lgdi32`.

## Notes
- Keyboard input is sent as hardware **scancodes**, which most games accept.
- If F8 clashes with a game binding, change `VK_F8` in `main.cpp` (e.g. `VK_F9`).
- Some games with anti-cheat block synthetic input; that's expected and not a bug here.
