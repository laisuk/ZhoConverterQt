#pragma once

#include <cstddef>

namespace uchardet_trimmed {

enum class Encoding {
    Unknown,
    Big5,
    Gb18030,
    ShiftJis
};

struct Result {
    Encoding encoding = Encoding::Unknown;
    float confidence = 0.0F;
    float big5Confidence = 0.0F;
    float gb18030Confidence = 0.0F;
    float shiftJisConfidence = 0.0F;
};

// Chinese-only statistical fallback detector.
// Call this only after deterministic BOM/UTF-8/UTF-16 checks have failed.
[[nodiscard]] Result detectChineseLegacy(const char* data,
                                         std::size_t size,
                                         float minConfidence = 0.20F,
                                         float minMargin = 0.05F);

} // namespace uchardet_trimmed
