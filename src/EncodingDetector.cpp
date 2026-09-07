//
// Created by bryan on 9/7/2026.
//

#include "EncodingDetector.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include "ChineseLegacyDetector.h"

namespace {
    constexpr auto u8(const char value) noexcept -> std::uint8_t {
        return static_cast<unsigned char>(value);
    }

    bool startsWith(
        const QByteArrayView data,
        const std::initializer_list<std::uint8_t> bytes) {
        if (data.size() < static_cast<qsizetype>(bytes.size()))
            return false;

        qsizetype index = 0;

        for (const auto expected: bytes) {
            if (u8(data[index]) != expected)
                return false;

            ++index;
        }

        return true;
    }
} // namespace

EncodingDetector::Result
EncodingDetector::detect(const QByteArrayView data) {
    if (data.isEmpty())
        return {};

    // ------------------------------------------------------------
    // 1. BOM detection
    // ------------------------------------------------------------

    if (startsWith(data, {0xEF, 0xBB, 0xBF})) {
        return {
            .encoding = Encoding::Utf8Bom,
            .bomSize = 3
        };
    }

    if (startsWith(data, {0xFF, 0xFE})) {
        return {
            .encoding = Encoding::Utf16LEBom,
            .bomSize = 2
        };
    }

    if (startsWith(data, {0xFE, 0xFF})) {
        return {
            .encoding = Encoding::Utf16BEBom,
            .bomSize = 2
        };
    }

    // ------------------------------------------------------------
    // 2. Pure ASCII
    // ------------------------------------------------------------

    if (isAscii(data)) {
        return {
            .encoding = Encoding::Ascii,
            .bomSize = 0
        };
    }

    // ------------------------------------------------------------
    // 3. Strict UTF-8
    // ------------------------------------------------------------

    if (isValidUtf8(data)) {
        return {
            .encoding = Encoding::Utf8,
            .bomSize = 0
        };
    }

    // ------------------------------------------------------------
    // 4. UTF-16 without BOM
    //
    // Do this after UTF-8 validation so ordinary UTF-8 data isn't
    // unnecessarily subjected to heuristic detection.
    // ------------------------------------------------------------

    if (looksLikeUtf16LE(data)) {
        return {
            .encoding = Encoding::Utf16LE,
            .bomSize = 0
        };
    }

    if (looksLikeUtf16BE(data)) {
        return {
            .encoding = Encoding::Utf16BE,
            .bomSize = 0
        };
    }

    // ------------------------------------------------------------
    // 5. Unknown legacy 8-bit encoding.
    //
    // Later:
    //   uchardet
    //      ↓
    //   Big5 / GB18030 / Shift-JIS / etc.
    // ------------------------------------------------------------

    if (const auto legacy = detectLegacyEncoding(data);
    legacy != Encoding::Unknown) {
        return {
            .encoding = legacy,
            .bomSize = 0
        };
    }

    return {
        .encoding = Encoding::Unknown,
        .bomSize = 0
    };
}

bool EncodingDetector::isAscii(const QByteArrayView data) {
    for (const char ch: data) {
        if (u8(ch) >= 0x80)
            return false;
    }

    return true;
}

bool EncodingDetector::isValidUtf8(const QByteArrayView data) {
    qsizetype i = 0;

    while (i < data.size()) {
        const auto c0 = u8(data[i]);

        // ASCII
        if (c0 <= 0x7F) {
            ++i;
            continue;
        }

        // --------------------------------------------------------
        // 2-byte sequence
        //
        // C2..DF 80..BF
        //
        // C0/C1 are rejected because they would create
        // overlong encodings.
        // --------------------------------------------------------

        if (c0 >= 0xC2 && c0 <= 0xDF) {
            if (i + 1 >= data.size())
                return false;

            if (const auto c1 = u8(data[i + 1]); (c1 & 0xC0) != 0x80)
                return false;

            i += 2;
            continue;
        }

        // --------------------------------------------------------
        // 3-byte sequence
        // --------------------------------------------------------

        if (c0 >= 0xE0 && c0 <= 0xEF) {
            if (i + 2 >= data.size())
                return false;

            const auto c1 = u8(data[i + 1]);

            if (const auto c2 = u8(data[i + 2]); (c2 & 0xC0) != 0x80)
                return false;

            // Avoid overlong sequences.
            if (c0 == 0xE0) {
                if (c1 < 0xA0 || c1 > 0xBF)
                    return false;
            }
            // UTF-16 surrogate range U+D800..U+DFFF is invalid
            // in UTF-8.
            else if (c0 == 0xED) {
                if (c1 < 0x80 || c1 > 0x9F)
                    return false;
            } else {
                if ((c1 & 0xC0) != 0x80)
                    return false;
            }

            i += 3;
            continue;
        }

        // --------------------------------------------------------
        // 4-byte sequence
        // --------------------------------------------------------

        if (c0 >= 0xF0 && c0 <= 0xF4) {
            if (i + 3 >= data.size())
                return false;

            const auto c1 = u8(data[i + 1]);
            const auto c2 = u8(data[i + 2]);

            if (const auto c3 = u8(data[i + 3]); (c2 & 0xC0) != 0x80 ||
                                                 (c3 & 0xC0) != 0x80) {
                return false;
            }

            // Avoid overlong sequences.
            if (c0 == 0xF0) {
                if (c1 < 0x90 || c1 > 0xBF)
                    return false;
            }
            // U+10FFFF is the highest valid Unicode code point.
            else if (c0 == 0xF4) {
                if (c1 < 0x80 || c1 > 0x8F)
                    return false;
            } else {
                if ((c1 & 0xC0) != 0x80)
                    return false;
            }

            i += 4;
            continue;
        }

        return false;
    }

    return true;
}

bool EncodingDetector::looksLikeUtf16LE(const QByteArrayView data) {
    if (data.size() < 4 || (data.size() % 2) != 0)
        return false;

    qsizetype zeroHighBytes = 0;
    qsizetype pairs = 0;

    for (qsizetype i = 0; i + 1 < data.size(); i += 2) {
        const auto lo = u8(data[i]);

        // Typical UTF-16LE ASCII:
        //
        //   41 00 42 00 43 00
        //
        if (const auto hi = u8(data[i + 1]); hi == 0 && lo != 0)
            ++zeroHighBytes;

        ++pairs;
    }

    if (pairs == 0)
        return false;

    // Deliberately conservative heuristic.
    return zeroHighBytes * 100 / pairs >= 60;
}

bool EncodingDetector::looksLikeUtf16BE(const QByteArrayView data) {
    if (data.size() < 4 || (data.size() % 2) != 0)
        return false;

    qsizetype zeroLowBytes = 0;
    qsizetype pairs = 0;

    for (qsizetype i = 0; i + 1 < data.size(); i += 2) {
        const auto hi = u8(data[i]);

        // Typical UTF-16BE ASCII:
        //
        //   00 41 00 42 00 43
        //
        if (const auto lo = u8(data[i + 1]); hi == 0 && lo != 0)
            ++zeroLowBytes;

        ++pairs;
    }

    if (pairs == 0)
        return false;

    return zeroLowBytes * 100 / pairs >= 60;
}

QString EncodingDetector::encodingName(const Encoding encoding) {
    switch (encoding) {
        case Encoding::Ascii:
            return QStringLiteral("ASCII");

        case Encoding::Utf8:
            return QStringLiteral("UTF-8");

        case Encoding::Utf8Bom:
            return QStringLiteral("UTF-8 BOM");

        case Encoding::Utf16LE:
            return QStringLiteral("UTF-16 LE");

        case Encoding::Utf16LEBom:
            return QStringLiteral("UTF-16 LE BOM");

        case Encoding::Utf16BE:
            return QStringLiteral("UTF-16 BE");

        case Encoding::Utf16BEBom:
            return QStringLiteral("UTF-16 BE BOM");

        case Encoding::Big5:
            return QStringLiteral("Big5");

        case Encoding::Gb18030:
            return QStringLiteral("GB18030");

        case Encoding::ShiftJis:
            return QStringLiteral("Shift-JIS");

        case Encoding::Unknown:
        default:
            return QStringLiteral("Unknown");
    }
}

EncodingDetector::Encoding
EncodingDetector::detectLegacyEncoding(const QByteArrayView data)
{
    if (data.isEmpty())
        return Encoding::Unknown;

    constexpr qsizetype MaxSampleSize = 128 * 1024;
    const QByteArrayView sample =
        data.first(std::min(data.size(), MaxSampleSize));

    const auto result = uchardet_trimmed::detectChineseLegacy(
        sample.data(),
        static_cast<std::size_t>(sample.size()));

    switch (result.encoding) {
        case uchardet_trimmed::Encoding::Big5:
            return Encoding::Big5;

        case uchardet_trimmed::Encoding::Gb18030:
            return Encoding::Gb18030;

        case uchardet_trimmed::Encoding::Unknown:
        default:
            return Encoding::Unknown;
    }
}
