#pragma once

// On-screen speed indicator, see overlay.cpp. Call OverlayInstall from DllMain.
bool OverlayInstall();

// Provided by main.cpp. Read on the game thread (the indicator draws in Present).
bool SpeedIsActive();
float SpeedMultiplier();
bool OverlayEnabled();
float OverlayScale();
