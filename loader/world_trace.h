#pragma once
#include <windows.h>

struct DiagnosticRange { ULONG64 address; ULONG size; };
void WorldTraceStart();
// Reads only live memory with ReadProcessMemory; history contains copied values.
unsigned WorldTraceReport(const CONTEXT& context, DiagnosticRange* ranges, unsigned capacity);
