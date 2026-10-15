@echo off
rem Optional independent reference reader; the Rust decoder does not depend on PhysX DLLs.
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars32.bat" >nul || exit /b 1
cd /d "%~dp0.."
if not exist build\recon mkdir build\recon
cl /nologo /O2 /MT /EHsc /std:c++17 tools\xcom-native-check.cpp /Fobuild\recon\xcom-native-check.obj /Febuild\recon\xcom-native-check.exe
