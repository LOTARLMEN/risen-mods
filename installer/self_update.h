#pragma once

// Self-update: the latest release carries RisenModsInstaller.exe. GitHub lists a SHA-256 for every asset, so a
// launch only compares it with the running exe; when they differ the release exe is downloaded and its file
// version (VERSIONINFO) compared. Only a newer one is offered. Updating swaps the files (a running exe may be
// renamed: self -> .old, new -> self) and starts the new exe; it deletes the .old copy at start.

#include <string>
#include <vector>

#include "mods.h"

std::string Sha256Hex(const std::vector<unsigned char>& data);
// "1.1" (".patch" added when not 0) from the VS_FIXEDFILEINFO inside an exe image; empty when missing.
std::string ExeVersion(const std::vector<unsigned char>& exe);
std::wstring OwnExePath();

// True when the release exe is newer than the running one; *exe gets its bytes, *version its version.
bool FindInstallerUpdate(const ReleaseInstaller& release, std::vector<unsigned char>* exe, std::string* version,
                         std::string* error, FetchProgress* progress);
// Puts the new exe in place of the running one and starts it. False + error text (nothing changed then).
bool ApplyInstallerUpdate(const std::vector<unsigned char>& exe, std::string* error);
// Deletes "<exe>.old" left by an update (retries while the old process is still closing).
void DeleteOldInstaller();
