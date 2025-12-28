#pragma once

#include <QString>

#include "opencc_fmmseg_capi.h"

bool isOfficeExt(const QString &extLower);
bool isTextExt(const QString &extLower);
bool isAllowedTextLike(const QString &extLower);

QString makeOutputPath(const QString &outDir,
                       const QString &baseName,
                       const opencc_config_t &config,
                       const QString &extLower);
