#pragma once
// Stuck-input protection, three layers:
//   1. normal paths    - engine releases everything on stop / pause / emergency / exit
//   2. crash handler   - unhandled exception / terminate / purecall -> release + minidump
//   3. guardian process- a tiny child (same EXE, "--guardian <pid>") that waits on
//                        our process handle; if we die without a clean exit (killed
//                        from Task Manager, debugger, power-user kill), it reads the
//                        shared held-state and sends the matching UP events.
#include "input/HeldState.h"

#include <windows.h>

#include <string>

namespace infclick::guardian {

// Main process: creates the shared held-state block. Spawning the watchdog is optional.
HeldShared* createShared();
bool spawnWatchdog();
void markCleanExit(); // call right before a normal exit (after releasing input)

// Entry point for "--guardian <pid>" mode.
int runGuardian(DWORD parentPid);

void installCrashHandler(HeldShared* shared, const std::wstring& dumpDir);

} // namespace infclick::guardian
