@echo off
rem Builds build\version.dll. "build.bat deploy" also copies it to ..\bin (close the game first).
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
cd /d "%~dp0"
if not exist build mkdir build
cl /nologo /LD /O2 /MT /EHsc /std:c++17 /W3 /DWIN32_LEAN_AND_MEAN src\*.cpp /Fobuild\ /Fe:build\version.dll /link user32.lib || exit /b 1
if /i "%~1"=="deploy" copy /y build\version.dll ..\bin\version.dll || exit /b 1
