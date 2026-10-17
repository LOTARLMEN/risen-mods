---
name: risen-live-bridge
description: Query and change the RUNNING Risen game from a shell through the dev live bridge (`risen live <verb>`) - hero status/teleport, entities near the hero by GUID, move/rotate objects, time and weather - and update the bridge's verbs without restarting the game. Use whenever a task needs live game state or an in-game check that a command can do instead of the user.
---

# Live bridge: work in the running game

Reference: `notes/live-bridge.md` (verbs, protocol, engine facts). Client: `.\rust\target\release\risen.exe live`
from the repo root (PowerShell). Needs the game running with `bin\mods\live_bridge.dll` and
`bin\mods\live\live_verbs.dll`; `ping` tells whether it is up.

## Context rules (user 2026-10-17: don't flood the context, don't waste tokens)
- Ask only what the step needs: `status` first; `near` with a small `--radius` / `--limit` (default 20 m, 20)
  and `--kind` / `--name` filters; `show <guid>` only for the one entity you act on.
- Never loop `near --all` over a big radius into the context; if you need bulk data, write a script that
  calls the bridge and prints a summary.
- Changing verbs answer in one line - don't re-query `show` after every change unless the value matters.
- The game never pushes data; there is nothing to poll. Verbose detail goes to `bin\mods\RisenMods.log`
  (one line per request): grep it, don't read it whole.
- Frames (when `frame` exists, TODO 7.1 B5): only on request, downscaled, one per visual check, game frame only.

## Typical flow
1. `risen live ping` → `risen live status` (world `G3_Startup` = main menu, hero null = no game loaded).
   The user starts a test world with the "Тест" button; the agent cannot click in the game.
2. Find: `risen live near --radius 1500 --kind object --name Crate` → take the GUID.
3. Act: `risen live move <guid> ~0 ~100 ~0`, `risen live rotate <guid> ~45`, `teleport`, `look`, `time 12:00`,
   `weather Stormy_Rain` / `weather off` (weather needs World Debug on).
4. Visual result = ask the user (or a frame once B5 exists). Record confirmed results per skill risen-devlog.
5. Live changes vanish without a save; a save keeps them. Don't save for the user; after the test close the
   game as AGENTS.md says (that discards live changes).

## Changing verbs without a restart
1. Edit `mods/live-verbs/main.cpp` (keep its rules: no hooks/threads/subclassing, no pointers across calls),
   bump `kVersion`.
2. `cmd /c "<repo>\build.bat"` → exit 0, no warnings.
3. Back up `..\bin\mods\live\live_verbs.dll` into `backup\deploy-<time>\`, copy
   `build\bin\mods\live_verbs.dll` to `..\bin\mods\live\` - allowed while the game runs (the host uses a copy).
4. `risen live reload` → `{"ok":true,"verbs":"<new version>"}`; on an error the old verbs keep working.
Changes to `mods/live-bridge` (host), hooks or other mods still need the normal closed-game deploy
(skill risen-deploy-test). Never put `live_verbs.dll` into `bin\mods\` itself.
