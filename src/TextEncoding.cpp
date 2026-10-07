#include "TextEncoding.h"
#include <QStringConverter>
#include <utility>
#ifdef Q_OS_WIN
#include <windows.h>
#else
#include <cerrno>
#include <iconv.h>
#endif

namespace {
    QString stripBom(QString text) {
        if (!text.isEmpty() && text.front() == QChar::ByteOrderMark)
            text.remove(0, 1);
        return text;
    }

    QString decodeUtf(const QByteArray &bytes,
                      const QStringDecoder::Encoding encoding,
                      bool *ok) {
        QStringDecoder decoder(encoding);
        QString text = decoder.decode(bytes);

        if (decoder.hasError()) {
            if (ok)
                *ok = false;
            return {};
        }

        if (ok)
            *ok = true;

        return stripBom(std::move(text));
    }

#ifdef Q_OS_WIN

    QString decodeWindowsCodePage(const QByteArray &bytes,
                                  const UINT codePage,
                                  bool *ok) {
        if (bytes.isEmpty()) {
            if (ok)
                *ok = true;
            return {};
        }

        const int required = MultiByteToWideChar(
            codePage,
            MB_ERR_INVALID_CHARS,
            bytes.constData(),
            static_cast<int>(bytes.size()),
            nullptr,
            0);

        if (required <= 0) {
            if (ok)
                *ok = false;
            return {};
        }

        QString text(required, Qt::Uninitialized);

        const int written = MultiByteToWideChar(
            codePage,
            MB_ERR_INVALID_CHARS,
            bytes.constData(),
            static_cast<int>(bytes.size()),
            reinterpret_cast<wchar_t *>(text.data()),
            required);

        if (written <= 0) {
            if (ok)
                *ok = false;
            return {};
        }

        text.resize(written);

        if (ok)
            *ok = true;

        return stripBom(std::move(text));
    }

#else

    QString decodeIconv(const QByteArray &bytes,
                        const char *fromEncoding,
                        bool *ok) {
        iconv_t cd = iconv_open("UTF-8", fromEncoding);
        if (cd == reinterpret_cast<iconv_t>(-1)) {
            if (ok)
                *ok = false;
            return {};
        }

        const char *inputConst = bytes.constData();
        std::size_t inputLeft = static_cast<std::size_t>(bytes.size());

        QByteArray output;
        output.resize(qMax(bytes.size() * 4, 64));

        char *outputPtr = output.data();
        std::size_t outputLeft = static_cast<std::size_t>(output.size());

        while (inputLeft > 0) {
            char *inputPtr = const_cast<char *>(inputConst);

            const std::size_t result =
                    iconv(cd, &inputPtr, &inputLeft, &outputPtr, &outputLeft);

            inputConst = inputPtr;

            if (result != static_cast<std::size_t>(-1))
                continue;

            if (errno == E2BIG) {
                const qsizetype used =
                        static_cast<qsizetype>(outputPtr - output.data());

                output.resize(output.size() * 2);
                outputPtr = output.data() + used;
                outputLeft =
                        static_cast<std::size_t>(output.size() - used);
                continue;
            }

            iconv_close(cd);

            if (ok)
                *ok = false;
            return {};
        }

        iconv_close(cd);

        output.resize(
            static_cast<qsizetype>(outputPtr - output.data()));

        QStringDecoder decoder(QStringDecoder::Utf8);
        QString text = decoder.decode(output);

        if (decoder.hasError()) {
            if (ok)
                *ok = false;
            return {};
        }

        if (ok)
            *ok = true;

        return stripBom(std::move(text));
    }

#endif

} // namespace

QString TextEncoding::decodeTextBytes(const QByteArray &bytes,
                        const QString &encoding,
                        bool *ok) {
    if (encoding.compare(QStringLiteral("UTF-8"),
                         Qt::CaseInsensitive) == 0)
        return decodeUtf(bytes, QStringDecoder::Utf8, ok);

    if (encoding.compare(QStringLiteral("UTF-16LE"),
                         Qt::CaseInsensitive) == 0)
        return decodeUtf(bytes, QStringDecoder::Utf16LE, ok);

    if (encoding.compare(QStringLiteral("UTF-16BE"),
                         Qt::CaseInsensitive) == 0)
        return decodeUtf(bytes, QStringDecoder::Utf16BE, ok);

#ifdef Q_OS_WIN
    if (encoding.compare(QStringLiteral("GB18030"),
                         Qt::CaseInsensitive) == 0)
        return decodeWindowsCodePage(bytes, 54936, ok);

    if (encoding.compare(QStringLiteral("Big5"),
                         Qt::CaseInsensitive) == 0)
        return decodeWindowsCodePage(bytes, 950, ok);

    if (encoding.compare(QStringLiteral("Shift-JIS"),
                         Qt::CaseInsensitive) == 0)
        return decodeWindowsCodePage(bytes, 932, ok);

    // Windows has no separate Big5-HKSCS code page exposed here.
    // CP950 is used as the native fallback without adding dependencies.
    if (encoding.compare(QStringLiteral("Big5-HKSCS"),
                         Qt::CaseInsensitive) == 0)
        return decodeWindowsCodePage(bytes, 950, ok);
#else
    if (encoding.compare(QStringLiteral("GB18030"),
                         Qt::CaseInsensitive) == 0)
        return decodeIconv(bytes, "GB18030", ok);

    if (encoding.compare(QStringLiteral("Big5"),
                         Qt::CaseInsensitive) == 0)
        return decodeIconv(bytes, "BIG5", ok);

    if (encoding.compare(QStringLiteral("Shift-JIS"),
                         Qt::CaseInsensitive) == 0)
        return decodeIconv(bytes, "SHIFT-JIS", ok);

    if (encoding.compare(QStringLiteral("Big5-HKSCS"),
                         Qt::CaseInsensitive) == 0)
        return decodeIconv(bytes, "BIG5-HKSCS", ok);
#endif

    if (ok)
        *ok = false;
    return {};
}

QString TextEncoding::codecNameForDetectedEncoding(const EncodingDetector::Encoding encoding) {
    using Encoding = EncodingDetector::Encoding;

    switch (encoding) {
        case Encoding::Ascii:
        case Encoding::Utf8:
        case Encoding::Utf8Bom:
            return QStringLiteral("UTF-8");

        case Encoding::Utf16LE:
        case Encoding::Utf16LEBom:
            return QStringLiteral("UTF-16LE");

        case Encoding::Utf16BE:
        case Encoding::Utf16BEBom:
            return QStringLiteral("UTF-16BE");

        case Encoding::Big5:
            return QStringLiteral("Big5");

        case Encoding::Gb18030:
            return QStringLiteral("GB18030");

        case Encoding::ShiftJis:
            return QStringLiteral("Shift-JIS");

        case Encoding::Unknown:
        default:
            return {};
    }
}

