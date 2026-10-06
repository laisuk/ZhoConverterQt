#include "ChineseLegacyDetector.h"

#include "nsBig5Prober.h"
#include "nsGB2312Prober.h"
#include "nsSJISProber.h"

#include <algorithm>
#include <limits>
#include <cstdio>

namespace uchardet_trimmed {
    Result detectChineseLegacy(const char *data,
                               const std::size_t size,
                               const float minConfidence,
                               const float minMargin) {
        Result result;

        if (!data || size == 0)
            return result;

        const auto capped = std::min<std::size_t>(
            size, std::numeric_limits<PRUint32>::max());
        const auto length = static_cast<PRUint32>(capped);

        nsBig5Prober big5(PR_FALSE);
        nsGB18030Prober gb18030(PR_FALSE);
        nsSJISProber shiftJis(PR_FALSE);

        big5.HandleData(data, length);
        gb18030.HandleData(data, length);
        shiftJis.HandleData(data, length);

        result.big5Confidence = big5.GetConfidence();
        result.gb18030Confidence = gb18030.GetConfidence();
        result.shiftJisConfidence = shiftJis.GetConfidence();

        // debug
        /*std::fprintf(
            stderr,
            "Big5=%.6f GB18030=%.6f ShiftJIS=%.6f\n",
            result.big5Confidence,
            result.gb18030Confidence,
            result.shiftJisConfidence);*/

        struct Candidate {
            Encoding encoding;
            float confidence;
        };

        const Candidate candidates[] = {
            {.encoding = Encoding::Big5, .confidence = result.big5Confidence},
            {.encoding = Encoding::Gb18030, .confidence = result.gb18030Confidence},
            {.encoding = Encoding::ShiftJis, .confidence = result.shiftJisConfidence},
        };

        Candidate best{.encoding = Encoding::Unknown, .confidence = 0.0F};
        float second = 0.0F;

        for (const auto &candidate: candidates) {
            if (candidate.confidence > best.confidence) {
                second = best.confidence;
                best = candidate;
            } else if (candidate.confidence > second) {
                second = candidate.confidence;
            }
        }

        // Debug
        /*std::fprintf(
            stderr,
            "best=%.6f second=%.6f diff=%.6f minConfidence=%.6f minMargin=%.6f\n",
            best.confidence,
            second,
            best.confidence - second,
            minConfidence,
            minMargin);*/

        if (best.confidence < minConfidence ||
            best.confidence - second < minMargin) {
            return result;
        }

        result.encoding = best.encoding;
        return result;
    }
} // namespace uchardet_trimmed
