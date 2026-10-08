#include "DictionaryFile.h"
#include <QFile>
#include <QStringDecoder>
#include <QRegularExpression>
#include <stdexcept>

std::vector<OpenccFmmsegHelper::CustomPair> loadDictionaryFile(const QString &path) {
    auto fail = [&](const int line, const QString &message) {
        throw std::runtime_error(QString("%1:%2: %3").arg(path).arg(line).arg(message).toUtf8().toStdString());
    };
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) fail(0, file.errorString());
    const QByteArray bytes = file.readAll();
    if (file.error() != QFileDevice::NoError) fail(0, file.errorString());
    std::vector<OpenccFmmsegHelper::CustomPair> pairs;
    const auto lines = bytes.split('\n');
    static const QRegularExpression whitespaceRegex(QStringLiteral("\\s+"));
    for (int i = 0; i < lines.size(); ++i) {
        QStringDecoder decoder(QStringDecoder::Utf8, QStringConverter::Flag::Stateless);
        QString line = decoder(lines[i]);
        if (decoder.hasError()) fail(i + 1, "Invalid UTF-8");
        if (i == 0 && line.startsWith(QChar(0xfeff))) line.remove(0, 1);
        while (!line.isEmpty() && line.back().isSpace()) line.chop(1);
        if (line.isEmpty() || line.startsWith('#')) continue;
        if (line.contains(QChar(0))) fail(i + 1, "Embedded NUL is not allowed");
        const int tab = static_cast<int>(line.indexOf('\t'));
        if (tab < 0) fail(i + 1, "Missing TAB separator");
        const QString source = line.left(tab);
        const auto targets = line.mid(tab + 1).split(
            whitespaceRegex,
            Qt::SkipEmptyParts
        );
        if (source.isEmpty() || targets.isEmpty()) fail(i + 1, "Source and target must be nonempty");
        pairs.push_back({.source = source.toUtf8().toStdString(), .target = targets.front().toUtf8().toStdString()});
    }
    return pairs;
}
