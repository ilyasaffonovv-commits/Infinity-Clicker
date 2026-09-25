@echo off
rem Optimized x64 Release build (static CRT, LTCG) + unit tests.
rem Result: InfinityClicker.exe next to this script.
setlocal
cd /d "%~dp0"
call tools\env.bat || exit /b 1
cmake -S . -B build\release -G "%INFCLICK_GEN%" -DCMAKE_BUILD_TYPE=Release || exit /b 1
cmake --build build\release || exit /b 1
ctest --test-dir build\release --output-on-failure || exit /b 1
copy /y build\release\bin\InfinityClicker.exe InfinityClicker.exe >nul || exit /b 1
echo.
echo [infclick] Release build OK: %CD%\InfinityClicker.exe
endlocal
