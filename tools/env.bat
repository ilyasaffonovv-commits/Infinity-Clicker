@echo off
rem Locates MSVC x64 (via vswhere), CMake and Ninja. Called by build_*.bat.
if defined INFCLICK_ENV_OK exit /b 0

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
  echo [infclick] vswhere.exe not found - install Visual Studio 2022/2026 with "Desktop development with C++".
  exit /b 1
)
set "VSINSTALL="
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSINSTALL=%%i"
if not defined VSINSTALL (
  echo [infclick] MSVC x64 toolset not found.
  exit /b 1
)
call "%VSINSTALL%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1

rem CMake / Ninja: PATH first, then the pip packages (python -m pip install --user cmake ninja).
where cmake >nul 2>nul || for /f "usebackq tokens=*" %%p in (`python -c "import cmake;print(cmake.CMAKE_BIN_DIR)" 2^>nul`) do set "PATH=%%p;%PATH%"
where ninja >nul 2>nul || for /f "usebackq tokens=*" %%p in (`python -c "import ninja;print(ninja.BIN_DIR)" 2^>nul`) do set "PATH=%%p;%PATH%"
where cmake >nul 2>nul || (
  echo [infclick] CMake not found. Install: python -m pip install --user cmake ninja
  exit /b 1
)
set "INFCLICK_GEN=Ninja"
where ninja >nul 2>nul || set "INFCLICK_GEN=NMake Makefiles"
set "INFCLICK_ENV_OK=1"
exit /b 0
