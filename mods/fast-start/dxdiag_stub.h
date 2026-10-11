#pragma once

// Replaces Engine.dll's DxDiag query with a fast DXGI-based answer, see dxdiag_stub.cpp. Call from DllMain.
bool DxDiagStubInstall();
