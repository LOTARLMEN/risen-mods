#pragma once

// Text appended to the name shown over the focused object, or nullptr for none. Called on the game thread every
// frame the focus name is built.
using FocusSuffixFn = const wchar_t* (*)();

// Hooks the game's focus name. False (and logged) when the game version is unknown.
bool InstallFocusNameHook(FocusSuffixFn suffix);
