@echo off
rem Baut die Werkzeuge (Release, Ninja) mit den VS 2022 Build Tools.
setlocal
set "VSROOT=%ProgramFiles(x86)%\Microsoft Visual Studio\2022\BuildTools"
if not exist "%VSROOT%\VC\Auxiliary\Build\vcvars64.bat" set "VSROOT=%ProgramFiles%\Microsoft Visual Studio\2022\Community"
call "%VSROOT%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
cd /d "%~dp0.."
if not exist build\build.ninja cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release || exit /b 1
cmake --build build %* || exit /b 1
