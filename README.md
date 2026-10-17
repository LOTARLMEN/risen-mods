# Risen Mods

Quality-of-life mods for **Risen** — the 64-bit THQ Nordic re-release (2023, Steam). Native C++ DLLs loaded by a
small proxy `version.dll`; no game files or saves are changed, uninstalling = deleting two things.

| Mod | What it does |
|---|---|
| **AutoLoot** | Right mouse button loots the focused body / open chest / item at once, no loot window or animation. `F` toggles automatic pickup of loose items (optionally bodies and unowned open chests) around you - by default only in front of you (camera cone), with per-category filters (gold, ammo, weapons, armour, potions, scrolls...). A small silent sparkle marks each looted spot. Owned items, quest items, locked chests and unconscious people are never touched. |
| **Speed** | `Z` toggles a game speed-up (x3 by default, hourglass icon while on), like Gothic's Z-mod. Optional: normal speed in dialogs, step keys to change the speed by a set step (x1 to x10). |
| **No Limits** | Strength and dexterity go past the game's maximum of 200 (1000 by default, configurable), and their teachers train them that far instead of their personal limit. Learning point costs unchanged. |
| **Fast Start** | The game window appears in ~10 s instead of ~32 s (buffered `.pak` reads, skips the DxDiag video card probe). |
| **Window** | Borderless window over the whole monitor, hides the double Windows cursor, instant Alt+Tab. |
| **Quick Lockpick** | Right mouse button on a locked chest or door opens it at once, no minigame. The game's own rules decide (lockpick skill, lockpick or key, key-only locks); a lockpick may break. |
| **Menu** | `F10` in-game settings for all mods (RU / EN), styled with the game's own parchment, cursor and fonts, plus the status icons. Its own zip; the installer adds it with the first mod. |

Only these are in [Releases](../../releases). Everything else in this repository is work in progress or
developer tooling, see [Repository layout](#repository-layout).

## Install

**Installer:** download `RisenModsInstaller.exe` from [Releases](../../releases) and run it. It finds Risen (Steam), lists
the mods of the latest release, installs, updates and removes them together with the F10 menu (close the game
first). Your settings are kept. The exe is not signed, so Windows SmartScreen may warn: "More info" → "Run anyway".

**By hand:** download the zips from [Releases](../../releases), close the game, extract into the Risen folder
(Steam → Risen → Manage → Browse local files). A mod zip gives `bin\version.dll` (loader, the same in every zip)
and `bin\mods\<mod>.dll`; `RisenMenu-<version>.zip` adds `bin\mods\menu.dll` (the F10 window and the status icons -
without it the mods still work, settings are then in their `.ini` files only).
Every zip has a README (Russian + English) with details, settings and uninstall steps.

**Uninstall:** delete `bin\version.dll` and the `bin\mods` folder.

Only the Steam 64-bit build (`RISEN, V1.0.0 (Rev. 2045)`) is tested. Windows must be on drive `C:`.

**Bugs and crashes:** open an [issue](../../issues) and attach `Risen\bin\mods\RisenMods.log` - the one log of all
mods (after a crash and a restart: `RisenMods.prev.log`). The "Log folder" button at the bottom of the F10 window
opens its folder.

## Build

Visual Studio 2022 Build Tools (MSVC x64). `build.bat` builds everything into `build\bin`;
`build.bat deploy` also copies it into `..\bin` (the repo is meant to live in `Risen\risen-mods`; close the game first).
`powershell -File tools\package.ps1` makes `dist\Risen<Mod>-<version>.zip`.

The Rust tools build separately with Cargo in `rust/` (`cargo build --release` → `rust\target\release\risen.exe`).

The mods call functions exported by the game's own DLLs (`Engine.dll`, `Game.dll`, `Script.dll`, ...) by their
mangled names. No game code or assets are in this repository; the menu reads the game's textures and fonts from
its archives at runtime.

## Repository layout

One repository, three kinds of content. Only the **released mods** reach players: `tools\package.ps1` zips a mod
only if its folder has a `README.txt`, and the installer only offers what is in a release.

**Released mods and what they need** (C++, `build.bat`)

| Path | What it is |
|---|---|
| `loader/` | Proxy `bin\version.dll`: loads `bin\mods\*.dll`, the shared log, crash reports |
| `mods/autoloot`, `speed`, `no-limits`, `fast-start`, `window`, `quick-lockpick`, `menu` | The mods, one DLL each (`mods/fast-start` → `fast_start.dll`); version = first line of the mod's `README.txt` |
| `scripts/risenmods/` | `bin\scripts\Script_RisenMods.dll`: our script names for the game's own GUI (the "Mods" tab in the options) |
| `shared/` | Code linked into every DLL: log, ini, keys, D3D9/ImGui overlay, input capture, F10 menu API, game art reader, Risen style |
| `installer/` | `RisenModsInstaller.exe` (installs / updates the mods of the latest release) |
| `third_party/imgui` | Dear ImGui |

**Developer tools** (not in releases)

| Path | What it is |
|---|---|
| `mods/world-debug` | In-game developer controls: move/freeze items, free camera, time and weather (has a README, not released yet) |
| `mods/test-start` | "Test" button next to "New Game": a new game in a small test world, no intro (dev only) |
| `mods/ui-lab`, `mods/startup-trace` | GUI tree dumps and a startup file-read trace (dev only) |
| `rust/` | `risen-formats` library + `risen` CLI: game archives (VFS), worlds, layers, templates, string tables, collision, placement on the ground, own models; edits are installed as loose files with backup and undo. The base of a planned own world editor |
| `tools/` | Reverse engineering helpers (`pe_disasm.py`, `pe_symbol.py`, ...), packaging, deploy scripts, regression checks (`check-*`, `test-*.ps1`) |

**Knowledge and process**

| Path | What it is |
|---|---|
| `notes/` | Findings about the game: start with [notes/reference-implementations.md](notes/reference-implementations.md) (how each technique is done here, with the reference code), then the topic notes (GUI, VFS, world editing, geometry, assets, crash diagnostics) |
| `TODO.md` | The plan by stages, with findings per item |
| `DEVELOPMENT_LOG.md` | Dated development diary (Russian) |
| `AGENTS.md`, `.agents/skills/` | Working rules and step-by-step procedures for AI coding agents (deploy/test, crash triage, world edits, RE patches, release, diary) |

Generated and local, not in git: `build/`, `dist/`, `backup/`, `rust/target/`, `notes/exports/`.

---

## По-русски

Моды для удобства в **Risen** (64-битное переиздание 2023 года, Steam): быстрый лут и автоподбор, ускорение
игры, сила и ловкость без лимитов учителей, быстрый запуск, окно без рамки и меню настроек на `F10`.
Файлы игры и сохранения не меняются. Проще всего ставить установщиком `RisenModsInstaller.exe` из
[Releases](../../releases): сам найдёт игру, поставит выбранные моды вместе с меню F10 и будет их обновлять.
Там же архивы для ручной установки, в каждом README с инструкцией.

В репозитории, кроме выпущенных модов, лежат инструменты разработчика: World Debug, кнопка «Тест», Rust-библиотека
и CLI для правки мира (основа будущего редактора), заметки по устройству игры и план. В релизы они не входят;
что где лежит - в разделе [Repository layout](#repository-layout).

**Баги и вылеты:** создайте [issue](../../issues) и приложите `Risen\bin\mods\RisenMods.log` - общий лог всех модов
(после вылета и перезапуска игры - `RisenMods.prev.log`). Папку с ним открывает кнопка "Папка с логом" внизу окна F10.

## License

MIT, see [LICENSE](LICENSE). Uses [Dear ImGui](https://github.com/ocornut/imgui) (MIT). Risen is a trademark of its
respective owners; this is an unofficial fan project.
