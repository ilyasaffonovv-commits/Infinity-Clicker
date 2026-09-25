@echo off
rem Debug x64 build (no optimizations, debug CRT) + unit tests.
rem Result: build\debug\bin\InfinityClicker.exe
setlocal
cd /d "%~dp0"
call tools\env.bat || exit /b 1
cmake -S . -B build\debug -G "%INFCLICK_GEN%" -DCMAKE_BUILD_TYPE=Debug || exit /b 1
cmake --build build\debug || exit /b 1
ctest --test-dir build\debug --output-on-failure || exit /b 1
echo.
echo [infclick] Debug build OK: %CD%\build\debug\bin\InfinityClicker.exe
endlocal
