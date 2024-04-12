#ifndef TEXTEDITWIDGET_H
#define TEXTEDITWIDGET_H

#include <QPlainTextEdit>
#include <QDragEnterEvent>

class [[maybe_unused]] TextEditWidget : public QPlainTextEdit {
Q_OBJECT

public:
    [[maybe_unused]] explicit TextEditWidget(QWidget *parent = nullptr);

    QString contentFilename;

protected:
    void dragEnterEvent(QDragEnterEvent *event) override;

    void dropEvent(QDropEvent *event) override;

    void loadFile(const QString &filePath) const;
};

#endif // TEXTEDITWIDGET_H
