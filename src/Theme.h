#pragma once

#include <QIcon>

namespace Theme {
    // Menu/settings values: 0 = system, 1 = light, 2 = dark.
    void initialize();

    void apply(int mode);

    static QIcon recolorIcon(const QIcon &icon);
}
