# Risen Mods

Quality-of-life mods for **Risen** — the 64-bit THQ Nordic re-release (2023, Steam). Native C++ DLLs loaded by a
small proxy `version.dll`; no game files or saves are changed, uninstalling = deleting two things.

| Mod | What it does |
|---|---|
| **AutoLoot** | Right mouse button loots the focused body / open chest / item at once, no loot window or animation. `F` toggles automatic pickup of loose items (optionally bodies and unowned open chests) around you. A small silent sparkle marks each looted spot. Owned items, quest items, locked chests and unconscious people are never touched. |
| **Speed** | `Z` toggles a game speed-up (x3 by default, hourglass icon while on), like Gothic's Z-mod. |
| **No Limits** | Strength and dexterity teachers train up to the game's maximum (200) instead of their personal limit. Learning point costs unchanged. |
| **Fast Start** | The game window appears in ~10 s instead of ~32 s (buffered `.pak` reads, skips the DxDiag video card probe). |
| **Window** | Borderless window over the whole monitor, hides the double Windows cursor, instant Alt+Tab. |
| **Menu** | `F10` in-game settings for all mods (RU / EN), styled with the game's own parchment, cursor and fonts. Shipped with every mod. |

## Install

Download the zips from [Releases](../../releases), close the game, extract into the Risen folder
(Steam → Risen → Manage → Browse local files). You get `bin\version.dll` (loader), `bin\mods\<mod>.dll` and
`bin\mods\menu.dll`. Extract any number of mods; shared files are identical, the newest wins.
Every zip has a README (Russian + English) with details, settings and uninstall steps.

**Uninstall:** delete `bin\version.dll` and the `bin\mods` folder.

Only the Steam 64-bit build (`RISEN, V1.0.0 (Rev. 2045)`) is tested. Windows must be on drive `C:`.

## Build

Visual Studio 2022 Build Tools (MSVC x64). `build.bat` builds everything into `build\bin`;
`build.bat deploy` also copies it into `..\bin` (the repo is meant to live in `Risen\risen-mods`; close the game first).
`powershell -File tools\package.ps1` makes `dist\Risen<Mod>-<version>.zip`.

Layout: `loader/` (proxy `version.dll`, loads `bin\mods\*.dll`), `mods/<name>/` (one DLL each),
`shared/` (log, ini, keys, D3D9 hook, input capture, menu API), `third_party/imgui`, `tools/` (PE export /
disassembly helpers used for reverse engineering), `notes/` (findings).

The mods call functions exported by the game's own DLLs (`Engine.dll`, `Game.dll`, `Script.dll`, ...) by their
mangled names. No game code or assets are in this repository; the menu reads the game's textures and fonts from
its archives at runtime.

---

## По-русски

Моды для удобства в **Risen** (64-битное переиздание 2023 года, Steam): быстрый лут и автоподбор, ускорение
игры, сила и ловкость без лимитов учителей, быстрый запуск, окно без рамки и меню настроек на `F10`.
Файлы игры и сохранения не меняются. Архивы — в [Releases](../../releases), в каждом README с установкой.

## License

MIT, see [LICENSE](LICENSE). Uses [Dear ImGui](https://github.com/ocornut/imgui) (MIT). Risen is a trademark of its
respective owners; this is an unofficial fan project.
