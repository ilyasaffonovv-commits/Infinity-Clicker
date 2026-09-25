#pragma once
// Headless benchmark suite:  InfinityClicker.exe --bench [quick] [nostress] [only=a,b,...]
//
// Every number in BENCHMARK.md comes from this code running on the real machine.
// The benchmark opens a full-screen, top-most test pad so that every synthetic
// click lands in our own window (nothing on the user's desktop is touched) and
// the safety gate pauses output the moment the pad loses foreground.
#include <string>
#include <vector>

namespace infclick::bench {

struct Options {
    bool quick = false;
    bool stress = true;
    int stressSeconds = 60;
    std::vector<std::string> only; // test ids: sys,timer,margin,sendinput,cps,prio,downtime,latency,stress
    std::wstring outDir;
};

Options parseArgs(const std::vector<std::wstring>& args);
int run(const Options& opt);

// Console output helper shared with the autotest (works for a GUI-subsystem EXE).
void consoleInit();
void consolePrint(const char* fmt, ...);

} // namespace infclick::bench
