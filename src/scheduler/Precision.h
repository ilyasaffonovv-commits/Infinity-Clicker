#pragma once
#include <cstdint>

namespace infclick {

enum class Precision : uint8_t { Eco = 0, Standard = 1, Ultra = 2 };

inline const char* precisionName(Precision p)
{
    switch (p) {
    case Precision::Eco: return "ECO";
    case Precision::Standard: return "STANDARD";
    case Precision::Ultra: return "ULTRA";
    }
    return "?";
}

} // namespace infclick
