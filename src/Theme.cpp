#include "Theme.h"

#include <QApplication>
#include <QSettings>
#include <QStyleHints>
#include <QWidget>

namespace {
    int currentMode = 0;

    void refreshStyleSheets() {
        // Local QSS (including Find/Replace's selection rule) caches the palette
        // from when it was installed. Reinstall it against the new app palette.
        for (QWidget *widget: QApplication::allWidgets()) {
            if (const QString sheet = widget->styleSheet(); !sheet.isEmpty()) {
                widget->setStyleSheet(QString());
                widget->setStyleSheet(sheet);
            }
        }
    }

    QPalette explicitPalette(bool dark) {
        QPalette palette;
        const QColor window(dark ? "#292929" : "#f0f0f0");
        const QColor base(dark ? "#1e1e1e" : "#ffffff");
        const QColor text(dark ? "#eeeeee" : "#202020");
        const QColor muted(dark ? "#aaaaaa" : "#666666");
        const QColor highlight(dark ? "#80bfff" : "#005a9e");
        palette.setColor(QPalette::Window, window);
        palette.setColor(QPalette::WindowText, text);
        palette.setColor(QPalette::Base, base);
        palette.setColor(QPalette::AlternateBase, dark ? QColor(0x333333) : QColor(0xf5f5f5));
        palette.setColor(QPalette::Text, text);
        palette.setColor(QPalette::Button, window);
        palette.setColor(QPalette::ButtonText, text);
        palette.setColor(QPalette::ToolTipBase, base);
        palette.setColor(QPalette::ToolTipText, text);
        palette.setColor(QPalette::PlaceholderText, muted);
        palette.setColor(QPalette::Highlight, highlight);
        palette.setColor(QPalette::HighlightedText, dark ? QColor(0x101010) : QColor(0xffffff));
        palette.setColor(QPalette::Accent, highlight);
        palette.setColor(QPalette::Link, dark ? QColor(0x80bfff) : QColor(0x005a9e));
        palette.setColor(QPalette::LinkVisited, dark ? QColor(0xd6a8ff) : QColor(0x703090));
        palette.setColor(QPalette::Light, dark ? QColor(0x555555) : QColor(0xffffff));
        palette.setColor(QPalette::Midlight, dark ? QColor(0x444444) : QColor(0xe3e3e3));
        palette.setColor(QPalette::Mid, dark ? QColor(0x666666) : QColor(0xa0a0a0));
        palette.setColor(QPalette::Dark, dark ? QColor(0x181818) : QColor(0x707070));
        palette.setColor(QPalette::Shadow, dark ? QColor(0, 0, 0) : QColor(0x404040));
        for (auto role: {
                 QPalette::WindowText, QPalette::Text, QPalette::ButtonText,
                 QPalette::PlaceholderText
             })
            palette.setColor(QPalette::Disabled, role, muted);
        palette.setColor(QPalette::Disabled, QPalette::Highlight, dark ? QColor(0x505050) : QColor(0xd0d0d0));
        palette.setColor(QPalette::Disabled, QPalette::HighlightedText, text);
        return palette;
    }
}

void Theme::apply(const int mode) {
    currentMode = mode >= 0 && mode <= 2 ? mode : 0;
    auto *hints = QGuiApplication::styleHints();
    if (currentMode == 0) {
        hints->unsetColorScheme();
        // An unresolved palette clears our overrides and resolves against the
        // current platform palette, including when the scheme is Unknown.
        QApplication::setPalette(QPalette());
    } else {
        hints->setColorScheme(currentMode == 2 ? Qt::ColorScheme::Dark : Qt::ColorScheme::Light);
        QApplication::setPalette(explicitPalette(currentMode == 2));
    }
    refreshStyleSheets();
}

void Theme::initialize() {
    QObject::connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged,
                     qApp, [](Qt::ColorScheme) {
                         // Qt updates the platform palette after emitting colorSchemeChanged.
                         // Queue this reset so we resolve against that updated palette.
                         if (currentMode == 0) {
                             QApplication::setPalette(QPalette());
                             refreshStyleSheets();
                         }
                     }, Qt::QueuedConnection);
    apply(QSettings().value("theme/themeMode", 0).toInt());
}
