#pragma once

#include <QString>

bool isOfficeExt(const QString &extLower);
bool isTextExt(const QString &extLower);
bool isAllowedTextLike(const QString &extLower);

QString makeOutputPath(const QString &outDir,
                       const QString &baseName,
                       const QString &config,
                       const QString &extLower);
