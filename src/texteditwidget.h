#ifndef TEXTEDITWIDGET_H
#define TEXTEDITWIDGET_H

#include <QDragEnterEvent>
#include <QPlainTextEdit>

class TextEditWidget : public QPlainTextEdit
{
    Q_OBJECT

public:
    explicit TextEditWidget(QWidget *parent = nullptr);

    QString contentFilename;

    signals:
        void fileDropped(const QString &filePath);

protected:
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;
};

#endif // TEXTEDITWIDGET_H
