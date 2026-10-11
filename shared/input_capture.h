#pragma once

// Takes mouse and keyboard away from the game while one of our windows is open.
// Engine.dll reads input with Raw Input (RegisterRawInputDevices + WM_INPUT in its window procedure) and moves
// the cursor with SetCursorPos/ClipCursor for mouse look. While captured, WM_INPUT and keyboard/mouse messages
// never reach the game and the game's SetCursorPos/ClipCursor calls do nothing.

#include <windows.h>

// Call from DllMain (patches the imports of Engine.dll and Risen.exe).
bool InputCaptureInstall();
// Game thread. Turning capture on frees the cursor inside the window; turning it off puts it back.
void InputCaptureSet(HWND hwnd, bool on);
bool InputCaptured();
// Call first in the subclassed window procedure: true means the message was swallowed, return *result.
bool InputCaptureFilter(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, LRESULT* result);
