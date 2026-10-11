#pragma once

// Startup diagnostics, see startup_trace.cpp.
void StartupTraceInit(const char* tracePath); // call from DllMain (main thread)
void StartupTraceMark(const char* what);
void StartupTraceStopSoon(); // game window found: stop sampling a bit later
