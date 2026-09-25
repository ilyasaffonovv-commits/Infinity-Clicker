cmake_minimum_required(VERSION 3.24)
project(InfinityClicker VERSION 1.0.0 LANGUAGES C CXX RC)

if(NOT MSVC)
  message(FATAL_ERROR "InfinityClicker is a native Windows project and requires MSVC (x64).")
endif()
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
  message(FATAL_ERROR "Build InfinityClicker for x64.")
endif()

option(INFCLICK_CORE_ONLY "Build only the core library and unit tests" OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
# Static CRT: the EXE runs on a clean Windows 10/11 without the VC++ redistributable.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

set(INFCLICK_DEFS
  UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX
  _WIN32_WINNT=0x0A00 WINVER=0x0A00 NTDDI_VERSION=0x0A00000C   # Windows 10+ API surface (NI headers)
  _CRT_SECURE_NO_WARNINGS)

set(INFCLICK_CXX_FLAGS /utf-8 /W4 /permissive- /Zc:__cplusplus /Zc:preprocessor /EHsc /w44265)
set(INFCLICK_REL_FLAGS /O2 /Oi /GL /Gy /Gw)
set(INFCLICK_REL_LINK /LTCG /OPT:REF /OPT:ICF /INCREMENTAL:NO)

function(infclick_target_settings tgt)
  target_compile_definitions(${tgt} PRIVATE ${INFCLICK_DEFS})
  target_compile_options(${tgt} PRIVATE ${INFCLICK_CXX_FLAGS} "$<$<CONFIG:Release,RelWithDebInfo>:${INFCLICK_REL_FLAGS}>")
  target_link_options(${tgt} PRIVATE "$<$<CONFIG:Release,RelWithDebInfo>:${INFCLICK_REL_LINK}>")
endfunction()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# ---------------------------------------------------------------- core library
add_library(infclick_core STATIC
  src/core/Clock.cpp
  src/core/I18n.cpp
  src/core/Json.cpp
  src/core/Log.cpp
  src/core/Paths.cpp
  src/core/Str.cpp
  src/input/KeyChord.cpp
  src/input/InputAction.cpp
  src/input/InputSender.cpp
  src/input/HeldState.cpp
  src/scheduler/PreciseWaiter.cpp
  src/scheduler/Engine.cpp
  src/telemetry/Stats.cpp
  src/telemetry/CpuMeter.cpp
  src/trigger/TriggerEngine.cpp
  src/profiles/Profile.cpp
  src/platform/win/Power.cpp
  src/platform/win/ProcessUtil.cpp
  src/platform/win/Guardian.cpp
  src/platform/win/Autostart.cpp
  src/lab/TestPad.cpp
  src/lab/Bench.cpp
)
target_include_directories(infclick_core PUBLIC src include)
infclick_target_settings(infclick_core)
target_link_libraries(infclick_core PUBLIC avrt winmm shell32 user32 advapi32 psapi)

# ---------------------------------------------------------------- unit tests
add_executable(infclick_tests tests/unit_tests.cpp)
infclick_target_settings(infclick_tests)
target_link_libraries(infclick_tests PRIVATE infclick_core)
enable_testing()
add_test(NAME unit_tests COMMAND infclick_tests)

# ---------------------------------------------------------------- console benchmark runner
add_executable(infclick_bench benchmarks/bench_main.cpp)
infclick_target_settings(infclick_bench)
target_link_libraries(infclick_bench PRIVATE infclick_core)

if(INFCLICK_CORE_ONLY)
  return()
endif()

# ---------------------------------------------------------------- Dear ImGui (vendored source)
set(IMGUI_DIR ${CMAKE_SOURCE_DIR}/third_party/imgui)
add_library(imgui STATIC
  ${IMGUI_DIR}/imgui.cpp
  ${IMGUI_DIR}/imgui_draw.cpp
  ${IMGUI_DIR}/imgui_tables.cpp
  ${IMGUI_DIR}/imgui_widgets.cpp
  ${IMGUI_DIR}/backends/imgui_impl_win32.cpp
  ${IMGUI_DIR}/backends/imgui_impl_dx11.cpp
)
target_include_directories(imgui PUBLIC ${IMGUI_DIR} ${IMGUI_DIR}/backends)
target_compile_definitions(imgui PRIVATE UNICODE _UNICODE NOMINMAX IMGUI_DISABLE_OBSOLETE_FUNCTIONS)
target_compile_definitions(imgui PUBLIC IMGUI_DISABLE_OBSOLETE_FUNCTIONS)
target_compile_options(imgui PRIVATE /utf-8 /W0 "$<$<CONFIG:Release,RelWithDebInfo>:${INFCLICK_REL_FLAGS}>")

# ---------------------------------------------------------------- application
add_executable(InfinityClicker WIN32
  src/main.cpp
  src/app/App.cpp
  src/ui/MainWindow.cpp
  src/ui/Theme.cpp
  src/ui/Widgets.cpp
  src/ui/Views.cpp
  src/ui/AdvancedView.cpp
  src/ui/SettingsView.cpp
  src/ui/Tray.cpp
  src/lab/AutoTest.cpp
  resources/infclick.rc
)
infclick_target_settings(InfinityClicker)
target_include_directories(InfinityClicker PRIVATE src include resources)
target_link_libraries(InfinityClicker PRIVATE infclick_core imgui d3d11 dxgi d3dcompiler dwmapi comctl32 shcore)
target_link_options(InfinityClicker PRIVATE /MANIFEST:NO)
set_target_properties(InfinityClicker PROPERTIES OUTPUT_NAME "InfinityClicker")
