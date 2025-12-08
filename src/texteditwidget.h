#ifndef TEXTEDITWIDGET_H
#define TEXTEDITWIDGET_H

#include <QPlainTextEdit>
#include <QDragEnterEvent>

class TextEditWidget : public QPlainTextEdit
{
    Q_OBJECT

public:
    explicit TextEditWidget(QWidget* parent = nullptr);

    QString contentFilename;

signals:
    void fileDropped(const QString& filePath);
    void pdfDropped(const QString& filePath);

protected:
    void dragEnterEvent(QDragEnterEvent* event) override;

    void dropEvent(QDropEvent* event) override;

    void loadFile(const QString& filePath) const;

    static bool isPdf(const QString &path);
};

#endif // TEXTEDITWIDGET_H
