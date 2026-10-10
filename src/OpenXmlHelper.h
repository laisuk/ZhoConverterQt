#pragma once

#include <QString>

class OpenXmlHelper final {
public:
    static bool isDocx(const QString &path);
    static bool isOdt(const QString &path);

    static QString extractDocxAllText(const QString &path,
                                     bool includePartHeadings = false,
                                     bool normalizeNewlines = true,
                                     bool includeNumbering = true);

    static QString extractOdtAllText(const QString &path,
                                    bool normalizeNewlines = true);
};
