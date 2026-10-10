#pragma once
#include <QString>

class EpubHelper final {
public:
    static bool isEpub(const QString &path);
    static QString extractEpubAllText(const QString &epubPath,
                                      bool includePartHeadings = false,
                                      bool normalizeNewlines = true,
                                      bool skipNavDocuments = true);
};
