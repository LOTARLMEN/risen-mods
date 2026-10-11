@echo off
rem Builds the loader and every mod into build\bin, mirroring Risen\bin:
rem   build\bin\version.dll          loader (loads bin\mods\*.dll)
rem   build\bin\mods\<mod>.dll       one DLL per folder in mods\
rem "build.bat deploy" also copies build\bin to ..\bin (close the game first).
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
cd /d "%~dp0"
set CFLAGS=/nologo /LD /O2 /MT /EHsc /std:c++17 /W3 /DWIN32_LEAN_AND_MEAN /Ishared
set LIBS=user32.lib oleaut32.lib
if not exist build\bin\mods mkdir build\bin\mods

if not exist build\obj\loader mkdir build\obj\loader
cl %CFLAGS% loader\*.cpp shared\*.cpp /Fobuild\obj\loader\ /Fe:build\bin\version.dll /link %LIBS% || exit /b 1

rem Mod folder "fast-start" -> fast_start.dll
for /d %%M in (mods\*) do call :mod %%~nxM || exit /b 1

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
cl %CFLAGS% /Imods\%1 mods\%1\*.cpp shared\common.cpp /Fobuild\obj\%NAME%\ /Fe:build\bin\mods\%NAME%.dll /link %LIBS%
exit /b %errorlevel%
