@echo off
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars32.bat" >nul || exit /b 1
cd /d "%~dp0.."
if not exist build\recon mkdir build\recon
cl /nologo /O2 /MT /EHsc /std:c++17 /W3 tools\mesh-cook-check.cpp /Fobuild\recon\mesh-cook-check.obj /Febuild\recon\mesh-cook-check.exe
exit /b %errorlevel%
