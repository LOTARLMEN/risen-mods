@echo off
rem Builds build\version.dll (all mods) and build\fast_start\version.dll (Risen Fast Start: startup speed-ups only).
rem "build.bat deploy" also copies build\version.dll to ..\bin (close the game first).
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
cd /d "%~dp0"
if not exist build\fast_start mkdir build\fast_start
set CFLAGS=/nologo /LD /O2 /MT /EHsc /std:c++17 /W3 /DWIN32_LEAN_AND_MEAN
set LIBS=user32.lib oleaut32.lib
cl %CFLAGS% src\*.cpp /Fobuild\ /Fe:build\version.dll /link %LIBS% || exit /b 1
cl %CFLAGS% src\fast_start\main.cpp src\fast_io.cpp src\dxdiag_stub.cpp src\proxy_exports.cpp ^
   /Fobuild\fast_start\ /Fe:build\fast_start\version.dll /link %LIBS% || exit /b 1
if /i "%~1"=="deploy" copy /y build\version.dll ..\bin\version.dll || exit /b 1
