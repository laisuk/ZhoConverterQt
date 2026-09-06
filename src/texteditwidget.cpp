#include "texteditwidget.h"

#include <QMimeData>
#include <QUrl>

TextEditWidget::TextEditWidget(QWidget *parent)
    : QPlainTextEdit(parent)
{
    setAcceptDrops(true);
}

void TextEditWidget::dragEnterEvent(QDragEnterEvent *event)
{
    if (const QMimeData *mimeData = event->mimeData();
        mimeData->hasUrls() || mimeData->hasText()) {
        event->acceptProposedAction();
        }
}

void TextEditWidget::dropEvent(QDropEvent *event)
{
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
