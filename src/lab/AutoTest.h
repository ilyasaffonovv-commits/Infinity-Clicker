#pragma once
#include <string>

namespace infclick::autotest {

// Runs the functional acceptance suite with the real UI. Returns 0 if all pass.
int run(const std::wstring& outDir);

} // namespace infclick::autotest
