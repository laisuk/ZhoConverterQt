#ifndef TEXTEDITWIDGET_H
#define TEXTEDITWIDGET_H

#include <QPlainTextEdit>

class LineNumberArea;

class TextEditWidget : public QPlainTextEdit {
    Q_OBJECT

public:
    explicit TextEditWidget(QWidget *parent = nullptr);

    QString contentFilename;

    void setLineNumbersVisible(bool visible);

    [[nodiscard]] bool lineNumbersVisible() const noexcept;

signals:
    void fileDropped(const QString &filePath);

protected:
    void dragEnterEvent(QDragEnterEvent *event) override;

    void dropEvent(QDropEvent *event) override;

    void resizeEvent(QResizeEvent *event) override;

    void changeEvent(QEvent *event) override;

private:
    friend class LineNumberArea;

    [[nodiscard]] int lineNumberAreaWidth() const;

    void updateLineNumberAreaWidth();

    void updateLineNumberArea(const QRect &rect, int dy);

    void updateLineNumberAreaGeometry() const;

    void paintLineNumberArea(const QPaintEvent *event) const;

    LineNumberArea *m_lineNumberArea = nullptr;
    bool m_lineNumbersVisible = false;
};

#endif // TEXTEDITWIDGET_H
