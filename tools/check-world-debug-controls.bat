@echo off
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
cd /d "%~dp0.."
if not exist build\tests\world-debug-controls mkdir build\tests\world-debug-controls
cl /nologo /O2 /MT /EHsc /std:c++17 /utf-8 /W3 /DWIN32_LEAN_AND_MEAN /Ishared tools\world-debug-controls-check.cpp /Fobuild\tests\world-debug-controls\check.obj /Fe:build\tests\world-debug-controls\check.exe /link build\lib\shared.lib user32.lib /OPT:REF || exit /b 1
build\tests\world-debug-controls\check.exe "%CD%\build\tests\world-debug-controls\scratch"
exit /b %errorlevel%
