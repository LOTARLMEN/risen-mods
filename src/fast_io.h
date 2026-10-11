#pragma once

// Buffered .pak reads for FileSystem2.dll, see fast_io.cpp. Call from DllMain; false if the game DLLs don't match.
bool FastIoInstall();
void FastIoStats(long long* reads, long long* windowFills, long long* directReads);
