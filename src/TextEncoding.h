#pragma once

#include "EncodingDetector.h"
#include <QByteArray>
#include <QString>

namespace TextEncoding {
QString decodeTextBytes(const QByteArray &bytes, const QString &encoding, bool *ok);
QString codecNameForDetectedEncoding(EncodingDetector::Encoding encoding);
}
