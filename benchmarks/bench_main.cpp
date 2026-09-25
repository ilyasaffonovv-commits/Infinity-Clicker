// Console front-end for the benchmark suite (identical to `InfinityClicker.exe --bench ...`).
//   infclick_bench.exe [quick] [nostress] [stress=60] [only=timer,margin,...] [out=<dir>]
#include "lab/Bench.h"

#include <windows.h>

#include <string>
#include <vector>

int wmain(int argc, wchar_t** argv)
{
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    std::vector<std::wstring> args;
    for (int i = 1; i < argc; ++i) args.emplace_back(argv[i]);
    return infclick::bench::run(infclick::bench::parseArgs(args));
}
