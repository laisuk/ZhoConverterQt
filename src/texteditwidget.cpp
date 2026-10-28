#include "texteditwidget.h"

#include <QEvent>
#include <QFontMetrics>
#include <QMimeData>
#include <QPainter>
#include <QPaintEvent>
#include <QTextBlock>
#include <QUrl>
#include <QWidget>

class LineNumberArea final : public QWidget {
public:
    explicit LineNumberArea(TextEditWidget *editor)
        : QWidget(editor), m_editor(editor) {
    }

protected:
    void paintEvent(QPaintEvent *event) override {
        m_editor->paintLineNumberArea(event);
    }

private:
    TextEditWidget *m_editor;
};

TextEditWidget::TextEditWidget(QWidget *parent)
    : QPlainTextEdit(parent), m_lineNumberArea(new LineNumberArea(this)) {
    setAcceptDrops(true);
    m_lineNumberArea->hide();

    connect(this, &QPlainTextEdit::blockCountChanged,
            this, [this](int) { updateLineNumberAreaWidth(); });
    connect(this, &QPlainTextEdit::updateRequest,
            this, &TextEditWidget::updateLineNumberArea);
    updateLineNumberAreaWidth();
}

void TextEditWidget::setLineNumbersVisible(const bool visible) {
    if (m_lineNumbersVisible == visible)
        return;

    m_lineNumbersVisible = visible;
    m_lineNumberArea->setVisible(visible);
    updateLineNumberAreaWidth();
    updateLineNumberAreaGeometry();
    if (visible)
        m_lineNumberArea->update();
}

bool TextEditWidget::lineNumbersVisible() const noexcept {
    return m_lineNumbersVisible;
}

int TextEditWidget::lineNumberAreaWidth() const {
    if (!m_lineNumbersVisible)
        return 0;

    int digits = 1;
    for (int count = qMax(1, blockCount()); count >= 10; count /= 10)
        ++digits;

    return 8 + fontMetrics().horizontalAdvance(QLatin1Char('9')) * digits;
}

void TextEditWidget::updateLineNumberAreaWidth() {
    setViewportMargins(lineNumberAreaWidth(), 0, 0, 0);
    updateLineNumberAreaGeometry();
    if (m_lineNumbersVisible)
        m_lineNumberArea->update();
}

void TextEditWidget::updateLineNumberAreaGeometry() const {
    if (!m_lineNumbersVisible)
        return;

    const QRect cr = contentsRect();
    m_lineNumberArea->setGeometry(cr.left(), cr.top(),
                                  lineNumberAreaWidth(), cr.height());
}

void TextEditWidget::updateLineNumberArea(const QRect &rect, const int dy) {
    if (!m_lineNumbersVisible)
        return;

    if (dy)
        m_lineNumberArea->scroll(0, dy);
    else
        m_lineNumberArea->update(0, rect.y(), m_lineNumberArea->width(), rect.height());

    if (rect.contains(viewport()->rect()))
        updateLineNumberAreaWidth();
}

void TextEditWidget::resizeEvent(QResizeEvent *event) {
    QPlainTextEdit::resizeEvent(event);
    updateLineNumberAreaGeometry();
}

void TextEditWidget::changeEvent(QEvent *event) {
    QPlainTextEdit::changeEvent(event);
    if (event->type() == QEvent::FontChange || event->type() == QEvent::StyleChange)
        updateLineNumberAreaWidth();
}

void TextEditWidget::paintLineNumberArea(const QPaintEvent *event) const {
    QPainter painter(m_lineNumberArea);
    painter.fillRect(event->rect(), palette().alternateBase());
    painter.setPen(palette().color(QPalette::PlaceholderText));
    painter.setFont(font());

    QTextBlock block = firstVisibleBlock();
    int number = block.blockNumber();
    int top = qRound(blockBoundingGeometry(block).translated(contentOffset()).top());
    int bottom = top + qRound(blockBoundingRect(block).height());

    while (block.isValid() && top <= event->rect().bottom()) {
        if (block.isVisible() && bottom >= event->rect().top()) {
            painter.drawText(0, top, m_lineNumberArea->width() - 4,
                             fontMetrics().height(),
                             Qt::AlignRight | Qt::AlignVCenter,
                             QString::number(number + 1));
        }
        block = block.next();
        ++number;
        top = bottom;
        if (block.isValid())
            bottom = top + qRound(blockBoundingRect(block).height());
    }
}

void TextEditWidget::dragEnterEvent(QDragEnterEvent *event) {
    if (const QMimeData *mimeData = event->mimeData();
        mimeData->hasUrls() || mimeData->hasText()) {
        event->acceptProposedAction();
    }
}

void TextEditWidget::dropEvent(QDropEvent *event) {
    const QMimeData *mimeData = event->mimeData();

    if (mimeData->hasUrls()) {
        const QList<QUrl> urls = mimeData->urls();
        if (urls.isEmpty())
            return;

        const QString filePath = urls.first().toLocalFile();
        if (filePath.isEmpty())
            return;

        // File decoding/loading is owned by MainWindow::loadTextFile().
        // This widget only reports the dropped path.
        emit fileDropped(filePath);
        event->acceptProposedAction();
        return;
    }

    if (mimeData->hasText()) {
        document()->setPlainText(mimeData->text());
        contentFilename.clear();
        emit fileDropped(QString{});
        event->acceptProposedAction();
    }
}
