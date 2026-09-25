#include "core/Clock.h"

#include <windows.h>

namespace infclick::clk {

int64_t freq()
{
    // Function-local static: safe regardless of static-initialisation order.
    static const int64_t f = [] {
        LARGE_INTEGER v;
        QueryPerformanceFrequency(&v);
        return v.QuadPart;
    }();
    return f;
}

int64_t now()
{
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return t.QuadPart;
}

int64_t ticksTo100ns(int64_t t)
{
    const int64_t f = freq();
    if (f == 10'000'000) return t; // the common case on Windows 10/11
    return int64_t(double(t) * 1e7 / double(f));
}

} // namespace infclick::clk
