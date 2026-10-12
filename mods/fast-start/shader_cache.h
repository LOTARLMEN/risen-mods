#pragma once

// Disk cache for compiled effects (Engine.dll compiles its .fx effects on every start). cachePath: the cache file.
bool ShaderCacheInstall(const char* cachePath);
// Logs calls / hits / misses / time spent compiling.
void ShaderCacheLogStats();
