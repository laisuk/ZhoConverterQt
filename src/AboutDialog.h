#pragma once

#include <QDialog>
#include <QString>
#include <QIcon>

class QTextBrowser;

struct AboutInfo {
    QString app_name;
    QString version;
    QString author;
    QString year;
    QString description;

    QString website_text = "GitHub";
    QString website_url;

    QString license_text = "MIT";
    QString license_url;

    QString details; // multi-line
};

class AboutDialog : public QDialog {
    Q_OBJECT
public:
    explicit AboutDialog(AboutInfo  info,
                         QWidget* parent = nullptr,
                         const QIcon& icon = QIcon());

private slots:
    void copyInfo();

private:
    void applyStyles();
    static QString htmlEscape(const QString& s);

    AboutInfo m_info;
    QTextBrowser* m_browser = nullptr;
};