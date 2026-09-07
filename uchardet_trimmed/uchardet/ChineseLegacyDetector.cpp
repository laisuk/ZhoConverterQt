#include "ChineseLegacyDetector.h"

#include "nsBig5Prober.h"
#include "nsGB2312Prober.h"

#include <algorithm>
#include <limits>

namespace uchardet_trimmed {

Result detectChineseLegacy(const char* data,
                           const std::size_t size,
                           const float minConfidence,
                           const float minMargin)
{
    Result result;

    if (!data || size == 0)
        return result;

    const auto capped = std::min<std::size_t>(
        size, std::numeric_limits<PRUint32>::max());
    const auto length = static_cast<PRUint32>(capped);

    nsBig5Prober big5(PR_FALSE);
    nsGB18030Prober gb18030(PR_FALSE);

    big5.HandleData(data, length);
    gb18030.HandleData(data, length);

    result.big5Confidence = big5.GetConfidence();
    result.gb18030Confidence = gb18030.GetConfidence();

    const float best = std::max(result.big5Confidence,
                                result.gb18030Confidence);
    const float second = std::min(result.big5Confidence,
                                  result.gb18030Confidence);

    if (best < minConfidence || (best - second) < minMargin)
        return result;

    result.encoding = result.big5Confidence > result.gb18030Confidence
                          ? Encoding::Big5
                          : Encoding::Gb18030;
    result.confidence = best;
    return result;
}

} // namespace uchardet_trimmed
