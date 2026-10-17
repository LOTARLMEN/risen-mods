---
name: risen-release
description: Prepare a GitHub release of Risen mods - version bumps, README RU+EN, packaging zips, installer asset, release notes and the gh command for the user. Use when the user agrees to a release or asks to package/publish mods.
---

# Release

Repo: public github.com/LOTARLMEN/risen-mods, branch main. Tags `vYYYY.MM.DD.N`.

## 1. Decide what ships
- List mods changed since the previous tag: `git log <prev tag>..HEAD --stat -- mods shared loader installer`.
- Developer-only mods (world_debug, ui_lab, startup_trace) are not released unless the user says so.
- Only hands-on tested changes ship; check TODO.md for pending tests.
- Diagnostics must be off by default in shipped INIs/defaults (loader `[Diagnostics]`).

## 2. Versions and READMEs
- Version = first line of the mod's `README.txt` ("Risen AutoLoot 0.7"); the DLL logs the same string
  (update both). Menu and loader are versioned separately.
- README in Russian + English; user-facing only: features, keys, install/uninstall, report bugs as GitHub
  issues with RisenMods.log. No development history or test status.
- Installer changes: bump INSTALLER_VERSION.

## 3. Build and package
- `cmd /c "<repo>\build.bat"` - exit 0, no warnings/errors.
- `powershell -NoProfile -File tools\package.ps1` -> `dist\*.zip`. Delete the old zip of a bumped mod.
- Lint: no game files, no build/backup content, no personal paths or keys in the zips.

## 4. Commit and notes
- Commit with the repo-local LOTARLMEN noreply identity (never a personal email). Push only with consent.
- Write `build\release-notes.md`: English then Russian, per-mod changes, and an "Unchanged: ..." line.

## 5. Hand over the gh command (the agent does not run gh release create)
- Installer unchanged: attach the previous exe (self-update compares sha256):
  `gh release download <prev tag> -p RisenModsInstaller.exe -D build/release`.
- Give the user: `! gh release create vYYYY.MM.DD.N <every dist zip> <RisenModsInstaller.exe> --title "..." --notes-file build/release-notes.md`
- After release: mark the TODO release item done with the tag.
