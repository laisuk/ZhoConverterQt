//
// Created by bryan on 9/7/2026.
//

#ifndef ZHOCONVERTERQT_ENCODINGDETECTOR_H
#define ZHOCONVERTERQT_ENCODINGDETECTOR_H

#pragma once

#include <QByteArrayView>
#include <QString>

class EncodingDetector final {
public:
    enum class Encoding {
        Unknown,

        Ascii,

        Utf8,
        Utf8Bom,

        Utf16LE,
        Utf16LEBom,

        Utf16BE,
        Utf16BEBom,

        Big5,
        Gb18030,
        ShiftJis
    };

    struct Result {
        Encoding encoding = Encoding::Unknown;
        int bomSize = 0;

        [[nodiscard]]
        bool detected() const noexcept {
            return encoding != Encoding::Unknown;
        }

        [[nodiscard]] bool hasBom() const noexcept {
            return bomSize != 0;
        }

        [[nodiscard]] bool isUnicode() const noexcept {
            switch (encoding) {
                case Encoding::Ascii:
                case Encoding::Utf8:
                case Encoding::Utf8Bom:
                case Encoding::Utf16LE:
                case Encoding::Utf16LEBom:
                case Encoding::Utf16BE:
                case Encoding::Utf16BEBom:
                    return true;

                default:
                    return false;
            }
        }
    };

    [[nodiscard]]
    static Result detect(QByteArrayView data);

    [[nodiscard]]
    static QString encodingName(Encoding encoding);

private:
    [[nodiscard]]
    static bool isValidUtf8(QByteArrayView data);

    [[nodiscard]]
    static bool isAscii(QByteArrayView data);

    [[nodiscard]]
    static bool looksLikeUtf16LE(QByteArrayView data);

    [[nodiscard]]
    static bool looksLikeUtf16BE(QByteArrayView data);

    [[nodiscard]]
    static Encoding detectLegacyEncoding(QByteArrayView data);
};

#endif //ZHOCONVERTERQT_ENCODINGDETECTOR_H
