@echo off
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul 2>nul || exit /b 1
cd /d "%~dp0.."
if not exist build\tests\world-trace mkdir build\tests\world-trace
cl /nologo /O2 /MT /EHsc /std:c++17 /utf-8 /W3 /DWIN32_LEAN_AND_MEAN /Ishared tools\world-trace-check.cpp /Fobuild\tests\world-trace\check.obj /Fe:build\tests\world-trace\check.exe /link build\lib\shared.lib user32.lib /OPT:REF || exit /b 1
build\tests\world-trace\check.exe "%CD%\build\tests\world-trace\scratch" || exit /b 1
python -I tools\check-world-trace-dumps.py build\tests\world-trace\scratch\crashes
exit /b %errorlevel%
