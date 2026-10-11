#pragma once

// Startup diagnostics, see startup_trace.cpp. No-ops unless [Debug] StartupTrace=1.
void StartupTraceInit(const char* iniPath, const char* tracePath); // call from DllMain (main thread)
void StartupTraceMark(const char* what);
void StartupTraceStopSoon(); // game window found: stop sampling a bit later
