@echo off
rem Builds the loader and every mod into build\bin, mirroring Risen\bin:
rem   build\bin\version.dll          loader (loads bin\mods\*.dll)
rem   build\bin\mods\<mod>.dll       one DLL per folder in mods\
rem shared\ and third_party\imgui are static libraries: each mod only gets the parts it uses.
rem "build.bat deploy" also copies build\bin to ..\bin (close the game first).
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
cd /d "%~dp0"
set IMGUI=third_party\imgui
set CFLAGS=/nologo /O2 /MT /EHsc /std:c++17 /utf-8 /W3 /DWIN32_LEAN_AND_MEAN /Gy /Ishared /I%IMGUI% /I%IMGUI%\backends
set LIBS=build\lib\shared.lib build\lib\imgui.lib user32.lib oleaut32.lib gdi32.lib imm32.lib /OPT:REF
for %%D in (bin\mods lib obj\shared obj\imgui obj\loader) do if not exist build\%%D mkdir build\%%D

cl %CFLAGS% /c shared\*.cpp /Fobuild\obj\shared\ || exit /b 1
lib /nologo /out:build\lib\shared.lib build\obj\shared\*.obj || exit /b 1
if not exist build\lib\imgui.lib (
    cl %CFLAGS% /W1 /c %IMGUI%\imgui*.cpp %IMGUI%\backends\imgui_impl_dx9.cpp %IMGUI%\backends\imgui_impl_win32.cpp ^
       /Fobuild\obj\imgui\ || exit /b 1
    lib /nologo /out:build\lib\imgui.lib build\obj\imgui\*.obj || exit /b 1
)

cl %CFLAGS% /LD loader\*.cpp /Fobuild\obj\loader\ /Fe:build\bin\version.dll /link %LIBS% || exit /b 1

rem Mod folder "fast-start" -> fast_start.dll
for /d %%M in (mods\*) do call :mod %%~nxM || exit /b 1

rem build\RisenModsInstaller.exe: installs the mods of the latest GitHub release.
if not exist build\obj\installer mkdir build\obj\installer
rc /nologo /fobuild\obj\installer\installer.res installer\installer.rc || exit /b 1
cl %CFLAGS% installer\*.cpp /Fobuild\obj\installer\ build\obj\installer\installer.res /Fe:build\RisenModsInstaller.exe ^
   /link %LIBS% d3d9.lib winhttp.lib ole32.lib shell32.lib advapi32.lib /SUBSYSTEM:WINDOWS || exit /b 1

if /i "%~1"=="deploy" (
    copy /y build\bin\version.dll ..\bin\version.dll >nul || exit /b 1
    if not exist ..\bin\mods mkdir ..\bin\mods
    for %%F in (build\bin\mods\*.dll) do if /i not "%%~nF"=="startup_trace" copy /y %%F ..\bin\mods\ >nul || exit /b 1
    echo deployed to ..\bin
)
exit /b 0

:mod
set NAME=%1
set NAME=%NAME:-=_%
if not exist build\obj\%NAME% mkdir build\obj\%NAME%
cl %CFLAGS% /LD /Imods\%1 mods\%1\*.cpp /Fobuild\obj\%NAME%\ /Fe:build\bin\mods\%NAME%.dll /link %LIBS%
exit /b %errorlevel%
